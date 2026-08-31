# Migrating to Tempo v0.3.0

v0.3.0 reworks TempoWorld's transform, property, and function-calling APIs. The plugin layout,
module names, and proto packages are unchanged — if you migrated to v0.1.0, nothing here touches
your Blueprints, config, or asset references. All of the work is in client code.

## What changed at a glance

| Change | Affects | Breaks |
|---|---|---|
| `set_vector2_d_property` renamed to `set_vector2d_property` | Python, Rust, C++ clients | compile/import |
| `CallFunction` returns `CallFunctionResponse`, not `Empty` | Rust, C++ clients | compile |
| Transform RPCs gained a trailing `scale` argument | Rust, C++ clients | compile |
| `set_actor_transform` / `set_component_transform` are partial updates | all clients | silently, at runtime |
| `relative_to_actor` composes in the other order | all clients | silently, at runtime |

The last two are the dangerous ones: nothing fails to build, and the Actor ends up somewhere else.

## What you need to do

### 1. Rename `set_vector2_d_property`

Generated client function names now treat a capital that directly follows a digit as part of the
same word, so `SetVector2DProperty` reads as `set_vector2d_property` rather than
`set_vector2_d_property`.

| Language | Before | After |
|---|---|---|
| Python | `tempo_world.set_vector2_d_property(...)` | `tempo_world.set_vector2d_property(...)` |
| Rust | `tempo_world::set_vector2_d_property(...)` | `tempo_world::set_vector2d_property(...)` |
| C++ | `tempo::tempo_world::set_vector2_d_property(...)` | `tempo::tempo_world::set_vector2d_property(...)` |

This is the only previously-shipped name that changed. `set_actor_scale3d` and
`set_component_scale3d` are new in v0.3.0, so they have no old spelling to migrate from.

The same rule names generated packages, so if your **project directory** has a digit followed by a
capital, its package and crate name change too — a project called `MyGame2D` publishes as
`my-game2d` (import `my_game2d`) instead of `my-game2-d`. Update your imports and any
`Cargo.toml`/`requirements.txt` that names it. Project names without that pattern are unaffected.

### 2. `call_function` now returns results

`CallFunction`'s response type changed from `TempoCore.Empty` to `CallFunctionResponse`, which
carries a `results` list — one entry per return value and out parameter, each with `name`,
`property_type`, and `value`. The request also gained a repeated `args` field.

Python callers that ignore the return value need no change. Rust and C++ callers must update the
return type they bind:

```rust
// Before: -> Result<Empty, TempoError>
tempo_world::call_function("MyActor".into(), "".into(), "DoThing".into())?;

// After: -> Result<CallFunctionResponse, TempoError>
let response = tempo_world::call_function("MyActor".into(), "".into(), "DoThing".into(), vec![])?;
for result in response.results { /* result.name, result.property_type, result.value */ }
```

Passing arguments is easier through the new `Call` builder than by constructing `Value` messages
by hand — see the "Calling Functions" section of `TempoWorld/README.md`.

### 3. Audit every `set_actor_transform` / `set_component_transform` call

**Rust and C++ callers get a new trailing argument.** `scale` is a new field on
`SetActorTransformRequest` and `SetComponentTransformRequest`, and the generated Rust and C++
wrappers take request fields positionally — so every call site needs one more argument. Pass `None`
(Rust) or `std::nullopt` (C++) to leave scale alone:

```rust
// Before
tempo_world::set_actor_transform_async(actor, t, String::new()).await
tempo_world::set_component_transform_async(owner, name, t, false).await

// After
tempo_world::set_actor_transform_async(actor, t, String::new(), None).await
tempo_world::set_component_transform_async(owner, name, t, false, None).await
```

Python passes these by keyword, so Python callers are unaffected. The same positional shift applies
to `call_function`, which gained `args` (step 2).

These RPCs are also now **partial updates**: a member you don't set is left exactly as it was.
Three consequences, in decreasing order of how likely they are to bite you.

**An empty transform is now a no-op.** It used to be a full-pose write, so it reset the Actor to
the origin with identity rotation. If you relied on that, spell it out:

```python
# Before: reset to origin + identity
tw.set_actor_transform(actor="MyActor", transform=Geometry.Transform())

# After: say what you mean. Assigning a member marks it present, even when the value is zero.
t = Geometry.Transform()
t.location.SetInParent()
t.rotation.SetInParent()
tw.set_actor_transform(actor="MyActor", transform=t)
```

Presence, not value, decides what gets applied — `t.location.x = 0.0` counts as "move to x=0",
while never touching `location` at all means "leave the location alone".

**Scale is no longer reset.** The old full-pose write always set scale back to 1, because
`TempoCore.Transform` carries no scale. Scale now rides in its own `scale` field on the request and
is only applied when you supply it. If your code depended on the implicit reset, pass
`scale=Geometry.Vector(x=1.0, y=1.0, z=1.0)` explicitly.

**`relative_to_actor` composes the other way around.** The transform you send is now interpreted
*in the reference Actor's frame* — the same order `spawn_actor` has always used. Previously the two
RPCs disagreed, and `set_actor_transform` effectively treated the reference Actor as the thing
being placed inside the transform you sent. The old and new results are identical whenever the
reference Actor is unrotated, and diverge as soon as it has any rotation. If you were compensating
for the old order — pre-composing the inverse yourself, or restricting yourself to unrotated
reference Actors — drop the workaround.

If a call only ever set one part of the pose, prefer the new single-purpose RPCs, which require
their one value and reject a request that omits it:

```python
tw.set_actor_location(actor="MyActor", location=Geometry.Vector(x=1.0, y=2.0, z=3.0))
tw.set_actor_rotation(actor="MyActor", rotation=Geometry.Rotation(r=0.0, p=0.0, y=1.57))
tw.set_actor_scale3d(actor="MyActor", scale=Geometry.Vector(x=2.0, y=2.0, z=2.0))
```

### 4. Expect errors where the server used to return OK

Three places that previously reported success on a request that had not fully succeeded now return
a status. Code that checks responses may start seeing failures it was silently ignoring:

- **`set_*_array_property`** now fails on the first element it cannot write, instead of returning
  OK with the rest of the array defaulted. `set_int_array_property(property="MyInt8Array",
  values=[1, 999, 3])` returns `OUT_OF_RANGE` rather than leaving `[1, 0, 3]` behind.
- **`set_enum_property` on a plain `uint8`** (a byte property with no `UENUM` attached) returns
  `INVALID_ARGUMENT` instead of crashing the sim.
- **Enum values that do not fit a byte property** return `OUT_OF_RANGE`.

## Also new, with nothing to migrate

- `set_property` — the type-carrying generic form of the `set_*_property` family, for when the
  type isn't known until the call site runs.
- `get_actor_functions` / `get_component_functions` — list the callable `UFUNCTION`s on an object,
  with signatures and per-function `callable` / `error` fields.
- `set_actor_location` / `set_actor_rotation` / `set_actor_scale3d` and their `set_component_*`
  counterparts.

Three fixes may change what you observe without changing any call you make: `set_enum_property` now
writes the correct entry for `UENUM`s with non-contiguous values (it previously used the value as
an index a second time), `set_asset_property` and `set_asset_array_property` now reach
`TSoftObjectPtr` properties, and `get_*_functions` reports an overridden `UFUNCTION` once rather
than once per class that declares it.
