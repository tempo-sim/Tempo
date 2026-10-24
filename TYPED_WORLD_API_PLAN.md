# Typed World API — Design Scope

Status: **proposal**, revision 2. Scopes a generated, statically-checkable client API for
TempoWorld's reflection surface, replacing string-keyed property access with attribute access on
typed handles. Python first; the design keeps Rust and C++ reachable from the same inputs.

Verified against `main` at `12adc4f7`. Revision 2 follows a review that measured revision 1
against the drift-detection goal in §1 and found a hole that defeated it (§9.1).

---

## 1. The goal

TempoWorld exposes Unreal's reflection system over gRPC. Every address in it is a string:

```python
tempo_world.set_float_property(actor="my_actor", property="MaxSpeed", value=1.0)
```

The property name, its type, and the choice of RPC are all decided by the caller and only validated
on the server, at the moment of the call. There is no completion, no type checking, and no way to
find affected call sites except grep.

**The primary goal is drift detection, not convenience.** Stated as the test the design has to pass:

> A new downstream release, with its regenerated stubs, makes scenarios that use removed or changed
> API **fail under Pyright**.

Completion and typo-catching fall out of that for free, but they are not the point, and designing
for them alone produces a system that silently passes the cases that matter. Three consequences run
through the whole design:

1. **An escape hatch that the type checker can see is not an escape hatch — it is a hole.** A
   catch-all `__getattr__` anywhere in the statically visible hierarchy makes every attribute on
   every generated class legal (§9.1).
2. **A type checker enforces nothing unless CI requires it.** The strictness policy for consuming
   repos is part of the deliverable, not advice (§10.4).
3. **Unsupported types must be absent from the stubs, never typed `Any`.** `Any` passes silently,
   which is the failure mode this exists to prevent.

The target surface, leading with the form that actually carries a guarantee:

```python
for truck in tempo_world.get_actors(BP_Cat745_C):   # list[BP_Cat745_C]
    truck.MaxSpeed = 1200.0
```

---

## 2. What already exists

Most of the wire protocol is in place. This is substantially a **client-side** layer over RPCs
TempoWorld already serves, plus the server-side gaps in §3.

| Capability | RPC | Notes |
|---|---|---|
| Write any property, type carried in the value | `SetProperty(SetPropertyRequest) -> Empty` | `Value` oneof covers 38 variants |
| Write a batch in one call | `SetProperties` | per-op failures by index; **no generic-`Value` op** (§3d) |
| Call any `UFUNCTION` | `CallFunction` | typed `Value` args; **results are strings** (§3e) |
| Deferred spawn | `SpawnActor(deferred=true)` + `FinishSpawningActor` | already enough for §6.2's context manager |
| Nested addressing | — | `MyStruct.Inner`, `MyArray[0]`, `MyMap[key]`, arbitrary depth |
| Instance property listing | `GetActorProperties`, `GetComponentProperties` | values as **strings** |
| Instance function listing | `GetActorFunctions`, `GetComponentFunctions` | `ParameterDescriptor` is already instance-free |
| Class-name leniency | `GetSubClassWithName` (`TempoClassUtils.h`) | by **name** only, not path (§8.4) |
| Type vocabulary | `GetPropertyTypeAndValue` | `bool`, `int`, `int64`, `float`, `double`, `string`, `vector`, `rotator`, `color`, `FMyStruct`, `array<T>`, `map<K,V>`, `set<T>`, `TSubclassOf<X>`, `unsupported` |

---

## 3. Server-side work

Nothing here needs an engine change, which is a hard constraint: Tempo builds against Unreal exactly
as Epic ships it (AGENTS.md §5). `GetProperty`, `GetProperties` and `DescribeClasses` are all still
free as RPC names in the module (names must be unique per module).

### 3a. One classification function — the keystone

Today "which `Value` variants may be written to this `FProperty`" (`SetValueOnTarget`) and "what type
string does it report" (`GetPropertyTypeAndValue`) are **separate, file-local** code paths in
`TempoWorldControlServiceSubsystem.cpp`. This plan would add a third (the schema walk) and a fourth
(the typed getter). Four independent opinions about the same `FProperty`, kept in agreement by tests.

That is not good enough here. If the generator's view of what is writable diverges from the server's
accept/reject behavior, the stubs promise a guarantee the server does not honor — and the whole
feature rests on that guarantee.

**Requirement: all four derive from one public, world-independent classifier**, in a new
`TempoWorld/Public/TempoWorldReflection.h`, which maps an `FProperty` to a structured descriptor:

- kind, and the `Value` variant(s) it accepts;
- the referenced enum / struct / class;
- read-only and Blueprint-visible flags;
- accessor function names when the property has a `BlueprintGetter`/`BlueprintSetter` (§5.3);
- or *unsupported, with a reason*.

Plus the equivalent for function signatures and callability. Being world-independent makes it both
unit-testable (§11) and usable from a commandlet (§5). Then the stubs and the server cannot diverge
by construction, rather than by vigilance.

### 3b. A typed getter

`GetActorProperties` returns *every* property of an object with each value stringified, so there is
no way to read one property as a typed value.

```proto
message GetPropertyRequest {
  string actor = 1;
  string component = 2;
  string property = 3;         // same nested addressing as SetProperty
  bool native_units = 4;       // §7
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

`GetPropertyByName` already resolves the nested address; what's missing is a `GetValueFromTarget`
that is the inverse of `SetValueOnTarget`, driven by the §3a classifier. The batch form is not
optional: without it, reading five properties off one actor is five round trips.

### 3c. Class-level reflection

Everything today is instance-keyed — you must have spawned an actor to ask what it has.

```proto
message DescribeClassesRequest {
  repeated string base_classes = 1;     // restrict the walk; empty = every UClass
  bool include_blueprint_classes = 2;   // force-load from the asset registry (Risk 3)
  bool blueprint_accessible_only = 3;
  bool include_functions = 4;
}

message ClassDescriptor {
  string name = 1;              // real UClass name, "_C" and all
  string class_path = 2;        // full object path -- the unambiguous identity (§8.4)
  string super_class = 3;
  string package = 4;           // "/Script/Engine" or "/Game/..." -> output layout (§8.3)
  bool is_abstract = 5;
  bool is_blueprint = 6;
  // Declared on THIS class only. Inherited members come from super_class.
  repeated ClassPropertyDescriptor properties = 7;
  repeated ClassFunctionDescriptor functions = 8;
  repeated ComponentTemplateDescriptor components = 9;   // Phase 3
}

message ClassPropertyDescriptor {
  string name = 1;
  string property_type = 2;
  bool readable = 3;            // CPF_BlueprintVisible
  bool writable = 4;            // readable && !CPF_BlueprintReadOnly
  bool value_expressible = 5;   // false => reachable only by nested address
  string unsupported_reason = 6;
  string enum_name = 7;
  string struct_name = 8;
  string class_name = 9;        // pointee's class, so the accessor can be typed
  // Non-empty when the property has a BlueprintGetter/Setter: writing it through SetProperty
  // bypasses the accessor, so the typed layer routes through CallFunction instead (§5.3).
  string blueprint_getter = 10;
  string blueprint_setter = 11;
  string category = 12;         // editor-only metadata (§5.1)
  string tooltip = 13;          // editor-only metadata (§5.1)
}

message StructDescriptor { string name = 1; repeated ClassPropertyDescriptor members = 2; }
message EnumDescriptor   { string name = 1; repeated string enumerators = 2; }

message SchemaHeader {
  string schema_version = 1;    // this message layout
  string engine_version = 2;    // 5.7 and 5.8 produce different schemas
  string content_hash = 3;      // §10.1
}

message DescribeClassesResponse {
  SchemaHeader header = 1;
  repeated ClassDescriptor classes = 2;
  repeated StructDescriptor structs = 3;
  repeated EnumDescriptor enums = 4;
}
rpc DescribeClasses(DescribeClassesRequest) returns (DescribeClassesResponse);
```

`ClassFunctionDescriptor` is a **new** message, not a reuse of `FunctionDescriptor`: that one carries
`actor` and `component`, meaningless for a class that was never instantiated. It reuses
`ParameterDescriptor` and `ParameterKind` unchanged.

Two choices worth noting:

- **Declared-only members.** Inheritance is reconstructed from `super_class`. Otherwise `AActor`'s
  members repeat into every one of ~2,000 actor classes, multiplying payload and stubs alike.
- **Unary, not streaming** (revised during implementation). Revision 2 specified a stream, on the
  grounds that a full walk is tens of megabytes and a generator would want to consume it
  incrementally. But §5 made an editor commandlet the generator's source, so nothing streams it any
  more: the RPC's callers are the verification test and the dynamic path, both of which narrow the
  walk with `base_classes`. 21.5 MB also sits well inside the clients' existing 1 GB
  `max_receive_message_length` (`_tempo_context.py`). A unary call is simpler, and the streaming
  handlers in this codebase are shaped for subscriptions rather than finite batches.

Note that §5 makes an **editor commandlet** the source of truth for generation. `DescribeClasses`
stays an RPC regardless: the §5.4 verification test must ask a running sim "do you have this class
with these members", for classes that cannot all be instantiated, and the dynamic path (§9.1) needs
runtime introspection. Both are backed by the same §3a classifier as the commandlet.

### 3d. Blueprint-accessibility filtering

`GetObjectProperties` currently walks every `FProperty` via `TFieldIterator` with **no** flag
filtering — zero `CPF_Blueprint*` checks in the function today.

| Surface | Condition |
|---|---|
| readable property | `CPF_BlueprintVisible` |
| writable property | `CPF_BlueprintVisible && !CPF_BlueprintReadOnly` |
| callable function | `FUNC_BlueprintCallable \|\| FUNC_BlueprintPure`, and not `FUNC_Delegate` |

These flags are part of the serialized property and **do survive cooking**, unlike metadata (§5.1).
The filter applies to the schema walk only; the existing string-keyed RPCs stay unfiltered, as the
escape hatch for everything the typed layer hides.

### 3e. Closing the unit and batching gaps

Three concrete proto gaps that revision 1 missed, all verified against the current file:

- **`CallFunctionRequest` has no `native_units`.** Rotator and transform *arguments* to generated
  methods would still be converted. Add it.
- **`SetPropertyOp` has no generic-`Value` variant** — only the 38 typed `Set*PropertyRequest`
  ones. So `batch()` (§6.3) cannot carry native-unit writes at all. Add
  `SetPropertyRequest generic_op`.
- **`FunctionResult.value` is a `string`.** `d = car.GetDistanceTo(other) -> float` (§6.4) therefore
  needs `CallFunction` to return typed `Value` results honoring `native_units`. Phase 2 is **not**
  "reuse `ParameterDescriptor` as-is with no server work".

---

## 4. Architecture

```
editor commandlet  (source of truth -- has metadata, runs on every PR)
    │
    ▼
