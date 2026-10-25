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
from typing import Any, Iterator, List, Optional, Sequence, Tuple, TypeVar, overload

# The SI types, re-exported (the `as` is what marks a re-export to a type checker): this module
# mirrors tempo_world, and takes and returns what it does -- meters, radians and right-handed --
# with handles where tempo_world takes names. The generated classes are what speak Unreal's own
# units and types; tempo_sim.conversion moves between the two.
from .TempoCore.Geometry_pb2 import Rotation as Rotation
from .TempoCore.Geometry_pb2 import Transform as Transform
from .TempoCore.Geometry_pb2 import Vector as Vector
from .TempoCore.Geometry_pb2 import Vector2D as Vector2D
from .unreal import Batch, DynamicObject, Object, registered_classes

T = TypeVar("T", bound=Object)


def _world():
    from tempo_sim import tempo_world
    return tempo_world


def _actor(handle: Optional[Object], role: str = "actor") -> str:
    """The name tempo_world wants for an actor, from a handle; None is "", the world or nothing."""
    if handle is None:
        return ""
    if handle.subsystem or handle.component:
        raise TypeError("{} must be an actor, not {!r}".format(role, handle))
    return handle.actor


def _component(handle: Object, role: str = "component") -> Tuple[str, str]:
    """The actor and component names tempo_world wants for a component, from a handle."""
    if not handle.component:
        raise TypeError("{} must be a component, not {!r}".format(role, handle))
    return handle.actor, handle.component


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


def get_subsystem(cls: type[T]) -> T:
    """The subsystem of one class: a world, game instance, engine or local player subsystem,
    belonging to the sim's game world.

        labeler = world.get_subsystem(TempoActorLabeler)

    There is one of each, so the class is the whole address and there is no name to get wrong. The
    sim resolves it here rather than at first use, because a subsystem may decline to exist
    (`ShouldCreateSubsystem`) and that is better reported at the lookup. A class that is not a
    subsystem at all is refused the same way.
    """
    _world().get_properties(subsystem=cls.__tempo_class__, properties=[], native_units=True)
    return cls(subsystem=cls.__tempo_class__, class_path=cls.__tempo_class__)


def spawn_actor(cls: type[T], transform: Optional[Transform] = None,
                relative_to_actor: Optional[Object] = None) -> T:
    """Spawn one actor of a class and return a handle to it: tempo_world.spawn_actor, typed.

    Typed for free: the RPC already takes the class, so there is nothing to infer.
    """
    response = _world().spawn_actor(actor_type=cls.__tempo_class__, deferred=False,
                                    relative_to_actor=_actor(relative_to_actor,
                                                             "relative_to_actor"),
                                    transform=transform or Transform())
    return cls(response.name, class_path=cls.__tempo_class__)


@contextmanager
def spawn_actor_deferred(cls: type[T], transform: Optional[Transform] = None,
                         relative_to_actor: Optional[Object] = None) -> Iterator[T]:
    """Spawn an actor, configure it, and only then let it begin.

    Properties set inside the block land before the construction script and BeginPlay run, which is
    the only way to influence anything that reads them -- a plain spawn has already run both by the
    time the handle exists.

        with world.spawn_actor_deferred(BP_Cat745_C, transform=t) as truck:
            truck.InitialTerrainHeight = 1.0
    """
    response = _world().spawn_actor(actor_type=cls.__tempo_class__, deferred=True,
                                    relative_to_actor=_actor(relative_to_actor,
                                                             "relative_to_actor"),
                                    transform=transform or Transform())
    handle = cls(response.name, class_path=cls.__tempo_class__)
    try:
        yield handle
    finally:
        # Finish even if the block raised: a deferred actor left unfinished is in the world but
        # never initialized, which is worse than one configured only partway.
        _world().finish_spawning_actor(actor=response.name)


def destroy_actor(actor: Object) -> None:
    """tempo_world.destroy_actor, given a handle."""
    _world().destroy_actor(actor=_actor(actor))


def get_components(actor: Object, cls: type[T]) -> List[T]:
    """The actor's components of this class, or of a class derived from it, as handles of it.

    tempo_world.get_all_components, filtered by class as get_actors is: matched on each component's
    real class through the generated hierarchy, so a subclass counts and a name never has to.
    """
    found = []
    for descriptor in _world().get_all_components(actor=_actor(actor)).components:
        handle = cls(descriptor.actor, descriptor.name, class_path=descriptor.class_path)
        if handle.is_a(cls):
            found.append(handle)
    return found


