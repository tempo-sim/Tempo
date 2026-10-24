# Copyright Tempo Simulation, LLC. All Rights Reserved
#
# The hand-written base of the typed World API: the root class every generated class derives from,
# and the value types that carry Unreal's structs.
#
# Everything in this module is in Unreal's own units -- centimeters, degrees, left-handed -- which is
# the one place in the Tempo API where that is true. Raw UPROPERTY values carry no semantic tagging,
# so converting them would be guesswork; the typed API therefore does not convert at all and says so
# here rather than per property.
#
# WARNING: `Vector` here is NOT `tempo_sim.TempoCore.Geometry_pb2.Vector`. This one is centimeters
# and left-handed, as Unreal stores it. That one is meters and right-handed, as every other Tempo
# RPC speaks. They are not interchangeable; convert explicitly.
#
# Note on `Object`: it declares no `__getattr__`. That is deliberate and load-bearing. A catch-all
# visible to a type checker makes *every* attribute legal on *every* generated subclass, which would
# silently defeat the drift detection the generated stubs exist to provide. When the dynamic,
# schema-free path is added it must land as a separate type, and any catch-all must be hidden from
# the type checker by a .pyi that omits it.

import difflib
import json
import os
import threading

from contextlib import contextmanager

from dataclasses import dataclass, field


@dataclass
class Vector:
    """A location or direction in centimeters, left-handed."""
    X: float = 0.0
    Y: float = 0.0
    Z: float = 0.0


@dataclass
class Vector2D:
    X: float = 0.0
    Y: float = 0.0


@dataclass
class IntVector:
    X: int = 0
    Y: int = 0
    Z: int = 0


@dataclass
class IntPoint:
    X: int = 0
    Y: int = 0


@dataclass
class Rotator:
    """An orientation in degrees, left-handed."""
    Roll: float = 0.0
    Pitch: float = 0.0
    Yaw: float = 0.0


@dataclass
class Quat:
    """A rotation, left-handed."""
    X: float = 0.0
    Y: float = 0.0
    Z: float = 0.0
    W: float = 1.0


@dataclass
class Transform:
    """A pose: centimeters and degrees, left-handed."""
    Location: Vector = field(default_factory=Vector)
    Rotation: Rotator = field(default_factory=Rotator)


@dataclass
class Color:
    """8-bit RGB."""
    R: int = 0
    G: int = 0
    B: int = 0


class AssetRef:
    """A reference to a cooked asset, by path.

    The generated catalog holds one per asset the build cooked -- `assets.Game.Vehicles.SM_Truck` --
    typed by the asset's class, and an asset-valued property is typed by the class it accepts. So a
    moved or renamed asset is an attribute that no longer exists, and a material where a mesh is
    wanted is a type error. Generic in the stub only; at runtime the class is carried for display.

    Constructing one from a path is the escape hatch, for a path that comes from data: it is
    untyped, and visibly so where it is written.
    """

    __slots__ = ("path", "class_path")

    def __init__(self, path, class_path=""):
        object.__setattr__(self, "path", path)
        object.__setattr__(self, "class_path", class_path)

    def __class_getitem__(cls, item):
        # AssetRef[StaticMesh] in an annotation that does get evaluated.
        return cls

    def __setattr__(self, name, value):
        raise AttributeError("an AssetRef cannot be changed; it names one asset")

    def __eq__(self, other):
        return isinstance(other, AssetRef) and other.path == self.path

    def __hash__(self):
        return hash(self.path)

    def __str__(self):
        return self.path

    def __repr__(self):
        return "AssetRef({!r})".format(self.path)


class Struct:
    """The base of every generated struct: a value, as opposed to a StructProxy's live handle.

    Built by a scenario to pass whole -- to a function, or to assign to a struct property -- and
    handed back by a read of one. Only the members it has been given travel, and a write sets only
    those, leaving the rest of the destination as it was; so a value built with one member is the
    same write as assigning that member through the proxy.

    Holds its members in a dict rather than in slots, because a struct read back from the server can
    carry members this package does not declare. The generated subclass names the Unreal struct it
    stands for and nothing else; what its members are comes from the package's metadata, as a
    class's do.
    """

    __tempo_struct__ = ""

    __slots__ = ("_fields",)

    def __init__(self, **fields):
        object.__setattr__(self, "_fields", {})
        for name, value in fields.items():
            # Through __setattr__, so a constructor argument is checked exactly as an assignment is.
            setattr(self, name, value)

    @classmethod
    def _from_fields(cls, fields):
        """A value as the server reported it: every member, read-only ones included."""
        value = cls.__new__(cls)
        object.__setattr__(value, "_fields", dict(fields))
        return value

    @classmethod
    def _members(cls):
        return _classes.struct_members(cls.__tempo_struct__)

    def __getattr__(self, name):
        if name.startswith("_"):
            raise AttributeError(name)
        fields = object.__getattribute__(self, "_fields")
        if name in fields:
            return fields[name]
        members = self._members()
        if name in members:
            raise AttributeError("{}.{} was not set on this value".format(type(self).__name__, name))
        raise AttributeError("{} has no member {!r}.{}".format(
            type(self).__name__, name, _suggest(name, list(members))))

    def __setattr__(self, name, value):
        if name.startswith("_"):
            object.__setattr__(self, name, value)
            return
        members = self._members()
        member = members.get(name)
        if member is None:
            raise AttributeError("{} has no member {!r}.{}".format(
                type(self).__name__, name, _suggest(name, list(members))))
        if member.get("variant") is None:
            raise AttributeError("{}.{} cannot be carried as one value".format(type(self).__name__, name))
        if not member.get("writable"):
            raise AttributeError("{}.{} is read-only".format(type(self).__name__, name))
        self._fields[name] = value

    def __eq__(self, other):
        return (isinstance(other, Struct) and type(self).__tempo_struct__ == type(other).__tempo_struct__
                and self._fields == other._fields)

    def __repr__(self):
        return "{}({})".format(type(self).__name__, ", ".join(
            "{}={!r}".format(name, value) for name, value in sorted(self._fields.items())))