tempo_schema.json          ← language-neutral, versioned, hashed, deterministic
    │
    ├── gen_py_classes.py   → <project>-classes wheel
    │                          .py        class identity
    │                          .pyi       the type surface
    │                          .meta.json per-module runtime types (§8.2)
    ├── gen_rust_classes.py → (later)
    └── gen_cpp_classes.py  → (later)

packaged sim ──► DescribeClasses ──► verification test (§5.4) + dynamic path (§9.1)

tempo_sim (hand-written runtime)
    Actor / Component / StructProxy  → GetProperty / SetProperty / CallFunction
```

Both the commandlet and the RPC are backed by the one classifier in §3a, so they cannot disagree.
Generated code carries only names, types and a metadata table — **no generated method bodies
anywhere**, which is what makes a 30,000-member artifact tractable.

---

## 5. Where generation runs

Revision 1 harvested from the packaged sim over gRPC. That was wrong on two counts, and the fix
reverses it.

### 5.1 A cooked build has no metadata

`WITH_METADATA` is defined as `WITH_EDITORONLY_DATA`
(`Engine/Source/Runtime/Core/Public/Misc/CoreMiscDefines.h:32`), and `FField::GetMetaData`,
`FindMetaData` and `HasMetaData` all sit inside `#if WITH_METADATA` in
`CoreUObject/Public/UObject/Field.h`. So in a packaged build:

- `tooltip` and `category` are always empty — the "becomes the generated docstring" promise cannot
  be kept;
- `BlueprintGetter` / `BlueprintSetter` are invisible, so the harvester cannot even *detect* the
  case in §5.3.

Property *flags* survive cooking, so the core type information was never at risk — but the design
depended on metadata it could not see.

### 5.2 PR-time checking needs a schema without a package

The drift goal (§1) wants breakage caught when the breaking change is proposed, not at release. A
packaged harvest can only run after `BuildCookRun`, so the earliest signal is release time. An
editor build is something CI already produces and runs headless (`Scripts/Test.sh`), so a commandlet
can run on every PR.

### 5.3 The resolution

1. **Generate in the editor.** A commandlet in a new `TempoWorldEditor` module writes
   `tempo_schema.json`, with metadata, explicitly excluding editor-only modules and `CPF_EditorOnly`
   properties. Blueprint discovery is also *easier* here: the asset registry is fully populated and
   `GetDerivedClassNames` works, which softens Risk 3.
2. **Filter against the cook.** At release, intersect the schema with what was actually cooked, from
   the cook's asset registry output (`Packaged/AssetRegistry.bin`, already copied by
   `Package.sh`). No sim launch needed.
3. **Verify against the packaged build.** An integration test launches the packaged sim — as
   `test_packaged.yml` already does — and asserts every generated class, property and function
   resolves at runtime with the declared type. This keeps the packaged harvest's real advantage
   (ground truth for what ships) without making the package the *source* of truth.

On `BlueprintGetter`/`BlueprintSetter`: seeing them in the editor does not let a packaged server
honor them — `SetProperty` writes property memory directly either way, skipping a setter that might,
say, mark render state dirty. Detection only enables the fix, which is for the typed layer to route
those members through `CallFunction` on the accessor instead of through `SetProperty`. That is why
`ClassPropertyDescriptor` carries the accessor names (§3c).

### 5.4 Endpoint, for the verification test and the dynamic path

Prefer a **Unix domain socket** where supported (`TempoServerEndpoint::IsUnixSocketTransportSupported()`
— true on Linux/macOS, false on Windows, where the vendored gRPC 1.62 has no AF_UNIX). Launch with
`-ServerSocket=<bare name>`, falling back to `-ServerPort=`. A verification run that grabs TCP 10001
would collide with a developer's editor or a parallel CI job; the socket transport also refuses to
displace a live server. Honor the established `TEMPO_SERVER_SOCKET` / `TEMPO_SERVER_PORT` contract.

### 5.5 Distribution

The schema is build-specific — engine version (5.7 and 5.8 both supported, and they differ), enabled
plugins, and project Blueprints all change it — so it cannot live in the canonical,
PyPI-publishable `tempo-sim`.

| Distribution | Contents | Built |
|---|---|---|
| `tempo-sim` | runtime proxies, `get_actors`, `spawn_actor`, schema/metadata loader, base `Actor`/`Component`/`StructProxy` | hand-written, as today |
| `<project>-classes` (e.g. `tempo-sample-classes`) | every class in the build: engine, plugin, project | per release |

`TestPythonAPI.sh` already installs *all* wheels from `Packaged/API/Python/`, so the classes package
is picked up by the test jobs with no script change. The generated package registers its classes
with `tempo_sim` on import so narrowing and spawning can resolve a `UClass` name to a Python class.

---

## 6. The Python surface

### 6.0 Where the entry points live

The examples below say `tempo_world.get_actors(...)`, which is where they belong by rights. They are
**`tempo_sim.world`** instead, because `tempo_sim.tempo_world` is generated from the protos by
`gen_api.py` and hand-written functions cannot live in a generated module without making the
generator preserve them -- a fragile arrangement for no gain. So:

```python
from tempo_sim import world
from my_project_classes.Game.Vehicles import BP_Cat745_C

for truck in world.get_actors(BP_Cat745_C):
    truck.MaxSpeed = 1200.0
```

The value types and the base class live in `tempo_sim.unreal`, which is also where the runtime
behavior is.

### 6.1 Discovery by type, not by name

An actor *name* is a runtime string: no type checker can infer a class from `get_actor("my_actor")`,
and a hard-coded name is itself drift-prone (editor labels differ from packaged names —
`docs/concepts/naming.md`). So the API leads with type-based discovery, where there is no unchecked
string at all:

```python
from tempo_sample_classes.Game.Vehicles import BP_Cat745_C

for truck in tempo_world.get_actors(BP_Cat745_C):    # -> list[BP_Cat745_C]
    truck.MaxSpeed = 1200.0
```

`get_actors(Cls)` returns instances of the class **and its subclasses**. No new RPC is strictly
required: the schema carries the full inheritance graph, so the client can compute the subclass set
and filter `GetAllActors` locally. A server-side class filter is a later optimization.

Name lookup remains available and narrowing is checked at runtime:

```python
truck = tempo_world.get_actor("Cat745_2", BP_Cat745_C)   # -> BP_Cat745_C; raises if it isn't one
```

### 6.2 Spawning, including deferred initialization

Typed spawning is nearly free — `SpawnActor` already takes a class — and it is a primary use case,
so it belongs in Phase 1, not Phase 4:

```python
truck = tempo_world.spawn_actor(BP_Cat745_C, transform=t)     # -> BP_Cat745_C

with tempo_world.spawn_actor_deferred(BP_Cat745_C, transform=t) as truck:
    truck.InitialTerrainHeight = 1.0    # before construction script / BeginPlay
# FinishSpawningActor on exit; the block's writes go out as one batch
```

Without the deferred form, properties read by a construction script or `BeginPlay` are unreachable
from the typed API. The wire support already exists (`SpawnActorRequest.deferred`).

### 6.3 Properties and batching

```python
truck.bEnabled = True
print(truck.Mass)                              # one GetProperty round trip
truck.PostProcessSettings.BloomIntensity = 2.0 # nested dot-path, no new server work
truck.WaypointTags = ["alpha", "bravo"]        # whole-array replace

with tempo_world.batch():          # buffered, flushed as one SetProperties (needs §3e)
    truck.MaxSpeed = 1200.0
    truck.Mass = 1500.0

with truck.snapshot():             # one GetProperties; reads served from the snapshot
    print(truck.MaxSpeed, truck.Mass)
```

Naive attribute access is one RPC per attribute — a real regression against today's explicit
batching, so these two context managers are part of the deliverable, not a nicety.

### 6.4 Functions and components

```python
truck.SetActorTickEnabled(True)
d = truck.GetDistanceTo(other)                            # needs typed results, §3e
cam = truck.component("TempoCamera", TempoCameraComponent)
truck.Mesh.RelativeLocation = Vector(0, 0, 150)           # Phase 3, generated from SCS
```

