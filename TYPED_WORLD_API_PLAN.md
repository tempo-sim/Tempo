# Typed World API — Design Scope

Status: **proposal**. Scopes a generated, statically-checkable client API for TempoWorld's
reflection surface, replacing string-keyed property access with attribute access on typed
handles. Python first; the design keeps Rust and C++ reachable from the same inputs.

Verified against `main` at `12adc4f7` (post engine-mods retirement, post TempoROS externalization).

---

## 1. The problem

TempoWorld exposes Unreal's reflection system over gRPC. Every address in it is a string:

```python
tempo_world.set_float_property(actor="my_actor", property="MaxSpeed", value=1.0)
```

The property name, its type, and the choice of RPC are all decided by the caller and only
validated on the server, at the moment of the call. Rename `MaxSpeed` in C++ or in a Blueprint and
nothing breaks until a client runs and gets `NOT_FOUND`. There is no completion, no type checking,
and no way to find affected call sites except grep.

The target:

```python
actor = tempo_world.get_actor("my_actor")
actor.MaxSpeed = 1.0
```

…where `MaxSpeed` is a declared member of a generated type, so a type checker rejects a
misspelling, an IDE completes it, and the write is checked against the property's real type and
writability before it ever leaves the process.

---

## 2. What already exists

Most of the wire protocol is in place. This is substantially a **client-side** layer over RPCs
TempoWorld already serves, plus three server-side gaps (§3).

| Capability | RPC | Notes |
|---|---|---|
| Write any property, type carried in the value | `SetProperty(SetPropertyRequest) -> Empty` | `Value` oneof covers 38 variants |
| Write a batch in one call | `SetProperties` | returns per-op failures by index |
| Call any `UFUNCTION` | `CallFunction` | typed `Value` args, results by name |
| Nested addressing | — | `MyStruct.Inner`, `MyArray[0]`, `MyMap[key]`, arbitrary depth (`SplitPropertyName` / `GetPropertyByName`) |
| Instance property listing | `GetActorProperties`, `GetComponentProperties` | values as **strings** |
| Instance function listing | `GetActorFunctions`, `GetComponentFunctions` | `FunctionDescriptor` + `ParameterDescriptor` already model params, kinds, callability |
| Class-name leniency | `GetSubClassWithName` (`TempoClassUtils.h`) | `BP_Foo` and `BP_Foo_C` both resolve |
| Type vocabulary | `GetPropertyTypeAndValue` | `bool`, `int`, `int64`, `float`, `double`, `string`, `vector`, `rotator`, `color`, `FMyStruct`, `array<T>`, `map<K,V>`, `set<T>`, `TSubclassOf<X>`, `unsupported` |

So: `SetProperty` and `CallFunction` are already the generic write/invoke primitives the typed
layer needs, and `ParameterDescriptor` is already an instance-free parameter model.

---

## 3. Server-side gaps

