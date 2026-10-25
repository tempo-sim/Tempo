# Copyright Tempo Simulation, LLC. All Rights Reserved
#
# Narrows tempo_schema.json to the classes a *published* package can honestly describe.
#
# Everything else in this pipeline generates per project, per release: `<project>-classes` describes
# one build and ships beside it. That is the only way to describe a build completely, and it means a
# user cannot `pip install` their way to a typed surface at all until they have packaged one.
#
# A published package closes that gap for the part of the surface that does not vary. The variable
# is not the engine release -- it is which plugins a project enables. Measured on TempoSample, an
# editor reports 11,297 classes and the packaged game ships 6,068, so 46% of what an editor can see
# depends on the project. A package that guessed at those would be wrong about nearly half its
# content, and wrong in the worst direction: a stub that type-checks and then fails at runtime.
#
# So the criterion is narrow and checkable: **a class whose module ships in every build, whatever
# the project enables.** That is the engine's always-loaded modules plus Tempo's own.
#
#   python3 subset_schema.py <schema.json> <subset.json> [--plugin-root <dir>]
#
# See TYPED_WORLD_API_PLAN.md section 16.

import argparse
import json
import os
import re
import sys

from filter_schema import filter_schema, reroot

# Engine modules that are compiled into every build and loaded unconditionally -- not plugins, so
# no project can turn one off. Hand-listed rather than derived, because "is this module always
# loaded" is not a question the schema can answer: the schema is one project's answer to it.
#
# Engine and CoreUObject are the surface anyone writing a scenario addresses. The other five are
# here because leaving them out does not remove them from the API -- it only makes the members that
# mention them degrade to a bare Object. They cost 161 classes between them and recover the
# references of several hundred Engine members.
ENGINE_PACKAGES = (
    "/Script/CoreUObject",
    "/Script/Engine",
    "/Script/DeveloperSettings",
    "/Script/InputCore",
    "/Script/PhysicsCore",
    "/Script/SlateCore",
    "/Script/UMG",
)


def _read_uplugin(path):
    """A `.uplugin`, which is JSON that Unreal's parser accepts and Python's does not.

    Unreal tolerates a trailing comma before a closing brace or bracket, and several of Tempo's own
    descriptors have one. A BOM is likewise normal in a file Unreal wrote.
    """
    with open(path, encoding="utf-8-sig") as f:
        text = f.read()
    return json.loads(re.sub(r",(\s*[}\]])", r"\1", text))


def tempo_packages(plugin_root):
    """The `/Script/<Module>` packages of Tempo's own runtime modules, as published.

    Derived from the plugin descriptors rather than listed, so a new plugin is published by
    existing and a renamed module cannot be silently dropped. The descriptor is read rather than
    the `Source/` directory listing because the descriptor is what says a module's *type*, and an
    `Editor` module is not in a packaged build at all -- nor are the directories under `Source/`
    that are not modules (`TempoModuleRules` is a Build.cs in a folder of its own; `ThirdParty`
    holds prebuilt libraries).

    Two exclusions, both from the same rule as the engine list -- it has to be in every build:

    - A plugin a project has to opt into. `TempoROSBridge` is `EnabledByDefault: false`, so a
      project that ignores it never builds it, and a published stub for one of its classes would be
      a stub for something not there.
    - A module supplied from outside this tree. `TempoROS` is a separate repository the host project
      adds beside Tempo, so a project may not have it at all; it is absent from `plugin_root` and so
      is never collected here. `External/` is likewise left out: those are vendored Epic plugins
      whose presence is Tempo's choice to keep making, not a promise to a client.
    """
    packages = []
    for entry in sorted(os.listdir(plugin_root)):
        descriptor = os.path.join(plugin_root, entry, entry + ".uplugin")
        if not os.path.isfile(descriptor):
            continue
        plugin = _read_uplugin(descriptor)
        if plugin.get("EnabledByDefault", True) is False:
            continue
        for module in plugin.get("Modules", []):
            if module.get("Type") == "Runtime":
                packages.append("/Script/" + module["Name"])
    return sorted(packages)


