# Copyright Tempo Simulation, LLC. All Rights Reserved
#
# Compares two tempo_schema.json files and reports what a client could break against.
#
# A release regenerates the typed classes, and a scenario written against the old ones either still
# type-checks or does not. This says which, before the release rather than after: removed classes,
# removed or retyped properties, properties that became read-only, changed signatures, functions no
# longer offered, removed or retyped components, removed enumerators, struct members that went away
# or became read-only, and moved, removed or retyped assets. Additions are counted but not listed --
# nothing breaks by gaining a member.
#
# What counts as breaking is whatever the generators act on: a component gets a typed accessor of its
# own (gen_py_classes.py), a function is withheld unless it is callable and positional, and a struct
# member is a constructor argument only while it is writable. A field the generators ignore is not
# compared, which is also why documentation is not.
#
# Assets are compared only when both documents have a catalog, which only a schema narrowed against
# a packaged build does; comparing one against none would report every asset as removed.
#
# Exits 1 when there is a breaking change, so it can gate a pull request:
#
#   python3 diff_schema.py old_schema.json new_schema.json
#   python3 diff_schema.py old.json new.json --json      (for machine use)
#
# Documentation is deliberately not compared. A new tooltip is not a drift event, which is also why
# the content hash excludes it.

import json
import sys


def _by_path(schema):
    return {cls["path"]: cls for cls in schema.get("classes", [])}


def _members(cls):
    return {member["name"]: member for member in cls.get("properties", [])}


def _functions(cls):
    return {function["name"]: function for function in cls.get("functions", [])}


def _components(cls):
    return {component["name"]: component for component in cls.get("components", [])}


def _named(items):
    return {item["name"]: item for item in items}