class Object:
    """The root of the generated class hierarchy.

    An instance is a handle to something in the running simulation, not a copy of it: reading an
    attribute asks the server, and assigning to one tells it. The generated subclasses add no
    behavior -- they carry names and types only -- so all of it lives here.
    """

    # The Unreal class path this type stands for. Generated subclasses override it; a path rather
    # than a name because two Blueprints in different folders can share a name.
    __tempo_class__ = "/Script/CoreUObject.Object"

    __slots__ = ("_actor", "_component", "_class_path")

    def __init__(self, actor: str = "", component: str = "", class_path: str = ""):
        # Addressed the way every TempoWorld RPC addresses things: an actor name, and a component
        # name when the handle is to a component rather than to the actor itself.
        object.__setattr__(self, "_actor", actor)
        object.__setattr__(self, "_component", component)
        # The class the thing really has, where whoever built the handle knew it. A query for a base
        # class hands back handles typed as that base -- which is what lets the call site
        # type-check -- so the handle's own type is not the answer to what it points at.
        object.__setattr__(self, "_class_path", class_path)

    @property
    def actor(self) -> str:
        """The name of the actor this handle addresses."""
        return self._actor

    @property
    def component(self) -> str:
        """The name of the component this handle addresses, or "" for the actor itself."""
        return self._component

    def __repr__(self) -> str:
        where = "{}:{}".format(self._actor, self._component) if self._component else self._actor
        return "{}({!r})".format(type(self).__name__, where)

    def is_a(self, other) -> bool:
        """Whether the thing this handle addresses is `other`, or derives from it.

        A question about the simulation, not about the handle. `get_actors(Pawn)` hands back handles
        typed `Pawn`, because that is what makes the call site check, but each one points at an
        actor of whatever class it really is -- so answering from the handle's own type would do no
        more than repeat the question that was asked.

        Not `isinstance`: the generated classes are deliberately flat at runtime, because Unreal's
        inheritance has cycles across packages and a base has to exist when a class statement runs.
        The real hierarchy lives in the schema, so this is the question to ask.
        """
        ancestor = getattr(other, "__tempo_class__", other)
        # Recorded where the handle was made, which is where a call to find it out was expected
        # anyway. A predicate that reached for the network would be a surprise here, and would make
        # this unanswerable without a running sim.
        return _classes.is_subclass(self._class_path or type(self).__tempo_class__, ancestor)

    @contextmanager
    def snapshot(self, *names):
        """Read several properties in one call, then serve attribute reads from that.

        Attribute access costs a round trip each, which is the price of looking like a field. A
        snapshot pays it once:

            with truck.snapshot():
                print(truck.MaxSpeed, truck.Mass, truck.Drag)

        With no arguments it fetches every readable property of the class, which is one call however
        many there are -- a win past the second read, and worth knowing about on a class with
        hundreds. Naming the properties fetches only those.

        It is a point-in-time copy: the simulation moves on, and the values do not. A write inside
        the block goes straight through and drops that entry, so a read after it sees the new value
        rather than the stale one.
        """
        wanted = list(names) if names else self._readable_names()
        values = {}
        if wanted:
            response = _world().get_properties(
                actor=self._actor, component=self._component, properties=wanted, native_units=True)
            for entry in response.values:
                # An unreadable property does not fail the call; it simply is not in the snapshot,
                # and a read of it falls through to the server to report why.
                if entry.code == 0:
                    values[entry.name] = _from_value(entry.value)

        snapshots = getattr(_state, "snapshots", None)
        if snapshots is None:
            snapshots = _state.snapshots = {}
        key = (self._actor, self._component)
        previous = snapshots.get(key)
        snapshots[key] = values
        try:
            yield self
        finally:
            if previous is None:
                snapshots.pop(key, None)
            else:
                snapshots[key] = previous

    def _readable_names(self):
        """Every property of this class that can be read as one value."""
        class_path = type(self).__tempo_class__
        names, seen = [], set()
        while class_path and class_path not in seen:
            seen.add(class_path)
            entry = _classes.metadata(class_path)
            if entry is None:
                break
            for name, member in entry["properties"].items():
                # A struct property reads as a proxy, which is addressed through its members, so
                # there is nothing to prefetch.
                if member.get("variant") is not None and not _is_proxied(member):
                    names.append(name)
            class_path = entry.get("super") or ""
        return names

    def watch(self, name):
        """Yield this property's value once per simulation tick, until the caller stops asking.

            for speed in truck.watch("MaxSpeed"):
                if speed > limit:
                    break

        Dynamically typed, unavoidably: the property is named by a string here, so a type checker
        has nothing to work from. Annotate the loop variable if it matters. The name is still checked
        against the class before the subscription opens, so a typo fails immediately rather than
        after the first tick.
        """
        member = self._property(name)
        if member.get("variant") is None:
            raise AttributeError(
                "{}.{} cannot be watched: it is not readable as one value".format(
                    type(self).__name__, name))
        for message in _world().stream_property(actor=self._actor, component=self._component,
                                                property=name, native_units=True):
            yield _from_value(message)

    def _property(self, name):
        member = _classes.find_property(type(self).__tempo_class__, name)
        if member is None:
            raise AttributeError("{} has no property {!r}.{}".format(
                type(self).__name__, name,
                _suggest(name, _classes.property_names(type(self).__tempo_class__))))
        return member

    # Attribute access is the API. A generated class declares its members in a .pyi and carries no
    # descriptors, so every typed read and write arrives here.
    #
    # These two are hidden from type checkers by unreal.pyi, which omits them deliberately: a
    # catch-all the checker can see makes every attribute legal on every generated subclass, and the
    # whole point of the generated stubs is that an attribute which no longer exists is an error.
    def __getattr__(self, name):
        # Only reached when normal lookup fails, so this never shadows a real attribute.
        if name.startswith("_"):
            raise AttributeError(name)
        member = _classes.find_property(type(self).__tempo_class__, name)
        if member is not None:
            if _is_proxied(member):
                return StructProxy(self, name, member["struct"])
            return _read(self._actor, self._component, name)

        signature = _classes.find_function(type(self).__tempo_class__, name)
        if signature is not None:
            return BoundFunction(self, name, signature)

        component_class = _classes.find_component(type(self).__tempo_class__, name)
        if component_class is not None:
            # Addressed on this handle's actor, under the component's own name -- which is how
            # every TempoWorld RPC names a component.
            python_class = _classes.python_class(component_class) or Object
            return python_class(self._actor, name, class_path=component_class)

        raise AttributeError("{} has no property or function {!r}.{}".format(
            type(self).__name__, name,
            _suggest(name, _classes.member_names(type(self).__tempo_class__))))

    def __setattr__(self, name, value):
        if name.startswith("_"):
            object.__setattr__(self, name, value)
            return
        if _classes.find_function(type(self).__tempo_class__, name) is not None:
            raise AttributeError("{}.{} is a function, not a property".format(
                type(self).__name__, name))
        if _classes.find_component(type(self).__tempo_class__, name) is not None:
            raise AttributeError(
                "{}.{} is a component, not a property. Assign to its properties, or add and "
                "destroy components through the TempoWorld API.".format(type(self).__name__, name))
        member = self._property(name)
        if member.get("variant") is None:
            raise AttributeError(
                "{}.{} cannot be written as one value; assign to its members instead".format(
                    type(self).__name__, name))
        if not member.get("writable"):
            raise AttributeError("{}.{} is read-only".format(type(self).__name__, name))
        _write(self._actor, self._component, name, member, value)


