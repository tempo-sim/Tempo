# FastGeo and TempoSensors labels: what support would take

Notes from bringing up CitySample (UE 5.8) with Tempo. Today `Setup.sh` sidesteps the problem by
turning FastGeo off for the cook (`Scripts/ConfigureProject.sh`). This records what it would take to
label FastGeo geometry instead, so a project can keep FastGeo's streaming performance. All engine
references are UE 5.8, `Engine/Plugins/Experimental/FastGeoStreaming` unless noted.

## The problem

FastGeoStreaming is an experimental World Partition **runtime cell transformer**. At cook time it
converts the static actors of each streaming cell into lightweight primitives:
`FFastGeoStaticMeshComponent`, `FFastGeoInstancedStaticMeshComponent` and so on, grouped in
`FFastGeoComponentCluster`s and owned by a `UFastGeoContainer` (a `UAssetUserData`) in the cell's
level. They are plain C++ objects, not `UPrimitiveComponent`s, and the source actors are gone.

Tempo's labeler (`UTempoActorLabeler`) works only on actors and `UPrimitiveComponent`s: it resolves
a label and calls `SetRenderCustomDepth` / `SetCustomDepthStencilValue`. It never sees a FastGeo
primitive, with two consequences, both verified live in CitySample:

1. **No labels** for most of the static city: buildings, roads, sidewalks, props.
2. **Wrong labels, not just missing ones.** The labeler puts every unmatched component into custom
   depth with stencil 0, precisely so unlabeled geometry still occludes. FastGeo primitives aren't
   in custom depth at all, so the custom-depth pass sees *through* them: the label of whatever
   labeled object is behind (sky, distant HLODs) appears where a building stands. The lidar has the
   same problem, since its distance comes from scene depth but its label from the stencil.

The problem only shows in **cooked** builds. FastGeo doesn't run in the editor or PIE
(`FastGeo.Enable`: "used in PIE and at cook time"), so editor testing can't reproduce it.

## What is reachable, and how

### The baked custom depth survives the cook (the key fact)

When FastGeo converts a component, it copies the component's `FPrimitiveSceneProxyDesc`,
including `bRenderCustomDepth`, `CustomDepthStencilValue` and `CustomDepthStencilWriteMask`
(`FFastGeoPrimitiveComponent::InitializeFromComponent` → `InitializeSceneProxyDescFromComponent`,
`Private/FastGeoPrimitiveComponent.cpp:198`). It serializes them into the cooked primitive (`:114`,
`:135-136`). They are not among the properties it resets as unsupported (`:204-238`). Its PSO
precaching even assumes custom depth may be toggled (`:703-705`). So **a component that has its
label when FastGeo transforms it keeps that label at runtime**, rendered by FastGeo's own proxy.

### Cook time: a Tempo cell transformer ahead of FastGeo's

`UWorldPartitionRuntimeCellTransformer` is public engine API (`Engine/Public/WorldPartition/
WorldPartitionRuntimeCellTransformer.h`). Its `Transform(ULevel*)` sees each cell's actors before they
are converted. The stack lives on `UWorldPartition` (`RuntimeCellsTransformerStack`), and
`UWorldPartition::AddRuntimeCellTransformerOfType(Class, PositionToInsert)` is `ENGINE_API`. So an
editor module can insert a transformer **ahead of FastGeo's** while the cook loads the world, without
touching the map asset.

That transformer, `UTempoLabelBakingCellTransformer` in a TempoSensorsEditor module, would:

- resolve each actor's and component's semantic label with the *same* code the runtime labeler
  uses. That code is the rules in `ResolveActorSemanticId` / `ResolveComponentSemanticId`, which
  need factoring out of `UTempoActorLabeler` into something usable without a world subsystem;
- set `bRenderCustomDepth = true` and `CustomDepthStencilValue = <semantic id>` on every primitive
  component, with **0 for unmatched ones**. That alone fixes the show-through (consequence 2), even
  with no label table at all;
- take its label table from `UTempoSensorsSettings::SemanticLabelTable`, the configured asset. A
  table loaded with `LoadLabelTable` at runtime doesn't exist at cook time.

**Limits of baking:**
- **Semantic labels only.** Instance ids are allocated at runtime (1–253, reused), and a baked
  stencil can't take part in that. Instance mode would still mislabel FastGeo geometry, unless the
  runtime path below also exists.
- **The table is fixed at cook.** `LoadLabelTable`, `SetStaticMeshTypeSemanticId` and the other
  runtime overrides wouldn't reach FastGeo primitives. Changing their labels means re-cooking.
- **Class and tag rules must resolve at cook.** That's fine, since the transformer sees the actors.
  After the transform there are no actors left to resolve against.
- **Every cooked label is the semantic id, whatever the runtime label type.** Switching the runtime
  to Instance mode would leave FastGeo geometry at semantic ids. The labeler would need to know, or
  refuse, for FastGeo-cooked levels.

