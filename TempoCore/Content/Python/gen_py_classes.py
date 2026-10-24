# Copyright Tempo Simulation, LLC. All Rights Reserved
#
# Generates the typed Python class surface from tempo_schema.json: one importable class per Unreal
# class, with its properties and callable functions typed.
#
# The point of this output is not convenience, it is drift detection. A new release regenerates it,
# and a scenario that used a property which has since been removed, retyped or made read-only fails
# under Pyright instead of failing at runtime in the middle of a simulation. Two rules follow from
# that and are not negotiable:
#
#   1. A type the API cannot carry is OMITTED, never typed Any. Any passes silently, which is the
#      failure this exists to prevent.
#   2. A read-only property is emitted as a setter-less @property, because a plain annotation in a
#      .pyi is writable and would let "became read-only" through.
#
# Three files per module, which is what keeps a 38,000-member surface cheap:
#   <Module>.py         class identity only -- one small class object per Unreal class
#   <Module>.pyi        the whole type surface, read by type checkers and never imported
#   <Module>.meta.json  what the runtime needs to build a request, loaded lazily per module
#
# Run standalone:
#   python3 gen_py_classes.py <tempo_schema.json> <output-package-dir> [--docstrings]

import json
import keyword
import os
import re
import sys
from collections import defaultdict

from gen_classes_common import resolve_setters

# Where the hand-written runtime lives. Generated code takes its base class and value types from
# here and nothing else.
RUNTIME_MODULE = "tempo_sim.unreal"

# Generated code refers to everything it imports through a module alias, never by bare name.
#
# This is not style. Unreal has a class named `Object`, and a bare `from tempo_sim.unreal import
# Object` beside `from ...CoreUObject import Object` leaves the second shadowing the first, so
# `class Object(Object)` makes the root of the hierarchy Unknown -- and a type checker allows every
# attribute on an Unknown, which silently destroys the drift detection this output exists for.
# Qualifying every reference makes that class of collision impossible rather than unlikely.
RUNTIME_ALIAS = "_unreal"
ENUMS_ALIAS = "_enums"
STRUCTS_ALIAS = "_structs"

# Scalar and struct kinds that map straight onto a Python or runtime type.
SIMPLE_TYPES = {
    "bool": "bool",
    "int": "int",
    "int64": "int",
    "float": "float",
    "double": "float",
    "string": "str",
    "vector": "Vector",
    "vector2d": "Vector2D",
    "intvector": "IntVector",
    "intpoint": "IntPoint",
    "rotator": "Rotator",
    "quat": "Quat",
    "transform": "Transform",
    "color": "Color",
}

# The names those runtime types are imported under.
RUNTIME_TYPES = {name for name in SIMPLE_TYPES.values() if name[0].isupper()}

# Which Value variant carries each kind. This is the server's rule, mirrored: the runtime has to
# pick a variant to write, and Python's own types do not determine it -- a str could be a string, an
# enum, a class, an asset, an actor or a component.
SINGULAR_VARIANTS = {
    "bool": "bool_value",
    "int": "int_value",
    "int64": "int64_value",
    "float": "float_value",
    "double": "float_value",
    "string": "string_value",
    "enum": "enum_value",
    "vector": "vector_value",
    "vector2d": "vector2d_value",
    "intvector": "int_vector_value",
    "intpoint": "int_point_value",
    "rotator": "rotator_value",
    "quat": "quat_value",
    "transform": "transform_value",
    "color": "color_value",
    "class": "class_value",
    "asset": "asset_value",
    "actor": "actor_value",
    "component": "component_value",
}

# Element kinds a Value has repeated variants for. Anything else makes its container
# nested-access-only, which the server already reports as value_expressible: false.
REPEATED_ELEMENTS = {
    "bool": "bool", "int": "int", "int64": "int64", "float": "float", "double": "float",
    "string": "string", "enum": "enum", "class": "class", "asset": "asset",
    "actor": "actor", "component": "component",
}


def identifier(segment):
    """Turn one path segment into a legal Python identifier."""
    cleaned = re.sub(r"\W", "_", segment)
    if not cleaned or cleaned[0].isdigit():
        cleaned = "_" + cleaned
    if keyword.iskeyword(cleaned):
        cleaned += "_"
    return cleaned


def module_for(package):
    """The module a class's package maps to.

    `/Script/Engine` is one module, `Engine`. A Blueprint lives in a content path naming the asset
    itself (`/Game/Vehicles/BP_Cat745`), so its module is the folder it sits in, `Game.Vehicles` --
    an Unreal package is not a module for Blueprints the way it is for native code.
    """
    if package.startswith("/Script/"):
        return identifier(package[len("/Script/"):])
    segments = [identifier(s) for s in package.strip("/").split("/")]
    return ".".join(segments[:-1]) if len(segments) > 1 else segments[0]


def py_class_name(class_name):
    """A generated class keeps its Unreal name, Blueprint `_C` and all."""
    return identifier(class_name)