class _Package:
    """One installed generated class package, loaded on demand.

    Three things are needed to serve an attribute: which module a class path lives in (the index),
    what members a class declares (one `<Module>.meta.json` per module), and what a struct's members
    are (`structs.meta.json`). Only the index is global -- resolving a superclass needs it before any
    module is known -- so that is loaded once and the rest per module, the first time a class from
    it is touched.
    """

    def __init__(self, package, directory):
        self.package = package
        self.directory = directory
        self._index = None
        self._modules = {}
        self._structs = None
        self._header = None
        self._hashes = None

    def _load(self, *parts):
        with open(os.path.join(self.directory, *parts)) as f:
            return json.load(f)

    @property
    def index(self):
        if self._index is None:
            self._index = self._load("index.json")
        return self._index

    @property
    def header(self):
        if self._header is None:
            self._header = self._load("schema_header.json")
        return self._header

    @property
    def structs(self):
        if self._structs is None:
            self._structs = self._load("structs.meta.json")
        return self._structs

    @property
    def hashes(self):
        """What each class, struct and enum hashed to in the build this package came from.

        Only `world.check_schema` reads this, so a package that is never checked never loads it.
        Absent in a package generated before the hashes existed, which reads as "nothing to
        compare" rather than as a mismatch.
        """
        if self._hashes is None:
            try:
                self._hashes = self._load("hashes.json")
            except IOError:
                self._hashes = {}
        return self._hashes

    def module_of(self, class_path):
        entry = self.index.get(class_path)
        return entry.split(":", 1)[0] if entry else None

    def metadata(self, class_path):
        """The declared members of one class, or None when this package does not have it."""
        module = self.module_of(class_path)
        if module is None:
            return None
        if module not in self._modules:
            # A module that is also a package carries its files in __init__.
            stem = os.path.join(*module.split("."))
            candidates = (stem + ".meta.json", os.path.join(stem, "__init__.meta.json"))
            for candidate in candidates:
                path = os.path.join(self.directory, candidate)
                if os.path.exists(path):
                    with open(path) as f:
                        self._modules[module] = json.load(f)
                    break
            else:
                self._modules[module] = {}
        return self._modules[module].get(class_path)

    def python_class(self, class_path):
        entry = self.index.get(class_path)
        if entry is None:
            return None
        module_name, class_name = entry.split(":", 1)
        import importlib
        module = importlib.import_module("{}.{}".format(self.package, module_name))
        return getattr(module, class_name, None)