Cost: one editor-only transformer, plus factoring out the rule resolution. No engine changes. This
is the most robust option, because FastGeo then renders the stencil itself across every render-state
recreate.

### Runtime: through public engine interfaces

FastGeo registers its primitives with the engine's **component interfaces** at module startup
(`FFastGeoStreamingModule::StartupModule`, `Private/FastGeoStreamingModule.cpp:41-49`):
`IPrimitiveComponent::AddImplementer` and `IStaticMeshComponent::AddImplementer` for
`UFastGeoContainer`. Those interfaces are **public engine API**
(`Engine/Classes/Components/ComponentInterfaces.h`), so Tempo can reach FastGeo primitives without
including a single FastGeo header:

- `TComponentInterfaceIterator<IStaticMeshComponent>` (`ComponentInterfaceIterator.h`) enumerates
  every registered implementer's primitives, FastGeo's included.
- `IStaticMeshComponent::GetStaticMesh()` gives the mesh, which is what mesh rules match on.
  `GetPrimitiveComponentInterface()` gives the `IPrimitiveComponent`.
- `IPrimitiveComponent::GetSceneProxy()` gives the live `FPrimitiveSceneProxy`, which has public
  `SetCustomDepthEnabled_GameThread` and `SetCustomDepthStencilValue_GameThread`
  (`Engine/Public/PrimitiveSceneProxy.h:709,723`).
- `IPrimitiveComponent::GetComponentLevel()` and `GetOwner()` (the container) say where it lives.

**Limits:**
- **The label lives on the proxy, not the description.** FastGeo recreates proxies from its stored
  `SceneProxyDesc`: when PSO precaching finishes for a primitive created with the fallback material
  (`NeedsPSORecreate`), and on every re-register as cells stream. Each recreate loses the label, so
  it has to be re-applied every time.
  - There is no public "render state created" event for FastGeo. Render state is created
    asynchronously on worker threads (`FastGeo.AllowAsyncRenderWork`), time-sliced
    (`FastGeo.AsyncRenderStateTask.TimeBudgetMS`).
  - So the labeler would poll: per frame, visit containers in newly visible levels until each
    primitive `IsRenderStateCreated()`, then label it, and watch for proxies that changed. This is
    workable but fiddly, and the visit has to be cheap: CitySample cells hold thousands of
    primitives.
- **Only mesh rules apply.** Class, tag and component-tag rules have nothing to match, since the
  actors are gone.
- **Instance mode is possible here**, because ids are allocated at runtime, unlike baking.

### Runtime, persistent: FastGeo's internal types

Writing the label into the primitive's `SceneProxyDesc` would make it survive recreates, but that
takes FastGeo's own types:
- `FFastGeoPrimitiveComponent::GetSceneProxyDesc()` and FastGeo's `MarkRenderStateDirty`.
- The classes are exported (`class FASTGEOSTREAMING_API FFastGeoPrimitiveComponent`,
  `Internal/FastGeoPrimitiveComponent.h:29`; likewise `UFastGeoContainer`,
  `FFastGeoStaticMeshComponentBase`), so linking works.
- But the headers are in the module's **`Internal/`** directory, and UBT exposes a module's internal
  include paths only to modules of the same scope (engine to engine) or ones marked
  `bTreatAsEngineModule` (`UnrealBuildTool/Configuration/UEBuildModule.cs:736-740`). Tempo is
  project-scoped. It could add the directory to `PrivateIncludePaths` by hand, as FastGeo itself
  does for `Engine/Internal`, but `Internal` means no API stability, in an experimental plugin.

Not recommended except as a stopgap.

## Recommendation

1. **Ship the cook-time transformer.** At minimum it should put every transformable primitive into
   custom depth with stencil 0, which ends the show-through. It should also bake semantic labels
   from the configured table. Under 300 lines in an editor module, all public API.
2. **Add the runtime proxy path later**, and only if instance labels or runtime-reconfigurable
   labels are needed on FastGeo geometry.
3. **Until then, keep turning FastGeo off in `ConfigureProject.sh`**, and make the labeler say so. At BeginPlay, if the
   `FastGeo.Enable` cvar is on (`IConsoleManager::FindConsoleVariable`, which needs no dependency on
   a plugin that's off by default) and the world holds any `UFastGeoContainer` (find the class by
   path), log an error that labels will be wrong. A project that skipped `Setup.sh`'s override
   should learn about it from the log, not from its training data.

## Testing it

- Only a cooked build shows the problem, so a test needs a package. In CitySample: package, run the
  `CitySampleAV` example, and compare the label image against the color image where near
  buildings meet the sky.
  - Before (FastGeo on, no fix): sky labels inside building silhouettes.
  - After: building labels, with `unlabeled` only where nothing matched.
- A unit test can cover the transformer in the editor: build a level with a static mesh actor, run
  the transformer's `Transform`, and assert that `bRenderCustomDepth` and `CustomDepthStencilValue`
  are set on its component. That's exactly what FastGeo copies.