def add_component(cls: type[T], actor: Object, name: str = "",
                  parent: Optional[Object] = None, transform: Optional[Transform] = None,
                  socket: str = "") -> T:
    """Add a component of this class to an actor and return a handle to it:
    tempo_world.add_component, typed. The handle carries the name the sim gave it, which is `name`
    unless that was taken or empty."""
    response = _world().add_component(component_type=cls.__tempo_class__, actor=_actor(actor),
                                      name=name,
                                      parent=_component(parent, "parent")[1] if parent else "",
                                      transform=transform, socket=socket)
    return cls(actor.actor, response.name, class_path=cls.__tempo_class__)


def destroy_component(component: Object) -> None:
    """tempo_world.destroy_component, given a handle."""
    actor, name = _component(component)
    _world().destroy_component(actor=actor, component=name)


def activate_component(component: Object) -> None:
    """tempo_world.activate_component, given a handle."""
    actor, name = _component(component)
    _world().activate_component(actor=actor, component=name)


def deactivate_component(component: Object) -> None:
    """tempo_world.deactivate_component, given a handle."""
    actor, name = _component(component)
    _world().deactivate_component(actor=actor, component=name)


# -- transforms ----------------------------------------------------------------------------------
#
# SI throughout, as tempo_world is: meters, radians, right-handed. A scale is a ratio, and rides
# beside a transform rather than in it, as it does there.

def set_actor_transform(actor: Object, transform: Transform,
                        relative_to_actor: Optional[Object] = None,
                        scale: Optional[Vector] = None) -> None:
    """tempo_world.set_actor_transform, given handles."""
    _world().set_actor_transform(actor=_actor(actor), transform=transform,
                                 relative_to_actor=_actor(relative_to_actor, "relative_to_actor"),
                                 scale=scale)


def set_actor_location(actor: Object, location: Vector,
                       relative_to_actor: Optional[Object] = None) -> None:
    """tempo_world.set_actor_location, given handles."""
    _world().set_actor_location(actor=_actor(actor), location=location,
                                relative_to_actor=_actor(relative_to_actor, "relative_to_actor"))


def set_actor_rotation(actor: Object, rotation: Rotation,
                       relative_to_actor: Optional[Object] = None) -> None:
    """tempo_world.set_actor_rotation, given handles."""
    _world().set_actor_rotation(actor=_actor(actor), rotation=rotation,
                                relative_to_actor=_actor(relative_to_actor, "relative_to_actor"))


def set_actor_scale3d(actor: Object, scale: Vector) -> None:
    """tempo_world.set_actor_scale3d, given a handle."""
    _world().set_actor_scale3d(actor=_actor(actor), scale=scale)


def set_component_transform(component: Object, transform: Transform,
                            relative_to_world: bool = False,
                            scale: Optional[Vector] = None) -> None:
    """tempo_world.set_component_transform, given a handle."""
    actor, name = _component(component)
    _world().set_component_transform(actor=actor, component=name, transform=transform,
                                     relative_to_world=relative_to_world, scale=scale)


def set_component_location(component: Object, location: Vector,
                           relative_to_world: bool = False) -> None:
    """tempo_world.set_component_location, given a handle."""
    actor, name = _component(component)
    _world().set_component_location(actor=actor, component=name, location=location,
                                    relative_to_world=relative_to_world)


def set_component_rotation(component: Object, rotation: Rotation,
                           relative_to_world: bool = False) -> None:
    """tempo_world.set_component_rotation, given a handle."""
    actor, name = _component(component)
    _world().set_component_rotation(actor=actor, component=name, rotation=rotation,
                                    relative_to_world=relative_to_world)


def set_component_scale3d(component: Object, scale: Vector,
                          relative_to_world: bool = False) -> None:
    """tempo_world.set_component_scale3d, given a handle."""
    actor, name = _component(component)
    _world().set_component_scale3d(actor=actor, component=name, scale=scale,
                                   relative_to_world=relative_to_world)


def get_actor_transform(actor: Object, relative_to_actor: Optional[Object] = None):
    """tempo_world.get_actor_transform, given handles: a GetTransformResponse."""
    return _world().get_actor_transform(
        actor=_actor(actor), relative_to_actor=_actor(relative_to_actor, "relative_to_actor"))


def get_actor_location(actor: Object, relative_to_actor: Optional[Object] = None):
    """tempo_world.get_actor_location, given handles: a GetLocationResponse."""
    return _world().get_actor_location(
        actor=_actor(actor), relative_to_actor=_actor(relative_to_actor, "relative_to_actor"))


def get_actor_rotation(actor: Object, relative_to_actor: Optional[Object] = None):
    """tempo_world.get_actor_rotation, given handles: a GetRotationResponse."""
    return _world().get_actor_rotation(
        actor=_actor(actor), relative_to_actor=_actor(relative_to_actor, "relative_to_actor"))


def get_actor_scale3d(actor: Object):
    """tempo_world.get_actor_scale3d, given a handle: a GetScale3DResponse."""
    return _world().get_actor_scale3d(actor=_actor(actor))