def compare(old, new):
    """What changed between two schemas, split by whether a client could break against it."""
    breaking = []
    additions = []

    old_classes, new_classes = _by_path(old), _by_path(new)

    for path in sorted(set(old_classes) - set(new_classes)):
        breaking.append((path, "class removed"))
    for path in sorted(set(new_classes) - set(old_classes)):
        additions.append((path, "class added"))

    for path in sorted(set(old_classes) & set(new_classes)):
        old_cls, new_cls = old_classes[path], new_classes[path]

        # A changed superclass can remove every inherited member at once, which is as breaking as
        # removing them one by one.
        if (old_cls.get("super") or "") != (new_cls.get("super") or ""):
            breaking.append((path, "superclass: {} -> {}".format(
                old_cls.get("super") or "(none)", new_cls.get("super") or "(none)")))

        old_members, new_members = _members(old_cls), _members(new_cls)
        for name in sorted(set(old_members) - set(new_members)):
            breaking.append((path, "property {} removed".format(name)))
        for name in sorted(set(new_members) - set(old_members)):
            additions.append((path, "property {} added".format(name)))

        for name in sorted(set(old_members) & set(new_members)):
            was, now = old_members[name], new_members[name]
            if was.get("type") != now.get("type"):
                breaking.append((path, "property {}: {} -> {}".format(
                    name, was.get("type"), now.get("type"))))
            if was.get("writable") and not now.get("writable"):
                breaking.append((path, "property {} became read-only".format(name)))
            if was.get("readable") and not now.get("readable"):
                breaking.append((path, "property {} is no longer readable".format(name)))

        old_fns, new_fns = _functions(old_cls), _functions(new_cls)
        for name in sorted(set(old_fns) - set(new_fns)):
            breaking.append((path, "function {} removed".format(name)))
        for name in sorted(set(new_fns) - set(old_fns)):
            additions.append((path, "function {} added".format(name)))
        for name in sorted(set(old_fns) & set(new_fns)):
            was, now = old_fns[name], new_fns[name]
            if was.get("signature") != now.get("signature"):
                breaking.append((path, "function {}: {} -> {}".format(
                    name, was.get("signature"), now.get("signature"))))
            # A function that stops being callable is as unusable as one that is gone.
            if was.get("callable") and not now.get("callable"):
                breaking.append((path, "function {} is no longer callable: {}".format(
                    name, now.get("unsupported_reason") or "no reason given")))
            # So is one whose inputs can no longer all travel as one value: the generators
            # withhold it, since there is no way to pass the argument it now needs.
            if was.get("positional") and not now.get("positional"):
                breaking.append((path, "function {} is no longer offered: an input can no longer "
                                       "be passed as one value".format(name)))

        # A component is a typed accessor of its own, typed as the component's class, so a changed
        # class is a different member under the same name.
        old_components, new_components = _components(old_cls), _components(new_cls)
        for name in sorted(set(old_components) - set(new_components)):
            breaking.append((path, "component {} removed".format(name)))
        for name in sorted(set(new_components) - set(old_components)):
            additions.append((path, "component {} added".format(name)))
        for name in sorted(set(old_components) & set(new_components)):
            was, now = old_components[name], new_components[name]
            if was.get("class") != now.get("class"):
                breaking.append((path, "component {}: {} -> {}".format(
                    name, was.get("class"), now.get("class"))))

    old_structs, new_structs = _named(old.get("structs", [])), _named(new.get("structs", []))
    for name in sorted(set(old_structs) - set(new_structs)):
        breaking.append((name, "struct removed"))
    for name in sorted(set(old_structs) & set(new_structs)):
        # Every function taking one is withheld once it cannot travel whole, and its value type with
        # them -- as breaking as removing each of those.
        if old_structs[name].get("value_expressible") and not new_structs[name].get("value_expressible"):
            breaking.append((name, "struct can no longer be passed as one value"))
        was, now = _named(old_structs[name].get("members", [])), _named(new_structs[name].get("members", []))
        for member in sorted(set(was) - set(now)):
            breaking.append((name, "struct member {} removed".format(member)))
        for member in sorted(set(was) & set(now)):
            if was[member].get("type") != now[member].get("type"):
                breaking.append((name, "struct member {}: {} -> {}".format(
                    member, was[member].get("type"), now[member].get("type"))))
            # A writable member is a constructor argument; a read-only one is not, so a struct
            # value built with it stops type-checking.
            if was[member].get("writable") and not now[member].get("writable"):
                breaking.append((name, "struct member {} became read-only".format(member)))

    old_enums, new_enums = _named(old.get("enums", [])), _named(new.get("enums", []))
    for name in sorted(set(old_enums) - set(new_enums)):
        breaking.append((name, "enum removed"))
    for name in sorted(set(old_enums) & set(new_enums)):
        was = set(old_enums[name].get("enumerators", []))
        now = set(new_enums[name].get("enumerators", []))
        for enumerator in sorted(was - now):
            breaking.append((name, "enumerator {} removed".format(enumerator)))
        for enumerator in sorted(now - was):
            additions.append((name, "enumerator {} added".format(enumerator)))

    # The asset catalog. A moved asset is a removed one under its old path and an added one under
    # its new path -- which is exactly how a scenario naming the old path experiences it.
    old_assets = {asset["path"]: asset["class"] for asset in old.get("assets", [])}
    new_assets = {asset["path"]: asset["class"] for asset in new.get("assets", [])}
    if old_assets and new_assets:
        for path in sorted(set(old_assets) - set(new_assets)):
            breaking.append((path, "asset removed"))
        for path in sorted(set(new_assets) - set(old_assets)):
            additions.append((path, "asset added"))
        for path in sorted(set(old_assets) & set(new_assets)):
            if old_assets[path] != new_assets[path]:
                breaking.append((path, "asset class: {} -> {}".format(old_assets[path], new_assets[path])))

    return breaking, additions


def render(old, new, breaking, additions):
    lines = []
    old_header, new_header = old.get("header", {}), new.get("header", {})
    if old_header.get("engine_version") != new_header.get("engine_version"):
        lines.append("Engine version changed: {} -> {}. Expect unrelated differences.".format(
            old_header.get("engine_version"), new_header.get("engine_version")))
        lines.append("")

    if not breaking:
        lines.append("No breaking changes. {} addition(s).".format(len(additions)))
        return "\n".join(lines)

    lines.append("{} breaking change(s). A client generated against the old schema will fail to "
                 "type-check against the new one:".format(len(breaking)))
    lines.append("")
    current = None
    for owner, change in breaking:
        if owner != current:
            lines.append("  {}".format(owner))
            current = owner
        lines.append("    - {}".format(change))
    lines.append("")
    lines.append("{} addition(s), which break nothing and are not listed.".format(len(additions)))
    return "\n".join(lines)


def main(argv):
    if len(argv) < 3:
        print("usage: diff_schema.py <old_schema.json> <new_schema.json> [--json]", file=sys.stderr)
        return 2

    with open(argv[1]) as f:
        old = json.load(f)
    with open(argv[2]) as f:
        new = json.load(f)

    breaking, additions = compare(old, new)

    if "--json" in argv:
        print(json.dumps({
            "breaking": [{"owner": owner, "change": change} for owner, change in breaking],
            "additions": [{"owner": owner, "change": change} for owner, change in additions],
        }, indent=1))
    else:
        print(render(old, new, breaking, additions))

    return 1 if breaking else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
