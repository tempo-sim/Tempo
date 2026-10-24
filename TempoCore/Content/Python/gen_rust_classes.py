"""Generate typed Rust classes from tempo_schema.json, as a module tree inside a host crate.

    gen_rust_classes.py <schema.json> <output-dir> [--docs] [--root=PATH]
                        [--published=SCHEMA [--published-root=PATH]]

The tree is rooted at `<output-dir>/mod.rs`, and there is no manifest: the types go inside an
existing crate rather than one of their own -- a project's into its own client crate, as
`tempo_sample::reflected` (TYPED_WORLD_API_PLAN.md §20), and the published subset into tempo-sim, as
`tempo_sim::reflected` (§16.4). `--root` is that path from the crate's root, `crate::reflected` by
default: the emitted modules reach each other by it. References to the *runtime* need no such
treatment -- they name `::tempo_sim::unreal::*` absolutely, which resolves in any crate depending on
tempo-sim, and inside tempo-sim through its `extern crate self as tempo_sim`.

`--published` names the published subset's schema. What it describes identically is re-exported
from `--published-root` rather than defined again (§16.5), so a project's `Pawn` and tempo-sim's
are one type.

The Rust sibling of gen_py_classes.py, off the same schema. Where that emits three files per
module -- identity, types and runtime metadata -- this emits one, because Rust needs no metadata
table: a generated method already knows its property's type, its Value variant and its
BlueprintSetter, all of which were decided here rather than looked up at run time.

Unreal's single inheritance becomes a chain of Deref, which is what makes an inherited member
resolve (`lidar.relative_location()` reaches SceneComponent) and what lets a derived handle be
passed where a base one is wanted. See TYPED_WORLD_API_PLAN.md §15.
"""

import json
import os
import re
import sys

from collections import defaultdict

from gen_classes_common import enumerator_members, resolve_setters, shared_with

RUNTIME = "::tempo_sim::unreal"
ERROR = "::tempo_sim::TempoError"

# Rust's reserved words, including the ones reserved for future use -- `box` and `final` are not
# keywords today but are not usable as identifiers either.
RESERVED = frozenset("""
as break const continue crate dyn else enum extern false fn for if impl in let loop match mod move
mut pub ref return self Self static struct super trait true type unsafe use where while async await
abstract become box do final macro override priv typeof unsized virtual yield try gen
""".split())

# Kinds that map straight onto a Rust type and are carried by `get`/`set`.
SIMPLE_TYPES = {
    "bool": "bool",
    "int": "i32",
    "int64": "i64",
    # Unreal's float and double properties both travel in float_value. The typed surface says f64
    # for both, because that is what Unreal stores; the narrowing is the wire's.
    "float": "f64",
    "double": "f64",
    "string": "String",
    "vector": RUNTIME + "::Vector",
    "vector2d": RUNTIME + "::Vector2D",
    "intvector": RUNTIME + "::IntVector",
    "intpoint": RUNTIME + "::IntPoint",
    "rotator": RUNTIME + "::Rotator",
    "quat": RUNTIME + "::Quat",
    "transform": RUNTIME + "::Transform",
    "color": RUNTIME + "::Color",
    "class": RUNTIME + "::ClassRef",
}

# Element kinds a container has a Value variant for. Anything else makes its container
# nested-access-only, which the server already reports as value_expressible: false.
CONTAINER_ELEMENTS = frozenset(
    ("bool", "int", "int64", "float", "double", "string", "enum", "class", "asset", "actor",
     "component"))

# How each kind is read and written. `plain` is get/set; the others need a runtime helper, either
# because the Rust type cannot say which Value variant to use or because the orphan rule forbids
# an impl on a Vec of a generated type.
#
# ASSET is read as PLAIN is, and written by reference: a setter taking `&AssetRef<MaterialInterface>`
# is what lets `&M_PAINT`, an `&AssetRef<Material>`, coerce into it through AssetRef's Deref.
PLAIN, OBJECT, ENUMS, OBJECTS, ACTOR_REF, COMPONENT_REF, ASSET = range(7)


class Unsupported(Exception):
    """A type the API cannot carry. Its member is omitted rather than loosely typed."""


def rust_str(text):
    """A Rust string literal.

    Not `json.dumps`: JSON escapes anything outside ASCII as `\\uXXXX`, and a character outside the
    basic plane as a surrogate pair, neither of which Rust accepts -- and Blueprint function names do
    contain emoji. A Rust literal can hold UTF-8 as is; only the control characters need an
    escape, and Rust spells that `\\u{XX}`.
    """
    escaped = []
    for character in str(text):
        if character in ('"', "\\"):
            escaped.append("\\" + character)
        elif character == "\n":
            escaped.append("\\n")
        elif ord(character) < 0x20 or ord(character) == 0x7F:
            escaped.append("\\u{{{:x}}}".format(ord(character)))
        else:
            escaped.append(character)
    return '"' + "".join(escaped) + '"'


def ident(text):
    """One path segment as a legal Rust identifier."""
    cleaned = re.sub(r"\W", "_", text)
    if not cleaned or cleaned[0].isdigit():
        cleaned = "_" + cleaned
    return cleaned + "_" if cleaned in RESERVED else cleaned


def snake(name):
    """An Unreal member name as idiomatic Rust.

    Two passes, which is what reads acronyms correctly: `FOVAngle` -> `fov_angle`, not
    `f_o_v_angle`. The second pattern excludes digits deliberately -- with `[a-z0-9]` there,
    `Use2DMode` becomes `use2_d_mode` and `Vector2D` becomes `vector2_d`, which is wrong 147 times
    in TempoSample's schema. See TYPED_WORLD_API_PLAN.md §15.3.
    """
    spaced = re.sub(r"(.)([A-Z][a-z]+)", r"\1_\2", name)
    spaced = re.sub(r"([a-z])([A-Z])", r"\1_\2", spaced).lower()
    cleaned = re.sub(r"__+", "_", ident(spaced)).strip("_")
    if not cleaned:
        return "unnamed"
    if cleaned[0].isdigit():
        cleaned = "n" + cleaned
    return cleaned + "_" if cleaned in RESERVED else cleaned