---

## 7. Units

The typed API is **Unreal-native throughout**: centimeters, degrees, left-handed. These are raw
`UPROPERTY` values with no reliable semantic tagging, so converting them is guesswork.

`Value` already converts `rotator_value`, `quat_value` and `transform_value` from radians/
right-handed, so reusing it unchanged would double-convert. Hence `native_units` on
`GetPropertyRequest`, `GetPropertiesRequest`, `SetPropertyRequest` **and `CallFunctionRequest`**
(§3e):

- `native_units = false` (default): today's behavior, unchanged for every existing caller.
- `native_units = true`: no conversion at all. The typed layer always sets it.

This also regularizes the documented wart where scalar and vector setters don't convert but
transform-shaped ones do (`docs/concepts/conventions.md#the-exception-set_property`). Inside the
typed API there is no exception.

**Consequence to flag loudly:** two `Vector` types in one client —
`tempo_sim.unreal.Vector` (cm, left-handed, typed API) and
`tempo_sim.TempoCore.Geometry_pb2.Vector` (m, right-handed, every other RPC). Distinct names and an
explicit conversion helper, never implicit coercion. Generated struct and enum names must be checked
against the hand-written runtime names so a harvested `Vector` cannot shadow `tempo_sim.unreal.Vector`.

---

## 8. Generated artifact

### 8.1 Measured ceiling

Declarations in UE 5.7 (counted from engine headers; a build links a subset):

| Source | `UCLASS` | `UPROPERTY` | `BlueprintReadWrite/ReadOnly` | `BlueprintCallable` |
|---|---|---|---|---|
| `Engine/Source` | 4,268 | 27,541 | 6,365 | not counted |
| `Engine/Plugins` | 9,346 | 52,820 | 13,284 | 12,570 |

Ceiling ~13,600 classes / ~80,000 properties, ~19,600 Blueprint-accessible. A TempoSample-shaped
build links a fraction — order 2,000–4,000 classes, 20,000–40,000 accessible members — but
"all types including engine" has to survive the upper end.

### 8.2 Three files per module

Per Unreal package, e.g. `/Script/Engine` → `Engine.{py,pyi,meta.json}`:

```python
# Engine.py — tiny, loaded at runtime
class StaticMeshComponent(SceneComponent):
    __tempo_class__ = "/Script/Engine.StaticMeshComponent"
```

```python
# Engine.pyi — large, never loaded at runtime
class StaticMeshComponent(SceneComponent):
    RelativeLocation: Vector
    @property
    def Mass(self) -> float: ...                      # read-only: no setter (§9.2)
    def SetStaticMesh(self, NewMesh: StaticMesh) -> bool: ...
```

**`Engine.meta.json` is not optional.** To build a `SetProperty` the runtime must choose a `Value`
variant, and Python types do not determine it: a `str` could be `string_value`, `enum_value`,
`class_value`, `asset_value`, `actor_value` or `component_value`; an `int` could be `int_value` or
`int64_value`. The "did you mean" check (§9.3) likewise needs the member list. Revision 1 said the
`.py` holds only `__tempo_class__` and left this registry unspecified — a genuine hole.

So: a per-module metadata table (member → `Value` variant, flags, accessor names), **loaded lazily
on first use of a class from that module**, and emitted in the same generator run as the `.pyi` so
the two cannot disagree.

Why the split earns its keep: runtime cost is one small class object per Unreal class, plus the
metadata for modules actually touched. Under PEP 563 (`from __future__ import annotations`, forced by
the `>=3.9` floor in `pyproject.toml`) class-body annotations still build an `__annotations__` dict
of strings at class creation — ~40,000 interned strings if they lived in the `.py`. Type checkers
read the `.pyi` and ignore the `.py`, so the full surface is statically available at zero import
cost. The classes are still real runtime objects, so narrowing, `isinstance` and the registry work —
which a stub-only (PEP 561 `-stubs`) distribution could not provide.

Rejected: a real `@property` descriptor per member — ~40,000 descriptor objects, slow import, no
static benefit over the `.pyi`.

### 8.3 Where Blueprint classes land

`/Script/Engine` → `Engine` does not extend to Blueprints, which live in `/Game/...` content paths.
(Revision 1's own example, `tempo_sample_classes.TempoSample.BP_Vehicle_C`, didn't follow its own
rule.) Use **path-based modules**: `/Game/Vehicles/BP_Cat745` → `<project>_classes.Game.Vehicles`.

### 8.4 Class identity is the path, not the name

Two Blueprints in different folders can share a class name. Generation must fail on a duplicate
within a module, and every generated class carries its full `class_path` so the runtime spawns and
narrows by exact class.

**Resolved:** `GetSubClassWithName` learns about paths rather than the request gaining a field. A
class name can never contain a `/`, so a leading one unambiguously marks a path and cannot shadow a
name. Callers that pass a name — every existing client, `SpawnActor` included — keep the documented
first-match behavior exactly; the typed API and its stubs always pass a full path and get the one
class they meant. This keeps AGENTS.md §6's rule that every client-supplied class name resolves
through that one helper, and it means a client that already had a path in hand stops getting no
class at all. Implemented as `GetSubClassWithPath<T>` in `TempoClassUtils.h`, which
`GetSubClassWithName` delegates to.

---

## 9. The static guarantee

This section exists because it is the feature. Each item below is a way the guarantee silently
evaporates.

### 9.1 No catch-all on the statically visible `Actor`

If Pyright can see `__getattr__` / `__setattr__` on `Actor`, it accepts **any** attribute on
`Actor` *and on every generated subclass*. `truck.MxSped = 1200.0` would type-check. A removed
property would type-check. That is precisely the case the feature exists to catch, and it defeats
the entire design — the single most important correction in revision 2.

Therefore:

- the `.pyi` for `Actor` declares **no** catch-all (the runtime `.py` still has one);
- the dynamic, schema-free path returns a **distinct** `DynamicActor` type that does declare it, so
  the escape hatch is visible at the call site and never leaks into a typed hierarchy;
- a Pyright negative test asserts that an undeclared attribute on a generated class is an error
  (§11).

### 9.2 Read-only members need the `@property` form

A plain `Mass: float` in a `.pyi` **is writable** — revision 1's claim that read-only fell out of
"no annotation for a writable form" was simply wrong. Read-only members must be emitted as a
setter-less property:

```python
@property
def Mass(self) -> float: ...
```

Pyright reports assignment to it as an error. (`Final` is not the right tool: it also forbids
reassignment in subclasses and carries different semantics.) This is exactly the "property became
read-only" drift case.

### 9.3 Runtime behavior

- **Validation.** The proxy checks names against the per-module metadata (§8.2), so a typo raises
  immediately with a "did you mean `MaxSpeed`?" rather than a server `NOT_FOUND`.
- **Writability.** Writing a read-only property is a static error *and* a runtime `AttributeError`.
- **Narrowing.** `get_actor(name, Cls)` verifies the actor really is a `Cls` or a subclass, and
  raises otherwise.
- **Accessors.** A member with a `BlueprintSetter` is written through `CallFunction` on the
  accessor, not `SetProperty` (§5.3).

### 9.4 Unsupported types are omitted

Anything the classifier reports as unsupported is **left out of the `.pyi`** — never typed `Any`.
Using it is then an error rather than a silent pass. `unsupported_reason` (§3c) is what the error
message and the drift report quote.

---

## 10. Release-to-release drift

Distinct from "stubs vs. the sim they are used against", and the thing the goal in §1 actually asks
for. Revision 1 only had the former.

### 10.1 Deterministic output

Sorted keys, no timestamps, stable formatting, in both `tempo_schema.json` and the generated files,
so two releases diff cleanly. Without this, every other item here is unreadable noise.

### 10.2 A drift report

Diff each release's schema against the previous one and emit removed/changed classes, members and
signatures as a reviewable list, so a breaking change is a decision made before release rather than
a discovery after it.

### 10.3 A PR-time check

The commandlet (§5) runs on every PR, so the schema diff can gate the PR that causes it. This is the
main practical payoff of moving generation into the editor. The check must be **reusable by
downstream projects** — the same pattern as `test_packaged.yml` — because the drift that breaks
scenarios happens on the *consuming* project's releases, not on Tempo's.

`diff_schema.py` is the tool: it compares two schemas, lists what a client could break against,
counts additions without listing them, and exits non-zero when anything is breaking.

**What it needs, and the one thing still undecided: a baseline to diff against.** The schema is
~20 MB, far too large to commit, so the previous state has to come from somewhere. Three options,
none of them obviously right:

1. **The previous release's schema, published as a release artifact.** Cheapest to check (one
   download) and matches what a consumer actually upgrades from, which is the comparison that
   matters. Needs the release process to publish it.
2. **Regenerate from the base branch in CI.** No stored artifact, always exact, but it means a
   second editor build and schema walk per PR — minutes, not seconds.
3. **Commit a digest** rather than the schema: one line per member, enough to diff. Perhaps 2–3 MB
   for this build, so still a large committed artifact that changes on nearly every PR.

Option 1 is the recommendation, and it is a release-process decision rather than a code one, which
is why it is called out here instead of implemented.

### 10.4 A strictness policy for consuming repos