Three additions to `TempoWorld/Source/TempoWorld/Public/WorldControl.proto` and its subsystem.
RPC names must be unique within the module — `GetProperty`, `GetProperties` and `DescribeClasses`
are all still free (verified against both of the module's protos).

Nothing here needs an engine change, which is now a hard constraint: Tempo builds against Unreal
exactly as Epic ships it (AGENTS.md §5). The whole feature is plugin-side.

### 3a. A typed getter (the biggest single gap)

There is no way to read one property as a typed value. `GetActorProperties` returns *every*
property of an object with each value stringified. `x = actor.MaxSpeed` needs:

```proto
message GetPropertyRequest {
  string actor = 1;
  string component = 2;
  string property = 3;         // same nested addressing as SetProperty
  bool native_units = 4;       // see §7
}
rpc GetProperty(GetPropertyRequest) returns (Value);

message GetPropertiesRequest {
  string actor = 1;
  string component = 2;
  repeated string properties = 3;
  bool native_units = 4;
}
message NamedValue { string name = 1; Value value = 2; int32 code = 3; string error = 4; }
message GetPropertiesByNameResponse { repeated NamedValue values = 1; }
rpc GetProperties(GetPropertiesRequest) returns (GetPropertiesByNameResponse);
```

Implementation mirrors the existing write path: `GetPropertyByName` already resolves the nested
address; what's missing is a `GetValueFromTarget` that is the inverse of `SetValueOnTarget` —
reading an `FProperty` into a `Value` oneof rather than into a string. The type dispatch in
`GetPropertyTypeAndValue` is the template.

The batch form matters more than it looks: without it, reading five properties off one actor is
five round trips (§6, §11 Risk 2).

### 3b. Class-level reflection

Everything today is instance-keyed — you must have spawned an actor to ask what it has. Stub
generation needs to describe classes that may never be instantiated.

```proto
message DescribeClassesRequest {
  // Restrict the walk, e.g. ["Actor", "ActorComponent"]. Empty = every UClass.
  repeated string base_classes = 1;
  // Force-load Blueprint generated classes from the asset registry (see Risk 3).
  bool include_blueprint_classes = 2;
  bool blueprint_accessible_only = 3;   // honor CPF_BlueprintVisible etc.
  bool include_functions = 4;
}

message ClassDescriptor {
  string name = 1;              // real UClass name, "_C" and all
  string super_class = 2;
  string package = 3;           // "/Script/Engine" -> drives output module layout
  bool is_abstract = 4;
  bool is_blueprint = 5;
  // Declared on THIS class only. Inherited members come from super_class.
  repeated ClassPropertyDescriptor properties = 6;
  repeated ClassFunctionDescriptor functions = 7;
  repeated ComponentTemplateDescriptor components = 8;   // Phase 3
}

message ClassPropertyDescriptor {
  string name = 1;
  string property_type = 2;     // existing vocabulary
  bool readable = 3;            // CPF_BlueprintVisible
  bool writable = 4;            // readable && !CPF_BlueprintReadOnly
  // False when `Value` can't express the type and it is reachable only by nested address
  // (arbitrary UStructs, maps, nested containers).
  bool value_expressible = 5;
  string enum_name = 6;         // -> EnumDescriptor
  string struct_name = 7;       // -> StructDescriptor
  string class_name = 8;        // object/class-typed: the pointee's class, so the generated
                                // accessor can be typed instead of falling back to Object
  string category = 9;
  string tooltip = 10;          // becomes the generated docstring
}

message StructDescriptor { string name = 1; repeated ClassPropertyDescriptor members = 2; }
message EnumDescriptor   { string name = 1; repeated string enumerators = 2; }

message SchemaHeader {
  string schema_version = 1;    // this message layout
  string engine_version = 2;    // 5.7 vs 5.8 produce different schemas
  string content_hash = 3;      // see §9
}

message SchemaChunk {
  oneof chunk {
    SchemaHeader header = 1;    // first chunk on the stream
    ClassDescriptor class_descriptor = 2;
    StructDescriptor struct_descriptor = 3;
    EnumDescriptor enum_descriptor = 4;
  }
}
rpc DescribeClasses(DescribeClassesRequest) returns (stream SchemaChunk);
```

`ClassFunctionDescriptor` is a **new** message, not a reuse of the existing `FunctionDescriptor`:
that one carries `actor` and `component` fields, which are meaningless for a class that was never
instantiated. It reuses `ParameterDescriptor` and `ParameterKind` unchanged, which are already
instance-free, and keeps `signature`, `callable` and `error`.

Two other choices worth noting:

- **Declared-only members.** Each class lists only what it declares; inheritance is reconstructed
  from `super_class`. Without this, `AActor`'s members are repeated into every one of ~2,000 actor
  classes and both the wire payload and the generated artifact blow up by an order of magnitude.
- **Streaming.** A full engine walk is tens of megabytes. Streaming avoids leaning on the client's
  1 GB `max_receive_message_length` (set in `_tempo_context.py`) and lets the generator write
  incrementally.

### 3c. Blueprint-accessibility filtering

`GetObjectProperties` currently walks every `FProperty` via `TFieldIterator` with **no** flag
filtering — zero `CPF_Blueprint*` checks in the function today. The ask is "Blueprint-accessible
types with appropriate visibility", which means a new predicate:

| Surface | Condition |
|---|---|
| readable property | `CPF_BlueprintVisible` |
| writable property | `CPF_BlueprintVisible && !CPF_BlueprintReadOnly` |
| callable function | `FUNC_BlueprintCallable \|\| FUNC_BlueprintPure`, and not `FUNC_Delegate` |

**The filter applies to the new `DescribeClasses` RPC only.** The existing string-keyed RPCs stay
unfiltered — they are the escape hatch for everything the typed layer deliberately hides, and
narrowing them would be a breaking change. `GetPropertyTypeAndValue`'s existing exclusions
(`ArrayDim != 1` C-arrays, unresolved types → `"unsupported"`) carry over unchanged.

---

## 4. Architecture

```
packaged sim (headless)
    │  DescribeClasses (stream)
    ▼
tempo_schema.json          ← language-neutral, versioned, hashed
    │
    ├── gen_py_classes.py   → <project>-classes wheel   (.py identity + .pyi types)
    ├── gen_rust_classes.py → (later)
    └── gen_cpp_classes.py  → (later)

tempo_sim (hand-written runtime)
    Actor / Component / StructProxy  → GetProperty / SetProperty / CallFunction
```

The hinge is `tempo_schema.json`. Generated output in any language is a pure function of it, which
is what keeps Rust and C++ cheap later and mirrors the existing
`gen_api.py` / `gen_rust_api.py` / `gen_cpp_api.py` split.

**All behavior lives in the hand-written runtime; generated code carries only names and types.**
There is no generated method body anywhere. That is what makes a 30,000-member artifact tractable.

---

## 5. Where it runs

Harvest against the **packaged** sim over gRPC, not an editor commandlet:

- It is ground truth for what the shipped binary actually contains — cooked classes, the real
  enabled-plugin set — rather than what the editor could load.
- It needs no editor build, so a downstream project gets it from `Scripts/Package.sh` for free.
- The lifecycle machinery already exists in `Tests/Python/conftest.py`: launch headless with
  `-nullrhi -unattended -nopause -nosound` plus an endpoint flag, poll until the server answers,
  tear down. Factor that into a shared helper so `conftest.py` and the harvester share one
  implementation.

New: `TempoCore/Content/Python/gen_stubs.py`, hooked into `Scripts/Package.sh` between the
`RunUAT BuildCookRun` invocation (`Package.sh:104`) and `PACKAGE_PYTHON_PACKAGE`
(`Package.sh:213`) — today the wheels are built last, and the harvest has to land before them.

Order inside `Package.sh`: `BuildCookRun` → stage → launch packaged sim → `DescribeClasses` →
`tempo_schema.json` → generate the classes package into `Saved/` → build all wheels into
`Packaged/API/Python/`.

### Endpoint

Prefer a **Unix domain socket** for the harvest where the platform supports it
(`TempoServerEndpoint::IsUnixSocketTransportSupported()` — true on Linux/macOS, false on Windows,
where the vendored gRPC 1.62 has no AF_UNIX support). Launch with `-ServerSocket=<bare name>`,
which resolves into the short per-user directory, and fall back to `-ServerPort=` on Windows.

This is not incidental: a package-time harvest that grabs TCP 10001 will collide with a developer's
running editor or with a parallel CI job, and the socket transport refuses to displace a live
server rather than silently taking the name. Use a harvest-specific socket name, and honor the
established `TEMPO_SERVER_SOCKET` / `TEMPO_SERVER_PORT` env contract that the test harness and all
three clients already read.

### Distribution

The harvest is build-specific — engine version (5.7 and 5.8 are both supported and produce
different schemas), enabled plugins, and project Blueprints all change it — so it cannot live in
the canonical, PyPI-publishable `tempo-sim`. Ship it as its own generated distribution, named off
the project the same way the Rust crate and project Python package already are:

| Distribution | Contents | Built |
|---|---|---|
| `tempo-sim` | runtime proxies, `get_actor`, schema loader, base `Actor`/`Component`/`StructProxy` | hand-written, as today |
| `<project>-classes` (e.g. `tempo-sample-classes`) | every harvested class in the build: engine, plugin, project | per package |

`TestPythonAPI.sh` already installs *all* wheels from `Packaged/API/Python/`, so the classes
package is picked up by the test jobs with no script change. The generated package registers its
classes with `tempo_sim` on import (entry point or explicit registry call) so `get_actor` can
return the most-derived type it knows about.

---

## 6. The Python surface

### Acquiring a handle

```python
from tempo_sim import tempo_world

# Dynamic: works with no stubs installed, returns the base proxy.
actor = tempo_world.get_actor("my_actor")
actor.MaxSpeed = 1200.0

# Statically typed: the class is the second argument, so the checker knows the type.
from tempo_sample_classes.TempoSample import BP_Vehicle_C
car = tempo_world.get_actor("my_actor", BP_Vehicle_C)   # -> BP_Vehicle_C
car.MaxSpeed = 1200.0     # checked: exists, float, writable
car.MxSped = 1200.0       # pyright/mypy error
```

Note the asymmetry: an actor *name* is a runtime string, so no type checker can infer the class
from `get_actor("my_actor")` alone. The one-argument form is the convenience path and is typed
`Actor`; the two-argument form is the one that actually buys static checking. `spawn_actor` can
return a narrowed type for free, since it already takes the class.

### Properties

```python
car.bEnabled = True
print(car.Mass)                              # one GetProperty round trip
car.PostProcessSettings.BloomIntensity = 2.0 # nested dot-path, zero new server work
car.WaypointTags = ["alpha", "bravo"]        # whole-array replace via SetProperty
```

Nested structs need no new RPC: `StructProxy` accumulates the path and hands
`"PostProcessSettings.BloomIntensity"` to the existing nested addressing. Same for
`car.MyArray[0]` and `car.MyMap["key"]`.

### Components

```python
cam = car.component("TempoCamera")                        # -> Component
cam = car.component("TempoCamera", TempoCameraComponent)  # -> typed
car.Mesh.RelativeLocation = Vector(0, 0, 150)             # Phase 3: generated from SCS
```

Blueprint classes carry their component templates in the SimpleConstructionScript, and native
classes carry default subobjects — both are harvestable, so component accessors can be generated
and typed rather than string-keyed. Phase 3.

### Functions

```python
car.SetActorTickEnabled(True)
d = car.GetDistanceTo(other)     # -> float
```

Generated signature only; the body is the existing `CallFunction`. `ParameterDescriptor` and
`ParameterKind` already model params, direction and callability, so the generator mostly reshapes
data it is handed.

### Batching

Naive attribute access is one RPC per attribute. That is a real regression against today's
explicit `set_properties` batching, so the typed layer has to offer an equivalent:

```python
with tempo_world.batch():          # writes buffered, flushed as one SetProperties
    car.MaxSpeed = 1200.0
    car.Mass = 1500.0

with car.snapshot():               # one GetProperties; reads served from the snapshot
    print(car.MaxSpeed, car.Mass)
```

---

## 7. Units

The typed API is **Unreal-native throughout**: centimeters, degrees, left-handed. That is a
deliberate break from the rest of the Tempo API (meters, radians, right-handed) and it is the right
call here — these are raw `UPROPERTY` values with no reliable semantic tagging, so converting them
is guesswork.

It is also a conflict with the existing `Value` message, which **does** convert `rotator_value`,
`quat_value` and `transform_value` from radians/right-handed. Reusing `Value` unchanged would
double-convert. Hence `bool native_units` on `GetPropertyRequest` / `GetPropertiesRequest`, and a
matching field on `SetPropertyRequest`:

- `native_units = false` (default): today's behavior, unchanged for every existing caller.
- `native_units = true`: no conversion at all. The typed layer always sets it.

This also regularizes the documented wart where scalar and vector setters don't convert but
transform-shaped ones do (`docs/concepts/conventions.md#the-exception-set_property`). Inside the
typed API there is no exception — nothing converts.

**Consequence to flag loudly in the docs:** there will be two `Vector` types in the Python client.
`tempo_sim.unreal.Vector` (cm, left-handed, used by the typed API) and
`tempo_sim.TempoCore.Geometry_pb2.Vector` (m, right-handed, used by every other RPC). They are not
interchangeable. Distinct names and an explicit conversion helper, not implicit coercion.

---

## 8. Artifact form and scale — the main engineering risk

### Measured ceiling

Declarations in UE 5.7 (counted from engine headers; a packaged build links a subset):

| Source | `UCLASS` | `UPROPERTY` | `BlueprintReadWrite/ReadOnly` | `BlueprintCallable` |
|---|---|---|---|---|
| `Engine/Source` | 4,268 | 27,541 | 6,365 | not counted |
| `Engine/Plugins` | 9,346 | 52,820 | 13,284 | 12,570 |

So the hard ceiling is ~13,600 classes / ~80,000 properties, of which ~19,600 are
Blueprint-accessible. A real TempoSample-shaped package will link a fraction of that — order
2,000–4,000 classes and 20,000–40,000 accessible members — but the design has to survive the upper
end, because "all types including engine" is the stated requirement and a CitySample-class project
approaches it.

### Recommendation: `.py` for identity, `.pyi` for types

Emit, per Unreal module (`/Script/Engine` → `Engine.py` + `Engine.pyi`):

```python
# Engine.py  — tiny, loaded at runtime
class StaticMeshComponent(SceneComponent):
    __tempo_class__ = "StaticMeshComponent"
```

```python
# Engine.pyi — large, never loaded at runtime
class StaticMeshComponent(SceneComponent):
    bEnabled: bool
    RelativeLocation: Vector
    def SetStaticMesh(self, NewMesh: StaticMesh) -> bool: ...
```

Why this split:

- **Runtime cost is a class object per Unreal class and nothing else.** No annotation dictionaries,
  no descriptors, no per-member runtime objects. Note that under PEP 563 (`from __future__ import
  annotations`, required at the `>=3.9` floor in `pyproject.toml`) class-body annotations still
  build a `__annotations__` dict of strings at class creation — ~40,000 interned strings if the
  annotations lived in the `.py`. Putting them in the `.pyi` avoids that entirely.
- **Type checkers read the `.pyi` and ignore the `.py`**, so the full surface is statically
  available at zero import cost. Large stubs are the normal case for checkers (`types-*` packages
  are this shape).
- **The classes are real runtime objects**, so `get_actor(name, BP_Vehicle_C)` narrowing,
  `isinstance`, and the class registry all work — which a stub-only (PEP 561 `-stubs`) distribution
  could not provide.
- Per-module files plus a lazy package-level `__getattr__` mean a client that touches three classes
  parses three modules.

Alternative considered: generating real `@property` descriptors per member. Rejected — ~40,000
descriptor objects, slow import, no static benefit over the `.pyi`.

**Phase 0 must measure this against a real harvest before the rest is built.** If `.pyi` parse time
in pyright turns out to be the binding constraint, the fallback is to partition the classes package
(`<project>-classes-engine`, `-project`) and let a project depend only on what it uses.

---

## 9. Behavior, failure modes, and staleness

- **Runtime validation.** The runtime proxy checks attribute names against the registry it was
  generated with, so `actor.MxSped` raises immediately with a "did you mean `MaxSpeed`?" rather than
  producing a `NOT_FOUND` from the server. Unknown-but-plausible names still fall through to the
  server for the dynamic path.
- **Writability.** Writing a `BlueprintReadOnly` property is a static error (no annotation in the
  `.pyi` for a writable form) and a runtime `AttributeError`.
- **Schema drift.** Stubs generated from build X, run against sim Y, is silent wrongness. The
  `SchemaHeader` carries `engine_version` and a `content_hash`; the runtime fetches it on first
  connect and warns (or raises, configurable) on mismatch. This is cheap and it is the difference
  between a usable system and a confusing one. The two supported engine versions make it mandatory,
  not optional.
- **Actor names are still runtime strings.** This design makes *property* names static; it does not
  make actor names static. The existing warning in `docs/concepts/naming.md` — editor labels differ
  from packaged names, so query, don't hard-code — is unchanged.

---

## 10. Testing

Follows AGENTS.md §7: pure logic in C++ unit tests, client-facing behavior in the packaged Python
suite.

**C++ (`TempoWorld/Source/TempoWorld/Private/Tests/`, namespace `Tempo.World.Reflection.*`).** The
free-standing pieces are exactly the shape `Tempo.Core.ServerEndpoint` already demonstrates:

- the Blueprint-accessibility predicate against hand-built flag combinations;
- `property_type` → `Value` variant mapping, and the `value_expressible` determination;
- the declared-vs-inherited member split for a small synthetic class chain;
- `GetValueFromTarget` / `SetValueOnTarget` round trip over a transient `UObject` (the
  `FKinematicTestFixture` pattern — `NewObject` into `GetTransientPackage()`, no RHI needed).

**Python.** A new `classes` marker in `Tests/Python/pytest.ini` for the wheel-only surface check
(imports, a known class has a known member, no sim — the `contract` shape), plus typed get/set
coverage added to the existing `world` group against the live sim. A brand-new group needs a matrix
entry in `tempo_build_and_package.yml`.

**The generator.** A golden-output test: harvest with `base_classes` narrowed to one Tempo class,
assert the emitted `.py`/`.pyi` text. Keeps naming-rule changes (§12a) visible in a diff.

---

## 11. Phasing

| Phase | Scope | Why this order |
|---|---|---|
| **0 — Spike** | `DescribeClasses` + harvester; run against the existing TempoSample package; publish real class/member counts, wire size, `.pyi` size, pyright cold-check time, Blueprint-load cost | Every §8 decision is a guess until this exists. Small, and it de-risks the rest. |
| **1 — Properties** | `GetProperty`/`GetProperties`, `native_units`, BP-accessibility filter, runtime proxy, Python generator, `Package.sh` hook, `<project>-classes` wheel, schema-hash check, docs, tests | The core ask, end to end. |
| **2 — Functions** | Generated method signatures over `CallFunction` | Reuses `ParameterDescriptor` as-is. |
| **3 — Components, structs, enums** | SCS/default-subobject component accessors; nested `StructProxy`; `UENUM` → Python `Enum` | Needs Phase 1's proxy in place. |
| **4 — Ergonomics** | `batch()` / `snapshot()` context managers, typed `spawn_actor`, property streaming | Performance work; shape is known after 1–3. |
| **5 — Rust / C++** | Generators off the same `tempo_schema.json` | Only worth starting once the schema has stopped moving. |

### Out of scope

- Replacing the string-keyed API. It stays — as the escape hatch for non-Blueprint-accessible
  properties, as the dynamic path, and for backward compatibility.
- Editor-time generation.
- Delegates and multicast events (`FUNC_Delegate` is already excluded).
- C-style array properties (`ArrayDim != 1`), already `"unsupported"`.
- Making actor names statically checkable.

---

## 12. Risks

1. **Artifact size / type-checker performance** at 30,000+ members. → Measure in Phase 0; per-module
   split; `.pyi` rather than `.py` annotations; partition the distribution if needed.
2. **Round-trip amplification.** `for a in actors: a.X = 1` over 1,000 actors is 1,000 RPCs where the
   explicit API would have been one `set_properties`. → `batch()`/`snapshot()` in Phase 4, and say
   so plainly in the docs from Phase 1.
3. **Blueprint classes are not loaded in a cooked build.** `TObjectIterator<UClass>` only sees loaded
   classes; harvesting Blueprint classes means walking the asset registry and `LoadObject`-ing every
   `BlueprintGeneratedClass`. On a large project that is slow and memory-hungry, and it is the most
   likely thing to make the Phase-0 harvest fail. → `base_classes` / path filters on the request,
   `include_blueprint_classes` opt-out, and measure the cost in Phase 0.
4. **Schema drift** between stubs and sim. → `engine_version` + content hash, checked on connect.
5. **Two `Vector` types** in one client. → distinct module paths, explicit conversion, prominent docs.
6. **Package-time dependency on a working headless launch.** `Package.sh` would gain a step that can
   fail for reasons unrelated to packaging. → non-fatal by default (warn, skip the classes wheel);
   the CI test jobs already prove the headless launch works, so a hard failure is reasonable in CI.
   Use the socket transport where available (§5) so the harvest cannot collide with a running editor
   or a parallel CI job.
7. **Name mangling.** See §13a; whichever way it goes, the generator must detect collisions at
   generation time and fail the build rather than emit a silently shadowed member.

---

## 13. Open decisions

**(a) Member naming — verbatim or snake_case.** Recommendation: **verbatim Unreal names**
(`actor.MaxSpeed`, `actor.bEnabled`). It is collision-free, it matches what the editor and every
existing Tempo doc show, and a rename in Unreal maps 1:1 to a rename in client code — which is the
point of the exercise. The cost is inconsistency with the snake_case RPC wrappers and a slightly
un-Pythonic feel. snake_case is viable if the generator fails the build on collisions, but it needs
an acronym-aware rule that the existing `pascal_to_snake` does not have (`FOVAngle` →
`f_o_v_angle`). Offering both spellings is possible and not recommended — it doubles the stub and
splits the docs.

**(b) Handle acquisition.** Recommendation: both forms — `get_actor(name) -> Actor` for the dynamic
path and `get_actor(name, Cls) -> Cls` for the checked path — with the docs leading on the second,
since the first provides no static guarantee.

**(c) Harvest breadth.** Recommendation: everything by default, with `base_classes` available to
narrow it. Revisit after Phase 0 measures what "everything" actually costs.

**(d) Distribution granularity.** Recommendation: one `<project>-classes` wheel. Split engine from
project only if Phase 0 shows the combined artifact is a problem.