class _Classes:
    """Every installed generated class package, searched in registration order.

    More than one can be installed, and they do not describe the same thing. A project's own
    package describes its whole build; the published one inside `tempo_sim` describes the modules
    every build ships (TYPED_WORLD_API_PLAN.md section 16). A registry that held only the last
    registration would make whichever lost unresolvable -- no metadata, no `is_a`, no dynamic path --
    so this holds all of them.

    There is no overlap to arbitrate: a project's package re-exports the published classes it
    matches rather than redefining them, and leaves them out of its own index.
    """

    def __init__(self):
        self._packages = []
        self._struct_classes = {}

    def register(self, package, directory):
        # Re-importing the same package is not a second one. It happens whenever a test reloads a
        # package from a different path, and two entries would then disagree about where to read.
        self._packages = [p for p in self._packages if p.package != package]
        self._packages.append(_Package(package, directory))
        self._struct_classes = {}

    @property
    def registered(self):
        return bool(self._packages)

    @property
    def packages(self):
        return list(self._packages)

    def _require(self):
        if not self.registered:
            raise RuntimeError(
                "No generated classes are registered. Install this build's project package and "
                "import its classes (e.g. `import tempo_sample.UE`); the wheel ships in "
                "Packaged/API/Python alongside the tempo-sim wheel.")

    @property
    def index(self):
        """Every known class path, across packages. The first registration of a path wins."""
        self._require()
        merged = {}
        for package in reversed(self._packages):
            merged.update(package.index)
        return merged

    @property
    def headers(self):
        """One header per installed package, each describing what that package was generated from."""
        self._require()
        return [(package.package, package.header) for package in self._packages]

    @property
    def header(self):
        """The first package's header.

        Kept for the single-package case, which is every project that has not installed the
        published classes beside its own. `headers` is what a check across all of them wants.
        """
        self._require()
        return self._packages[0].header

    @property
    def structs(self):
        self._require()
        merged = {}
        for package in reversed(self._packages):
            merged.update(package.structs)
        return merged

    def struct_members(self, name):
        """One struct's members, from whichever installed package declares it."""
        for package in self._packages:
            found = package.structs.get(name)
            if found is not None:
                return found
        return {}

    def struct_class(self, name):
        """The generated value type for a struct, or a bare one when no installed package has it.

        A struct the sim reports and no package declares still comes back as a value -- its members
        readable, nothing about it checked -- rather than failing a read that succeeded.
        """
        cached = self._struct_classes.get(name)
        if cached is not None:
            return cached
        import importlib
        found = None
        for package in self._packages:
            if name not in package.structs:
                continue
            module = importlib.import_module("{}.structs".format(package.package))
            found = getattr(module, _identifier(name), None)
            if found is not None:
                break
        if found is None:
            found = type(_identifier(name), (Struct,), {"__slots__": (), "__tempo_struct__": name})
        self._struct_classes[name] = found
        return found

    def _owner(self, class_path):
        """The package that has this class, or None."""
        for package in self._packages:
            if package.module_of(class_path) is not None:
                return package
        return None

    def module_of(self, class_path):
        self._require()
        owner = self._owner(class_path)
        return owner.module_of(class_path) if owner else None

    def metadata(self, class_path):
        """The declared members of one class, or None when no installed package has it."""
        self._require()
        owner = self._owner(class_path)
        return owner.metadata(class_path) if owner else None

    def find_property(self, class_path, name):
        """One property, searched up the inheritance chain.

        Each class records only what it declares -- repeating AActor's members into every subclass
        would multiply the schema -- so inherited members are found by walking `super`.
        """
        seen = set()
        while class_path and class_path not in seen:
            seen.add(class_path)
            entry = self.metadata(class_path)
            if entry is None:
                return None
            found = entry["properties"].get(name)
            if found is not None:
                return found
            class_path = entry.get("super") or ""
        return None

    def find_component(self, class_path, name):
        """One component, searched up the inheritance chain, as the members are."""
        seen = set()
        while class_path and class_path not in seen:
            seen.add(class_path)
            entry = self.metadata(class_path)
            if entry is None:
                return None
            found = entry.get("components", {}).get(name)
            if found is not None:
                return found
            class_path = entry.get("super") or ""
        return None

    def find_function(self, class_path, name):
        """One function, searched up the inheritance chain, as find_property does."""
        seen = set()
        while class_path and class_path not in seen:
            seen.add(class_path)
            entry = self.metadata(class_path)
            if entry is None:
                return None
            found = entry.get("functions", {}).get(name)
            if found is not None:
                return found
            class_path = entry.get("super") or ""
        return None

    def member_names(self, class_path):
        """Every property and function reachable on a class, for suggesting a near miss."""
        names, seen = [], set()
        while class_path and class_path not in seen:
            seen.add(class_path)
            entry = self.metadata(class_path)
            if entry is None:
                break
            names.extend(entry["properties"])
            names.extend(entry.get("functions", {}))
            names.extend(entry.get("components", {}))
            class_path = entry.get("super") or ""
        return names

    def property_names(self, class_path):
        """Every property reachable on a class, for suggesting a near miss."""
        names, seen = [], set()
        while class_path and class_path not in seen:
            seen.add(class_path)
            entry = self.metadata(class_path)
            if entry is None:
                break
            names.extend(entry["properties"])
            class_path = entry.get("super") or ""
        return names

    def is_subclass(self, class_path, ancestor_path):
        seen = set()
        while class_path and class_path not in seen:
            if class_path == ancestor_path:
                return True
            seen.add(class_path)
            entry = self.metadata(class_path)
            if entry is None:
                return False
            class_path = entry.get("super") or ""
        return False

    def python_class(self, class_path):
        """The generated class for an Unreal class path, importing its module if needed."""
        self._require()
        owner = self._owner(class_path)
        return owner.python_class(class_path) if owner else None


_classes = _Classes()


def register_classes(package, directory):
    """Called by a generated class package when it is imported."""
    _classes.register(package, directory)


