# Copyright Tempo Simulation, LLC. All Rights Reserved
#
# Entry points for the typed World API: how a scenario gets a handle it can address by attribute.
#
# The form to reach for is discovery by type:
#
#     from tempo_sim import world
#     from my_project_classes.Game.Vehicles import BP_Cat745_C
#
#     for truck in world.get_actors(BP_Cat745_C):
#         truck.MaxSpeed = 1200.0
#
# There is no unchecked string in that at all, which is the point. Looking an actor up by name is
# available and sometimes necessary, but a name is decided at runtime and cannot be verified by a
# type checker -- and in a packaged build it is not even the name the editor showed, so a
# hard-coded one is its own kind of drift.

from contextlib import contextmanager
from typing import Any, Iterator, List, Optional, TypeVar, overload

from .unreal import (Batch, DynamicObject, Object, Rotator, Transform, Vector,
                     registered_classes)

T = TypeVar("T", bound=Object)


def _world():
    from tempo_sim import tempo_world
    return tempo_world


def _transform_fields(transform: Optional[Transform]):
    """A Transform as the fields SpawnActor takes, in Unreal's own units."""
    transform = transform or Transform()
    return {
        "x": transform.Location.X, "y": transform.Location.Y, "z": transform.Location.Z,
        "r": transform.Rotation.Roll, "p": transform.Rotation.Pitch, "yaw": transform.Rotation.Yaw,
    }


def get_actors(cls: type[T]) -> List[T]:
    """Every actor of this class, or of a class derived from it.

    Matched on the class path rather than the name, because two Blueprints in different folders can
    generate classes with the same name. Subclasses are found through the schema's inheritance
    graph, which the client already has -- no extra round trip.
    """
    classes = registered_classes()
    wanted = cls.__tempo_class__
    found = []
    for descriptor in _world().get_all_actors().actors:
        path = descriptor.class_path
        if path == wanted or classes.is_subclass(path, wanted):
            found.append(cls(descriptor.name))
    return found


@overload
def get_actor(name: str) -> DynamicObject: ...
@overload
def get_actor(name: str, cls: type[T]) -> T: ...


def get_actor(name: str, cls: Optional[type] = None) -> Any:
    """One actor by name.

    With a class, the handle is that class and the actor is checked to really be one -- a name is a
    runtime string, so without the check a typo or a renamed actor would surface later as a
    confusing failure on first attribute access rather than here.

    Without a class, the result is a DynamicObject: every attribute is permitted and checked only by
    the server. That is a deliberate trade, and a separate type so that taking it is visible.
    """
    if cls is None:
        return DynamicObject(name)

    classes = registered_classes()
    wanted = cls.__tempo_class__
    for descriptor in _world().get_all_actors().actors:
        if descriptor.name != name:
            continue
        path = descriptor.class_path
        if path == wanted or classes.is_subclass(path, wanted):
            return cls(name)
        raise TypeError("actor {!r} is {}, which is not {}".format(name, path, wanted))
    raise LookupError("no actor named {!r}".format(name))


def spawn_actor(cls: type[T], transform: Optional[Transform] = None,
                relative_to: str = "") -> T:
    """Spawn one actor of a class and return a handle to it.

    Typed for free: the RPC already takes the class, so there is nothing to infer.
    """
    response = _world().spawn_actor(actor_type=cls.__tempo_class__, deferred=False,
                                    relative_to_actor=relative_to, **_transform_fields(transform))
    return cls(response.name)


@contextmanager
def spawn_actor_deferred(cls: type[T], transform: Optional[Transform] = None,
                         relative_to: str = "") -> Iterator[T]:
    """Spawn an actor, configure it, and only then let it begin.

    Properties set inside the block land before the construction script and BeginPlay run, which is
    the only way to influence anything that reads them -- a plain spawn has already run both by the
    time the handle exists.

        with world.spawn_actor_deferred(BP_Cat745_C, transform=t) as truck:
            truck.InitialTerrainHeight = 1.0
    """
    response = _world().spawn_actor(actor_type=cls.__tempo_class__, deferred=True,
                                    relative_to_actor=relative_to, **_transform_fields(transform))
    handle = cls(response.name)
    try:
        yield handle
    finally:
        # Finish even if the block raised: a deferred actor left unfinished is in the world but
        # never initialized, which is worse than one configured only partway.
        _world().finish_spawning_actor(actor=response.name)


def batch() -> Batch:
    """Collect writes made inside the block and send them together.

        with world.batch():
            truck.MaxSpeed = 1200.0
            truck.Mass = 1500.0

    One call instead of two, and one instead of twenty when there are twenty. Writes keep their
    order, and the block is abandoned without sending anything if it raises.
    """
    return Batch()


class SchemaMismatch(RuntimeError):
    """The installed generated classes describe a different build than the sim is running."""


def check_schema(raise_on_mismatch: bool = True) -> bool:
    """Compare the installed generated classes against the sim's own reflection.

    Stubs generated from one build and used against another is silent wrongness: the type checker
    is satisfied, and the first attribute access that disagrees fails for reasons that look like
    anything but a version skew. The content hash covers names, types and writability -- not
    documentation -- so this answers "can these stubs address this sim", not "is anything different
    at all".

    Not automatic on connect, and deliberately so: the sim computes its hash by walking reflection,
    which is dominated by loading every Blueprint and takes the best part of a minute. Paying that
    on every client start would be worse than the problem. Call it once from a test, or at the start
    of a long run. (A packaged build could report a hash baked in at package time, which would make
    an on-connect check cheap; that is packaging work, not client work.)
    """
    installed = registered_classes().header
    reported = _world().describe_classes(include_blueprint_classes=True,
                                         blueprint_accessible_only=True,
                                         include_functions=True).header

    if reported.content_hash == installed["content_hash"]:
        return True

    message = (
        "the installed generated classes do not match this sim.\n"
        "  classes were generated from: engine {}, schema {}, hash {}\n"
        "  the sim reports:             engine {}, schema {}, hash {}\n"
        "Regenerate the client against this build, or connect to the build the client came from."
    ).format(installed.get("engine_version"), installed.get("schema_version"),
             installed.get("content_hash"),
             reported.engine_version, reported.schema_version, reported.content_hash)
    if raise_on_mismatch:
        raise SchemaMismatch(message)
    import warnings
    warnings.warn(message, stacklevel=2)
    return False
