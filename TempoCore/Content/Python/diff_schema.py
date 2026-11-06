# Copyright Tempo Simulation, LLC. All Rights Reserved
#
# Compares two tempo_schema.json files and reports what a client could break against.
#
# A release regenerates the reflected API, and a scenario written against the old ones either still
# type-checks or does not. This says which, before the release rather than after: removed classes,
# removed or retyped properties, properties that became read-only, changed signatures, functions no
# longer offered, removed or retyped components, removed enumerators, struct members that went away
# or became read-only, and moved, removed or retyped assets. Additions -- new entries, and anything a
# client can newly use, such as a property that became writable -- are counted but not listed:
# nothing breaks by gaining a member, given the `..Default::default()` rule for Rust struct literals.
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


def _named(items):
    return {item["name"]: item for item in items}


def _keyed(old, new, breaking, additions, record):
    """Record what one side has and the other lacks, in both directions, and return the keys both
    have. Every removal check is paired with its addition here, so neither can be written alone.

    `record(key, verb)` builds the (owner, change) entry, verb being "removed" or "added"."""
    for key in sorted(set(old) - set(new)):
        breaking.append(record(key, "removed"))
    for key in sorted(set(new) - set(old)):
        additions.append(record(key, "added"))
    return sorted(set(old) & set(new))


def _gained(was, now, flag):
    return bool(now.get(flag)) and not was.get(flag)


def _lost(was, now, flag):
    return bool(was.get(flag)) and not now.get(flag)


def _offered(function):
    # The generators emit a function only when it is both callable and positional.
    return bool(function.get("callable")) and bool(function.get("positional"))


def compare(old, new):
    """What changed between two schemas, split by whether a client could break against it."""
    breaking = []
    additions = []

    old_classes, new_classes = _by_path(old), _by_path(new)
    for path in _keyed(old_classes, new_classes, breaking, additions,
                       lambda key, verb: (key, "class " + verb)):
        old_cls, new_cls = old_classes[path], new_classes[path]

        # A changed superclass can remove every inherited member at once, which is as breaking as
        # removing them one by one.
        if (old_cls.get("super") or "") != (new_cls.get("super") or ""):
            breaking.append((path, "superclass: {} -> {}".format(
                old_cls.get("super") or "(none)", new_cls.get("super") or "(none)")))

        old_members = _named(old_cls.get("properties", []))
        new_members = _named(new_cls.get("properties", []))
        for name in _keyed(old_members, new_members, breaking, additions,
                           lambda key, verb: (path, "property {} {}".format(key, verb))):
            was, now = old_members[name], new_members[name]
            if was.get("type") != now.get("type"):
                breaking.append((path, "property {}: {} -> {}".format(
                    name, was.get("type"), now.get("type"))))
            if _lost(was, now, "writable"):
                breaking.append((path, "property {} became read-only".format(name)))
            elif _gained(was, now, "writable"):
                additions.append((path, "property {} became writable".format(name)))
            if _lost(was, now, "readable"):
                breaking.append((path, "property {} is no longer readable".format(name)))
            elif _gained(was, now, "readable"):
                additions.append((path, "property {} became readable".format(name)))

        old_fns, new_fns = _named(old_cls.get("functions", [])), _named(new_cls.get("functions", []))
        for name in _keyed(old_fns, new_fns, breaking, additions,
                           lambda key, verb: (path, "function {} {}".format(key, verb))):
            was, now = old_fns[name], new_fns[name]
            if was.get("signature") != now.get("signature"):
                breaking.append((path, "function {}: {} -> {}".format(
                    name, was.get("signature"), now.get("signature"))))
            # A function that stops being callable is as unusable as one that is gone.
            if _lost(was, now, "callable"):
                breaking.append((path, "function {} is no longer callable: {}".format(
                    name, now.get("unsupported_reason") or "no reason given")))
            # So is one whose inputs can no longer all travel as one value: the generators
            # withhold it, since there is no way to pass the argument it now needs.
            if _lost(was, now, "positional"):
                breaking.append((path, "function {} is no longer offered: an input can no longer "
                                       "be passed as one value".format(name)))
            # And one the generators start emitting is as new as one that was just declared.
            if _offered(now) and not _offered(was):
                additions.append((path, "function {} is now offered".format(name)))

        # A component is a typed accessor of its own, typed as the component's class, so a changed
        # class is a different member under the same name.
        old_components = _named(old_cls.get("components", []))
        new_components = _named(new_cls.get("components", []))
        for name in _keyed(old_components, new_components, breaking, additions,
                           lambda key, verb: (path, "component {} {}".format(key, verb))):
            was, now = old_components[name], new_components[name]
            if was.get("class") != now.get("class"):
                breaking.append((path, "component {}: {} -> {}".format(
                    name, was.get("class"), now.get("class"))))

    old_structs, new_structs = _named(old.get("structs", [])), _named(new.get("structs", []))
    for name in _keyed(old_structs, new_structs, breaking, additions,
                       lambda key, verb: (key, "struct " + verb)):
        # Every function taking one is withheld once it cannot travel whole, and its value type with
        # them -- as breaking as removing each of those. The reverse un-withholds them all.
        if _lost(old_structs[name], new_structs[name], "value_expressible"):
            breaking.append((name, "struct can no longer be passed as one value"))
        elif _gained(old_structs[name], new_structs[name], "value_expressible"):
            additions.append((name, "struct can now be passed as one value"))
        was = _named(old_structs[name].get("members", []))
        now = _named(new_structs[name].get("members", []))
        # A new member is an addition, not a break, under the same rule as a new field of a request
        # message: Python's constructor is keyword-only and C++'s is a designated-initializer
        # aggregate, but a Rust struct value is a plain struct, so a literal survives a new member
        # only when it ends in `..Default::default()`, as docs/clients/rust.md asks of every one.
        for member in _keyed(was, now, breaking, additions,
                             lambda key, verb: (name, "struct member {} {}".format(key, verb))):
            if was[member].get("type") != now[member].get("type"):
                breaking.append((name, "struct member {}: {} -> {}".format(
                    member, was[member].get("type"), now[member].get("type"))))
            # A writable member is a constructor argument; a read-only one is not, so a struct
            # value built with it stops type-checking.
            if _lost(was[member], now[member], "writable"):
                breaking.append((name, "struct member {} became read-only".format(member)))
            elif _gained(was[member], now[member], "writable"):
                additions.append((name, "struct member {} became writable".format(member)))

    old_enums, new_enums = _named(old.get("enums", [])), _named(new.get("enums", []))
    for name in _keyed(old_enums, new_enums, breaking, additions,
                       lambda key, verb: (key, "enum " + verb)):
        _keyed(dict.fromkeys(old_enums[name].get("enumerators", [])),
               dict.fromkeys(new_enums[name].get("enumerators", [])), breaking, additions,
               lambda key, verb: (name, "enumerator {} {}".format(key, verb)))

    # The asset catalog. A moved asset is a removed one under its old path and an added one under
    # its new path -- which is exactly how a scenario naming the old path experiences it.
    old_assets = {asset["path"]: asset["class"] for asset in old.get("assets", [])}
    new_assets = {asset["path"]: asset["class"] for asset in new.get("assets", [])}
    if old_assets and new_assets:
        for path in _keyed(old_assets, new_assets, breaking, additions,
                           lambda key, verb: (key, "asset " + verb)):
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
