"""Schema reading shared by the typed class generators.

`gen_py_classes.py`, `gen_rust_classes.py` and `gen_cpp_classes.py` need the same answers out of
`tempo_schema.json`, and a second implementation of one of them is a second thing that can be wrong.
"""

import re


def snake_case(name, reserved):
    """An Unreal member name as snake_case, legal in a language with these reserved words.

    Rust and C++ spell members the same way, so a member is called the same thing in both; only
    what each language reserves differs, and a reserved result takes a trailing underscore.

    Two passes, which is what reads acronyms correctly: `FOVAngle` -> `fov_angle`, not
    `f_o_v_angle`. The second pattern excludes digits deliberately -- with `[a-z0-9]` there,
    `Use2DMode` becomes `use2_d_mode` and `Vector2D` becomes `vector2_d`, which is wrong 147 times
    in TempoSample's schema. See TYPED_WORLD_API_PLAN.md §15.3.
    """
    spaced = re.sub(r"(.)([A-Z][a-z]+)", r"\1_\2", name)
    spaced = re.sub(r"([a-z])([A-Z])", r"\1_\2", spaced).lower()
    cleaned = re.sub(r"__+", "_", re.sub(r"\W", "_", spaced)).strip("_")
    if not cleaned:
        return "unnamed"
    if cleaned[0].isdigit():
        cleaned = "n" + cleaned
    return cleaned + "_" if cleaned in reserved else cleaned


def setter_argument(classes_by_path, class_path, setter):
    """What a property's `BlueprintSetter` calls its argument, or None if it cannot be resolved.

    `CallFunction` requires every argument to be named — an unnamed one is a
    `FAILED_PRECONDITION`, and the server matches the name against the function's own parameters.
    So writing a property through its accessor means knowing what that accessor calls its
    argument, and sending `""` fails every time. (It did: both runtimes sent `""` until this
    existed, and no test wrote such a property against a live sim.)

    The schema records a property's accessor *name* on the property, and the accessor's
    *parameters* in the function list of the class that declares it. This is where the two are
    joined. Searched up the inheritance chain, because a property can be declared on a subclass of
    the class whose function list has its accessor.

    Returns None when the accessor is not in the walk under that name at all — which happens, for
    exactly one property in UE 5.8: `UCineCameraSettings::DefaultFilmbackPreset` declares
    `BlueprintSetter=SetDefaultfilmbackPreset`, with a lowercase `f`, and no such function exists.
    A caller that cannot name the argument cannot call the setter, and writing the property
    directly would skip it, so such a property is not writable through the typed API.
    """
    seen = set()
    while class_path and class_path not in seen:
        seen.add(class_path)
        cls = classes_by_path.get(class_path)
        if cls is None:
            return None
        for function in cls.get("functions", []):
            if function["name"] != setter:
                continue
            for parameter in function.get("parameters", []):
                if parameter.get("direction") == "in":
                    return parameter["name"]
            # A setter that takes nothing cannot carry the value, so it is no use as one.
            return None
        class_path = cls.get("super") or ""
    return None


def resolve_setters(schema):
    """Record each property's setter argument, and demote the one property that has none.

    Run once, over the whole schema, before anything is emitted — so the stub, the metadata and the
    Rust accessors cannot disagree about whether a property is writable. Doing it per consumer is
    how they would: a stub that says writable and a runtime that refuses is the worst of both.

    Returns how many properties were demoted.
    """
    classes_by_path = {cls["path"]: cls for cls in schema["classes"]}
    demoted = 0
    for cls in schema["classes"]:
        for member in cls["properties"]:
            setter = member.get("setter") or ""
            if not setter:
                continue
            argument = setter_argument(classes_by_path, cls["path"], setter)
            if argument:
                member["setter_argument"] = argument
                continue
            # Its setter cannot be called, and writing the property directly would skip the setter
            # -- which is the thing the accessor path exists to prevent. So it is not writable.
            member["setter"] = ""
            member["writable"] = False
            demoted += 1
    return demoted