A type checker enforces nothing unless CI requires it, and Tempo cannot require it in someone
else's repository. What Tempo owes is the **mechanism**: stubs that make the drift a type error, a
reusable PR check, and a documented, recommended configuration — Pyright in strict mode, or at
minimum `reportUnknownMemberType`, `reportUnknownArgumentType` and
`reportUnnecessaryTypeIgnoreComment` as errors. (The last of those is what stops an ignore comment
outliving the thing it silenced.)

A scenario repo that declines to adopt it, lets `Any` leak in, or scatters `# type: ignore` gets a
weaker guarantee, and that is its own call to make. The design's obligation is that the mechanism is
there, correct, and easy to switch on — not that it is universally switched on.

### 10.5 Schema identity

`SchemaHeader` carries `schema_version`, `engine_version` and `content_hash`, and the client
compares the hash its stubs were generated from against the one the sim reports. Two supported
engine versions make this mandatory.

**Revised during implementation: the check is explicit, not on-connect.** Revision 2 said the
runtime would fetch the header on first connect. It cannot affordably: the sim computes its hash by
walking reflection, and that walk is dominated by loading every Blueprint — 55–95 s measured. Paying
that on every client start is worse than the problem it detects. So `world.check_schema()` is
called deliberately — from a test, or once at the start of a long run.

The on-connect version becomes affordable once a packaged build reports a hash computed at package
time rather than on demand, which is build plumbing rather than client work. Scoped with the
packaging step.

---

## 11. Testing

Per AGENTS.md §7: pure logic in C++ unit tests, client-facing behavior in the packaged Python suite.

**C++ (`TempoWorld/Private/Tests/`, `Tempo.World.Reflection.*`).** The §3a classifier is
world-independent by construction, which is what makes this cheap — the same shape as
`Tempo.Core.ServerEndpoint`:

- the classifier over hand-built flag/type combinations, including every `unsupported_reason`;
- `value_expressible` and the accepted-variant set;
- the declared-vs-inherited split over a synthetic class chain;
- `GetValueFromTarget` / `SetValueOnTarget` round trip on a transient `UObject` (the
  `FKinematicTestFixture` pattern — `NewObject` into `GetTransientPackage()`, no RHI).

**Pyright behavior tests — the ones that test the actual guarantee.** A golden-text test catches
changes in emitted text but says nothing about type-checker behavior. Generate from a fixture
schema, run Pyright over known-good and known-bad snippets, and assert which errors appear:

| Change in a new release | Expected Pyright error |
|---|---|
| Property removed | Unknown attribute |
| Property type changed | Assignment / usage type mismatch |
| Property became read-only | Assignment to read-only property |
| Function removed or signature changed | Call error |
| Enum value removed or renamed | Unknown member |
| Class removed or renamed | Unresolved import |
| Component removed (Phase 3) | Unknown attribute on the component accessor |
| **Any undeclared attribute on a generated class** | **Unknown attribute** — guards §9.1 |

**Python integration.** A `classes` marker in `Tests/Python/pytest.ini` for the wheel-only surface
check (no sim), plus typed get/set/spawn coverage in the existing `world` group. The §5.3
verification test — every generated member resolves against the packaged sim — is its own group. A
new group needs a matrix entry in `tempo_build_and_package.yml`.

---

## 12. Phasing

| Phase | Scope | Why this order |
|---|---|---|
| **0 — Spike** | §3a classifier; editor commandlet; run against TempoSample; publish class/member counts, `.pyi` size, Pyright cold-check time, Blueprint-load cost, and whether metadata is populated for the classes that matter | Every §8 decision is a guess until this exists, and it proves the commandlet choice. |
| **1 — The guarantee, end to end** | typed get/set (`GetProperty`/`GetProperties`), `native_units` everywhere incl. `CallFunction`, generic `SetPropertyOp`, BP-accessibility filter, runtime proxy + `DynamicActor` split, per-module metadata, **`get_actors(Cls)`, typed `spawn_actor`, deferred spawn**, class-path identity, read-only `@property` form, deterministic output, drift report, PR check, Pyright tests, docs | Type-based discovery and spawning are the primary use case, and the §9 items are the feature — none can slip. |
| **2 — Functions** | typed `Value` results on `CallFunction`; generated method signatures | Needs server work (§3e), not just reshaping. |
| **3 — Components, structs, enums** | SCS/default-subobject accessors; nested `StructProxy`; `UENUM` → Python `Enum` | Needs Phase 1's proxy. |
| **4 — Ergonomics** | property streaming, server-side class filter for `get_actors`, asset catalog | Optimizations once the shape is settled. |

**Phase 2 notes.** `FunctionResult` gained `typed_value` rather than changing `value`: the string
form is published API and still useful to read, so both ship, with the typed one documented as the
one a program should act on. A call frame is just another container, so the typed result is the
property reader pointed at the frame — which is what makes a returned vector identical to the same
vector read off a property, and is asserted as such. On the client, a generated method returns its
return value directly, or a dict keyed by name when the function also has out parameters, which is
how a Blueprint usually hands back more than one thing; position would be guesswork.

**Phase 3 notes.** Components come from two places, because Unreal has two ways of declaring one: a
Blueprint's SimpleConstructionScript, which already holds only its own nodes, and a native class's
default subobjects, which its CDO carries along with every inherited one — so those are found by
difference against the superclass's CDO. A component accessor is read-only: the attribute names a
component that exists, and replacing one is not an assignment.

**Phase 4 scope, as built.** Property streaming requires a tick, and the Control subsystem is not
tickable while the State one is. Rather than move the property machinery (which depends on the
generated `Value` and so cannot live in the deliberately protobuf-free classifier), Control becomes
tickable and the subscription lives beside the rest of the property code. The tick early-returns on
an empty subscription map, so a project that never streams pays nothing measurable.
| **5 — Rust** | `gen_rust_classes.py` + a `tempo_sim::unreal` runtime, off the same `tempo_schema.json` | Designed and measured in §15. |
| **6 — C++** | generator off the same `tempo_schema.json` | Not started. |
| **7 — Published classes** | an engine-core + Tempo subset inside the published `tempo-sim` wheel and crate, namespaced by engine version; per-class drift checking | Designed and measured in §16. Independent of 6. |
| **8 — Struct values** | a `StructValue` variant; value-expressible structs in the classifier; struct arguments, results and whole-struct writes in both clients | Designed in §17. A quarter of the function surface needs it. |
| **9 — One ordered batch** | `RunBatch` carrying sets, gets and calls in order within one tick; deferred and immediate reads in both clients | Designed in §18. Fixes reads inside a batch seeing pre-batch values. |
| **10 — Asset catalog** | cooked assets from the packaged build; `AssetRef[T]` typed by class; a generated catalog per mount point; drift reporting | Designed in §19. |

### Phase 0 results (measured)

Run against TempoSample on **UE 5.8**, macOS, via `-run=TempoWorldSchema`. The schema is
version-specific, so these are one engine's numbers.

| | |
|---|---|
| Classes | 12,155 (534 Blueprint) |
| Blueprint-visible properties | 21,290 — 15,659 writable, 163 unsupported |
| Properties not Blueprint-visible (filtered out) | 24,688 |
| Blueprint-callable functions | 21,415 — 265 uncallable |
| Functions not Blueprint-callable (filtered out) | 3,878 |
| Structs / enums referenced | 1,522 / 1,190 |
| `tempo_schema.json` | 21.5 MB (24.0 MB with documentation) |
| Blueprint load | 55–95 s depending on cache state |
| Walk + write | 0.2 s |
| `.pyi` total | 3.1 MB, or 3.9 MB with docstrings |
| `.pyi` modules | 756 |
| Members emitted / omitted | 38,227 (17,100 methods, 5,468 read-only) / 4,478 |
| Pyright, scenario importing two classes | **0.48 s** |
| Pyright, whole 13,004-line `Engine.pyi` | **1.33 s** |
| mypy, same two (for contrast) | 17.9 s / 62 s |

Six things this settles or changes.

**Class count was badly underestimated.** §8.1 guessed 2,000–4,000 from engine headers; the real
figure is **12,155**, because a project loads far more engine-plugin classes than the headers
suggest. Member counts were close, though: 38,227 emitted against an estimated 20,000–40,000.

**The editor-commandlet decision (§5) is confirmed by data, not just by argument.** This build has
**306 properties that are writable and carry a `BlueprintSetter`** — 414 with either accessor. A
packaged harvest cannot see one of them, so the typed API would have written straight through all
306, skipping setters that may be the only thing clamping the value or marking render state dirty.
Documentation says the same: 7,813 tooltips and 21,246 categories, none of which survive cooking.

**The content hash behaves as designed.** Two runs whose only difference was added documentation
produced the *identical* hash (`C53FB015…`), because it covers names, types and writability and
nothing else. A new tooltip is correctly not a drift event.

**Artifact size is a non-issue.** 3.1 MB over 756 files is unremarkable, so **decision (d)
resolves to a single wheel**. The distribution is lopsided — `/Script/Engine` alone is 2,069
classes, 6,084 members, 13,004 lines and 541 KB, while 575 of 756 modules hold ten classes or
fewer — but see below: that lopsidedness turned out not to cost anything worth fixing.

