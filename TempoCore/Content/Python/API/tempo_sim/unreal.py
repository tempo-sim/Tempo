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


class Object:
    """The root of the generated class hierarchy.

    An instance is a handle to something in the running simulation, not a copy of it: reading an
    attribute asks the server, and assigning to one tells it. The generated subclasses add no
    behavior -- they carry names and types only -- so all of it lives here.
    """

    # The Unreal class path this type stands for. Generated subclasses override it; a path rather
    # than a name because two Blueprints in different folders can share a name.
    __tempo_class__ = "/Script/CoreUObject.Object"

    __slots__ = ("_actor", "_component")

    def __init__(self, actor: str = "", component: str = ""):
        # Addressed the way every TempoWorld RPC addresses things: an actor name, and a component
        # name when the handle is to a component rather than to the actor itself.
        object.__setattr__(self, "_actor", actor)
        object.__setattr__(self, "_component", component)

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
        """Whether this handle's class is `other` or derives from it.

        Not `isinstance`: the generated classes are deliberately flat at runtime, because Unreal's
        inheritance has cycles across packages and a base has to exist when a class statement runs.
        The real hierarchy lives in the schema, so this is the question to ask.
        """
        ancestor = getattr(other, "__tempo_class__", other)
        return _classes.is_subclass(type(self).__tempo_class__, ancestor)

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
                # A struct is addressed through its members, so there is nothing to prefetch.
                if member.get("variant") is not None:
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
            if member.get("struct") and member.get("variant") is None:
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
            return python_class(self._actor, name)

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


class _Classes:
    """The generated class surface, loaded on demand.

    Three things are needed to serve an attribute: which module a class path lives in (the index),
    what members a class declares (one `<Module>.meta.json` per module), and what a struct's members
    are (`structs.meta.json`). Only the index is global -- resolving a superclass needs it before any
    module is known -- so that is loaded once and the rest per module, the first time a class from
    it is touched.
    """

    def __init__(self):
        self._package = None
        self._directory = None
        self._index = None
        self._modules = {}
        self._structs = None
        self._header = None

    def register(self, package, directory):
        self._package = package
        self._directory = directory
        self._index = None
        self._modules = {}
        self._structs = None
        self._header = None

    @property
    def registered(self):
        return self._directory is not None

    def _require(self):
        if not self.registered:
            raise RuntimeError(
                "No generated classes are registered. Install and import this build's generated "
                "class package (e.g. `import tempo_sample_classes`), which ships in "
                "Packaged/API/Python alongside the tempo-sim wheel.")

    def _load(self, *parts):
        self._require()
        with open(os.path.join(self._directory, *parts)) as f:
            return json.load(f)

    @property
    def index(self):
        if self._index is None:
            self._index = self._load("index.json")
        return self._index

    @property
    def header(self):
        """The schema header the stubs were generated from, for the drift check."""
        if self._header is None:
            self._header = self._load("schema_header.json")
        return self._header

    @property
    def structs(self):
        if self._structs is None:
            self._structs = self._load("structs.meta.json")
        return self._structs

    def module_of(self, class_path):
        entry = self.index.get(class_path)
        return entry.split(":", 1)[0] if entry else None

    def metadata(self, class_path):
        """The declared members of one class, or None when it is not in this build."""
        module = self.module_of(class_path)
        if module is None:
            return None
        if module not in self._modules:
            # A module that is also a package carries its files in __init__.
            stem = os.path.join(*module.split("."))
            candidates = (stem + ".meta.json", os.path.join(stem, "__init__.meta.json"))
            for candidate in candidates:
                path = os.path.join(self._directory, candidate)
                if os.path.exists(path):
                    with open(path) as f:
                        self._modules[module] = json.load(f)
                    break
            else:
                self._modules[module] = {}
        return self._modules[module].get(class_path)

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
        entry = self.index.get(class_path)
        if entry is None:
            return None
        module_name, class_name = entry.split(":", 1)
        import importlib
        module = importlib.import_module("{}.{}".format(self._package, module_name))
        return getattr(module, class_name, None)


_classes = _Classes()


def register_classes(package, directory):
    """Called by a generated class package when it is imported."""
    _classes.register(package, directory)


def registered_classes():
    """The class registry, for the entry points in tempo_sim.world."""
    return _classes


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
    if isinstance(value, Object):
        return "{}:{}".format(value.actor, value.component) if value.component else value.actor
    if isinstance(value, type) and hasattr(value, "__tempo_class__"):
        # A generated class stands for the Unreal class it was generated from.
        return value.__tempo_class__
    return str(value)


def _to_value(variant, value):
    """Build a Value carrying `value` in `variant`."""
    message = _value_type()()

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

    if variant in _SCALAR_VARIANTS:
        return getattr(message, variant)

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

    if variant.endswith("_set_value"):
        return set(getattr(message, variant).values)
    if variant.endswith("_array_value"):
        return list(getattr(message, variant).values)

    raise TypeError("no conversion from Value variant {!r}".format(variant))


# The open batch, if any. Thread-local because the client is not thread-safe anyway -- the gRPC
# channel is bound to the loop that created it -- so a batch opened on one thread has no business
# capturing writes made on another.
_state = threading.local()


def _open_batch():
    return getattr(_state, "batch", None)


class Batch:
    """Buffers writes and sends them together.

    Attribute access is one round trip per attribute, which is the cost of making it look like
    assignment. Setting twenty properties on an actor should not be twenty calls, so a batch
    collects them and flushes once.

    Order is preserved exactly. A property with a BlueprintSetter has to be written by calling its
    accessor, which is not something SetProperties can carry, so the queue holds both kinds and the
    flush sends each contiguous run of plain writes as one SetProperties call with the accessor
    calls in between. The common case -- no accessors -- is a single call.
    """

    def __init__(self):
        self._queue = []
        self._depth = 0

    def add_write(self, actor, component, address, variant, value):
        self._queue.append(("set", (actor, component, address, variant, value)))

    def add_call(self, actor, component, function, variant, value):
        self._queue.append(("call", (actor, component, function, variant, value)))

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
        return False

    def flush(self):
        queued, self._queue = self._queue, []
        pending = []
        for kind, payload in queued:
            if kind == "set":
                pending.append(payload)
                continue
            self._send(pending)
            pending = []
            actor, component, function, variant, value = payload
            _world().call_function(actor=actor, component=component, function=function,
                                   native_units=True,
                                   args=[{"name": "", "value": _to_value(variant, value)}])
        self._send(pending)

    @staticmethod
    def _send(writes):
        if not writes:
            return
        builder = _world().batch()
        for actor, component, address, variant, value in writes:
            # The generic op, which is the only one carrying native_units.
            builder.set_generic_property(actor=actor, component=component, property=address,
                                         value=_to_value(variant, value), native_units=True)
        response = builder.execute()
        if response.failures:
            first = response.failures[0]
            raise RuntimeError("batched write {} of {} failed: {}".format(
                first.op_index, len(writes), first.error))


def _read(actor, component, address):
    # A snapshot serves reads for the handle that opened it, so a block reading several properties
    # costs one call rather than one per property.
    cache = getattr(_state, "snapshots", {}).get((actor, component))
    if cache is not None and address in cache:
        return cache[address]
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
        if batch is not None:
            batch.add_call(actor, component, setter, member["variant"], value)
        else:
            _world().call_function(actor=actor, component=component, function=setter,
                                   native_units=True,
                                   args=[{"name": "", "value": _to_value(member["variant"], value)}])
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
        members = _classes.structs.get(self._struct, {})
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
        if member.get("struct"):
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