def registered_classes():
    """The class registry, for the entry points in tempo_sim.world."""
    return _classes


def _identifier(name):
    """A struct's name as the generator spells its class. Mirrors gen_py_classes.identifier."""
    import keyword
    import re
    cleaned = re.sub(r"\W", "_", name)
    if not cleaned or cleaned[0].isdigit():
        cleaned = "_" + cleaned
    return cleaned + "_" if keyword.iskeyword(cleaned) else cleaned


def _is_proxied(member):
    """Whether a member reads as a StructProxy: a struct, whether or not it is also one value.

    A struct-valued property keeps reading as a live handle even when it could travel whole, so that
    `truck.Settings.Scale = 2.0` goes on writing to the sim rather than to a copy. An array of
    structs is not proxied; it reads as a list of values.
    """
    return bool(member.get("struct")) and member.get("variant") in (None, "struct_value")


def _suggest(name, candidates):
    """`did you mean` for a near miss, which is the common case for a renamed property."""
    close = difflib.get_close_matches(name, candidates, n=1, cutoff=0.7)
    return " Did you mean {!r}?".format(close[0]) if close else ""


# -- the wire ------------------------------------------------------------------------------------
#
# Imported inside the functions, not at module scope: the generated wrappers import this module's
# siblings, and importing them back here at load time would be circular.

def _world():
    from tempo_sim import tempo_world
    return tempo_world


def _value_type():
    import tempo_sim.TempoWorld.WorldControl_pb2 as WorldControl
    return WorldControl.Value


# Variants whose payload is a plain scalar the proto field takes directly.
_SCALAR_VARIANTS = frozenset((
    "bool_value", "int_value", "int64_value", "float_value", "string_value", "enum_value",
    "class_value", "asset_value", "actor_value", "component_value",
))

# Variants whose payload is a message with these fields, in this order.
_COMPONENT_VARIANTS = {
    "vector_value": ("x", "y", "z"),
    "vector2d_value": ("x", "y"),
    "int_vector_value": ("x", "y", "z"),
    "int_point_value": ("x", "y"),
    "quat_value": ("x", "y", "z", "w"),
    "color_value": ("r", "g", "b"),
}

# How a component-shaped variant maps onto one of this module's value types.
_COMPONENT_TYPES = {
    "vector_value": (Vector, ("X", "Y", "Z")),
    "vector2d_value": (Vector2D, ("X", "Y")),
    "int_vector_value": (IntVector, ("X", "Y", "Z")),
    "int_point_value": (IntPoint, ("X", "Y")),
    "quat_value": (Quat, ("X", "Y", "Z", "W")),
    "color_value": (Color, ("R", "G", "B")),
}


def _name_of(value):
    """How a reference-valued property names its target."""
    if isinstance(value, AssetRef):
        return value.path
    if isinstance(value, Object):
        return "{}:{}".format(value.actor, value.component) if value.component else value.actor
    if isinstance(value, type) and hasattr(value, "__tempo_class__"):
        # A generated class stands for the Unreal class it was generated from.
        return value.__tempo_class__
    return str(value)


def value_of(value):
    """A struct property's current value, as a copy.

    Reading a struct property hands back a proxy, so that assigning to its members writes to the
    sim. This is the read of the whole thing, in one call, for when a copy is what is wanted.
    Anything that is not a proxy is returned as it is.
    """
    if isinstance(value, StructProxy):
        return _read(value._owner.actor, value._owner.component, value._address)
    return value


def _fill_struct(target, value):
    """Write a struct value into a StructValue message, one field per member it has."""
    value = value_of(value)
    if not isinstance(value, Struct):
        raise TypeError("expected a struct value, got {!r}".format(value))
    name = type(value).__tempo_struct__
    target.struct_name = name
    members = _classes.struct_members(name)
    for member_name, member_value in value._fields.items():
        member = members.get(member_name) or {}
        variant = member.get("variant")
        if variant is None:
            # Read back from a sim that knows a member this package does not. A write is a merge,
            # so leaving it out leaves the destination's own value in place.
            continue
        field = target.fields.add()
        field.name = member_name
        field.value.CopyFrom(_to_value(variant, member_value))


def _read_struct(source):
    return _classes.struct_class(source.struct_name)._from_fields(
        {field.name: _from_value(field.value) for field in source.fields})


def _to_value(variant, value):
    """Build a Value carrying `value` in `variant`."""
    message = _value_type()()

    if variant == "struct_value":
        _fill_struct(message.struct_value, value)
        return message

    if variant == "struct_array_value":
        for element in value:
            _fill_struct(message.struct_array_value.values.add(), element)
        return message

    if variant in _SCALAR_VARIANTS:
        if variant in ("class_value", "asset_value", "actor_value", "component_value"):
            setattr(message, variant, _name_of(value))
        elif variant == "enum_value":
            # A generated enum is str-valued, so its own value is the authored name.
            setattr(message, variant, str(getattr(value, "value", value)))
        else:
            setattr(message, variant, value)
        return message

    if variant in _COMPONENT_VARIANTS:
        target = getattr(message, variant)
        python_type, attributes = _COMPONENT_TYPES[variant]
        if not isinstance(value, python_type):
            raise TypeError("expected {}, got {!r}".format(python_type.__name__, value))
        for field_name, attribute in zip(_COMPONENT_VARIANTS[variant], attributes):
            setattr(target, field_name, getattr(value, attribute))
        return message

    if variant == "rotator_value":
        if not isinstance(value, Rotator):
            raise TypeError("expected Rotator, got {!r}".format(value))
        target = message.rotator_value
        target.r, target.p, target.y = value.Roll, value.Pitch, value.Yaw
        return message

    if variant == "transform_value":
        if not isinstance(value, Transform):
            raise TypeError("expected Transform, got {!r}".format(value))
        target = message.transform_value
        target.location.x, target.location.y, target.location.z = (
            value.Location.X, value.Location.Y, value.Location.Z)
        target.rotation.r, target.rotation.p, target.rotation.y = (
            value.Rotation.Roll, value.Rotation.Pitch, value.Rotation.Yaw)
        return message

    if variant.endswith("_array_value") or variant.endswith("_set_value"):
        target = getattr(message, variant)
        reference = variant.startswith(("class_", "asset_", "actor_", "component_"))
        for element in value:
            if reference:
                target.values.append(_name_of(element))
            elif variant.startswith("enum_"):
                target.values.append(str(getattr(element, "value", element)))
            else:
                target.values.append(element)
        return message

    raise TypeError("no conversion for Value variant {!r}".format(variant))