**Type-check cost is a non-problem, and nearly became a wrong decision.** Measured first with
`mypy` — the only checker to hand — it looked alarming: 62 s for `Engine.pyi` and 17 s for a
two-class scenario, which argued for sharding `Engine`. Pyright, the checker the goal in §1
actually names, does the same work in **1.33 s and 0.48 s**: roughly 47× and 37× faster. Sharding
`Engine` is therefore **not** needed, and the plan is not going to do it. The near-miss is worth
recording as a method note: a proxy tool's numbers were wrong by more than an order of magnitude,
in the direction that would have bought complexity nobody needed.

**The guarantee was verified end to end, on real generated stubs.** Not argued — run. Against
`Engine.pyi` generated from this schema, Pyright reports an error for a misspelled attribute
(`reportAttributeAccessIssue`, which is the §9.1 guard doing its job) and for assigning to a
`BlueprintReadOnly` property emitted as a setter-less `@property` (§9.2), and reports nothing for
valid use. The two drift cases the whole design exists to catch are confirmed to be catchable.

Pyright also pointed out a design detail in passing: checking a `.pyi` with no `.py` beside it
raises `reportMissingModuleSource`. That is exactly the pairing §8.2 calls for, so emitting the tiny
runtime `.py` is not only for class identity — it also keeps the checker quiet.

Two operational notes for Phase 1.

On macOS the editor process exits non-zero after a commandlet that succeeded (trace-server shutdown
noise), so the wrapper script must read the commandlet's own result — `Commandlet ... finished
execution (result N)` — or check the output file, never the process exit code.

Pyright installs from PyPI (`pip install pyright`) and bootstraps its own prebuilt Node into the
venv, so the PR check in §10.3 needs no system Node and no change to the CI image — one line in a
requirements file. Worth preferring over a system dependency given how the CI image is built.

### Out of scope

- Replacing the string-keyed API — it stays as the low-level layer and the escape hatch.
- Delegates and multicast events (`FUNC_Delegate` already excluded).
- C-style array properties (`ArrayDim != 1`), already `"unsupported"`.
- Making actor *names* statically checkable (§6.1 sidesteps the need).

---

## 13. Risks

1. **A hole in the static surface** silently voids the guarantee — §9.1 is the known one; the §11
   negative tests exist to catch the next. → Pyright tests gate CI.
2. **Checker performance — measured and closed.** Pyright checks the largest module in 1.33 s and
   a realistic scenario in 0.48 s, so neither the 3.1 MB total nor the lopsided `Engine` module
   needs addressing. Re-measure if the member count grows by an order of magnitude; do not
   re-measure with mypy, which was wrong here by ~47×.
3. **Blueprint loading.** Enumerating Blueprint classes means walking the asset registry and
   loading every `BlueprintGeneratedClass` — slow and memory-hungry on a large project, and the most
   likely thing to make Phase 0 fail. Easier in the editor (populated registry,
   `GetDerivedClassNames`) than it was in a cooked build. → `base_classes`/path filters, measure in
   Phase 0.
4. **Editor/cook divergence.** The commandlet sees more than ships. → §5.3 cook filter plus the
   §5.4 packaged verification test, which is what makes the reversal safe.
5. **Four classifiers drifting.** → §3a makes it one, in a public header.
6. **Round-trip amplification.** 1,000 actors × one property = 1,000 RPCs. → `batch()`/`snapshot()`
   in Phase 1, documented from the start.
7. **Two `Vector` types**, and generated names shadowing runtime ones. → distinct module paths,
   explicit conversion, a generation-time name check (§7).
8. **Consumer CI not enforcing strictness** — the guarantee is only as strong as the weakest
   consuming repo's Pyright config. → §10.4 ships the policy, not just advice.

---

## 14. Open decisions

**(a) Member naming.** Settled on **verbatim Unreal names** (`truck.MaxSpeed`, `truck.bEnabled`):
collision-free, matches the editor and the existing docs, and a rename in Unreal maps 1:1 onto a
rename in scenario code — which is the point. snake_case would need an acronym-aware rule the
existing `pascal_to_snake` lacks (`FOVAngle` → `f_o_v_angle`). Offering both doubles the stub and
splits the docs. The generator fails the build on collisions either way.

Rust answers this differently, and the acronym objection does not survive contact with a two-pass
rule — see §15.3. Python keeps verbatim names; each language gets the names its users would guess,
and the schema remains the one place both are derived from.

**(b) Does `DescribeClasses` stay? — settled: yes.** Even with the commandlet as source of truth,
the §5.3 verification test needs class-level introspection of a running sim for classes it cannot
instantiate, and the dynamic path needs it too. Same classifier behind both.

**(c) Harvest breadth — settled: everything.** The walk itself costs 0.2 s; the only real cost is
loading Blueprints (55–95 s), which is affordable for a per-PR check. `base_classes` stays
available for narrowing a run, and the tests use it to stay fast.

**(d) Distribution granularity — settled: one wheel.** 3.1–3.9 MB over 756 modules does not justify
splitting the distribution. The granularity that does need work is *within* it: `/Script/Engine`
must be sharded (see Phase 0 results).

**(e) Path-aware class resolution — settled:** `GetSubClassWithName` learns about paths; name
callers keep first-match. See §8.4.

**(f) A `TempoWorldEditor` module — settled: yes.** TempoWorld is single-module today; the schema
commandlet needs an editor module, and it is an ordinary addition.

---

## 15. The Rust surface

Phase 5. The same `tempo_schema.json`, a second generator, and a hand-written runtime that mirrors
`tempo_sim.unreal` / `tempo_sim.world`. Everything below was measured against the real packaged
schema for TempoSample on UE 5.8 — 6,068 classes, 5,500 properties, 13,054 functions, 267
components, max inheritance depth 9 — not estimated.

### 15.1 Rust has the inheritance this needs

The earlier assessment held that Rust would be much harder than Python because it has no
inheritance. That was wrong, and the reason is worth keeping: **generated code carries no data.** A
handle is an actor name, a component name and a class path; a generated class adds names and types
and nothing else. So there is no state to inherit — only a method surface — and Unreal's single
inheritance maps exactly onto a chain of `Deref`:

```rust
#[repr(transparent)]
pub struct TempoLidar(pub ObjectRef);

impl std::ops::Deref for TempoLidar {
    type Target = TempoTiledSceneCaptureComponent;
    fn deref(&self) -> &Self::Target { TempoTiledSceneCaptureComponent::from_ref(&self.0) }
}
```

One impl per class, not one per (class, ancestor) pair. Deref coercion then does the two things
inheritance is wanted for, both verified to compile:

- **Inherited members resolve.** `lidar.relative_location()` reaches `SceneComponent` five hops up.
- **Polymorphism at a call site.** `fn aim(c: &SceneComponent)` accepts `&lidar`.

`Deref` for inheritance is usually an anti-pattern because it leaks a type's internals; here the
"internals" *are* the base class, which is the relation being modeled.

**The rejected alternative, and why.** The gtk-rs shape — a marker type per class, a transitive
`impl IsA<Ancestor> for Derived`, and accessors in a blanket `impl<T: IsA<C>>` — was built and
measured too. It costs 15,754 `IsA` impls against 6,068 `Deref` impls, and it **does not compile
with inherent methods**: two accessors of the same name under different `IsA` bounds are 3,765
`error[E0592]: duplicate definitions`, because the compiler cannot prove the bounds disjoint. It
would need one extension trait per class, which the caller must bring into scope — so
`lidar.relative_location()` would require a `use` of `SceneComponentExt`, or a prelude importing
6,068 traits. Deref needs neither.

### 15.2 One crate, no feature gates

The feared cost did not materialize. Cold, from `cargo clean`, for the whole 6,068-class surface:

| shape | `cargo check` | `cargo build` | generated |
|---|---|---|---|
| sync only | 6.1 s | 7.3 s | 21,260 methods |
| lazy `.get()` / `.await` | 5.9 s | 7.1 s | 21,260 methods |
| twin `x()` + `x_async()`, async twins as `async fn` | 49.4 s | **51.9 s** | 42,520 methods |
| **twin, async twins returning `impl Future`** | 10.7 s | **13.0 s** | 42,520 methods |

So no cargo features per package, no `bindgen`-style allowlist, no proc macro reading the schema at
compile time — one plain crate, as Python ships one wheel. 41 MB `.rlib`, built once and cached.

**The cost driver is `async fn`, not class count.** Each one compiles to its own state machine, and
doubling the method count that way cost 7×. Writing the async twin as a plain function handing back
the runtime's future keeps the call syntax identical (`lidar.horizontal_beams_async().await?`) and
puts the state machine in the runtime, once per value type instead of once per member:

```rust
pub fn horizontal_beams_async(&self)
    -> impl std::future::Future<Output = Result<i32, TempoError>> + '_
{ self.0.get_async("HorizontalBeams") }
```

That is 13.0 s against 51.9 s for the same surface. The twin form is what every other generated
Tempo Rust API offers (§4), so the typed surface matches it rather than inventing a third
convention.

### 15.3 Naming: snake_case, and §14(a) revisited

§14(a) settled Python on verbatim Unreal names and noted snake_case "would need an acronym-aware
rule the existing `pascal_to_snake` lacks (`FOVAngle` → `f_o_v_angle`)". For Rust the answer is
snake_case anyway, because a Rust user guesses `horizontal_beams`, and the objection turns out not
to apply — a two-pass rule handles acronyms correctly, verified over all 15,611 real member names:

```python
s = re.sub(r"(.)([A-Z][a-z]+)", r"\1_\2", name)
s = re.sub(r"([a-z])([A-Z])",   r"\1_\2", s).lower()
```

`FOVAngle` → `fov_angle`, `MaxFOV` → `max_fov`, `UVChannel` → `uv_channel`, `LODBias` → `lod_bias`,
`HDRCaptureEnabled` → `hdr_capture_enabled`, `SizeXY` → `size_xy`. Note the second pattern excludes
digits deliberately: with `[a-z0-9]` there, `Use2DMode` becomes `use2_d_mode` and `Vector2D`
becomes `vector2_d`, which is wrong 147 times in this schema. A reserved word gets a trailing
underscore (`Box` → `box_`), as Python's generator already does.

Measured collisions, per class, over the whole schema:

| | |
|---|---|
| property vs. property | **0** |
| function vs. function | **0** |
| a property's setter vs. a `Set*` function | **257** |

Only the last is real, and it is the same shape as the component/property clash Python already
resolves: `RadialSlider` has a `ShowSliderHandle` property and a `SetShowSliderHandle` function,
and both want `set_show_slider_handle`. **The property wins and the function is omitted**, counted
in the generator's stats. They do the same thing — and where the property has a `BlueprintSetter`,
the property path already calls the accessor (§8.2), so nothing is lost.

### 15.4 The guarantee, verified

The point of the typed surface is that drift fails the build. Each case below was compiled:

| case | result |
|---|---|
| misspelled property | `error[E0599]: no method named horizontal_beems` |
| write to a read-only property | `error[E0599]: no method named set_b_color_enabled` |
| wrong value type | `error[E0308]: expected i32, found &str` |
| a component passed as an actor | `error[E0308]: expected &Actor, found &TempoLidar` |
| a property of an unrelated class | `error[E0599]: no method named max_draw_distance_scale` |
| all four of the above on the `_async` twin | same errors |

Rust states the read-only case better than Pyright does: a read-only property simply has no setter,
so the error names the method that does not exist rather than complaining about an assignment.

**§9.1 has no Rust analogue, which is the good news.** Python needs a `.pyi` that deliberately
omits `__getattr__`, because a catch-all visible to the checker would make every attribute legal.
Rust has no catch-all to hide: an undeclared method is an error with nothing to suppress it. The
dynamic escape hatch is therefore an explicit method on the handle, not a separate type.

### 15.5 Phasing

| | | |
|---|---|---|
| **5a** | `tempo_sim::unreal` — handle, `unsafe trait Class`, value types, `Value` conversion, sync + async get/set/call, batch, snapshot | mirrors `unreal.py` |
| **5b** | `tempo_sim::world` — `get_actors`, `get_actor`, `spawn_actor`, `spawn_actor_deferred`, `batch`, `check_schema` | mirrors `world.py` |
| **5c** | `gen_rust_classes.py` — enums, structs, one module per package, the crate manifest | mirrors `gen_py_classes.py` |
| **5d** | `GenTypedAPI.sh` stages the crate; `Package.sh` ships it to `Packaged/API/Rust` | behind `TEMPO_GEN_RUST_API=1`, as the RPC generator is |
| **5e** | tests — compile-rejection cases in the `codegen` group, live cases in the Rust `integration` group | §11 |
| **5f** | docs — a Rust tab in `docs/guides/typed-classes.md`, reference in `docs/clients/typed-classes.md` | |

The one piece of `unsafe` lives in 5a, in `from_ref`, and is what `#[repr(transparent)]` exists to
make sound. Generated code contains none: it gets the cast through an `unsafe trait Class` whose
contract is "I am a transparent newtype over `ObjectRef`", which only the generator implements.

