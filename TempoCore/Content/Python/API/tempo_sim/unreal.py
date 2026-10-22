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