def module_for(package):
    """The module path a class's package maps to, as a list of segments.

    `/Script/Engine` is one module, `engine`. A Blueprint lives in a content path naming the asset
    itself (`/Game/Vehicles/BP_Cat745`), so its module is the folder it sits in, `game::vehicles`
    -- an Unreal package is not a module for Blueprints the way it is for native code.
    """
    if package.startswith("/Script/"):
        return [snake(package[len("/Script/"):])]
    segments = [snake(s) for s in package.strip("/").split("/")]
    return segments[:-1] if len(segments) > 1 else segments


class Generator:
    def __init__(self, schema, docs=False, root="crate::reflected", published=None,
                 published_root="::tempo_sim::reflected"):
        self.schema = schema
        self.docs = docs
        # The path the emitted modules reach each other by: where the tree sits in its host crate --
        # `crate::reflected` both in a project's client crate and, for the published types, in
        # tempo-sim (TYPED_WORLD_API_PLAN.md section 16.4).
        self.root = root
        # What the published types already define identically, and where they live. Those are
        # re-exported rather than defined again, so there is one type per Unreal class -- and since
        # a re-export sits at the same path a definition would, nothing that names one changes.
        self.published_root = published_root
        self.shared_classes, self.shared_structs, self.shared_enums = shared_with(schema, published)
        self.classes_by_path = {c["path"]: c for c in schema["classes"]}
        self.struct_names = {s["name"] for s in schema.get("structs", [])}
        # The classifier's answer, recorded per struct by the walk -- not worked out again here.
        self.expressible_structs = {s["name"] for s in schema.get("structs", [])
                                    if s.get("value_expressible")}
        self.enum_names = {e["name"] for e in schema.get("enums", [])}
        self.by_module = defaultdict(list)
        for cls in schema["classes"]:
            self.by_module[tuple(module_for(cls["package"]))].append(cls)
        self.stats = defaultdict(int)
        # Before anything is emitted, so the accessors and the Python stubs agree about which
        # properties are writable.
        self.stats["setters_unresolvable"] = resolve_setters(schema)

    # -- type rendering -------------------------------------------------------------------------

    def rust_type(self, type_desc):
        """The Rust type for one schema type, and how the runtime carries it."""
        kind = type_desc.get("kind")

        if kind in SIMPLE_TYPES:
            return SIMPLE_TYPES[kind], PLAIN

        if kind == "enum":
            name = type_desc.get("enum")
            if not name or name not in self.enum_names:
                raise Unsupported("enum not in the schema")
            return self.root + "::enums::" + ident(name), PLAIN

        if kind in ("actor", "component"):
            return self.class_type(type_desc), OBJECT

        if kind == "asset":
            referenced = self.classes_by_path.get(type_desc.get("class", ""))
            if referenced is None:
                # Its class is outside this walk, so the most that can be said is that it is an asset.
                return RUNTIME + "::AssetRef", ASSET
            return "{}::AssetRef<{}>".format(RUNTIME, self.path_of(referenced)), ASSET

        if kind == "struct":
            name = type_desc.get("struct") or ""
            if name not in self.expressible_structs:
                # Reachable a member at a time through its proxy, never as one value.
                raise Unsupported("struct {!r} cannot travel as one value".format(name))
            return self.root + "::structs::" + ident(name), PLAIN

        if kind in ("array", "set"):
            element = type_desc.get("element") or {}
            element_kind = element.get("kind")
            if element_kind == "struct":
                # Arrays only: a set of structs has no Value variant.
                if kind != "array":
                    raise Unsupported("a set of structs has no Value variant")
                inner, _ = self.rust_type(element)
                return "Vec<{}>".format(inner), PLAIN
            if element_kind not in CONTAINER_ELEMENTS:
                # No repeated Value variant for it, so it is reachable only a member at a time.
                raise Unsupported("no Value variant for a container of {!r}".format(element_kind))
            inner, how = self.rust_type(element)
            if how == OBJECT:
                return "Vec<{}>".format(inner), OBJECTS
            if element_kind == "enum":
                return "Vec<{}>".format(inner), ENUMS
            return "Vec<{}>".format(inner), PLAIN

        raise Unsupported(type_desc.get("unsupported_reason") or "kind {!r}".format(kind))

    def class_type(self, type_desc):
        """The generated type a reference points at, qualified from the crate root."""
        referenced = self.classes_by_path.get(type_desc.get("class", ""))
        if referenced is None:
            # The pointee is outside this walk, so there is no generated type to name.
            raise Unsupported("the referenced class is not in this build")
        return self.path_of(referenced)

    def path_of(self, cls):
        return self.root + "::classes::{}::{}".format(
            "::".join(module_for(cls["package"])), ident(cls["name"]))

    def doc_lines(self, member, indent):
        text = (member.get("tooltip") or "").strip() if self.docs else ""
        if not text:
            return []
        # One line per source line, each its own doc comment.
        return ["{}/// {}".format(indent, line.strip()) for line in text.splitlines() if line.strip()]

    # -- emission -------------------------------------------------------------------------------

    def emit_property(self, member, owner, used):
        """The accessors for one property. Returns [] when its type cannot be carried."""
        try:
            rust, how = self.rust_type(member)
        except Unsupported:
            self.stats["properties_omitted"] += 1
            return []

        name = member["name"]
        getter = snake(name)
        if getter in used:
            self.stats["properties_shadowed"] += 1
            return []
        used.add(getter)
        used.add(getter + "_async")
        used.add("watch_" + getter)
        if member.get("writable"):
            # Reserved so a Set<Name> function cannot take the setter's name out from under it.
            # Only for a writable property: a read-only one has no setter to protect, and its
            # Set<Name> function is then the one way to change it -- UStaticMeshComponent's
            # SetStaticMesh, against its read-only StaticMesh.
            used.add("set_" + getter)
            used.add("set_" + getter + "_async")

        literal = rust_str(name)
        lines = self.doc_lines(member, "    ")
        self.stats["properties"] += 1

        read, read_async = {
            PLAIN: ("get", "get_async"),
            ASSET: ("get", "get_async"),
            OBJECT: ("get_object", "get_object_async"),
            ENUMS: ("get_enums", "get_enums_async"),
            OBJECTS: ("get_objects", "get_objects_async"),
        }[how]

        # A BlueprintGetter is called rather than read through, because it may be the only thing
        # that computes the value.
        accessor = member.get("getter") or ""
        if accessor and how in (PLAIN, ASSET):
            lines.append("    pub fn {}(&self) -> Result<{}, {}> {{ self.0.get_via({}) }}"
                         .format(getter, rust, ERROR, rust_str(accessor)))
            lines.append("    pub fn {}_async(&self) -> impl ::core::future::Future<Output = "
                         "Result<{}, {}>> + '_ {{ self.0.get_via_async({}) }}"
                         .format(getter, rust, ERROR, rust_str(accessor)))
        else:
            lines.append("    pub fn {}(&self) -> Result<{}, {}> {{ self.0.{}({}) }}"
                         .format(getter, rust, ERROR, read, literal))
            lines.append("    pub fn {}_async(&self) -> impl ::core::future::Future<Output = "
                         "Result<{}, {}>> + '_ {{ self.0.{}({}) }}"
                         .format(getter, rust, ERROR, read_async, literal))

        if how in (PLAIN, ASSET):
            # Streaming is a plain read repeated, so only the plainly-carried kinds offer it.
            lines.append("    pub fn watch_{}(&self) -> Result<impl ::core::iter::Iterator<Item = "
                         "Result<{}, {}>>, {}> {{ self.0.watch({}) }}"
                         .format(getter, rust, ERROR, ERROR, literal))

        if not member.get("writable"):
            # Readable but not writable: no setter at all, so writing one is a compile error that
            # names the method rather than complaining about an assignment.
            self.stats["read_only"] += 1
            return lines

        self.stats["writable"] += 1
        setter = member.get("setter") or ""
        if setter and how in (PLAIN, ASSET):
            # Both the function and what it calls its argument: CallFunction rejects an unnamed
            # argument and matches the name against the function's own parameters.
            accessor = "{}, {}".format(rust_str(setter),
                                       rust_str(member.get("setter_argument", "")))
            parameter, value = ("value: &{}".format(rust), "value.clone()") if how == ASSET \
                else ("value: {}".format(rust), "value")
            lines.append("    pub fn set_{}(&self, {}) -> Result<(), {}> "
                         "{{ self.0.set_via({}, {}) }}"
                         .format(getter, parameter, ERROR, accessor, value))
            lines.append("    pub fn set_{}_async(&self, {}) -> impl "
                         "::core::future::Future<Output = Result<(), {}>> + '_ "
                         "{{ self.0.set_via_async({}, {}) }}"
                         .format(getter, parameter, ERROR, accessor, value))
            return lines

        write = {
            PLAIN: ("set", "set_async", "value: {}".format(rust), "value"),
            ASSET: ("set", "set_async", "value: &{}".format(rust), "value.clone()"),
            OBJECT: self.object_write(member, rust),
            ENUMS: ("set_enums", "set_enums_async", "value: &[{}]".format(rust[4:-1]), "value"),
            OBJECTS: self.objects_write(member, rust),
        }[how]
        call, call_async, signature, argument = write
        lines.append("    pub fn set_{}(&self, {}) -> Result<(), {}> {{ self.0.{}({}, {}) }}"
                     .format(getter, signature, ERROR, call, literal, argument))
        lines.append("    pub fn set_{}_async(&self, {}) -> impl ::core::future::Future<Output = "
                     "Result<(), {}>> + '_ {{ self.0.{}({}, {}) }}"
                     .format(getter, signature, ERROR, call_async, literal, argument))
        return lines

    @staticmethod
    def object_write(member, rust):
        """Writing one reference: the name alone does not say which variant to use, so say it."""
        if member.get("kind") == "actor":
            return ("set_actor_ref", "set_actor_ref_async", "value: &{}".format(rust), "value")
        return ("set_component_ref", "set_component_ref_async", "value: &{}".format(rust), "value")

    @staticmethod
    def objects_write(member, rust):
        element = rust[4:-1]
        if ((member.get("element") or {}).get("kind")) == "actor":
            return ("set_actor_refs", "set_actor_refs_async", "value: &[{}]".format(element),
                    "value")
        return ("set_component_refs", "set_component_refs_async",
                "value: &[{}]".format(element), "value")

    def emit_struct_property(self, member, used):
        """A struct-valued property: a typed proxy to reach into, and a setter when it travels whole.

        The accessor always returns the proxy, whose `load()` reads the whole value -- so a member
        write through it goes on writing to the sim. A writable struct that can travel as one value
        also gets `set_<name>`, which takes the generated value type and, like every setter, goes
        through the property's BlueprintSetter when it has one.
        """
        name = member.get("struct") or ""
        if name not in self.struct_names:
            self.stats["properties_omitted"] += 1
            return []
        accessor = snake(member["name"])
        if accessor in used:
            self.stats["properties_shadowed"] += 1
            return []
        used.add(accessor)
        self.stats["struct_properties"] += 1
        lines = self.doc_lines(member, "    ") + [
            "    pub fn {}(&self) -> {}::structs::{}Ref<'_> "
            "{{ {}::structs::{}Ref(self.0.struct_ref({})) }}".format(
                accessor, self.root, ident(name), self.root, ident(name),
                rust_str(member["name"]))]
        if not member.get("writable") or name not in self.expressible_structs:
            return lines

        setter_name = "set_" + accessor
        if setter_name in used:
            return lines
        used.add(setter_name)
        used.add(setter_name + "_async")
        value_type = "{}::structs::{}".format(self.root, ident(name))
        literal = rust_str(member["name"])
        setter = member.get("setter") or ""
        if setter:
            accessor_args = "{}, {}".format(rust_str(setter),
                                            rust_str(member.get("setter_argument", "")))
            call, call_async = "set_via({}, value)".format(accessor_args), \
                "set_via_async({}, value)".format(accessor_args)
        else:
            call, call_async = "set({}, value)".format(literal), "set_async({}, value)".format(literal)
        lines.append("    pub fn {}(&self, value: {}) -> Result<(), {}> {{ self.0.{} }}".format(
            setter_name, value_type, ERROR, call))
        lines.append("    pub fn {}_async(&self, value: {}) -> impl ::core::future::Future<Output = "
                     "Result<(), {}>> + '_ {{ self.0.{} }}".format(
                         setter_name, value_type, ERROR, call_async))
        return lines

    def emit_component(self, component, used):
        """A component the class brings with it, typed as its own class.

        Read-only: the accessor names a component that already exists, not a slot to put a
        different one in.
        """
        referenced = self.classes_by_path.get(component.get("class", ""))
        if referenced is None:
            self.stats["components_omitted"] += 1
            return []
        accessor = snake(component["name"])
        if accessor in used:
            self.stats["components_shadowed"] += 1
            return []
        used.add(accessor)
        self.stats["components"] += 1
        return ["    pub fn {}(&self) -> {} {{ self.0.component_handle({}) }}".format(
            accessor, self.path_of(referenced), rust_str(component["name"]))]

    def emit_function(self, function, used):
        """One callable function. Returns [] when its signature cannot be expressed."""
        if not function.get("callable") or not function.get("positional"):
            self.stats["functions_omitted"] += 1
            return []

        name = snake(function["name"])
        if name in used:
            # A property's accessor already took this name -- the 257 cases where a property and
            # its own Set<Name> function both want `set_<name>`. They do the same thing, and where
            # the property has a BlueprintSetter its setter path already calls the accessor.
            self.stats["functions_shadowed"] += 1
            return []

        inputs, outputs, returns = [], [], None
        for parameter in function.get("parameters", []):
            direction = parameter.get("direction")
            try:
                rust, how = self.rust_type(parameter)
                # A container of enums or object references would need a slice of a generated type
                # as an argument, or a decode this runtime does not offer as a result. Either way
                # it cannot be carried here.
                carryable = how not in (ENUMS, OBJECTS)
            except Unsupported:
                rust, how, carryable = None, None, False

            if not carryable:
                if direction == "out":
                    # Dropped from the result and nothing else: the caller does not supply an out
                    # parameter, so one that cannot be carried back does not stop the call. This is
                    # what keeps K2_SetActorLocation, whose out HitResult is an FHitResult.
                    self.stats["out_params_dropped"] += 1
                    continue
                # An input the caller cannot pass, or a return value the method would have to
                # discard. Offering a method that silently drops its answer is worse than not
                # offering it, so this is omitted -- §9.4, omitted rather than loosely typed.
                self.stats["functions_omitted"] += 1
                return []

            entry = (snake(parameter["name"]), parameter["name"], rust, how,
                     parameter.get("kind"))
            if direction == "return":
                returns = entry
            elif direction == "out":
                outputs.append(entry)
            elif direction == "in":
                inputs.append(entry)

        if len({argument for argument, _, _, _, _ in inputs}) != len(inputs):
            self.stats["functions_omitted"] += 1
            return []

        used.add(name)
        used.add(name + "_async")
        self.stats["functions"] += 1
        if outputs:
            self.stats["functions_with_out_params"] += 1

        # An object argument is passed by reference: a handle is not a value to hand over. So is an
        # asset, which is what lets a catalog entry of a derived class coerce into the parameter.
        signature = "".join(
            ", {}: {}{}".format(argument, "&" if how in (OBJECT, ASSET) else "", rust)
            for argument, _, rust, how, _ in inputs)
        args = "vec![{}]".format(", ".join(
            "{}::{}({}, {}{})".format(
                RUNTIME,
                "argument_actor" if (how == OBJECT and kind == "actor")
                else "argument_component" if how == OBJECT
                else "argument",
                rust_str(declared),
                "" if how in (OBJECT, ASSET) else "&",
                argument)
            for argument, declared, _, how, kind in inputs))
        literal = rust_str(function["name"])

        if not outputs:
            if returns is None:
                result, call, call_async = "()", "call", "call_async"
            elif returns[3] == OBJECT:
                result, call, call_async = returns[2], "call_for_object", "call_for_object_async"
            else:
                result, call, call_async = returns[2], "call_for", "call_for_async"
            return [
                "    pub fn {}(&self{}) -> Result<{}, {}> {{ self.0.{}({}, {}) }}".format(
                    name, signature, result, ERROR, call, literal, args),
                "    pub fn {}_async(&self{}) -> impl ::core::future::Future<Output = "
                "Result<{}, {}>> + '_ {{ self.0.{}({}, {}) }}".format(
                    name, signature, result, ERROR, call_async, literal, args),
            ]

        # Out parameters, which is how a Blueprint usually hands back more than one thing. They
        # come back as a tuple with the return value first when there is one, then the out
        # parameters in declaration order -- but each is read by the name it was declared with, so
        # adding a parameter cannot silently shift the others onto the wrong values.
        wanted = ([returns] if returns else []) + outputs
        tuple_type = "({}{})".format(", ".join(rust for _, _, rust, _, _ in wanted),
                                     "," if len(wanted) == 1 else "")
        reads = ", ".join(
            "{}::ObjectRef::{}::<{}>(&response, {})?".format(
                RUNTIME, "object_result_of" if how == OBJECT else "result_of", rust,
                rust_str(declared))
            for _, declared, rust, how, _ in wanted)
        trailing = "," if len(wanted) == 1 else ""
        return [
            "    pub fn {}(&self{}) -> Result<{}, {}> {{".format(
                name, signature, tuple_type, ERROR),
            "        let response = self.0.call_raw({}, {})?;".format(literal, args),
            "        Ok(({}{}))".format(reads, trailing),
            "    }",
            "    pub fn {}_async(&self{}) -> impl ::core::future::Future<Output = "
            "Result<{}, {}>> + '_ {{".format(name, signature, tuple_type, ERROR),
            "        let request = self.0.call_raw_async({}, {});".format(literal, args),
            "        async move {",
            "            let response = request.await?;",
            "            Ok(({}{}))".format(reads, trailing),
            "        }",
            "    }",
        ]

    def emit_class(self, cls):
        """One generated class: the handle, its identity, its superclass, and its own members."""
        name = ident(cls["name"])
        lines = [
            "#[repr(transparent)]",
            "#[derive(Clone, Debug, PartialEq, Eq)]",
            "pub struct {}(pub {}::ObjectRef);".format(name, RUNTIME),
            "",
            "// SAFETY: a transparent newtype over ObjectRef, which is what upcast() relies on.",
            "unsafe impl {}::Class for {} {{".format(RUNTIME, name),
            '    const PATH: &\'static str = {};'.format(rust_str(cls["path"])),
            "    fn from_object(object: {}::ObjectRef) -> Self {{ Self(object) }}".format(RUNTIME),
            "    fn as_object(&self) -> &{}::ObjectRef {{ &self.0 }}".format(RUNTIME),
            "    fn hierarchy() -> &'static (dyn {}::Hierarchy + 'static) "
            "{{ &{}::hierarchy::TABLE }}".format(RUNTIME, self.root),
            "}",
            "",
        ]

        base = self.classes_by_path.get(cls.get("super") or "")
        if base is not None:
            # Unreal's single inheritance, as a Deref chain. Inherited members resolve through it,
            # and a derived handle coerces where a base one is wanted. A root class gets none, so
            # ObjectRef's string-keyed methods stay behind an explicit `.0`.
            lines += [
                "impl ::core::ops::Deref for {} {{".format(name),
                "    type Target = {};".format(self.path_of(base)),
                "    #[inline]",
                "    fn deref(&self) -> &Self::Target {{ {}::upcast(&self.0) }}".format(RUNTIME),
                "}",
                "",
            ]

        # A Blueprint component variable is two things at once: a component template, and the
        # object property that holds it. Both would want the same accessor name; the component is
        # the useful one, so it is emitted first and the property stands down.
        used = set()
        body = []
        for component in cls.get("components", []):
            body += self.emit_component(component, used)
        for member in cls.get("properties", []):
            if member.get("kind") == "struct":
                body += self.emit_struct_property(member, used)
            else:
                body += self.emit_property(member, cls, used)
        for function in cls.get("functions", []):
            body += self.emit_function(function, used)

        if body:
            lines += ["impl {} {{".format(name)] + body + ["}", ""]
        self.stats["classes"] += 1
        return lines

    def emit_module(self, segments, classes, submodules):
        header = ["// Generated by gen_rust_classes.py from tempo_schema.json. Do not edit.", ""]
        header += ["pub mod {};".format(child) for child in sorted(submodules)]
        if submodules:
            header.append("")
        names_seen = {}
        body = []
        for cls in sorted(classes, key=lambda c: c["path"]):
            name = ident(cls["name"])
            if name in names_seen:
                # Two Blueprints in different folders can share a name. Rather than let one shadow
                # the other, which would silently point a scenario at the wrong class, say so.
                raise Exception(
                    "module {} has two classes named {}: {} and {}. Class names must be unique "
                    "within a module.".format("::".join(segments), name, names_seen[name],
                                              cls["path"]))
            names_seen[name] = cls["path"]
            if cls["path"] in self.shared_classes:
                body += ["pub use {}::classes::{}::{};".format(
                    self.published_root, "::".join(segments), name), ""]
                self.stats["classes_reexported"] += 1
                continue
            body += self.emit_class(cls)
        return "\n".join(header + body) + "\n"

    def emit_enums(self):
        lines = ["// Generated by gen_rust_classes.py from tempo_schema.json. Do not edit.", ""]
        for enum in sorted(self.schema.get("enums", []), key=lambda e: e["name"]):
            self.stats["enums"] += 1
            if enum["name"] in self.shared_enums:
                lines += ["pub use {}::enums::{};".format(self.published_root, ident(enum["name"])),
                          ""]
                continue
            entries = ["    {} = {},".format(variant, rust_str(authored))
                       for variant, authored in enumerator_members(enum.get("enumerators", []), ident)]
            lines.append("::tempo_sim::unreal_enum! {")
            lines.append("    {} {{".format(ident(enum["name"])))
            lines += ["    " + entry for entry in entries]
            lines.append("    }")
            lines.append("}")
            lines.append("")
        return "\n".join(lines) + "\n"

    def emit_structs(self):
        """Per struct, a proxy to reach into it by address and -- when it travels whole -- a value.

        The proxy, `<Name>Ref`, is what a struct property's accessor returns, so a nested address
        like PostProcessSettings.BloomIntensity checks member by member. The value, `<Name>`, is what
        a function takes and returns and what `load()`/`store()` move whole: every member an
        `Option`, because a write is a merge and an unset field leaves the destination as it was.
        """
        lines = ["// Generated by gen_rust_classes.py from tempo_schema.json. Do not edit.", ""]
        for struct in sorted(self.schema.get("structs", []), key=lambda s: s["name"]):
            name = ident(struct["name"])
            expressible = struct["name"] in self.expressible_structs
            if struct["name"] in self.shared_structs:
                # The proxy and, when there is one, the value: both are what members name.
                names = "{{{}Ref, {}}}".format(name, name) if expressible else name + "Ref"
                lines += ["pub use {}::structs::{};".format(self.published_root, names), ""]
                self.stats["structs"] += 1
                if expressible:
                    self.stats["struct_values"] += 1
                continue
            lines.append("pub struct {}Ref<'a>(pub {}::StructRef<'a>);".format(name, RUNTIME))
            # Reserved first, so a member called Load or Store cannot take the whole-value methods.
            used = {"load", "load_async", "store", "store_async"} if expressible else set()
            body = []
            if expressible:
                body += [
                    "    pub fn load(&self) -> Result<{}, {}> {{ self.0.load() }}".format(name, ERROR),
                    "    pub fn load_async(&self) -> impl ::core::future::Future<Output = Result<{}, "
                    "{}>> + '_ {{ self.0.load_async() }}".format(name, ERROR),
                    "    pub fn store(&self, value: {}) -> Result<(), {}> {{ self.0.store(value) }}"
                    .format(name, ERROR),
                    "    pub fn store_async(&self, value: {}) -> impl ::core::future::Future<Output = "
                    "Result<(), {}>> + '_ {{ self.0.store_async(value) }}".format(name, ERROR),
                ]
            for member in struct.get("members", []):
                body += self.emit_struct_member(member, used)
            if body:
                lines.append("")
                lines.append("impl<'a> {}Ref<'a> {{".format(name))
                lines += body
                lines.append("}")
            lines.append("")
            if expressible:
                lines += self.emit_struct_value(struct)
            self.stats["structs"] += 1
        return "\n".join(lines) + "\n"

    def emit_struct_value(self, struct):
        """The value type for one struct, and its conversions to and from a StructValue."""
        name = ident(struct["name"])
        literal = rust_str(struct["name"])
        fields, used = [], set()
        for member in struct.get("members", []):
            try:
                rust, how = self.rust_type(member)
            except Unsupported:
                self.stats["struct_value_fields_omitted"] += 1
                continue
            if how not in (PLAIN, OBJECT, ASSET):
                # A container of enums or of handles needs a decode this runtime does not offer
                # inside a struct. Leaving it out is safe: a write is a merge, so the member keeps
                # whatever the destination holds.
                self.stats["struct_value_fields_omitted"] += 1
                continue
            field = snake(member["name"])
            if field in used:
                self.stats["struct_value_fields_omitted"] += 1
                continue
            used.add(field)
            fields.append((field, member["name"], rust, how, member.get("kind")))

        lines = ["/// `{}` as a value. Unset fields are not sent, so the destination keeps them."
                 .format(struct["name"]),
                 "#[derive(Clone, Debug, Default, PartialEq)]",
                 "pub struct {} {{".format(name)]
        lines += ["    pub {}: Option<{}>,".format(field, rust) for field, _, rust, _, _ in fields]
        lines += ["}", ""]
        lines += [
            "impl {}::StructType for {} {{".format(RUNTIME, name),
            "    const NAME: &'static str = {};".format(literal),
            "}",
            "",
            "impl {}::FromValue for {} {{".format(RUNTIME, name),
            "    fn from_value(value: &::tempo_sim::proto::tempo_world::Value) -> Result<Self, {}> {{"
            .format(ERROR),
            # Read even when there is nothing to keep, so a different struct is still refused.
            "        let {} = {}::StructFields::of(value, {})?;".format(
                "fields" if fields else "_fields", RUNTIME, literal),
            "        Ok(Self {",
        ]
        lines += ["            {}: fields.{}({})?,".format(
            field, "object" if how == OBJECT else "get", rust_str(declared))
            for field, declared, _, how, _ in fields]
        lines += [
            "        })",
            "    }",
            "}",
            "",
            "impl {}::ToValue for {} {{".format(RUNTIME, name),
            "    fn to_value(&self) -> ::tempo_sim::proto::tempo_world::Value {",
            "        let {}builder = {}::StructBuilder::new({});".format(
                "mut " if fields else "", RUNTIME, literal),
        ]
        for field, declared, _, how, kind in fields:
            method = "field" if how != OBJECT else ("actor" if kind == "actor" else "component")
            lines.append("        builder.{}({}, &self.{});".format(method, rust_str(declared), field))
        lines += ["        builder.finish()", "    }", "}", ""]
        self.stats["struct_values"] += 1
        return lines

    def emit_struct_member(self, member, used):
        name = member["name"]
        accessor = snake(name)
        if accessor in used:
            return []
        literal = rust_str(name)

        if member.get("kind") == "struct":
            nested = member.get("struct") or ""
            if nested not in self.struct_names:
                self.stats["struct_members_omitted"] += 1
                return []
            used.add(accessor)
            self.stats["struct_members"] += 1
            # 'a, not '_: the nested proxy addresses the same object, not this proxy.
            return ["    pub fn {}(&self) -> {}Ref<'a> {{ {}Ref(self.0.nested({})) }}".format(
                accessor, ident(nested), ident(nested), literal)]

        try:
            rust, how = self.rust_type(member)
        except Unsupported:
            self.stats["struct_members_omitted"] += 1
            return []
        if how not in (PLAIN, ASSET):
            # A struct member is addressed through StructRef, which carries only the plain kinds.
            self.stats["struct_members_omitted"] += 1
            return []

        used.add(accessor)
        used.add("set_" + accessor)
        self.stats["struct_members"] += 1
        lines = ["    pub fn {}(&self) -> Result<{}, {}> {{ self.0.get({}) }}".format(
            accessor, rust, ERROR, literal)]
        if member.get("writable"):
            parameter, value = ("&" + rust, "value.clone()") if how == ASSET else (rust, "value")
            lines.append("    pub fn set_{}(&self, value: {}) -> Result<(), {}> "
                         "{{ self.0.set({}, {}) }}".format(accessor, parameter, ERROR, literal, value))
        return lines

    def emit_assets(self):
        """The asset catalog: one module per mount point, folders as modules, assets as statics.

        Each static is an `AssetRef` typed by the asset's class, so a moved or renamed asset is a
        path that no longer resolves and a material where a mesh is wanted does not compile. An
        asset is named by its package, which is unique within its folder where the object inside it
        need not be. Rust keeps modules and statics in separate namespaces, so a map beside a folder
        of the same name needs nothing; two entries that still land on one name are both left out
        and counted, rather than failing the crate.

        Returns {mount module: text}, empty when the schema has no catalog.
        """
        tree = {}
        for asset in self.schema.get("assets", []):
            segments = asset["path"].split(".", 1)[0].strip("/").split("/")
            if len(segments) < 2:
                self.stats["assets_unplaceable"] += 1
                continue
            node = tree.setdefault(segments[0], {"folders": {}, "assets": {}})
            for folder in segments[1:-1]:
                node = node["folders"].setdefault(folder, {"folders": {}, "assets": {}})
            node["assets"].setdefault(segments[-1], []).append(asset)

        def emit(node, indent):
            lines = []
            statics, modules = defaultdict(list), defaultdict(list)
            for name, assets in node["assets"].items():
                statics[ident(name)].append(assets)
            for folder in node["folders"]:
                modules[snake(folder)].append(folder)
            for name in sorted(modules):
                if len(modules[name]) > 1:
                    self.stats["assets_colliding"] += 1
                    continue
                lines.append("{}pub mod {} {{".format(indent, name))
                lines += emit(node["folders"][modules[name][0]], indent + "    ")
                lines.append("{}}}".format(indent))
            for name in sorted(statics):
                if len(statics[name]) > 1 or len(statics[name][0]) > 1:
                    self.stats["assets_colliding"] += 1
                    continue
                asset = statics[name][0][0]
                referenced = self.classes_by_path.get(asset["class"])
                asset_type = "{}::AssetRef<{}>".format(RUNTIME, self.path_of(referenced)) \
                    if referenced else RUNTIME + "::AssetRef"
                lines.append("{}pub static {}: {} = {}::AssetRef::from_static({});".format(
                    indent, name, asset_type, RUNTIME, rust_str(asset["path"])))
                self.stats["assets"] += 1
            return lines

        header = ["// Generated by gen_rust_classes.py from tempo_schema.json. Do not edit.", ""]
        return {snake(mount): "\n".join(header + emit(node, "")) + "\n"
                for mount, node in sorted(tree.items())}

    def emit_hierarchy(self):
        """The one table that has to be data rather than code: who derives from whom.

        Compiled in rather than read out of an installed package, which is where Python's
        index.json has to live. `is_a` therefore costs no I/O and works without a running sim.
        """
        # A re-exported class is the published table's, which this one names as a dependency so
        # that registering it registers that too: a chain that starts here ends there.
        pairs = sorted((cls["path"], cls.get("super") or "") for cls in self.schema["classes"]
                       if cls.get("super") and cls["path"] not in self.shared_classes)
        shared = {"classes": self.shared_classes, "structs": self.shared_structs,
                  "enums": self.shared_enums}
        header = self.schema["header"]
        lines = [
            "// Generated by gen_rust_classes.py from tempo_schema.json. Do not edit.",
            "",
            "/// Each class and the one it derives from, sorted by path so a lookup can bisect.",
            "static SUPERCLASS: &[(&str, &str)] = &[",
        ]
        lines += ["    ({}, {}),".format(rust_str(path), rust_str(parent))
                  for path, parent in pairs]
        lines += [
            "];",
            "",
            "/// What each entry hashed to in the build these classes came from, for",
            "/// `world::check_schema`. Sorted, as SUPERCLASS is.",
            "///",
            "/// Per entry and not just the header's one number, because a crate may hold part of a",
            "/// build by design -- the published classes describe the modules every build ships --",
            "/// and a whole-walk hash is then guaranteed to differ while every class in it is",
            "/// current.",
        ]
        for name, kind, key, value in (
                ("CLASS_HASH", "classes", "path", "content_hash"),
                ("STRUCT_HASH", "structs", "name", "content_hash"),
                ("ENUM_HASH", "enums", "name", "content_hash"),
                # An asset's identity is its path and class, so its class is what is compared.
                ("ASSET_HASH", "assets", "path", "class")):
            entries = [entry for entry in self.schema.get(kind, [])
                       if entry[key] not in shared.get(kind, ())]
            lines.append("static {}: &[(&str, &str)] = &[".format(name))
            lines += ["    ({}, {}),".format(rust_str(entry[key]),
                                             rust_str(entry.get(value, "")))
                      for entry in sorted(entries, key=lambda e: e[key])]
            lines += ["];", ""]
        lines += [
            "fn hash_of(table: &'static [(&'static str, &'static str)], name: &str)",
            "    -> Option<&'static str>",
            "{",
            "    table",
            "        .binary_search_by(|(key, _)| (*key).cmp(name))",
            "        .ok()",
            "        .map(|at| table[at].1)",
            "}",
            "",
            "pub struct Table;",
            "",
            "pub static TABLE: Table = Table;",
            "",
            "impl ::tempo_sim::unreal::Hierarchy for Table {",
            "    fn superclass(&self, class_path: &str) -> Option<&'static str> {",
            "        SUPERCLASS",
            "            .binary_search_by(|(path, _)| (*path).cmp(class_path))",
            "            .ok()",
            "            .map(|at| SUPERCLASS[at].1)",
            "    }",
            "",
            "    fn hashes(&self, kind: ::tempo_sim::unreal::EntryKind)",
            "        -> &'static [(&'static str, &'static str)]",
            "    {",
            "        match kind {",
            "            ::tempo_sim::unreal::EntryKind::Class => CLASS_HASH,",
            "            ::tempo_sim::unreal::EntryKind::Struct => STRUCT_HASH,",
            "            ::tempo_sim::unreal::EntryKind::Enum => ENUM_HASH,",
            "            ::tempo_sim::unreal::EntryKind::Asset => ASSET_HASH,",
            "        }",
            "    }",
            "",
            "    fn content_hash(&self, kind: ::tempo_sim::unreal::EntryKind, name: &str)",
            "        -> Option<&'static str>",
            "    {",
            "        hash_of(self.hashes(kind), name)",
            "    }",
            "",
            "    fn header(&self) -> ::tempo_sim::unreal::SchemaIdentity {",
            "        ::tempo_sim::unreal::SchemaIdentity {",
            "            schema_version: {}.to_string(),".format(
                rust_str(str(header.get("schema_version", "")))),
            "            engine_version: {}.to_string(),".format(
                rust_str(str(header.get("engine_version", "")))),
            "            content_hash: {}.to_string(),".format(
                rust_str(str(header.get("content_hash", "")))),
            "        }",
            "    }",
        ]
        if any(shared.values()):
            lines += [
                "",
                "    fn depends_on(&self) -> Option<&'static (dyn ::tempo_sim::unreal::Hierarchy + 'static)> {",
                "        Some(&{}::hierarchy::TABLE)".format(self.published_root),
                "    }",
            ]
        lines.append("}")
        return "\n".join(lines) + "\n"