---

## 16. Published classes

Phase 7, and independent of Phase 6. Everything above generates per project, per release:
`<project>-classes` describes one build and ships beside it in `Packaged/API`. That is the only way
to describe a build *completely* — and it means a user cannot `pip install` their way to a typed
surface, and has none at all until they have packaged one. A published subset closes that gap for
the part of the surface that does not vary.

### 16.1 The variable is the plugin set, not the engine release

The obvious reading is that a published package is impossible because the engine API is specific to
a UE release — hence one package per release, `tempo-sim-classes-5.8`. That is the wrong axis.
Measured on TempoSample: an **editor reports 11,297 classes and the packaged game ships 6,068**, so
5,229 — 46% — exist only because of what that project enabled or what its editor loaded. A package
that guessed at those would be wrong about nearly half its content, and wrong in the worst
direction: a stub that type-checks and then fails at runtime, which is the exact inverse of §1.

So the criterion is not "which engine version" but **which modules ship in every build, whatever
the project enables**. That is checkable, and it is what `subset_schema.py` applies.

### 16.2 The module set

| Package | Classes | Why it is always there |
|---|---|---|
| `/Script/Engine` | 1,389 | compiled in, loaded unconditionally |
| `/Script/UMG` | 144 | ditto |
| `/Script/CoreUObject` | 98 | ditto |
| `/Script/SlateCore` | 8 | ditto |
| `/Script/DeveloperSettings`, `/Script/PhysicsCore` | 4 each | ditto |
| `/Script/InputCore` | 1 | ditto |
| Tempo's own runtime modules (9) | 82 | a project that has Tempo has these |

**1,730 classes of 6,068** (28.5%). The engine list is hand-written, because "is this module always
loaded" is not a question the schema can answer — the schema is one project's answer to it. Tempo's
half is derived from the `.uplugin` descriptors, so a new plugin is published by existing.

Two exclusions follow from the same rule. `TempoROSBridge` is `EnabledByDefault: false`, so a
project that ignores it never builds it. `TempoROS` is a separate repository the host project
supplies, so a project may not have it at all. `External/` is left out too: those are vendored Epic
plugins whose presence is Tempo's choice to keep making, not a promise to a client.

**The five beyond Engine and CoreUObject are not there for their own classes.** 161 classes between
them is not the point; leaving them out does not remove them from the API, it only makes every
member that *mentions* one degrade to a bare `Object`. With them in, **25 members of 7,652** still
degrade that way — `UAudioComponent::GetModulators()` naming `/Script/AudioExtensions`,
`USkeletalMesh::MeshClothingAssets` naming `/Script/ClothingSystemRuntimeInterface`. 0.33%, and
`Object` is honest about what it is rather than wrong.

### 16.3 Rerooting, which a subset is the first thing to need

A subset is the first thing in this pipeline that can break the schema's one structural invariant:
every class's superclass is also in the document. The editor's full walk and the packaged filter are
both closed over superclasses by construction — a packaged build is a subset of its own editor — so
nothing until now had to think about it. A subset chosen by module is not closed: **32 classes
derive from something outside it, and ten of those are in `/Script/Engine` itself.**
`UEngineReplicationBridge` derives from IrisCore, `UWidgetAnimation` from MovieScene,
`UTempoChaosWheeledVehicleMovementComponent` from ChaosVehicles.

Leaving the dangling path in place is the one outcome to avoid, because **both generators read an
unresolvable superclass as "this class is a root"** — silently, with the stubs still type-checking.
A rooted `TempoWheeledVehiclePawn` would carry its own two properties and nothing else: no `Pawn`,
no `Actor`, and in Rust no `Deref`, so it could not even be passed where an actor is wanted.

So `subset_schema.py` **reroots**: each such class is pointed at its nearest kept ancestor.
`TempoWheeledVehiclePawn` *is* a `Pawn`, so deriving it from `Pawn` is sound; what it gives up is
whatever `WheeledVehiclePawn` declared in between. Nothing a rerooted class declares itself is
lost, and nothing it claims is wrong — there is only less of it, which is what a published subset
is. The whole bill is **55 properties, 200 functions and 6 components**, concentrated in classes
nobody addresses from a scenario; the one worth naming in release notes is
`TempoChaosWheeledVehicleMovementComponent`, which loses the 64 wheel functions ChaosVehicles
declares. A project's own package has them, as it has everything.

### 16.4 One distribution, namespaced by engine version

No new packages. The published classes go inside the existing `tempo-sim` wheel and `tempo-sim`
crate, under an engine-version namespace:

```python
from tempo_sim.UE.v5_8.Engine import Pawn
```
```rust
use tempo_sim::ue::v5_8::classes::engine::Pawn;   // feature = "ue-5-8"
```

`v5_8`, not `5_8`: an identifier cannot begin with a digit in either language, so the user-facing
spelling needs the prefix. CI already runs the schema commandlet on both 5.7 and 5.8, so the
per-version artifacts are nearly free.

**Python ships both versions unconditionally; Rust gates each behind a cargo feature.** The two
languages are not symmetric here and the measurements say so:

| | per UE version | two versions | for comparison |
|---|---|---|---|
| Python, compressed | **0.30 MB** | 0.60 MB | `tempo-sim` wheel is 0.10 MB today |
| Rust, `cargo build` (the crate alone) | **8.7 s** | ~17 s | full 6,068-class crate: 29.3 s |
| Rust, `cargo check` | 5.7 s | ~11 s | full crate: 22.6 s |

A wheel going from 0.10 MB to 0.70 MB is the size the full `tempo-sample-classes` wheel already is,
and Python is lazy — a version nobody imports costs nothing at runtime, and Pyright loads only the
module imported. Rust has no such laziness: 17 s of codegen would be paid by **every** `tempo-sim`
user, including everyone who only ever touches the RPC API. Default-off features are exactly the
tool for that, and §15.2's "no feature gates" holds for its own subject — a project's own classes
crate, which someone opted into by generating it.

### 16.5 Type identity: re-export, do not redefine