def enumerator_members(enumerators, legalize):
    """Each enumerator's member name, paired with the authored name the wire carries.

    `legalize` is the target language's identifier mangling, which makes every `\\W` an
    underscore. That is right for `EKeys::Left Shift` and wrong for an enumerator spelled only in
    symbols: `==`, `<` and `!=` come out as runs of underscores, which say nothing, collide with each
    other, and in Rust's case include `_`, which is not an identifier at all. Those are named
    `Enumerator<N>` after their position instead.

    Every enumerator gets a member. A name already taken -- two enumerators that mangle alike, or
    a real `Enumerator2` -- is suffixed with the later one's position, rather than the later one
    being dropped: a dropped enumerator is a value the sim can report and the client cannot read.
    """
    members, taken = [], set()
    for index, authored in enumerate(enumerators):
        if any(character.isalnum() for character in authored):
            name = legalize(authored)
        else:
            name = "Enumerator{}".format(index)
        candidate, suffix = name, index
        while candidate in taken:
            candidate = "{}_{}".format(name, suffix)
            suffix += 1
        taken.add(candidate)
        members.append((candidate, authored))
    return members


def _references(type_desc):
    """Every (kind, name) one schema type mentions: its class, struct or enum, and its elements'."""
    for kind in ("class", "struct", "enum"):
        if type_desc.get(kind):
            yield ("classes" if kind == "class" else kind + "s"), type_desc[kind]
    for nested in ("element", "key"):
        if isinstance(type_desc.get(nested), dict):
            yield from _references(type_desc[nested])


def _mentions(entry):
    """Every (kind, name) a class or struct's own members mention."""
    for member in entry.get("properties", []) + entry.get("members", []):
        yield from _references(member)
    for function in entry.get("functions", []):
        for parameter in function.get("parameters", []):
            yield from _references(parameter)
    for component in entry.get("components", []):
        if component.get("class"):
            yield "classes", component["class"]


def shared_with(schema, published):
    """What a project's types can take from the published ones instead of defining their own.

    Returns (class paths, struct names, enum names). A project's build has every class the
    published subset describes (TYPED_WORLD_API_PLAN.md section 16.5), and two generated types for
    one Unreal class would not unify: a helper written against the published `Pawn` would accept no
    project's Blueprint. So an entry the two describe alike is re-exported, and the project defines
    only what is its own or differs.

    Alike means more than an equal content hash, which covers only what an entry declares:

    - A class's superclass has to be the same, and shared too. One the subset rerooted, or one
      deriving from a class the project redefines, would otherwise deref to a published base while
      the project's code took its own.
    - Every struct and enum its members mention has to be shared too. The subset omits a member
      whose struct or enum it does not carry, so a published class could declare less than the
      project's though the hashes agree; and one carrying a struct the project redefines would hand
      back the published struct where the project expects its own.

    The classes its members mention are deliberately not constrained. Either stricter rule costs
    more than it saves, measured on TempoSample's build (TYPED_WORLD_API_PLAN.md section 16.11):
    requiring them shared spreads 44 rerooted or unusual classes through the reference graph to
    Actor, Pawn and every component; requiring them merely published unshares StaticMesh, whose
    setter on the shared StaticMeshComponent then takes a type the project's own asset catalog
    does not hand out. Unconstrained, a member mentioning a class the subset lacks is typed as a
    bare Object in both packages -- a dozen members -- and one mentioning a published class the
    project redefines hands back the published one, which two members do.

    Hence the largest set closed under both rules: start from what hashes alike and drop whatever
    leans on something outside it, until nothing more drops. Reference cycles stay in, as they
    should.
    """
    if not published:
        return set(), set(), set()

    entries, theirs, shared = {}, {}, {}
    for kind, key in (("classes", "path"), ("structs", "name"), ("enums", "name")):
        entries[kind] = {entry[key]: entry for entry in schema.get(kind, [])}
        theirs[kind] = {entry[key]: entry for entry in published.get(kind, [])}
        shared[kind] = {name for name, entry in entries[kind].items()
                        if entry.get("content_hash")
                        and entry.get("content_hash") == theirs[kind].get(name, {}).get("content_hash")}

    def leans_outside(kind, name):
        entry = entries[kind][name]
        if kind == "classes":
            superclass = entry.get("super") or ""
            if superclass != (theirs[kind][name].get("super") or ""):
                return True
            if superclass and superclass not in shared["classes"]:
                return True
        for other_kind, other in _mentions(entry):
            # What the schema itself does not have is omitted alike on both sides.
            if other_kind != "classes" and other in entries[other_kind] \
                    and other not in shared[other_kind]:
                return True
        return False

    changed = True
    while changed:
        changed = False
        for kind in ("classes", "structs"):
            for name in [name for name in shared[kind] if leans_outside(kind, name)]:
                shared[kind].discard(name)
                changed = True
    return shared["classes"], shared["structs"], shared["enums"]