class Unsupported(Exception):
    """A type the API cannot carry. Its member is omitted rather than typed Any."""


class Generator:
    def __init__(self, schema, docstrings=False):
        self.schema = schema
        self.docstrings = docstrings
        self.stats = defaultdict(int)
        self.stats["setters_unresolvable"] = resolve_setters(schema)
        self.classes_by_path = {c["path"]: c for c in schema["classes"]}
        self.struct_names = {s["name"] for s in schema.get("structs", [])}
        # The classifier's answer, recorded per struct by the walk, rather than a second opinion
        # worked out here from the members.
        self.expressible_structs = {s["name"] for s in schema.get("structs", [])
                                    if s.get("value_expressible")}
        self.enum_names = {e["name"] for e in schema.get("enums", [])}
        self.current_module = ""
        self.current_class = ""
        self.result_types = []
        self.by_module = defaultdict(list)
        for cls in schema["classes"]:
            self.by_module[module_for(cls["package"])].append(cls)

    # -- type rendering -------------------------------------------------------------------------

    @staticmethod
    def module_alias(module_name):
        return "_m_" + module_name.replace(".", "_")

    def qualified_class(self, cls, imports):
        """A class reference: bare within its own module, module-qualified from outside."""
        name = py_class_name(cls["name"])
        other = module_for(cls["package"])
        return name if other == self.current_module else "{}.{}".format(self.module_alias(other), name)

    def annotation(self, type_desc, imports):
        """The Python annotation for one schema type, or raise Unsupported.

        `imports` collects what the emitted module has to import, so a module only imports what it
        actually names.
        """
        kind = type_desc.get("kind")

        if kind in SIMPLE_TYPES:
            name = SIMPLE_TYPES[kind]
            if name in RUNTIME_TYPES:
                imports["runtime"].add(name)
                return "{}.{}".format(RUNTIME_ALIAS, name)
            return name

        if kind == "enum":
            name = type_desc.get("enum")
            if not name or name not in self.enum_names:
                raise Unsupported("enum not in the schema")
            imports["enums"].add(identifier(name))
            return "{}.{}".format(ENUMS_ALIAS, identifier(name))

        if kind == "struct":
            name = type_desc.get("struct")
            if not name or name not in self.struct_names:
                raise Unsupported("struct not in the schema")
            if self.current_module == "structs":
                return identifier(name)
            imports["structs"].add(identifier(name))
            return "{}.{}".format(STRUCTS_ALIAS, identifier(name))

        if kind == "asset":
            # What travels is the asset's path, typed by the class the property accepts -- so the
            # catalog's AssetRef[Material] is refused where an AssetRef[StaticMesh] is wanted.
            imports["runtime"].add("AssetRef")
            return "{}.AssetRef[{}]".format(RUNTIME_ALIAS, self.reference_name(type_desc, imports))

        if kind in ("actor", "component", "class"):
            referenced = self.reference_name(type_desc, imports)
            return "type[{}]".format(referenced) if kind == "class" else referenced

        if kind in ("array", "set"):
            element = type_desc.get("element")
            if element is None:
                raise Unsupported("container with no element type")
            inner = self.annotation(element, imports)
            return "list[{}]".format(inner) if kind == "array" else "set[{}]".format(inner)

        if kind == "map":
            key, value = type_desc.get("key"), type_desc.get("element")
            if key is None or value is None:
                raise Unsupported("map with no key or value type")
            return "dict[{}, {}]".format(self.annotation(key, imports), self.annotation(value, imports))

        raise Unsupported(type_desc.get("unsupported_reason") or "kind {!r}".format(kind))

    def reference_name(self, type_desc, imports):
        """The generated class an object reference points at, or the runtime base when unknown.

        `class`, not `class_path`: that is what `TempoWorldSchema.cpp` writes into the JSON, and
        what `emit_component` has always read. `class_path` is the *proto* field's name, and
        reading it here silently typed every reference-valued member as a bare `Object` -- the
        lookup never matched, so the fallback always won.
        """
        referenced = self.classes_by_path.get(type_desc.get("class", ""))
        if referenced is None:
            # The pointee is outside this walk, so the most that can be said is that it is an
            # object. Narrower than Any: its members are still not addressable.
            imports["runtime"].add("Object")
            return "{}.Object".format(RUNTIME_ALIAS)
        imports["classes"].add(referenced["path"])
        return self.qualified_class(referenced, imports)

    # -- variants the runtime writes with -------------------------------------------------------

    def variant_for(self, type_desc):
        kind = type_desc.get("kind")
        if kind in SINGULAR_VARIANTS:
            return SINGULAR_VARIANTS[kind]
        if kind == "struct":
            return "struct_value" if type_desc.get("struct") in self.expressible_structs else None
        if kind in ("array", "set"):
            element = type_desc.get("element") or {}
            if element.get("kind") == "struct":
                # Arrays only: a set of structs has no variant.
                carried = kind == "array" and element.get("struct") in self.expressible_structs
                return "struct_array_value" if carried else None
            base = REPEATED_ELEMENTS.get(element.get("kind"))
            if base is None:
                return None
            return "{}_{}_value".format(base, "array" if kind == "array" else "set")
        return None

    def struct_of(self, type_desc):
        """The struct a member, parameter or array element is, or "" when it is not one."""
        if type_desc.get("kind") == "struct":
            return type_desc.get("struct", "")
        element = type_desc.get("element") or {}
        if type_desc.get("kind") == "array" and element.get("kind") == "struct":
            return element.get("struct", "")
        return ""

    # -- emission -------------------------------------------------------------------------------

    def docstring(self, member, indent):
        text = (member.get("tooltip") or "").strip() if self.docstrings else ""
        if not text:
            return []
        # One line: a tooltip's own newlines would otherwise break out of the string.
        text = " ".join(text.split()).replace('"""', "'''")
        return ['{}"""{}"""'.format(indent, text)]

    def emit_component(self, component, imports, indent="    "):
        """A component the class brings with it, typed as its own class.

        Read-only: the attribute is a handle to a component that already exists, not a slot to put
        a different one in. Replacing a component is not an assignment.
        """
        name = component["name"]
        if not name.isidentifier() or keyword.iskeyword(name):
            self.stats["components_unnameable"] += 1
            return []

        referenced = self.classes_by_path.get(component.get("class", ""))
        if referenced is None:
            # Its class is outside this build, so there is nothing useful to type it as -- and a
            # handle whose members are unknown is not worth offering.
            self.stats["components_omitted"] += 1
            return []

        self.stats["components"] += 1
        annotation = self.qualified_class(referenced, imports)
        imports["classes"].add(referenced["path"])
        return ["{}@property".format(indent),
                "{}def {}(self) -> {}:".format(indent, name, annotation),
                "{}    ...".format(indent)]

    def emit_member(self, member, imports, indent="    "):
        """One property, as stub lines. Returns [] when the type cannot be carried."""
        if self.variant_for(member) is None and member.get("kind") != "struct":
            # Neither one value nor a struct to reach into -- an array of vectors, say. The runtime
            # has no way to read it, so a stub declaring it would type-check and then fail.
            self.stats["members_omitted"] += 1
            return []
        try:
            annotation = self.annotation(member, imports)
        except Unsupported:
            self.stats["members_omitted"] += 1
            return []

        name = member["name"]
        if not name.isidentifier() or keyword.iskeyword(name):
            # Verbatim Unreal names are the rule, so a name Python cannot spell is omitted rather
            # than silently mangled into something that would not survive a rename.
            self.stats["members_unnameable"] += 1
            return []

        self.stats["members"] += 1
        if member.get("writable"):
            self.stats["writable"] += 1
            return ["{}{}: {}".format(indent, name, annotation)] + self.docstring(member, indent)

        # Readable but not writable: a setter-less property, so assigning to it is a type error.
        self.stats["read_only"] += 1
        lines = ["{}@property".format(indent),
                 "{}def {}(self) -> {}:".format(indent, name, annotation)]
        # A docstring is the body when there is one; otherwise the body is `...`.
        doc = self.docstring(member, indent + "    ")
        lines.extend(doc if doc else ["{}    ...".format(indent)])
        return lines

    def emit_function(self, function, imports, indent="    "):
        """One callable function, as a stub method. Returns [] when it cannot be expressed."""
        if not function.get("callable") or not function.get("positional"):
            self.stats["functions_omitted"] += 1
            return []
        name = function["name"]
        if not name.isidentifier() or keyword.iskeyword(name):
            self.stats["functions_omitted"] += 1
            return []

        args, returns, outputs = [], "None", []
        for parameter in function.get("parameters", []):
            if parameter.get("direction") == "out":
                # An out parameter comes back in the results when it can travel, and is simply
                # absent from them when it cannot; either way it does not stop the call.
                if self.variant_for(parameter) is not None:
                    try:
                        outputs.append((parameter["name"], self.annotation(parameter, imports)))
                    except Unsupported:
                        pass
                continue
            try:
                annotation = self.annotation(parameter, imports)
            except Unsupported:
                self.stats["functions_omitted"] += 1
                return []
            # `direction` and not `kind`: a parameter carries both, and `kind` is the kind of its
            # *type*. Reading `kind` here matched neither "in" nor "return", so every parameter
            # looked like an out parameter and was dropped -- which emitted 4,590 methods with no
            # arguments and no return type, and looked perfectly plausible in the output.
            direction = parameter.get("direction")
            if direction == "return":
                if self.variant_for(parameter) is None:
                    # A result that cannot come back. Offering a method that silently answers None
                    # is worse than not offering it -- §9.4, as gen_rust_classes.py has it.
                    self.stats["functions_omitted"] += 1
                    return []
                returns = annotation
            elif direction == "in":
                parameter_name = parameter["name"]
                if not parameter_name.isidentifier() or keyword.iskeyword(parameter_name):
                    self.stats["functions_omitted"] += 1
                    return []
                args.append("{}: {}".format(parameter_name, annotation))

        if outputs:
            # With out parameters the runtime hands back every result keyed by name, because a
            # Blueprint usually returns several things and position would be guesswork. The keys
            # and their types are fixed by the signature, so they are a TypedDict -- functional
            # syntax, because a key need only be a string.
            results = ([("ReturnValue", returns)] if returns != "None" else []) + outputs
            result_type = "_Result_{}_{}".format(self.current_class, name)
            self.result_types.append('{} = _typing.TypedDict("{}", {{{}}})'.format(
                result_type, result_type,
                ", ".join('"{}": {}'.format(key, value) for key, value in results)))
            returns = result_type
            self.stats["functions_with_results"] += 1

        self.stats["functions"] += 1
        return ["{}def {}(self{}) -> {}: ...".format(
            indent, name, "".join(", " + a for a in args), returns)]

    def import_lines(self, module_name, imports, dist):
        """What a module imports, as aliases. Every reference through an alias, so nothing collides."""
        lines = []
        if imports["runtime"]:
            lines.append("import {} as {}".format(RUNTIME_MODULE, RUNTIME_ALIAS))
        if imports["enums"]:
            lines.append("import {}.enums as {}".format(dist, ENUMS_ALIAS))
        if imports["structs"]:
            lines.append("import {}.structs as {}".format(dist, STRUCTS_ALIAS))

        # One alias per referenced module. A class in this same module needs none. Cycles between
        # modules are fine in a .pyi -- a type checker resolves it lazily -- and the identity files
        # never reference another module at all.
        others = set()
        for path in imports["classes"]:
            other = module_for(self.classes_by_path[path]["package"])
            if other != module_name:
                others.add(other)
        for other in sorted(others):
            lines.append("import {}.{} as {}".format(dist, other, self.module_alias(other)))
        return lines

    def base_of(self, cls, imports):
        """A generated class's base: its real superclass when the walk has it, else the runtime base."""
        super_path = cls.get("super") or ""
        base = self.classes_by_path.get(super_path)
        if base is None:
            imports["runtime"].add("Object")
            return "{}.Object".format(RUNTIME_ALIAS)
        imports["classes"].add(base["path"])
        return self.qualified_class(base, imports)

    @staticmethod
    def in_definition_order(classes):
        """Classes ordered so a base is always defined before anything deriving from it.

        Path order alone is not enough: `ArrayProperty` sorts before `Property`, and at runtime the
        base has to exist when the subclass statement executes. Ancestors within the same module are
        emitted first, depth-first, with path order as the tiebreak so the output stays
        deterministic -- the whole point of sorting in the first place.
        """
        by_path = {c["path"]: c for c in classes}
        ordered, seen = [], set()

        def emit(cls):
            if cls["path"] in seen:
                return
            # Marked before recursing, so a cycle terminates rather than recursing forever. Unreal
            # cannot express one, but a corrupt schema should not hang the generator.
            seen.add(cls["path"])
            base = by_path.get(cls.get("super") or "")
            if base is not None:
                emit(base)
            ordered.append(cls)

        for cls in sorted(classes, key=lambda c: c["path"]):
            emit(cls)
        return ordered

    def emit_module(self, module_name, classes, dist):
        """The three files for one module: identity, types, and runtime metadata."""
        self.current_module = module_name
        names_seen = {}
        stub_imports = {"runtime": set(), "enums": set(), "structs": set(), "classes": set()}
        identity_imports = {"runtime": set(), "enums": set(), "structs": set(), "classes": set()}
        stub_bodies, identity_bodies = [], []
        metadata = {}
        self.result_types = []

        for cls in self.in_definition_order(classes):
            name = py_class_name(cls["name"])
            self.current_class = name
            if name in names_seen:
                # Two Blueprints in different folders can share a name. Rather than let one shadow
                # the other, which would silently point a scenario at the wrong class, say so.
                raise Exception(
                    "module {} has two classes named {}: {} and {}. Class names must be unique "
                    "within a module.".format(module_name, name, names_seen[name], cls["path"]))
            names_seen[name] = cls["path"]

            stub_base = self.base_of(cls, stub_imports)
            # The identity file derives everything from the runtime base rather than from the real
            # superclass, which keeps it free of cross-module imports.
            #
            # That is not just tidiness: Unreal's inheritance graph has cycles across packages --
            # Engine derives from PythonScriptPlugin, which derives from BlueprintGraph, which
            # derives from Engine -- and a base class has to exist when the class statement runs, so
            # real bases here deadlock on import. The true hierarchy lives in the .pyi, where a type
            # checker resolves it lazily and cycles are fine, and in the metadata's `super`, which
            # is what the runtime answers subclass questions from.
            identity_base = "{}.Object".format(RUNTIME_ALIAS)
            identity_imports["runtime"].add("Object")

            # Identity: a class object and its path, nothing more. No annotations, so no
            # __annotations__ dict per class and nothing to pay for at import.
            identity_bodies.append("class {}({}):".format(name, identity_base))
            identity_bodies.append('    __tempo_class__ = "{}"'.format(cls["path"]))
            identity_bodies.append("")

            body = []
            # A Blueprint component variable is two things at once: a component template, and the
            # object property that holds the component. Declaring both would put the same name in
            # the class body twice, and the later declaration is the one a type checker believes --
            # so the typed handle would lose to a bare object, and the runtime, which looks for a
            # property first, would answer a read with the component's name instead of a handle.
            # The component accessor is the useful one, so it wins and the property stands down.
            component_names = set()
            for component in cls.get("components", []):
                lines = self.emit_component(component, stub_imports)
                if lines:
                    component_names.add(component["name"])
                body.extend(lines)
            for member in cls.get("properties", []):
                if member["name"] in component_names:
                    self.stats["members_shadowed_by_component"] += 1
                    continue
                body.extend(self.emit_member(member, stub_imports))
            for function in cls.get("functions", []):
                body.extend(self.emit_function(function, stub_imports))

            stub_bodies.append("class {}({}):".format(name, stub_base))
            stub_bodies.extend(body if body else ["    ..."])
            stub_bodies.append("")

            # Metadata: what the runtime needs to build a request. Only members the stubs actually
            # expose, so the two cannot disagree about what exists.
            properties = {}
            for member in cls.get("properties", []):
                if member["name"] in component_names:
                    # Stood down above; the runtime must agree with the stubs about which one it is.
                    continue
                variant = self.variant_for(member)
                struct = self.struct_of(member)
                if variant is None and member.get("kind") != "struct":
                    # Neither carryable as one value nor addressable through members.
                    continue
                properties[member["name"]] = {
                    "variant": variant,
                    "writable": bool(member.get("writable")),
                    "getter": member.get("getter", ""),
                    "setter": member.get("setter", ""),
                    # What the setter calls its argument. CallFunction rejects an unnamed one, so
                    # the runtime cannot write through an accessor without this.
                    "setter_argument": member.get("setter_argument", ""),
                    "struct": struct,
                }
            # Functions the stubs expose, so the runtime can invoke exactly what they declare --
            # the parameter order is the calling convention, and the variants are how each argument
            # is carried.
            functions = {}
            for function in cls.get("functions", []):
                if not function.get("callable") or not function.get("positional"):
                    continue
                inputs, returns = [], None
                expressible = True
                for parameter in function.get("parameters", []):
                    variant = self.variant_for(parameter)
                    direction = parameter.get("direction")
                    if direction == "return":
                        if variant is None:
                            expressible = False
                            break
                        returns = variant
                    elif direction == "in":
                        if variant is None:
                            expressible = False
                            break
                        inputs.append({"name": parameter["name"], "variant": variant,
                                       "struct": self.struct_of(parameter)})
                    # An out parameter is not supplied, and comes back in the results.
                if not expressible:
                    continue
                functions[function["name"]] = {"inputs": inputs, "returns": returns}

            components = {}
            for component in cls.get("components", []):
                referenced = self.classes_by_path.get(component.get("class", ""))
                if referenced is None:
                    continue
                components[component["name"]] = component["class"]

            metadata[cls["path"]] = {"name": name, "super": cls.get("super", ""),
                                     "properties": properties, "functions": functions,
                                     "components": components}

        self.stats["classes"] += len(classes)
        header = "# Generated by gen_py_classes.py from tempo_schema.json. Do not edit.\n"
        identity = header + "\n".join(self.import_lines(module_name, identity_imports, dist)
                                      + [""] + identity_bodies)
        stub_lines = self.import_lines(module_name, stub_imports, dist)
        if self.result_types:
            stub_lines.append("import typing as _typing")
        stub_lines += [""] + stub_bodies
        if self.result_types:
            stub_lines += ["# What a method with out parameters hands back, keyed by name."] + \
                self.result_types + [""]
        stub = header + "\n".join(stub_lines)
        return identity, stub, metadata

    def emit_structs(self, dist):
        """Structs: the static type of a struct property's proxy, and a value type of their own.

        The same class serves both. A struct-valued property reads as a live handle, so that
        `PostProcessSettings.BloomIntensity = 2.0` keeps writing to the sim, and is typed as the
        struct so its members check. A value-expressible struct can also be built and passed whole --
        to a function, or to assign -- through a keyword-only constructor naming the members a
        scenario may write. Keyword-only because a struct's members are sorted by name, so a
        positional argument would silently move when a member was added.

        Returns the identity module and the stub, which differ: the identity carries no annotations,
        as a class module's does not.
        """
        self.current_module = "structs"
        imports = {"runtime": {"Struct"}, "enums": set(), "structs": set(), "classes": set()}
        identities, bodies = [], []
        for struct in sorted(self.schema.get("structs", []), key=lambda s: s["name"]):
            name = identifier(struct["name"])
            body, arguments = [], []
            for member in struct.get("members", []):
                lines = self.emit_member(member, imports)
                body.extend(lines)
                if (lines and member.get("writable") and struct["name"] in self.expressible_structs
                        and self.variant_for(member) is not None):
                    arguments.append("{}: {} = ...".format(
                        member["name"], self.annotation(member, imports)))
            if struct["name"] in self.expressible_structs:
                body.append("    def __init__(self{}) -> None: ...".format(
                    ", *, " + ", ".join(arguments) if arguments else ""))
                self.stats["struct_values"] += 1
            bodies.append("class {}({}.Struct):".format(name, RUNTIME_ALIAS))
            bodies.extend(body if body else ["    ..."])
            bodies.append("")
            identities.append("class {}({}.Struct):".format(name, RUNTIME_ALIAS))
            identities.append("    __slots__ = ()")
            identities.append('    __tempo_struct__ = "{}"'.format(struct["name"]))
            identities.append("")
        header = "# Generated by gen_py_classes.py from tempo_schema.json. Do not edit.\n"
        identity = header + "\n".join(["import {} as {}".format(RUNTIME_MODULE, RUNTIME_ALIAS), ""]
                                      + identities)
        # A struct can name another struct declared further down this same file, so annotations
        # have to be lazy for the stub to read in order.
        stub = (header + "from __future__ import annotations\n"
                + "\n".join(self.import_lines("structs", imports, dist) + [""] + bodies))
        return identity, stub

    def struct_metadata(self):
        """Per-struct member metadata, so a nested address can be resolved and written.

        A struct property is reachable a member at a time -- "PostProcessSettings.BloomIntensity" --
        which the server has always supported. The runtime needs each member's Value variant for the
        same reason a class property does.
        """
        metadata = {}
        for struct in self.schema.get("structs", []):
            members = {}
            for member in struct.get("members", []):
                variant = self.variant_for(member)
                members[member["name"]] = {
                    "variant": variant,
                    "writable": bool(member.get("writable")),
                    "struct": self.struct_of(member),
                }
            metadata[struct["name"]] = members
        return metadata

    def emit_asset_module(self, mount, node, dist):
        """One mount point of the catalog: its identity module and its stub.

        A folder is a class used as a namespace, and an asset a Final attribute on it typed by the
        asset's class. The identity module holds the AssetRefs themselves; the stub says what each
        one is, and is what makes a moved or renamed asset a type error.
        """
        self.current_module = "assets." + identifier(mount)
        imports = {"runtime": {"AssetRef"}, "enums": set(), "structs": set(), "classes": set()}
        identity, stub = [], []

        def names_in(node):
            """Each entry's Python name, with the collisions settled as asset_namespaces says."""
            entries = [("asset", identifier(name), assets) for name, assets in node["assets"].items()]
            asset_names = {name for _, name, _ in entries}
            for folder in node["folders"]:
                name = identifier(folder)
                if name in asset_names:
                    name += "_"
                    self.stats["asset_folders_renamed"] += 1
                entries.append(("folder", name, folder))
            counts = defaultdict(int)
            for _, name, _ in entries:
                counts[name] += 1
            for kind, name, payload in sorted(entries, key=lambda entry: entry[1]):
                if counts[name] > 1 or (kind == "asset" and len(payload) > 1):
                    self.stats["assets_colliding"] += 1
                    continue
                yield kind, name, payload

        def emit(node, indent):
            for kind, name, payload in names_in(node):
                if kind == "folder":
                    identity.append("{}class {}:".format(indent, name))
                    stub.append("{}class {}:".format(indent, name))
                    before = len(identity)
                    emit(node["folders"][payload], indent + "    ")
                    if len(identity) == before:
                        identity.append(indent + "    pass")
                        stub.append(indent + "    ...")
                    continue
                asset = payload[0]
                referenced = self.classes_by_path.get(asset["class"])
                if referenced is None:
                    imports["runtime"].add("Object")
                    asset_type = "{}.Object".format(RUNTIME_ALIAS)
                else:
                    imports["classes"].add(referenced["path"])
                    asset_type = self.qualified_class(referenced, imports)
                identity.append('{}{} = {}.AssetRef("{}", "{}")'.format(
                    indent, name, RUNTIME_ALIAS, asset["path"], asset["class"]))
                stub.append("{}{}: _typing.Final[{}.AssetRef[{}]]".format(
                    indent, name, RUNTIME_ALIAS, asset_type))
                self.stats["assets"] += 1

        emit(node, "")
        header = "# Generated by gen_py_classes.py from tempo_schema.json. Do not edit.\n"
        identity_text = header + "\n".join(
            ["import {} as {}".format(RUNTIME_MODULE, RUNTIME_ALIAS), ""] + identity) + "\n"
        stub_text = header + "\n".join(
            self.import_lines(self.current_module, imports, dist) + ["import typing as _typing", ""]
            + stub) + "\n"
        return identity_text, stub_text

    def emit_enums(self):
        """Enums as string-valued, because the authored name is what the wire carries."""
        lines = ["# Generated by gen_py_classes.py from tempo_schema.json. Do not edit.", "",
                 "from enum import Enum", ""]
        for enum in sorted(self.schema.get("enums", []), key=lambda e: e["name"]):
            lines.append("")
            lines.append("class {}(str, Enum):".format(identifier(enum["name"])))
            emitted = set()
            for enumerator in enum.get("enumerators", []):
                member = identifier(enumerator)
                if member in emitted:
                    continue
                emitted.add(member)
                lines.append('    {} = "{}"'.format(member, enumerator))
            if not emitted:
                lines.append("    pass")
        self.stats["enums"] = len(self.schema.get("enums", []))
        self.stats["structs"] = len(self.schema.get("structs", []))
        return "\n".join(lines) + "\n"


