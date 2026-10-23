"""Schema reading shared by the typed class generators.

`gen_py_classes.py` and `gen_rust_classes.py` need the same answers out of `tempo_schema.json`, and
a second implementation of one of them is a second thing that can be wrong.
"""


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
