# Copyright Tempo Simulation, LLC. All Rights Reserved
#
# Narrows tempo_schema.json to the classes a packaged build actually has.
#
# The schema is generated in the editor, because only an editor can report property metadata -- a
# BlueprintSetter, a tooltip, a category. But an editor also *has* thousands of classes a packaged
# game does not: entire editor-only modules (UnrealEd, NiagaraEditor, BlueprintGraph, AnimGraph) and
# types some plugin generated at editor startup. Measured on TempoSample, 5,229 of 11,297 classes.
#
# Generating client stubs from the unfiltered schema is worse than useless: a scenario addressing
# one of those classes type-checks and then fails at runtime, which is precisely the inverse of what
# the stubs are for.
#
# The filter is the set of class paths the packaged sim reports from its own reflection. Correct by
# construction, rather than by a predicate over module types that would have to be re-guessed for
# every engine version.
#
#   python3 filter_schema.py <schema.json> <reported.json> <filtered.json>
#
# where reported.json is what query_packaged_classes.py wrote: the class paths the sim has, and the
# header it identifies itself by.

import json
import sys


def referenced_type_names(type_desc, structs, enums):
    """Collect the struct and enum names one type mentions, following containers."""
    if not isinstance(type_desc, dict):
        return
    if type_desc.get("struct"):
        structs.add(type_desc["struct"])
    if type_desc.get("enum"):
        enums.add(type_desc["enum"])
    for nested in ("element", "key"):
        if nested in type_desc:
            referenced_type_names(type_desc[nested], structs, enums)