def published_packages(plugin_root):
    """Every package a published class package may describe, sorted."""
    return sorted(set(ENGINE_PACKAGES) | set(tempo_packages(plugin_root)))


def subset(schema, packages):
    """The schema narrowed to `packages`, rerooted, and stamped as a subset.

    A subset chosen by module leaves classes whose superclass is outside it -- 32 in the published
    set, ten of them in `/Script/Engine` itself -- so it is rerooted with `filter_schema.reroot`,
    which says what that costs. What a rerooted class gives up here is exactly what a published
    subset is: less, not wrong.
    """
    keep = {cls["path"] for cls in schema["classes"] if cls["package"] in packages}
    narrowed = filter_schema(schema, keep)
    rerooted = reroot(narrowed, {cls["path"]: cls for cls in schema["classes"]})

    # A project's assets are its own -- even the engine content it cooks depends on what the project
    # references -- so the published classes carry no catalog.
    narrowed.pop("assets", None)

    header = dict(narrowed.get("header", {}))
    # The whole-walk hash the header carries identifies a *build*, and a subset is not one: no sim
    # will ever report this number, so a client comparing it would only ever see a mismatch. The
    # flag is what tells `check_schema` to compare class by class instead. Dropped rather than left
    # in place, because a stale hash that cannot match is worse than an absent one.
    header["published_subset"] = True
    header["published_packages"] = list(packages)
    header.pop("content_hash", None)
    # Which classes see a narrower ancestry here than in a project's own package, and what they
    # were really derived from. The per-class hashes are untouched and still match the build -- a
    # rerooted class can address the sim exactly as well, it just declares less -- so this is the
    # only record that the view narrowed, and release notes and diagnostics both want it.
    header["rerooted"] = {path: skipped[0] for path, _, skipped in rerooted}
    narrowed["header"] = header
    return narrowed, rerooted


def main(argv):
    parser = argparse.ArgumentParser(
        prog="subset_schema.py",
        description="Narrow tempo_schema.json to the classes a published package can describe: "
                    "those in the engine's always-loaded modules and Tempo's own.")
    parser.add_argument("schema", help="the schema to narrow, as filter_schema.py wrote it")
    parser.add_argument("subset", help="where to write the narrowed schema")
    parser.add_argument("--plugin-root", metavar="DIR",
                        default=os.path.normpath(os.path.join(
                            os.path.dirname(os.path.abspath(__file__)), "..", "..", "..")),
                        help="the directory holding Tempo's plugin directories, whose descriptors "
                             "say which modules are published (default: this checkout)")
    options = parser.parse_args(argv[1:])

    with open(options.schema) as f:
        schema = json.load(f)

    packages = published_packages(options.plugin_root)
    before = len(schema.get("classes", []))
    narrowed, rerooted = subset(schema, packages)
    after = len(narrowed["classes"])

    if not after:
        print("refusing to write an empty subset: no class is in {}".format(packages),
              file=sys.stderr)
        return 1

    print("published packages {}".format(len(packages)))
    for package in packages:
        count = sum(1 for cls in narrowed["classes"] if cls["package"] == package)
        print("   {:34s} {}".format(package, count))
    print("classes {} -> {}".format(before, after))
    print("structs {} -> {}, enums {} -> {}".format(
        len(schema.get("structs", [])), len(narrowed["structs"]),
        len(schema.get("enums", [])), len(narrowed["enums"])))

    if rerooted:
        # Worth printing in full: each line is a class whose typed surface is narrower than the
        # same class in a project's own package, and the release notes should be able to say so.
        print("rerooted {} class(es) whose superclass is outside the subset:".format(len(rerooted)))
        for class_path, new_super, skipped in rerooted:
            print("   {} -> {} (skipping {})".format(
                class_path, new_super or "(root)", ", ".join(skipped)))

    with open(options.subset, "w") as f:
        json.dump(narrowed, f, indent=1, sort_keys=False)
        f.write("\n")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
