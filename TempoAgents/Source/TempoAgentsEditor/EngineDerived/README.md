# Engine-Derived Sources

Tempo changes how ZoneGraph lays lanes through intersections: it offers every turning connection
to the intersection for filtering, classifies turns, routes them along circular arcs, and more. The
code it has to change is private to the engine's ZoneGraph module, with no extension points.

Rather than modify the engine, `TempoAgentsEditor` compiles its own edited copies of three of
ZoneGraph's source files. The copies are **not checked in**. They are generated into
`Private/EngineDerived` (which is git-ignored) when Tempo is built, from the engine source already
on your machine (every standard engine installation ships its plugins' source) and the edits
stored in this folder.

| Engine file (`Engine/Plugins/Runtime/ZoneGraph/Source/ZoneGraph/Private`) | Generated file | What Tempo keeps |
|---|---|---|
| `ZoneShapeUtilities.cpp` | `TempoZoneShapeTessellation.cpp` | All of it, with Tempo's lane connection logic |
| `ZoneGraphBuilder.cpp` | `TempoZoneShapeAppend.cpp` | `AppendShapeToZoneStorage`, calling Tempo's tessellation |
| `ZoneGraphBVTree.cpp` | `TempoZoneBVTreeBuild.cpp` | `Build`, which the engine does not export |

Everything is in the `TempoZoneShape` namespace, declared in `Private/TempoZoneShapeTessellation.h`.
`FTempoZoneGraphBuilder` runs it after each of the engine's zone graph builds and overwrites the result.

## How edits are stored

An `.edits` file turns one exact version of an engine file into Tempo's copy:

```
d<line> <count>    delete <count> lines of the engine file, starting at <line>
a<line> <count>    add the <count> lines that follow, after <line> of the engine file
```

It holds the lines Tempo adds and none of the engine's. Line numbers only mean something for the
file the edits were made from, so each `.edits` file is tied to the SHA-256 of that file (with line
endings normalized), recorded in `Manifest.json` along with the engine versions known to ship it.
The build fails with an explanation if your engine's file matches none of them.

Earlier versions of Tempo patched and rebuilt the engine's ZoneGraph plugin in place. The files as
those patches left them have `.edits` of their own, which produce the same generated files, so an
engine Tempo once modified builds as it is. Its ZoneGraph still carries the old modifications, which
nothing uses any more; verifying the engine installation removes them.

## Supporting another engine version

A new engine release or hotfix is supported as-is when it ships the same three files. When one
changes, generate the copies for a supported version, bring Tempo's changes over to the new
engine's file by hand, and extract new edits (below). Add them alongside the existing ones;
several `.edits` files per engine file is the normal state.

## Changing Tempo's copy

1. Build once so `Private/EngineDerived` exists, then edit the generated file there.
2. Store the changes:

   ```
   <EngineDir>/Binaries/ThirdParty/Python3/<Platform>/bin/python3 \
       TempoAgents/Content/Python/gen_engine_derived.py <EngineDir> <path to TempoAgents> --extract
   ```

3. Commit the updated `.edits` file and `Manifest.json`. Do this for every supported engine file
   version (there is one `.edits` file per version), not just the one you have installed.

`--extract` compares the generated files against your engine's files. To extract edits for another
version of them, set `TEMPO_ENGINE_SOURCE_OVERRIDE` to a directory laid out like `<EngineDir>` that
holds that version, and pass a label after `--extract` to record in place of your engine's version.