def _from_value(message):
    """Read whichever variant a Value carries back into a Python value."""
    variant = message.WhichOneof("value")
    if variant is None:
        return None

    if variant == "asset_value":
        # An empty path is a property that points at nothing.
        return AssetRef(message.asset_value) if message.asset_value else None

    if variant in _SCALAR_VARIANTS:
        return getattr(message, variant)

    if variant == "struct_value":
        return _read_struct(message.struct_value)

    if variant == "struct_array_value":
        return [_read_struct(element) for element in message.struct_array_value.values]

    if variant in _COMPONENT_TYPES:
        source = getattr(message, variant)
        python_type, attributes = _COMPONENT_TYPES[variant]
        return python_type(*(getattr(source, field_name)
                             for field_name in _COMPONENT_VARIANTS[variant]))

    if variant == "rotator_value":
        source = message.rotator_value
        return Rotator(source.r, source.p, source.y)

    if variant == "transform_value":
        source = message.transform_value
        return Transform(Vector(source.location.x, source.location.y, source.location.z),
                         Rotator(source.rotation.r, source.rotation.p, source.rotation.y))

    if variant in ("asset_array_value", "asset_set_value"):
        assets = [AssetRef(path) for path in getattr(message, variant).values]
        return set(assets) if variant == "asset_set_value" else assets
    if variant.endswith("_set_value"):
        return set(getattr(message, variant).values)
    if variant.endswith("_array_value"):
        return list(getattr(message, variant).values)

    raise TypeError("no conversion from Value variant {!r}".format(variant))


# The open batch, if any. Thread-local because the client is not thread-safe anyway -- the gRPC
# channel is bound to the loop that created it -- so a batch opened on one thread has no business
# capturing writes made on another.
_state = threading.local()

# What a read or call made inside `Batch.get`/`Batch.call` returns in place of its value, which does
# not exist yet. Nothing outside this module sees it: the Deferred is handed back instead.
_PENDING = object()

# grpc.StatusCode.ABORTED: what RunBatch reports for an op it did not run because an earlier one
# failed. Spelled as a number so this module does not import grpc to compare one.
_ABORTED = 10


def _open_batch():
    return getattr(_state, "batch", None)


def _worldcontrol():
    import tempo_sim.TempoWorld.WorldControl_pb2 as WorldControl
    return WorldControl


class BatchError(RuntimeError):
    """An operation in a batch failed. Says which, and what it was."""

    def __init__(self, index, count, description, code, error):
        super().__init__("batched op {} of {} ({}) failed: {}".format(index, count, description, error))
        self.index = index
        self.code = code
        self.error = error


class Deferred:
    """The result of a read or call queued in a batch, available once the batch has run.

    Typed by what was deferred -- `b.get(lambda: truck.MaxSpeed)` is a `Deferred[float]` -- so a
    renamed property is still a type error inside the lambda.
    """

    __slots__ = ("_done", "_value", "_error")

    def __init__(self):
        self._done = False
        self._value = None
        self._error = None

    @property
    def done(self):
        """Whether the batch carrying this has run."""
        return self._done

    @property
    def value(self):
        """The result. Raises the op's error if it failed, and RuntimeError if it has not run yet."""
        if not self._done:
            raise RuntimeError("the batch has not run yet; read .value after the `with` block")
        if self._error is not None:
            raise self._error
        return self._value

    def _resolve(self, value):
        self._done, self._value = True, value

    def _fail(self, error):
        self._done, self._error = True, error

    def __repr__(self):
        if not self._done:
            return "Deferred(pending)"
        return "Deferred(error={!r})".format(self._error) if self._error else "Deferred({!r})".format(self._value)


