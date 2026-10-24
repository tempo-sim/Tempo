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


def _spawn_transform(transform: Optional[Transform]):
    """A Transform as the message SpawnActor takes.

    SpawnActor converts: it reads meters, radians and right-handed, unlike the typed property API,
    which is Unreal-native throughout. So a native-unit Transform has to be converted on the way
    in, rather than passed through.
    """
    import math

    import tempo_sim.TempoCore.Geometry_pb2 as Geometry

    transform = transform or Transform()
    message = Geometry.Transform()
    # cm -> m, and Unreal's left-handed Y is the API's right-handed -Y.
    message.location.x = transform.Location.X / 100.0
    message.location.y = -transform.Location.Y / 100.0
    message.location.z = transform.Location.Z / 100.0
    message.rotation.r = math.radians(transform.Rotation.Roll)
    message.rotation.p = -math.radians(transform.Rotation.Pitch)
    message.rotation.y = -math.radians(transform.Rotation.Yaw)
    return message


def get_actors(cls: type[T]) -> List[T]:
    """Every actor of this class, or of a class derived from it.

    Filtered by the server, which walks the actors it already has rather than describing all of them
    for the client to discard -- on a populated map that is the difference between a handful of
    descriptors and thousands.

    Matched on the class path, because two Blueprints in different folders can generate classes with
    the same name.
    """
    response = _world().get_actors_of_class(actor_type=cls.__tempo_class__,
                                            include_subclasses=True)
    # Carrying each actor's real class, not the one that was asked for: the handle is typed as
    # the queried class so the call site checks, and is_a() still has to be able to tell the
    # difference between the pawns that came back.
    return [cls(descriptor.name, class_path=descriptor.class_path)
            for descriptor in response.actors]


def _class_path_of(name: str) -> str:
    """The class of one actor by name, or "" when there is no such actor."""
    for descriptor in _world().get_all_actors().actors:
        if descriptor.name == name:
            return descriptor.class_path
    return ""


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
        # One call to learn what it is. Nothing here needs the class, but a handle that does not
        # know what it points at cannot answer is_a, and this is the place where finding out costs
        # a call that was coming anyway rather than one hidden inside a predicate.
        return DynamicObject(name, class_path=_class_path_of(name))

    # Ask for the class, then look for the name in the answer: one filtered call rather than a
    # description of every actor in the world.
    wanted = cls.__tempo_class__
    response = _world().get_actors_of_class(actor_type=wanted, include_subclasses=True)
    for descriptor in response.actors:
        if descriptor.name == name:
            return cls(name, class_path=descriptor.class_path)

    # Not of that class, or not there at all -- worth distinguishing, because the two mean very
    # different things to whoever wrote the name.
    for descriptor in _world().get_all_actors().actors:
        if descriptor.name == name:
            raise TypeError("actor {!r} is {}, which is not {}".format(
                name, descriptor.class_path, wanted))
    raise LookupError("no actor named {!r}".format(name))


def spawn_actor(cls: type[T], transform: Optional[Transform] = None,
                relative_to: str = "") -> T:
    """Spawn one actor of a class and return a handle to it.

    Typed for free: the RPC already takes the class, so there is nothing to infer.
    """
    response = _world().spawn_actor(actor_type=cls.__tempo_class__, deferred=False,
                                    relative_to_actor=relative_to,
                                    transform=_spawn_transform(transform))
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
                                    relative_to_actor=relative_to,
                                    transform=_spawn_transform(transform))
    handle = cls(response.name)
    try:
        yield handle
    finally:
        # Finish even if the block raised: a deferred actor left unfinished is in the world but
        # never initialized, which is worse than one configured only partway.
        _world().finish_spawning_actor(actor=response.name)


def batch() -> Batch:
    """Send what the block does as one RunBatch: in order, and between two ticks of the sim.

        with world.batch() as b:
            truck.MaxSpeed = 1200.0
            truck.Mass = 1500.0
            speed = b.get(lambda: truck.MaxSpeed)       # Deferred[float]
        print(speed.value)

    One call instead of twenty when there are twenty, accessor writes included. A read or call made
    directly inside the block, rather than through `b.get`/`b.call`, sends what is queued with it
    and returns its real value -- so it sees the writes before it. Nothing is sent if the block
    raises before that.
    """
    return Batch()


class SchemaMismatch(RuntimeError):
    """The installed generated classes describe a different build than the sim is running."""