def _component_query(component, relative_to_component):
    actor, name = _component(component)
    relative = _component(relative_to_component, "relative_to_component")[1] \
        if relative_to_component is not None else ""
    return actor, name, relative


def get_component_transform(component: Object, frame: int = 0,
                            relative_to_component: Optional[Object] = None, socket: str = ""):
    """tempo_world.get_component_transform, given handles: a GetTransformResponse. `frame` is a
    TempoWorld.ComponentFrame."""
    actor, name, relative = _component_query(component, relative_to_component)
    return _world().get_component_transform(actor=actor, component=name, frame=frame,
                                            relative_to_component=relative, socket=socket)


def get_component_location(component: Object, frame: int = 0,
                           relative_to_component: Optional[Object] = None, socket: str = ""):
    """tempo_world.get_component_location, given handles: a GetLocationResponse."""
    actor, name, relative = _component_query(component, relative_to_component)
    return _world().get_component_location(actor=actor, component=name, frame=frame,
                                           relative_to_component=relative, socket=socket)


def get_component_rotation(component: Object, frame: int = 0,
                           relative_to_component: Optional[Object] = None, socket: str = ""):
    """tempo_world.get_component_rotation, given handles: a GetRotationResponse."""
    actor, name, relative = _component_query(component, relative_to_component)
    return _world().get_component_rotation(actor=actor, component=name, frame=frame,
                                           relative_to_component=relative, socket=socket)


def get_component_scale3d(component: Object, frame: int = 0,
                          relative_to_component: Optional[Object] = None, socket: str = ""):
    """tempo_world.get_component_scale3d, given handles: a GetScale3DResponse."""
    actor, name, relative = _component_query(component, relative_to_component)
    return _world().get_component_scale3d(actor=actor, component=name, frame=frame,
                                          relative_to_component=relative, socket=socket)


# -- world state ---------------------------------------------------------------------------------

def get_current_actor_state(actor: Object, include_hidden_components: bool = False):
    """tempo_world.get_current_actor_state, given a handle: an ActorState."""
    return _world().get_current_actor_state(actor=_actor(actor),
                                            include_hidden_components=include_hidden_components)


def stream_actor_state(actor: Object, include_hidden_components: bool = False):
    """tempo_world.stream_actor_state, given a handle: ActorStates, one per tick."""
    return _world().stream_actor_state(actor=_actor(actor),
                                       include_hidden_components=include_hidden_components)


def get_current_actor_states_near(near_actor: Object, search_radius_m: float,
                                  include_static: bool = False, include_hidden_actors: bool = False,
                                  include_hidden_components: bool = False):
    """tempo_world.get_current_actor_states_near, given a handle: ActorStates."""
    return _world().get_current_actor_states_near(
        near_actor=_actor(near_actor, "near_actor"), search_radius_m=search_radius_m,
        include_static=include_static, include_hidden_actors=include_hidden_actors,
        include_hidden_components=include_hidden_components)


def stream_actor_states_near(near_actor: Object, search_radius_m: float,
                             include_static: bool = False, include_hidden_actors: bool = False,
                             include_hidden_components: bool = False):
    """tempo_world.stream_actor_states_near, given a handle: ActorStates, one per tick."""
    return _world().stream_actor_states_near(
        near_actor=_actor(near_actor, "near_actor"), search_radius_m=search_radius_m,
        include_static=include_static, include_hidden_actors=include_hidden_actors,
        include_hidden_components=include_hidden_components)


def stream_overlap_events(actor: Object):
    """tempo_world.stream_overlap_events, given a handle: OverlapEventResponses as they happen."""
    return _world().stream_overlap_events(actor=_actor(actor))


def raycast(start: Vector, end: Vector, collision_channel: int = 0,
            ignored_actors: Sequence[Object] = ()):
    """tempo_world.raycast, ignoring actors given as handles: a RaycastResponse."""
    return _world().raycast(start=start, end=end, collision_channel=collision_channel,
                            ignored_actors=[_actor(a, "ignored_actors") for a in ignored_actors])


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
_KINDS = ("classes", "structs", "enums", "assets")
_SINGULAR = {"classes": "class", "structs": "struct", "enums": "enum", "assets": "asset"}


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
            "no generated classes are registered, so there is nothing to check. Install this "
            "build's project package and import its classes (e.g. `import "
            "tempo_sample.reflected`); the wheel ships in Packaged/API/Python alongside the "
            "tempo-sim wheel.")

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
        "assets": {},
    }
    # The asset catalog's identity is each asset's path and class, which the sim answers from its
    # asset registry. Asked only when some installed package has a catalog to compare.
    if any(package.hashes.get("assets") for package in packages):
        assets = _world().get_assets(asset_type="/Script/CoreUObject.Object", include_subclasses=True)
        reported["assets"] = {asset.path: asset.class_path for asset in assets.assets}

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
