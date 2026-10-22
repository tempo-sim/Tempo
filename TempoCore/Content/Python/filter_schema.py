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