def narrow_to_surface(schema, surface):
    """Narrow each class and struct to what the build declares, and correct what it reports.

    The editor is the only thing that can report a property's metadata and the build is the only
    authority on what exists, so the two have to be combined. Narrowing the class *list* was only
    half of that: a class both have can still declare members only the editor does, and those were
    passing straight through into the stubs. AActor alone contributed seven -- ActorGuid,
    PivotOffset, RuntimeGrid and the rest are WITH_EDITORONLY_DATA -- to every one of the two
    thousand classes that derive from it.

    Three corrections, in order of how much they matter:

    - **A member the build does not have is dropped.** It is a stub that type-checks and then fails
      at runtime, which is the one outcome this whole exercise exists to prevent.
    - **A function's signature and parameter names come from the build.** A CallFunction argument is
      matched against the function's own parameters by name, and a cooked build cases some of them
      differently, so an editor-sourced name is a FAILED_PRECONDITION at the first call.
    - **Readability, writability and value-expressibility come from the build** -- a member's, a
      struct's as a whole, and a function's `positional`. They are derived from flags that survive
      cooking, so they should already agree; taken from the build so that nothing rests on
      "should". A struct's can genuinely differ: one editor-only member that cannot be carried
      makes the whole struct inexpressible in the editor alone.

    What is deliberately *not* done is adding a member the build has and the editor does not -- 23
    classes have a property like that and 7 a function, nearly all a Blueprint variable the cook
    cased differently. Carrying one over would mean no metadata for it, and so no BlueprintSetter,
    which is how a property gets written wrongly rather than not at all. The typed API offers less
    instead, which is its documented failure mode.

    Returns a count per correction, for the caller to report.
    """
    stats = {"members_dropped": 0, "functions_dropped": 0, "components_dropped": 0,
             "signatures_corrected": 0, "flags_corrected": 0,
             "members_only_in_build": 0, "functions_only_in_build": 0}

    def correct_flags(member, reported):
        corrected = False
        for field in ("readable", "writable", "value_expressible"):
            if member.get(field) != reported[field]:
                member[field] = reported[field]
                corrected = True
        return corrected

    for cls in schema.get("classes", []):
        declared = surface["classes"].get(cls["path"])
        if declared is None:
            continue
        cls["content_hash"] = declared["content_hash"]

        kept = []
        for member in cls.get("properties", []):
            reported = declared["properties"].get(member["name"])
            if reported is None:
                stats["members_dropped"] += 1
                continue
            stats["flags_corrected"] += 1 if correct_flags(member, reported) else 0
            kept.append(member)
        stats["members_only_in_build"] += len(declared["properties"]) - len(kept)
        cls["properties"] = kept

        kept = []
        for function in cls.get("functions", []):
            reported = declared["functions"].get(function["name"])
            if reported is None:
                stats["functions_dropped"] += 1
                continue
            # Whether every input travels as one value, which decides whether the function is
            # offered. Older reports do not carry it, and then the editor's answer stands.
            if "positional" in reported and function.get("positional") != reported["positional"]:
                function["positional"] = reported["positional"]
                stats["flags_corrected"] += 1
            if function.get("signature") != reported["signature"]:
                function["signature"] = reported["signature"]
                # The parameter list is positional in both documents, so the names line up with
                # it. A count mismatch would mean the two disagree about the function itself, in
                # which case the build's view is the one to keep and the editor's names are no
                # use.
                names = reported["parameters"]
                if len(names) == len(function.get("parameters", [])):
                    for parameter, name in zip(function["parameters"], names):
                        parameter["name"] = name
                stats["signatures_corrected"] += 1
            kept.append(function)
        stats["functions_only_in_build"] += len(declared["functions"]) - len(kept)
        cls["functions"] = kept

        kept = []
        for component in cls.get("components", []):
            if declared["components"].get(component["name"]) != component["class"]:
                stats["components_dropped"] += 1
                continue
            kept.append(component)
        cls["components"] = kept

    for struct in schema.get("structs", []):
        declared = surface["structs"].get(struct["name"])
        if declared is None:
            continue
        struct["content_hash"] = declared["content_hash"]
        # A member only the editor has can make a struct inexpressible there and not in the build.
        if "value_expressible" in declared and struct.get("value_expressible") != declared["value_expressible"]:
            struct["value_expressible"] = declared["value_expressible"]
            stats["flags_corrected"] += 1
        kept = []
        for member in struct.get("members", []):
            reported = declared["members"].get(member["name"])
            if reported is None:
                stats["members_dropped"] += 1
                continue
            stats["flags_corrected"] += 1 if correct_flags(member, reported) else 0
            kept.append(member)
        struct["members"] = kept

    for enum in schema.get("enums", []):
        declared = surface["enums"].get(enum["name"])
        if declared is not None:
            enum["content_hash"] = declared["content_hash"]

    return stats


def filter_schema(schema, keep_paths):
    """The schema with classes outside `keep_paths` removed, and types nothing names dropped."""
    kept_classes = [cls for cls in schema.get("classes", []) if cls["path"] in keep_paths]

    # A struct or enum only earns its place if something kept still mentions it; otherwise the
    # generated structs and enums modules carry types no class can reach.
    structs, enums = set(), set()
    for cls in kept_classes:
        for member in cls.get("properties", []):
            referenced_type_names(member, structs, enums)
        for function in cls.get("functions", []):
            for parameter in function.get("parameters", []):
                referenced_type_names(parameter, structs, enums)

    # A kept struct's own members can name further structs and enums, so close over them. A
    # visited set, because a struct can reach itself through a TArray of its own type.
    by_name = {struct["name"]: struct for struct in schema.get("structs", [])}
    pending, visited = list(structs), set()
    while pending:
        name = pending.pop()
        if name in visited:
            continue
        visited.add(name)
        struct = by_name.get(name)
        if struct is None:
            continue
        for member in struct.get("members", []):
            referenced_type_names(member, structs, enums)
        pending.extend(structs - visited)

    filtered = dict(schema)
    filtered["classes"] = kept_classes
    filtered["structs"] = [s for s in schema.get("structs", []) if s["name"] in structs]
    filtered["enums"] = [e for e in schema.get("enums", []) if e["name"] in enums]
    return filtered