The subtle failure, and the one that decides whether a published package is worth having. A project
with both installed would have **two unrelated generated types for `/Script/Engine.Pawn`**:
`tempo_sim.UE.v5_8.Engine.Pawn` and `tempo_sample_classes.Engine.Pawn`. At runtime they are
interchangeable, because dispatch is on the class-path string. Statically they are not:

```python
def aim(p: tempo_sim.UE.v5_8.Engine.Pawn) -> None: ...

aim(sweeper)        # error: BP_StreetSweeper_C derives from the *project's* Pawn
```

A shared helper library written against the published types could therefore accept no project's
Blueprints — which is most of what such a library is for.

So the project generator, when a published subset exists for its engine version, **re-exports rather
than redefines**: a class whose subset entry matches is emitted as
`from tempo_sim.UE.v5_8.Engine import Pawn as Pawn` (`pub use` in Rust) and omitted from the
project's own index. One type per Unreal class, and everything composes. The comparison is per
class, so a project that genuinely diverged still gets its own definition — and since Tempo builds
the engine exactly as Epic ships it (§5), divergence should not arise. A class the subset rerooted
is a divergence by that test, which is correct: the project has the real ancestor and should define
its own.

### 16.6 Two registries, not one

Both generated packages register themselves — Python through `register_classes` at import,
Rust through the self-registering class table — and today both registries hold exactly one entry.
Python's `register()` overwrites; Rust's `OnceLock::set` discards the later call. With a published
package and a project package installed together, whichever loses becomes unresolvable: no
metadata, no `is_a`, no dynamic path.

Both become lists, searched in registration order. There is no overlap to arbitrate, because §16.5
has the project omit from its own index exactly what it re-exports.

### 16.7 `check_schema` has to become per class

`check_schema` compares one SHA-1 over the whole walk against the one the sim reports. **A subset
can never match that**, by construction, so a published package has no way to verify itself — and
`subset_schema.py` accordingly drops `content_hash` from the header and sets `published_subset`,
rather than leaving a number that could only ever mismatch.

The replacement is a hash per class, folded into the whole-walk hash so there is still one
definition of what a class's identity is:

- `HashClass(const FClassEntry&)` in `TempoWorldSchemaWalk`, with `HashResult` accumulating those.
- `content_hash` on `ClassDescriptor`, `StructDescriptor` and `EnumDescriptor`, and in the JSON.
- Both clients compare class by class over what they have, and report the classes the sim lacks and
  the classes whose hash differs. A class the sim has and the package does not is not an error —
  for a subset it is the normal case.

This is a better check than today's regardless of publishing: a project adding one Blueprint
currently invalidates the hash for every class in the build, so the only repair is to regenerate,
and the report cannot say what actually moved. Per class, it can.

### 16.8 What is given up, honestly

A published package has **no entry points of its own**: `/Script/TempoSample` is one class and
TempoSample's Blueprints are 25 more — 0.4% of classes, and 100% of the places a scenario starts.
That is survivable because a handle constructor is public, so `TempoLidar("Sweeper", "Lidar")`
checks every member while leaving the two names unchecked, and `world::get_actors::<Pawn>()` needs
no project class at all. It is a real step down from the full package, and the docs should say so
rather than sell the subset as equivalent.

Bundling also pins the engine classes to Tempo's release cadence: if Epic changes reflection in a
5.8.x patch, the published classes are stale until the next Tempo release, where a separate package
could ship on its own. Reflection rarely moves in a patch release, and `v5_8` is the right
granularity for that risk.

### 16.9 Phasing

| | | |
|---|---|---|
| **7a** | `subset_schema.py` — the module set, the subset, rerooting; `codegen` tests | pure Python, no engine. **Done.** |
| **7b** | per-entry hashes — `HashClass`/`HashStruct`/`HashEnum`, the three proto fields, the JSON writer; C++ unit tests | the §16.7 prerequisite. **Done.** |
| **7b′** | **the build is the authority on members too** — `query_packaged_classes.py` reports its whole declared surface and `filter_schema.py` narrows to it | Not foreseen; see §16.10. **Done.** |
| **7c** | per-entry `check_schema` in both clients; both registries become lists (§16.6) | needs 7b and 7b′. **Done.** |
| **7d** | generate into `tempo_sim/UE/v5_8` (Python) | needs 7a. **Done.** |
| **7e** | `tempo_sim::ue::v5_8` behind a `ue-5-8` cargo feature | needs 7d; the Rust half of §16.4 |
| **7f** | re-export rather than redefine, in both generators (§16.5) | needs 7d and 7e |
| **7g** | CI collects the per-version subset; a release wheel carrying 5.7 and 5.8; docs | |

7a through 7c stand on their own: the per-entry check is worth having before anything is published,
and it is the one piece that cannot be added afterwards without changing a published package's
meaning.

### 16.10 The editor is not the authority on members either

Found while stamping the per-entry hashes, and the reason 7b′ exists. §5.3 established that an
editor cannot be trusted for the class *list* and that the packaged build has to be asked. Members
were still being taken from the editor, and of the 6,068 classes both have, **270 disagree about
what they declare** — along with 72 of 1,034 structs.

`AActor` is one of them. It carries seven `WITH_EDITORONLY_DATA` properties — `ActorGuid`,
`ActorInstanceGuid`, `ContentBundleGuid`, `PivotOffset`, `RuntimeGrid`, `SpriteScale`,
`bIsSpatiallyLoaded` — and eleven editor-only functions including `GetActorLabel` and
`SetFolderPath`. It is the root of two thousand classes, so **every actor in the generated package
declared members that type-check and then fail at runtime**, which is precisely the outcome §1 is
about. 48 further classes differ by an editor-only component, such as `ANavLinkProxy`'s billboard.

96 classes report a different **function signature**, and that one is not cosmetic: a cooked build
cases some parameter names differently (`Position` against `position`), and a `CallFunction`
argument is matched against the function's own parameters by name. An editor-sourced name is a
`FAILED_PRECONDITION` at the first call — the same defect as the `BlueprintSetter` write path, in a
different place.

So the build now reports its whole declared surface and the filter narrows to it: **318 members, 168
functions and 64 components dropped**, 298 signatures and their parameter names corrected, and every
per-entry hash taken from the build. What it deliberately does not do is carry over the 25 members
and 23 functions the build has under a name the editor does not — without the editor's metadata
there is no `BlueprintSetter` for one, and writing such a property directly is how it gets written
*wrongly* rather than not at all.

The invariant this buys is what makes a build-sourced hash honest: **the generated package declares
nothing the build lacks.** A subset of what ships can address what ships; a superset cannot.

---

## 17. Struct values

Phase 8. Everything above reaches an arbitrary `USTRUCT` a member at a time, by nested address
(`PostProcessSettings.BloomIntensity`), and never as one value — `IsValueExpressible` has said
`false` for `EValueKind::Struct` since §3a, because a `Value` had no variant to carry one. That was
survivable for properties, which a `StructProxy` reaches into, and not for functions: a parameter
cannot be supplied a member at a time through a generated signature, so **a function with a struct
input or a struct result was not offered at all**. Measured on TempoSample's editor walk, that is
**4,840 of 19,083 Blueprint-callable functions** — 3,797 with a struct input, 1,878 with a struct out
parameter or return value. A quarter of the function surface, and the quarter that includes
`LineTraceSingle`'s `FHitResult`.

### 17.1 The wire

```proto
message StructValue {
  string struct_name = 1;          // "FHitResult" -- the schema's name for it; empty accepts any
  map<string, Value> fields = 2;   // by authored member name
}
message StructArray { repeated StructValue values = 1; }

// in Value's oneof:
StructValue struct_value = 39;
StructArray struct_array_value = 40;
```

Recursive — a field is a `Value`, which may be another `StructValue` — so a struct of structs needs
nothing further. Keyed by **authored** name, because that is what a nested address already resolves
a member by and what the schema reports for struct members (a Blueprint struct's internal names look
like `BandThickness_26_54B1BF…`). Arrays of structs get a variant; sets of structs do not, because a
`TSet` of a `USTRUCT` needs a hash function reflection cannot see and is rare enough in Blueprint-facing
code not to justify one.

`struct_name` is checked against the destination on a write, exactly as the first-class struct
variants check theirs (`SetStructOnTarget` refuses an `FVector` into an `FRotator`). It is the CPP
name, which is what the schema keys struct entries by.

### 17.2 A write is a merge, not a replacement

**Only the fields present are written.** The others keep whatever the destination already holds —
its current value for a property, its default-constructed value for a function parameter, whose
frame is initialized before any argument is written.