class Batch:
    """Sends the writes, reads and calls made inside it as one RunBatch, in order.

    Attribute access is one round trip per attribute, which is the cost of making it look like
    assignment. Setting twenty properties on an actor should not be twenty calls, so a batch
    collects them and flushes once -- and because the server runs a whole batch between two ticks,
    nothing simulates against the half-configured actor.

    A write through a BlueprintSetter is a call, and travels in the same batch. A read or call
    comes back one of two ways:

    - **Deferred**, through `get` or `call`: queued with everything else, its result available
      from the returned Deferred once the batch has run. One round trip for the whole block.
    - **Immediately**, by simply reading or calling: what is queued so far goes out with it, and
      the real result comes back, so the read sees every write before it. Correct, and ordered, but
      it is a round trip of its own and the batch has then partly run.

    The server stops at the first failure and reports what it did not run, and the flush raises a
    BatchError naming the op that failed. Nothing is sent if the block raises before a flush.
    """

    def __init__(self):
        self._ops = []
        self._depth = 0

    def _add(self, op, description, on_result=None):
        self._ops.append((op, description, on_result))

    def add_write(self, actor, component, address, variant, value):
        WorldControl = _worldcontrol()
        self._add(WorldControl.BatchOp(set=WorldControl.SetPropertyRequest(
            actor=actor, component=component, property=address,
            value=_to_value(variant, value), native_units=True)),
            "write {}".format(_where(actor, component, address)))

    def add_call(self, actor, component, function, argument, variant, value):
        WorldControl = _worldcontrol()
        self._add(WorldControl.BatchOp(call=WorldControl.CallFunctionRequest(
            actor=actor, component=component, function=function, native_units=True,
            args=[WorldControl.FunctionArg(name=argument, value=_to_value(variant, value))])),
            "call {}".format(_where(actor, component, function)))

    def defer(self, op, description, decode):
        """Queue an op whose result something will want, and hand back where it will arrive."""
        deferred = Deferred()

        def on_result(result, error):
            if error is not None:
                deferred._fail(error)
            else:
                deferred._resolve(decode(result))

        self._add(op, description, on_result)
        return deferred

    def run_now(self, op, description, decode):
        """Queue an op, send everything queued, and return the op's own result."""
        deferred = self.defer(op, description, decode)
        self.flush()
        return deferred.value

    def get(self, read):
        """Defer a read: `b.get(lambda: truck.MaxSpeed)`. The lambda's result type is the Deferred's."""
        return self._capture(read)

    def call(self, invoke):
        """Defer a call: `b.call(lambda: truck.TeleportTo(dest, rot, False, False))`."""
        return self._capture(invoke)

    def _capture(self, body):
        previous = getattr(_state, "capture", None)
        captured = _state.capture = []
        try:
            returned = body()
        finally:
            _state.capture = previous
        if not captured:
            # Answered without asking the server -- a snapshot served it. Nothing to wait for.
            deferred = Deferred()
            deferred._resolve(returned)
            return deferred
        if len(captured) != 1 or returned is not _PENDING:
            raise TypeError("a deferred lambda must make exactly one read or call and return its "
                            "result unchanged; this one made {}".format(len(captured)))
        return captured[0]

    def __enter__(self):
        self._depth += 1
        _state.batch = self
        return self

    def __exit__(self, exc_type, exc, traceback):
        self._depth -= 1
        if self._depth > 0:
            # A nested batch joins the outer one rather than flushing early, so the whole block is
            # still one flush.
            return False
        _state.batch = None
        if exc_type is None:
            self.flush()
        else:
            self._ops = []
        return False

    def flush(self):
        queued, self._ops = self._ops, []
        if not queued:
            return
        response = _world().run_batch(ops=[op for op, _, _ in queued], stop_on_failure=True)
        first_failure = None
        for index, ((_, description, on_result), result) in enumerate(zip(queued, response.results)):
            error = None
            if result.code != 0:
                error = BatchError(index, len(queued), description, result.code, result.error)
                # The op that failed, not the ones the server then declined to run.
                if first_failure is None and result.code != _ABORTED:
                    first_failure = error
            if on_result is not None:
                on_result(result, error)
        if first_failure is not None:
            raise first_failure


def _where(actor, component, address):
    return "{}:{}.{}".format(actor, component, address) if component else "{}.{}".format(actor, address)


def _read(actor, component, address):
    # A snapshot serves reads for the handle that opened it, so a block reading several properties
    # costs one call rather than one per property.
    cache = getattr(_state, "snapshots", {}).get((actor, component))
    if cache is not None and address in cache:
        return cache[address]

    batch = _open_batch()
    if batch is not None:
        # Through the batch, so the read lands after the writes queued before it rather than ahead
        # of them -- a read that skipped the queue would report the value from before the batch.
        WorldControl = _worldcontrol()
        op = WorldControl.BatchOp(get=WorldControl.GetPropertyRequest(
            actor=actor, component=component, property=address, native_units=True))
        description = "read {}".format(_where(actor, component, address))
        capture = getattr(_state, "capture", None)
        if capture is not None:
            capture.append(batch.defer(op, description, lambda result: _from_value(result.value)))
            return _PENDING
        return batch.run_now(op, description, lambda result: _from_value(result.value))

    return _from_value(_world().get_property(
        actor=actor, component=component, property=address, native_units=True))


def _write(actor, component, address, member, value):
    """Write one property, through its BlueprintSetter when it has one.

    Writing such a property directly would write its memory and skip the setter, which may be the
    only thing that clamps the value or marks render state dirty -- so the accessor is called
    instead. The schema records it; a cooked build could not have told us it exists.
    """
    setter = member.get("setter")
    batch = _open_batch()

    if setter:
        # Named, not positional: CallFunction rejects an unnamed arg outright, and matches the name
        # against the function's own parameters. The generator resolved what this setter calls its
        # argument; a property whose setter could not be named is not writable at all.
        argument = member.get("setter_argument") or ""
        if batch is not None:
            batch.add_call(actor, component, setter, argument, member["variant"], value)
        else:
            _world().call_function(actor=actor, component=component, function=setter,
                                   native_units=True,
                                   args=[{"name": argument,
                                          "value": _to_value(member["variant"], value)}])
        return

    cache = getattr(_state, "snapshots", {}).get((actor, component))
    if cache is not None:
        # The snapshot is now stale for this property; drop it rather than serve the old value.
        cache.pop(address, None)

    if batch is not None:
        batch.add_write(actor, component, address, member["variant"], value)
        return

    _world().set_property(actor=actor, component=component, property=address,
                          value=_to_value(member["variant"], value), native_units=True)