# The three things a generated package records a hash for, as `hashes.json` keys them, and what one
# of each is called -- "classes"[:-1] is "classe", which is how the first report read.
_KINDS = ("classes", "structs", "enums")
_SINGULAR = {"classes": "class", "structs": "struct", "enums": "enum"}


def _compare(installed, reported):
    """What the sim no longer agrees about, as (changed, absent) lists of (kind, name).

    The two findings are not the same thing. An entry whose hash moved is one the package may
    describe wrongly -- a renamed property, a changed signature, a member that became read-only. An
    entry the sim does not have at all is one nothing can address.

    What is deliberately *not* a finding is an entry the sim has and the package does not. A
    published package describes the modules every build ships and nothing else, so that is its
    normal condition rather than a problem with it.
    """
    changed, absent = [], []
    for kind in _KINDS:
        current = reported[kind]
        for name, hashed in sorted(installed.get(kind, {}).items()):
            if name not in current:
                absent.append((kind, name))
            elif hashed and current[name] != hashed:
                changed.append((kind, name))
    return changed, absent


def _examples(findings, limit=5):
    """A few findings by name. Enough to recognise the change; the count carries the scale."""
    shown = ", ".join("{} {}".format(_SINGULAR[kind], name) for kind, name in findings[:limit])
    return shown + ", ..." if len(findings) > limit else shown


def check_schema(raise_on_mismatch: bool = True) -> bool:
    """Compare the installed generated classes against the sim's own reflection, class by class.

    Stubs generated from one build and used against another is silent wrongness: the type checker
    is satisfied, and the first attribute access that disagrees fails for reasons that look like
    anything but a version skew. A hash covers names, types, writability and signatures -- not
    documentation -- so this answers "can these stubs address this sim", not "is anything different
    at all".

    Compared per class rather than per build, for two reasons. A package may describe part of a
    build by design: the published classes inside `tempo_sim` describe the modules every build
    ships (see TYPED_WORLD_API_PLAN.md section 16), so a whole-walk comparison would reject every
    one of them. And for a whole-build package it is simply a better report -- one number says only
    that something moved, where today a project that adds a single Blueprint invalidates every
    class it has.

    Every installed package is checked, because more than one can be installed and they describe
    different things.

    Not automatic on connect, and deliberately so: the sim computes its answer by walking
    reflection, which is dominated by loading every Blueprint and takes the best part of a minute.
    Paying that on every client start would be worse than the problem. Call it once from a test, or
    at the start of a long run. (A packaged build could report hashes baked in at package time,
    which would make an on-connect check cheap; that is packaging work, not client work.)
    """
    packages = registered_classes().packages
    if not packages:
        raise SchemaMismatch(
            "no generated classes are installed, so there is nothing to check. Install this "
            "build's generated class package (e.g. `tempo_sample_classes`), which ships in "
            "Packaged/API/Python alongside the tempo-sim wheel.")

    # These three options must match what query_packaged_classes.py asked when the installed
    # classes were stamped. The hashes cover function signatures, so a different request yields
    # different numbers for the same build.
    response = _world().describe_classes(include_blueprint_classes=True,
                                         blueprint_accessible_only=True,
                                         include_functions=True)
    reported = {
        "classes": {entry.class_path: entry.content_hash for entry in response.classes},
        "structs": {entry.name: entry.content_hash for entry in response.structs},
        "enums": {entry.name: entry.content_hash for entry in response.enums},
    }

    problems = []
    for package in packages:
        hashes = package.hashes
        if not hashes:
            problems.append(
                "  {}: generated before the per-class hashes existed (schema version {}), so it "
                "cannot be checked. Regenerate it.".format(
                    package.package, package.header.get("schema_version")))
            continue
        changed, absent = _compare(hashes, reported)
        if not changed and not absent:
            continue
        detail = []
        if changed:
            detail.append("{} changed ({})".format(len(changed), _examples(changed)))
        if absent:
            detail.append("{} not in this sim ({})".format(len(absent), _examples(absent)))
        problems.append("  {}: {}".format(package.package, "; ".join(detail)))

    if not problems:
        return True

    message = (
        "the installed generated classes do not match this sim.\n"
        + "\n".join(problems)
        + "\n  the sim reports: engine {}, schema {}\n"
        "Regenerate the client against this build, or connect to the build the client came from."
    ).format(response.header.engine_version, response.header.schema_version)
    if raise_on_mismatch:
        raise SchemaMismatch(message)
    import warnings
    warnings.warn(message, stacklevel=2)
    return False