def main(argv):
    if len(argv) < 4:
        print("usage: filter_schema.py <schema.json> <class_paths.json> <filtered.json>",
              file=sys.stderr)
        return 2

    with open(argv[1]) as f:
        schema = json.load(f)
    with open(argv[2]) as f:
        reported = json.load(f)
    keep_paths = set(reported["classes"])

    if not keep_paths:
        print("refusing to filter against an empty class list: that would discard everything",
              file=sys.stderr)
        return 1

    before = len(schema.get("classes", []))
    filtered = filter_schema(schema, keep_paths)
    after = len(filtered["classes"])

    # Narrow the members too, which is the other half of "the build is the authority on what
    # ships". Older reports carry no surface, in which case the class list is all that can be
    # narrowed and the members stay the editor's -- said out loud, because it is the difference
    # between stubs that address this build and stubs that mostly do.
    surface = reported.get("surface")
    if surface:
        corrections = narrow_to_surface(filtered, surface)
        print("dropped {} member(s), {} function(s) and {} component(s) the build does not have"
              .format(corrections["members_dropped"], corrections["functions_dropped"],
                      corrections["components_dropped"]))
        print("corrected {} signature(s) and {} member flag(s) from the build".format(
            corrections["signatures_corrected"], corrections["flags_corrected"]))
        if corrections["members_only_in_build"] or corrections["functions_only_in_build"]:
            # Not carried over: without the editor's metadata there is no BlueprintSetter for it,
            # and writing such a property directly is how it gets written wrongly rather than not
            # at all.
            print("note: {} member(s) and {} function(s) the build has are not in the editor's "
                  "walk under the same name, and are left out".format(
                      corrections["members_only_in_build"],
                      corrections["functions_only_in_build"]))
        # And close over the types again, because the first pass closed over the *editor's*
        # members. A struct reachable only from a member the build does not have survives that
        # closure and is then declared by a package no sim will confirm -- 38 of them in
        # TempoSample, FExportHLODAssetsParams and FBakingConfiguration among them, all editor-only
        # and all reached from editor-only members. DescribeClasses reports exactly the types its
        # reported members refer to, so re-closing over the narrowed members lands on the same set.
        filtered = filter_schema(filtered, keep_paths)
    else:
        print("warning: this report has no declared surface, so only the class list is narrowed; "
              "regenerate it with the current query_packaged_classes.py", file=sys.stderr)

    # Stamp the header with the build's own identity rather than the editor's.
    #
    # The hash answers one question -- can these stubs address this sim -- and the sim is the one
    # that has to recognise it. An editor's hash never would: it walked thousands of classes the
    # build does not have. Taking the sim's hash verbatim also keeps one definition of how a hash
    # is computed, in C++, rather than a second one here that would drift from it.
    header = dict(filtered.get("header", {}))
    header["content_hash"] = reported["header"]["content_hash"]
    header["engine_version"] = reported["header"]["engine_version"]
    header["schema_version"] = reported["header"]["schema_version"]
    header["filtered_against_packaged_build"] = True
    filtered["header"] = header

    # Everything the schema had and the build does not. Reported rather than silently dropped,
    # because a large number here is worth noticing: it means the editor and the packaged build
    # disagree more than expected.
    print("classes {} -> {} ({} not present in the packaged build)".format(
        before, after, before - after))
    print("structs {} -> {}, enums {} -> {}".format(
        len(schema.get("structs", [])), len(filtered["structs"]),
        len(schema.get("enums", [])), len(filtered["enums"])))

    missing = len(keep_paths - {cls["path"] for cls in filtered["classes"]})
    if missing:
        # The sim has classes the editor's walk did not report. A handful is expected (types created
        # at runtime); many would mean the editor walk is missing something it should have.
        print("note: {} class(es) the build has are absent from the schema".format(missing))

    with open(argv[3], "w") as f:
        json.dump(filtered, f, indent=1, sort_keys=False)
        f.write("\n")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