def write_file(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as f:
        f.write(text)


def generate(schema_path, output_dir, docs=False, root="crate::reflected", published_path=None,
             published_root="::tempo_sim::reflected"):
    with open(schema_path) as f:
        schema = json.load(f)
    published = None
    if published_path:
        with open(published_path) as f:
            published = json.load(f)

    expected = "3"
    actual = schema.get("header", {}).get("schema_version")
    if actual != expected:
        raise Exception(
            "tempo_schema.json is schema_version {!r}, but this generator reads {!r}. Regenerate "
            "the schema, or use the generator that shipped with it.".format(actual, expected))

    generator = Generator(schema, docs, root=root, published=published,
                          published_root=published_root)
    source = output_dir

    write_file(os.path.join(source, "enums.rs"), generator.emit_enums())
    write_file(os.path.join(source, "structs.rs"), generator.emit_structs())
    write_file(os.path.join(source, "hierarchy.rs"), generator.emit_hierarchy())

    # The asset catalog, when the schema has one -- only a schema narrowed against a packaged build
    # does, since the catalog is of what was cooked.
    asset_modules = generator.emit_assets()
    for mount, text in asset_modules.items():
        write_file(os.path.join(source, "assets", mount + ".rs"), text)
    if asset_modules:
        write_file(os.path.join(source, "assets.rs"),
                   "// Generated by gen_rust_classes.py from tempo_schema.json. Do not edit.\n\n"
                   "//! The assets this build cooked, by mount point.\n\n"
                   + "".join("pub mod {};\n".format(mount) for mount in sorted(asset_modules)))
    assets_line = "pub mod assets;\n" if asset_modules else ""

    # A module can also be a prefix of another, because Unreal's mount points flatten onto the
    # same Rust names: /Script/Engine wants to be `engine`, and so does /Engine/EditorResources.
    # Rust is happy for a module to hold both items and submodules, so no disambiguation is needed
    # -- unlike Python, where such a module has to become a package carrying its classes in
    # __init__.
    modules = dict(generator.by_module)
    children = defaultdict(set)
    for segments in list(modules):
        for depth in range(1, len(segments)):
            parent, child = segments[:depth], segments[depth]
            children[parent].add(child)
            modules.setdefault(parent, [])

    for segments, classes in sorted(modules.items()):
        text = generator.emit_module(segments, classes, children[segments])
        write_file(os.path.join(source, "classes", *segments[:-1], segments[-1] + ".rs"), text)

    write_file(os.path.join(source, "classes.rs"),
               "// Generated by gen_rust_classes.py from tempo_schema.json. Do not edit.\n\n"
               + "".join("pub mod {};\n".format(segments[0])
                         for segments in sorted(modules) if len(segments) == 1))

    write_file(os.path.join(source, "mod.rs"), MOD_RS.replace("pub mod classes;\n",
                                                               assets_line + "pub mod classes;\n"))

    return generator.stats, modules


MOD_RS = """// Generated by gen_rust_classes.py from tempo_schema.json. Do not edit.

//! Typed Rust classes, generated from Unreal's reflection.
//!
//! A class here is a handle, not a copy: calling a method asks the running simulation. Unreal's
//! inheritance is a chain of `Deref`, so an inherited member resolves without anything extra and
//! a derived handle passes where a base one is wanted.
//!
//! Everything is in Unreal's own units -- centimetres, degrees, left-handed. See
//! `tempo_sim::unreal`.

// Unreal's names are kept as Unreal spells them, so the types do not read as Rust types would --
// an asset static included, which is named as the asset is.
#![allow(non_camel_case_types, non_snake_case, non_upper_case_globals)]
// A generated surface describes the whole build; a scenario uses a little of it.
#![allow(dead_code)]

pub mod classes;
pub mod enums;
pub mod hierarchy;
pub mod structs;
"""


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2
    options = {"--root": "crate::reflected", "--published": None,
               "--published-root": "::tempo_sim::reflected"}
    for argument in argv[3:]:
        name, _, value = argument.partition("=")
        if name in options:
            options[name] = value
    stats, modules = generate(argv[1], argv[2], "--docs" in argv, options["--root"],
                              options["--published"], options["--published-root"])
    print("modules            {}".format(len(modules)))
    print("classes            {} ({} re-exported from the published types)".format(
        stats["classes"] + stats["classes_reexported"], stats["classes_reexported"]))
    print("structs / enums    {} / {} ({} passable as values, {} value fields omitted)".format(
        stats["structs"], stats["enums"], stats["struct_values"],
        stats["struct_value_fields_omitted"]))
    print("properties         {} ({} writable, {} read-only, {} struct-valued)".format(
        stats["properties"], stats["writable"], stats["read_only"], stats["struct_properties"]))
    print("functions          {} ({} with out parameters, {} out params dropped)".format(
        stats["functions"], stats["functions_with_out_params"], stats["out_params_dropped"]))
    print("components         {} ({} omitted)".format(
        stats["components"], stats["components_omitted"]))
    print("struct members     {} ({} omitted)".format(
        stats["struct_members"], stats["struct_members_omitted"]))
    print("omitted            {} properties, {} functions".format(
        stats["properties_omitted"], stats["functions_omitted"]))
    print("assets             {} ({} left out colliding)".format(
        stats["assets"], stats["assets_colliding"]))
    print("unnameable setters {} (property demoted to read-only)".format(
        stats["setters_unresolvable"]))
    print("name taken already {} properties, {} functions, {} components".format(
        stats["properties_shadowed"], stats["functions_shadowed"], stats["components_shadowed"]))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