def asset_namespaces(assets, stats):
    """The cooked assets as a tree: mount point -> folders -> assets, with names a module can use.

    An asset is named by its package -- `/Engine/EngineResources/Black_Low` is `Black_Low`, though
    the object inside it is called `Black` -- because a package name is unique within its folder
    and an object name is not. Names stay verbatim, mangled only where Python cannot spell them.

    A map routinely sits beside a folder of the same name (`OpenWorld.umap` and `OpenWorld/`), and
    Python has one namespace for both; the asset keeps the name and the folder takes a trailing
    underscore. Two entries that still land on one name are both left out and counted rather than
    failing the package: absent is the documented failure, and the rest of the catalog is still
    worth shipping.
    """
    tree = {}
    for asset in assets:
        package = asset["path"].split(".", 1)[0]
        segments = package.strip("/").split("/")
        if len(segments) < 2:
            stats["assets_unplaceable"] += 1
            continue
        node = tree.setdefault(segments[0], {"folders": {}, "assets": {}})
        for folder in segments[1:-1]:
            node = node["folders"].setdefault(folder, {"folders": {}, "assets": {}})
        node["assets"].setdefault(segments[-1], []).append(asset)
    return tree


def write_file(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as f:
        f.write(text)


def generate(schema_path, output_dir, docstrings=False, package=None):
    with open(schema_path) as f:
        schema = json.load(f)

    expected = "3"
    actual = schema.get("header", {}).get("schema_version")
    if actual != expected:
        raise Exception(
            "tempo_schema.json is schema_version {!r}, but this generator reads {!r}. Regenerate "
            "the schema, or use the generator that shipped with it.".format(actual, expected))

    # The dotted name the emitted modules import each other by. The directory's own name serves
    # for a top-level distribution; a package generated *inside* another one -- the published
    # classes at tempo_sim.UE.v5_8 (TYPED_WORLD_API_PLAN.md section 16.4) -- has to be told, since
    # its directory is only the last segment of it.
    dist = package or os.path.basename(output_dir.rstrip(os.sep))
    generator = Generator(schema, docstrings)

    # The enums module is emitted first, because structs and classes both import from it.
    write_file(os.path.join(output_dir, "enums.py"), generator.emit_enums())
    struct_identity, struct_stub = generator.emit_structs(dist)
    write_file(os.path.join(output_dir, "structs.py"), struct_identity)
    write_file(os.path.join(output_dir, "structs.pyi"), struct_stub)

    # A module name can also be a package prefix, because Unreal's mount points flatten onto the
    # same Python names: /Script/Engine wants to be `Engine`, and so does /Engine/EditorResources/...
    # Such a module becomes a package carrying its own classes in __init__, which keeps the import
    # path people actually use (`...Engine import SceneComponent`) rather than pushing everything
    # down a level to disambiguate.
    module_names = set(generator.by_module)
    is_package = {name for name in module_names
                  if any(other.startswith(name + ".") for other in module_names)}

    modules = []
    for module_name, classes in sorted(generator.by_module.items()):
        identity, stub, metadata = generator.emit_module(module_name, classes, dist)
        relative = module_name.replace(".", os.sep)
        stem = os.path.join(relative, "__init__") if module_name in is_package else relative
        write_file(os.path.join(output_dir, stem + ".py"), identity)
        write_file(os.path.join(output_dir, stem + ".pyi"), stub)
        write_file(os.path.join(output_dir, stem + ".meta.json"),
                   json.dumps(metadata, indent=1, sort_keys=True) + "\n")
        modules.append(module_name)

    # Every intermediate package needs an __init__ so `Game.Vehicles` is importable.
    packages = {""}
    for module_name in modules:
        segments = module_name.split(".")
        for depth in range(1, len(segments)):
            packages.add(os.sep.join(segments[:depth]))
    for package in sorted(p for p in packages if p):
        init = os.path.join(output_dir, package, "__init__.py")
        if not os.path.exists(init):
            write_file(init, "# Generated by gen_py_classes.py. Do not edit.\n")

    # The asset catalog: one module per mount point, `assets.Game`, `assets.Engine` and so on. Only
    # a schema narrowed against a packaged build has one -- the catalog is of what was cooked.
    tree = asset_namespaces(schema.get("assets", []), generator.stats)
    if tree:
        write_file(os.path.join(output_dir, "assets", "__init__.py"),
                   "# Generated by gen_py_classes.py. Do not edit.\n"
                   "#\n"
                   "# The assets this build cooked, by mount point: `from {}.assets import Game`.\n"
                   .format(dist))
        for mount, node in sorted(tree.items()):
            identity, stub = generator.emit_asset_module(mount, node, dist)
            stem = os.path.join(output_dir, "assets", identifier(mount))
            write_file(stem + ".py", identity)
            write_file(stem + ".pyi", stub)

    # The header the runtime compares against the sim it connects to.
    write_file(os.path.join(output_dir, "schema_header.json"),
               json.dumps(schema["header"], indent=1, sort_keys=True) + "\n")

    write_file(os.path.join(output_dir, "structs.meta.json"),
               json.dumps(generator.struct_metadata(), indent=1, sort_keys=True) + "\n")

    # Class path -> where its generated class lives. Inherently global: resolving a superclass, or
    # turning the class name a sim reports into a Python class, needs it before any module is known.
    index = {cls["path"]: "{}:{}".format(module_for(cls["package"]), py_class_name(cls["name"]))
             for cls in schema["classes"]}
    write_file(os.path.join(output_dir, "index.json"),
               json.dumps(index, indent=0, sort_keys=True) + "\n")

    # What `world.check_schema` compares, one entry at a time. A separate file because nothing else
    # reads it: a package that is never checked never loads it.
    #
    # Per entry rather than the header's one number, because a package may hold part of a build by
    # design -- the published classes describe the modules every build ships -- and a whole-walk
    # hash is then guaranteed to differ while every class in it is current. It is the better report
    # for a whole-build package too, which otherwise learns only that something moved.
    write_file(os.path.join(output_dir, "hashes.json"),
               json.dumps({
                   "classes": {cls["path"]: cls.get("content_hash", "")
                               for cls in schema["classes"]},
                   "structs": {struct["name"]: struct.get("content_hash", "")
                               for struct in schema.get("structs", [])},
                   "enums": {enum["name"]: enum.get("content_hash", "")
                             for enum in schema.get("enums", [])},
                   # An asset's identity is its path and its class: one that moved is absent, one
                   # whose class changed is a different asset under the same name.
                   "assets": {asset["path"]: asset["class"] for asset in schema.get("assets", [])},
               }, indent=0, sort_keys=True) + "\n")

    # PEP 561. Without this marker a type checker ignores the inline stubs of an *installed*
    # package entirely -- the generated classes would import and run and check nothing, which is
    # the one outcome this whole exercise is meant to prevent.
    write_file(os.path.join(output_dir, "py.typed"), "")

    # Importing the package is what tells the runtime where to find all of the above.
    write_file(os.path.join(output_dir, "__init__.py"),
               "# Generated by gen_py_classes.py. Do not edit.\n"
               "import os as _os\n\n"
               "import tempo_sim.unreal as _unreal\n\n"
               "_unreal.register_classes(__name__, _os.path.dirname(__file__))\n")

    return generator.stats, modules


def main(argv):
    arguments = [a for a in argv[1:] if not a.startswith("--")]
    options = {a.split("=", 1)[0]: a.split("=", 1)[-1] for a in argv[1:] if a.startswith("--")}
    if len(arguments) < 2:
        print(__doc__ or
              "usage: gen_py_classes.py <schema.json> <output-dir> [--docstrings] [--package=NAME]")
        return 2
    stats, modules = generate(arguments[0], arguments[1], "--docstrings" in options,
                              options.get("--package"))
    print("modules            {}".format(len(modules)))
    print("classes            {}".format(stats["classes"]))
    print("structs / enums    {} / {} ({} passable as values)".format(
        stats["structs"], stats["enums"], stats["struct_values"]))
    print("members            {} ({} writable, {} read-only)".format(
        stats["members"], stats["writable"], stats["read_only"]))
    print("functions          {}".format(stats["functions"]))
    print("components         {} ({} omitted, {} unnameable, {} also a property)".format(
        stats["components"], stats["components_omitted"], stats["components_unnameable"],
        stats["members_shadowed_by_component"]))
    print("omitted            {} members, {} functions, {} unnameable".format(
        stats["members_omitted"], stats["functions_omitted"], stats["members_unnameable"]))
    print("assets             {} ({} folders renamed beside an asset, {} left out colliding)".format(
        stats["assets"], stats["asset_folders_renamed"], stats["assets_colliding"]))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