That is the choice that makes a struct value safe to round-trip. A replacement would reset every
member the client did not mention, and a client is never in a position to mention all of them: a
struct can carry members the generated type does not declare, and native state that reflection does
not see at all (`FKey`'s cached `KeyDetails`). Under a merge neither can be clobbered, and the
semantics are the ones the nested-address write has always had — a `StructValue` with one field is
exactly `SetProperty("Settings.Scale", …)`, and is implemented as that, member by member through
`SetValueOnTarget`, so a struct member is converted, range-checked and type-matched by the same code
as any other property.

### 17.3 Which structs are value-expressible

**A struct is value-expressible when every one of its reflected members is**, recursively, and it has
at least one. A struct with a map, a weak pointer, a C-style array or a delegate among its members
is not, and stays reachable a member at a time exactly as before.

The rule is over **all** members, not the Blueprint-visible ones. A read reports every member, so a
struct value is the whole struct as reflection knows it; a struct with an inexpressible member would
come back with a hole in it, and a value that silently omits part of the thing it claims to be is the
§9.4 failure in a new place. A struct with no members at all is excluded for the same reason from
the other side: whatever it holds, reflection does not report it.

The decision lives in the classifier (`IsValueExpressible`), so the schema's `value_expressible`, the
function's `positional`, `GetProperty`, `SetProperty` and `CallFunction` all change together — that
was the point of §3a. A struct property that becomes value-expressible also becomes `writable`, which
changes the hash of every class declaring one; that is the schema honestly describing a new
capability, not drift.

Native structs are cached, keyed by `UScriptStruct*`, because a struct check walks members and the
classifier runs on every read. A `UUserDefinedStruct` is not cached: it can be recompiled under the
same pointer in an editor.

### 17.4 Struct members include inherited ones

The schema walk enumerated struct members with `EFieldIterationFlags::None`, which is **declared
members only** — right for classes, where inheritance is reconstructed from `super`, and wrong for
structs, which carry no `super` in the schema at all. `FVector_NetQuantize` derives from `FVector`, so
it declared nothing and was emitted as an empty proxy. Struct entries now include inherited members.

### 17.5 The clients

The generated struct type becomes a **value type** as well as the proxy's static type:

```python
moved = truck.K2_SetActorLocation(NewLocation=target, bSweep=True, bTeleport=False)
hit = moved["SweepHitResult"]                            # FHitResult: a value, newly offered
print(hit.ImpactPoint.X, hit.bBlockingHit)

truck.PostProcessSettings.BloomIntensity = 2.0           # unchanged: a live handle, by address
truck.WheelSetup = FWheelSetup(WheelClass=Wheel, BoneName="FL")   # new: one write, merged
```

Reading a struct property still returns the proxy, because `truck.Settings.Scale = 2.0` has to keep
writing to the sim rather than to a local copy — making the read return a value would turn every
existing member write into a silent no-op. A proxy passed where a value is wanted (a function
argument, an assignment) is read first, so the two compose. In Python the constructor is
keyword-only with every field optional — positional arguments would silently reassign when a member
is added — and an unset field is simply absent from the `StructValue`. Rust gets a `#[derive(Default)]`
struct of `Option` fields beside the existing proxy, for the same reason.

### 17.6 What is still not offered

A function whose struct parameter is not value-expressible by §17.3, and one with a `TSet` of
structs or a map. Those were already absent and stay absent, by the rule that an unsupported type is
omitted rather than loosened.

---

## 18. One ordered batch

Phase 9. §6.3's `batch()` collects writes and flushes them as `SetProperties`. Three things it could
not do:

- **Carry a call.** A property with a `BlueprintSetter` is written by calling its accessor, which
  `SetProperties` cannot express, so a batch with one accessor write in the middle was three round
  trips: the writes before it, the call, the writes after.
- **Carry a read.** A read inside a batch went straight to the server — *ahead of the writes queued
  before it*, so it reported the value from before the batch. That is a correctness bug, not a
  missing optimization, and it is in both clients today.
- **Promise anything about time.** Separate RPCs are separate game-thread tasks, and a tick can fall
  between any two of them.

### 18.1 The RPC

```proto
message BatchOp {
  oneof op {
    SetPropertyRequest set = 1;
    GetPropertyRequest get = 2;
    CallFunctionRequest call = 3;
  }
}
message RunBatchRequest  { repeated BatchOp ops = 1; bool stop_on_failure = 2; }
message BatchOpResult {
  int32 code = 1;  string error = 2;           // per op; 0 is OK
  oneof result { Value value = 3; CallFunctionResponse call = 4; }
}
message RunBatchResponse { repeated BatchOpResult results = 1; }   // one per op, in order

rpc RunBatch(RunBatchRequest) returns (RunBatchResponse);
```

`RunBatch`, not `Batch`: the Python and Rust RPC generators already emit a `batch()` builder for
`SetProperties`, and RPC names are flattened per module.

The server runs the ops **in order, in one handler invocation**, so the whole batch happens between
two ticks. That is a guarantee worth having on its own — configure twenty properties and call
`Activate`, and nothing simulates against the half-configured actor.

One result per op, failed or not, unlike `SetProperties`, which reports failures only: a read's
answer has to come back by position. `stop_on_failure` stops at the first failure and reports every
later op as `ABORTED`; the typed clients set it, because an op after a failed one was written
assuming the failed one had happened. Off, the batch behaves as `SetProperties` does.

### 18.2 The clients

Writes and accessor writes queue as before, now into one `RunBatch`. Reads and calls inside a batch
have two forms:

```python
with world.batch() as b:
    truck.MaxSpeed = 1200.0
    truck.ScaleFactor = 2.0                      # a BlueprintSetter: queued as a call
    speed = b.get(lambda: truck.MaxSpeed)        # Deferred[float], one round trip in all
    moved = b.call(lambda: truck.TeleportTo(p))  # Deferred[bool]
print(speed.value, moved.value)
```

**Deferred**, through `b.get`/`b.call`: the lambda is evaluated in capture mode, so the read or call
it makes is queued and its type comes from the lambda's own return type — `Deferred[float]` is
inferred, not declared, and a renamed property is still a type error inside the lambda. The value is
available once the batch has run.

**Immediate**, by just reading or calling: the queued ops and this one go out together as one
`RunBatch`, and the real result comes back. Correct and ordered; two round trips instead of one if
the batch continues, and the batch has then partly run. That is the price of not lying to the type
checker — the stub says `truck.MaxSpeed` is a `float`, so it cannot be a placeholder.

Rust mirrors both: `b.get(|| truck.max_speed())` returns `Deferred<f64>`. A function with out
parameters cannot be deferred in Rust, because its generated method decodes after the call returns
and there is nothing yet to decode; it runs immediately instead.

---

## 19. The asset catalog

Phase 10. An asset-valued property is typed today as the class it points at — a `StaticMesh`
handle — while what travels is a path string, so nothing checks the path and the stub misdescribes
the value. A scenario that hard-codes `/Game/Vehicles/SM_Truck` keeps type-checking after the asset
is moved or renamed, and fails at runtime with `NOT_FOUND`. That is §1's failure in a third place.

### 19.1 Where the catalog comes from

**From the packaged build, and only from it.** Classes need the editor for their metadata (§5), and
assets have no metadata that matters; what they need is the one fact only the build knows, which is
whether the asset was cooked. An editor's registry lists everything on disk, including assets
nothing references and the cook therefore dropped — a catalog of those would type-check and then
fail, which is the thing this exists to prevent.

So `query_packaged_classes.py` also asks `GetAssets` for every asset the build has, and
`filter_schema.py` writes them into the filtered schema as a top-level `assets` list. With no
packaged build there is no catalog, for the same reason there are no published classes then: absent
beats wrong. Measured on TempoSample: **877 cooked assets**, 233 under `/Game` and 337 under
`/Engine`, the rest spread across 18 plugin mount points.

Excluded:

- **Redirectors.** Moving an asset in the editor leaves an `ObjectRedirector` at the old path. A
  catalog that listed it would keep the old name compiling, which is precisely the case to catch.
- **Blueprint classes** (`BlueprintGeneratedClass`, `WidgetBlueprintGeneratedClass`, `Blueprint`).
  They are already in the class surface, as classes, which is how a scenario uses them.

### 19.2 The typed handle

`AssetRef` becomes generic in the asset's class, and an asset-valued property is typed by what it
accepts:

```python
from tempo_sample_classes.assets import Game

truck.Mesh.StaticMesh = Game.Vehicles.SM_Truck     # AssetRef[StaticMesh]
truck.Mesh.StaticMesh = Game.Materials.M_Paint     # error: AssetRef[Material]
```

That is a second guarantee on top of the rename one — the right *kind* of asset — and it costs
nothing extra, since the schema already records an object property's pointee class.

**Python**: `AssetRef` is covariant, so an `AssetRef[Material]` goes where an
`AssetRef[MaterialInterface]` is wanted. **Rust**: `AssetRef<T>` derefs to `AssetRef<T::Super>`
through the same `Deref` chain a class handle uses, so `&M_PAINT` coerces to
`&AssetRef<MaterialInterface>` and a setter takes `&AssetRef<…>` to allow it. That is a second
`unsafe` reinterpretation beside `upcast`, sound for the same reason — `AssetRef` is
`#[repr(transparent)]` over its path, with the class only in a `PhantomData`.

The escape hatch is the constructor, `AssetRef("/Game/…")` / `AssetRef::from_path`, which takes a path
from data — a config file, a CLI argument — that no type checker could verify. It is untyped
(`AssetRef[Any]` in Python), visibly so at the call site.

### 19.3 Layout

One module per mount point, with folders as nested namespaces: `assets.Game.Vehicles.SM_Truck` in
Python, `assets::game::vehicles::SM_Truck` in Rust. Folder segments follow each language's module
naming; asset names stay verbatim, as class names do, mangled only where the language cannot spell
them, with a collision failing generation as a class collision does.

### 19.4 Drift

The catalog *is* the drift detection: a removed asset is an attribute that no longer exists, a
retyped one a type error. `diff_schema.py` reports both as breaking, and an added asset as
additive. `check_schema` compares the installed catalog against the sim's `GetAssets` and names any
asset the package has and the sim does not.