class DynamicObject(Object):
    """A handle with no generated type behind it.

    What the schema-free lookups return, where the class is not known until runtime. It behaves
    exactly like Object; the difference is entirely in the type checker's view, where this one
    declares the catch-alls and Object does not. Reaching for it is therefore a visible choice, and
    one that trades the drift guarantee for not needing the generated classes installed.
    """

    __slots__ = ()


class BoundFunction:
    """A function on a handle, ready to call.

    What attribute lookup returns for a generated method. Separate from the handle so that getting
    the attribute costs nothing until it is called, and so a method and a property of the same name
    cannot be confused for one another.
    """

    __slots__ = ("_owner", "_name", "_signature")

    def __init__(self, owner, name, signature):
        self._owner = owner
        self._name = name
        self._signature = signature

    def __call__(self, *args, **kwargs):
        inputs = self._signature["inputs"]
        if len(args) > len(inputs):
            raise TypeError("{}() takes {} argument(s), {} given".format(
                self._name, len(inputs), len(args)))

        # Positional first, then by name -- the declaration order is the calling convention, and the
        # generated stub spells the names, so either reads correctly at the call site.
        supplied = {}
        for parameter, value in zip(inputs, args):
            supplied[parameter["name"]] = value
        for name, value in kwargs.items():
            if name in supplied:
                raise TypeError("{}() got multiple values for {!r}".format(self._name, name))
            supplied[name] = value

        expected = {parameter["name"] for parameter in inputs}
        unexpected = set(supplied) - expected
        if unexpected:
            raise TypeError("{}() got unexpected argument(s) {}".format(
                self._name, ", ".join(sorted(unexpected))))
        missing = expected - set(supplied)
        if missing:
            raise TypeError("{}() is missing argument(s) {}".format(
                self._name, ", ".join(sorted(missing))))

        arguments = [{"name": parameter["name"],
                      "value": _to_value(parameter["variant"], supplied[parameter["name"]])}
                     for parameter in inputs]

        batch = _open_batch()
        if batch is not None:
            # In order with whatever the batch has queued, exactly as a read is.
            WorldControl = _worldcontrol()
            op = WorldControl.BatchOp(call=WorldControl.CallFunctionRequest(
                actor=self._owner.actor, component=self._owner.component, function=self._name,
                native_units=True,
                args=[WorldControl.FunctionArg(name=argument["name"], value=argument["value"])
                      for argument in arguments]))
            description = "call {}".format(_where(self._owner.actor, self._owner.component, self._name))
            signature = self._signature

            def decode(result):
                return _function_result(result.call, signature)

            capture = getattr(_state, "capture", None)
            if capture is not None:
                capture.append(batch.defer(op, description, decode))
                return _PENDING
            return batch.run_now(op, description, decode)

        response = _world().call_function(actor=self._owner.actor, component=self._owner.component,
                                          function=self._name, args=arguments, native_units=True)
        return _function_result(response, self._signature)

    def __repr__(self):
        return "<bound {}.{}>".format(type(self._owner).__name__, self._name)


def _function_result(response, signature):
    """What a call hands back.

    A void function returns None. A function with a return value returns it. A function that also
    has out parameters -- which is how a Blueprint usually hands back more than one thing -- returns
    a dict keyed by name, because position would be guesswork.
    """
    results = {entry.name: _from_value(entry.typed_value)
               for entry in response.results if entry.HasField("typed_value")}
    if not results:
        return None

    has_return = signature.get("returns") is not None
    if has_return and len(results) == 1 and "ReturnValue" in results:
        return results["ReturnValue"]
    return results


class StructProxy:
    """A handle to a struct-valued property, addressed a member at a time.

    A struct has no single Value that can carry it, but the server has always resolved a nested
    address, so reaching into one is a matter of accumulating the path: `PostProcessSettings`
    becomes `PostProcessSettings.BloomIntensity` on the way out.
    """

    __slots__ = ("_owner", "_address", "_struct")

    def __init__(self, owner, address, struct):
        object.__setattr__(self, "_owner", owner)
        object.__setattr__(self, "_address", address)
        object.__setattr__(self, "_struct", struct)

    def _member(self, name):
        members = _classes.struct_members(self._struct)
        member = members.get(name)
        if member is None:
            raise AttributeError("{!r} has no member {!r}.{}".format(
                self._struct, name, _suggest(name, list(members))))
        return member

    def __getattr__(self, name):
        if name.startswith("_"):
            raise AttributeError(name)
        member = self._member(name)
        address = "{}.{}".format(self._address, name)
        if _is_proxied(member):
            return StructProxy(self._owner, address, member["struct"])
        return _read(self._owner.actor, self._owner.component, address)

    def __setattr__(self, name, value):
        if name.startswith("_"):
            object.__setattr__(self, name, value)
            return
        member = self._member(name)
        if member.get("variant") is None:
            raise AttributeError("{!r}.{} cannot be written as one value; assign to its members"
                                .format(self._struct, name))
        if not member.get("writable"):
            raise AttributeError("{!r}.{} is read-only".format(self._struct, name))
        _write(self._owner.actor, self._owner.component,
               "{}.{}".format(self._address, name), member, value)

    def __repr__(self):
        return "{}({!r})".format(self._struct, self._address)
