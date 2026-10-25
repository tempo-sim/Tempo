// Copyright Tempo Simulation, LLC. All Rights Reserved

//! Entry points for the typed World API: how a scenario gets a handle it can address by method.
//!
//! The form to reach for is discovery by type:
//!
//! ```no_run
//! use tempo_sim::world;
//! # mod my_project_classes { pub mod game { pub mod vehicles {
//! #     #[repr(transparent)]
//! #     pub struct BP_Cat745_C(pub tempo_sim::unreal::ObjectRef);
//! #     unsafe impl tempo_sim::unreal::Class for BP_Cat745_C {
//! #         const PATH: &'static str = "/Game/Vehicles/BP_Cat745.BP_Cat745_C";
//! #         fn from_object(o: tempo_sim::unreal::ObjectRef) -> Self { Self(o) }
//! #         fn as_object(&self) -> &tempo_sim::unreal::ObjectRef { &self.0 }
//! #         fn hierarchy() -> &'static (dyn tempo_sim::unreal::Hierarchy + 'static) { &T }
//! #     }
//! # struct Table;
//! # impl tempo_sim::unreal::Hierarchy for Table {
//! #     fn superclass(&self, _: &str) -> Option<&'static str> { None }
//! #     fn header(&self) -> tempo_sim::unreal::SchemaIdentity { Default::default() }
//! #     fn hashes(&self, _: tempo_sim::unreal::EntryKind)
//! #         -> &'static [(&'static str, &'static str)] { &[] }
//! #     fn content_hash(&self, _: tempo_sim::unreal::EntryKind, _: &str)
//! #         -> Option<&'static str> { None }
//! # }
//! # static T: Table = Table;
//! #     impl BP_Cat745_C {
//! #         pub fn set_max_speed(&self, v: f64) -> Result<(), tempo_sim::TempoError> {
//! #             self.0.set("MaxSpeed", v)
//! #         }
//! #     }
//! # } } }
//! use my_project_classes::game::vehicles::BP_Cat745_C;
//!
//! # fn main() -> Result<(), tempo_sim::TempoError> {
//! for truck in world::get_actors::<BP_Cat745_C>()? {
//!     truck.set_max_speed(1200.0)?;
//! }
//! # Ok(())
//! # }
//! ```
//!
//! There is no unchecked string in that at all, which is the point. Looking an actor up by name is
//! available and sometimes necessary, but a name is decided at runtime and cannot be verified by a
//! compiler — and in a packaged build it is not even the name the editor showed, so a hard-coded
//! one is its own kind of drift.

use crate::error::TempoError;
use crate::unreal::{note_origin, Class, ObjectRef};

pub use crate::unreal::{batch, batch_async, defer, BatchFuture, Deferred};

/// The SI types, re-exported: this module mirrors `tempo_world`, and takes and returns what it
/// does — meters, radians and right-handed — with handles where `tempo_world` takes names. The
/// generated classes are what speak Unreal's own units and types; [`crate::conversion`] moves
/// between the two.
pub use crate::proto::tempo_core::{Rotation, Transform, Vector, Vector2D};

/// Every actor of this class, or of a class derived from it.
///
/// Filtered by the server, which walks the actors it already has rather than describing all of
/// them for the client to discard — on a populated map that is the difference between a handful of
/// descriptors and thousands.
///
/// Matched on the class path, because two Blueprints in different folders can generate classes
/// with the same name.
pub fn get_actors<T: Class>() -> Result<Vec<T>, TempoError> {
    note_origin::<T>();
    let response = crate::tempo_world::get_actors_of_class(T::PATH.to_string(), true)?;
    Ok(described(response))
}

/// Every actor of this class, without blocking.
pub async fn get_actors_async<T: Class>() -> Result<Vec<T>, TempoError> {
    note_origin::<T>();
    let response = crate::tempo_world::get_actors_of_class_async(T::PATH.to_string(), true).await?;
    Ok(described(response))
}

fn described<T: Class>(response: crate::proto::tempo_world::GetAllActorsResponse) -> Vec<T> {
    // Carrying each actor's real class, not the one that was asked for: the handle is typed as the
    // queried class so the call site checks, and `is_a` still has to be able to tell the
    // difference between the pawns that came back.
    response
        .actors
        .into_iter()
        .map(|descriptor| {
            T::from_object(ObjectRef::actor(descriptor.name).with_class(descriptor.class_path))
        })
        .collect()
}

/// One actor by name, checked to really be of this class.
///
/// A name is a runtime string, so without the check a typo or a renamed actor would surface later
/// as a confusing failure on first property access rather than here.
pub fn get_actor<T: Class>(name: &str) -> Result<T, TempoError> {
    note_origin::<T>();
    // Ask for the class, then look for the name in the answer: one filtered call rather than a
    // description of every actor in the world.
    let response = crate::tempo_world::get_actors_of_class(T::PATH.to_string(), true)?;
    if let Some(found) = matching::<T>(&response, name) {
        return Ok(found);
    }
    Err(not_that_class(crate::tempo_world::get_all_actors()?, name, T::PATH))
}

/// One actor by name, without blocking.
pub async fn get_actor_async<T: Class>(name: &str) -> Result<T, TempoError> {
    note_origin::<T>();
    let response = crate::tempo_world::get_actors_of_class_async(T::PATH.to_string(), true).await?;
    if let Some(found) = matching::<T>(&response, name) {
        return Ok(found);
    }
    Err(not_that_class(crate::tempo_world::get_all_actors_async().await?, name, T::PATH))
}

/// The subsystem of one class: a world, game instance, engine or local player subsystem, belonging
/// to the sim's game world.
///
/// There is one of each, so the class is the whole address and there is no name to get wrong. The
/// sim resolves it here rather than at first use, because a subsystem may decline to exist
/// (`ShouldCreateSubsystem`) and that is better reported at the lookup. A class that is not a
/// subsystem at all is refused the same way.
pub fn get_subsystem<T: Class>() -> Result<T, TempoError> {
    note_origin::<T>();
    crate::tempo_world::get_properties(
        String::new(),
        String::new(),
        Vec::new(),
        true,
        T::PATH.to_string(),
    )?;
    Ok(T::from_object(ObjectRef::subsystem(T::PATH).with_class(T::PATH)))
}

/// The subsystem of one class, without blocking.
pub async fn get_subsystem_async<T: Class>() -> Result<T, TempoError> {
    note_origin::<T>();
    crate::tempo_world::get_properties_async(
        String::new(),
        String::new(),
        Vec::new(),
        true,
        T::PATH.to_string(),
    )
    .await?;
    Ok(T::from_object(ObjectRef::subsystem(T::PATH).with_class(T::PATH)))
}

fn matching<T: Class>(
    response: &crate::proto::tempo_world::GetAllActorsResponse,
    name: &str,
) -> Option<T> {
    response.actors.iter().find(|d| d.name == name).map(|descriptor| {
        T::from_object(
            ObjectRef::actor(&descriptor.name).with_class(descriptor.class_path.clone()),
        )
    })
}

/// Not of that class, or not there at all — worth distinguishing, because the two mean very
/// different things to whoever wrote the name.
fn not_that_class(
    all: crate::proto::tempo_world::GetAllActorsResponse,
    name: &str,
    wanted: &str,
) -> TempoError {
    match all.actors.iter().find(|d| d.name == name) {
        Some(descriptor) => TempoError::Protocol(format!(
            "actor {:?} is {}, which is not {}",
            name, descriptor.class_path, wanted
        )),
        None => TempoError::Protocol(format!("no actor named {:?}", name)),
    }
}

/// One actor by name, with no class to check it against.
///
/// Every property is addressed by string and checked only by the server, so nothing here is
/// verified at compile time. That is the escape hatch, and it is a separate function rather than
/// an overload so that taking it is visible in the code that took it.
pub fn find_actor(name: &str) -> Result<ObjectRef, TempoError> {
    // One call to learn what it is. Nothing here needs the class, but a handle that does not know
    // what it points at cannot answer `is_a`, and this is the place where finding out costs a call
    // that was coming anyway rather than one hidden inside a predicate.
    let all = crate::tempo_world::get_all_actors()?;
    match all.actors.iter().find(|d| d.name == name) {
        Some(descriptor) => {
            Ok(ObjectRef::actor(name).with_class(descriptor.class_path.clone()))
        }
        None => Err(TempoError::Protocol(format!("no actor named {:?}", name))),
    }
}

/// [`find_actor`], without blocking.
pub async fn find_actor_async(name: &str) -> Result<ObjectRef, TempoError> {
    let all = crate::tempo_world::get_all_actors_async().await?;
    match all.actors.iter().find(|d| d.name == name) {
        Some(descriptor) => {
            Ok(ObjectRef::actor(name).with_class(descriptor.class_path.clone()))
        }
        None => Err(TempoError::Protocol(format!("no actor named {:?}", name))),
    }
}

/// Spawn one actor of a class and return a handle to it: `tempo_world::spawn_actor`, typed.
///
/// Typed for free: the RPC already takes the class, so there is nothing to infer.
pub fn spawn_actor<T: Class>(
    transform: Option<Transform>,
    relative_to_actor: Option<&ObjectRef>,
) -> Result<T, TempoError> {
    note_origin::<T>();
    let response = crate::tempo_world::spawn_actor(
        T::PATH.to_string(),
        false,
        transform,
        optional_actor(relative_to_actor, "relative_to_actor")?,
    )?;
    Ok(T::from_object(ObjectRef::actor(response.name).with_class(T::PATH)))
}

/// Spawn one actor of a class, without blocking.
pub async fn spawn_actor_async<T: Class>(
    transform: Option<Transform>,
    relative_to_actor: Option<&ObjectRef>,
) -> Result<T, TempoError> {
    note_origin::<T>();
    let response = crate::tempo_world::spawn_actor_async(
        T::PATH.to_string(),
        false,
        transform,
        optional_actor(relative_to_actor, "relative_to_actor")?,
    )
    .await?;
    Ok(T::from_object(ObjectRef::actor(response.name).with_class(T::PATH)))
}

/// Spawn an actor, configure it, and only then let it begin.
///
/// Properties set inside `configure` land before the construction script and `BeginPlay` run,
/// which is the only way to influence anything that reads them — a plain spawn has already run
/// both by the time the handle exists.
///
/// ```no_run
/// # use tempo_sim::world;
/// # #[repr(transparent)]
/// # struct BP_Cat745_C(tempo_sim::unreal::ObjectRef);
/// # unsafe impl tempo_sim::unreal::Class for BP_Cat745_C {
/// #     const PATH: &'static str = "/Game/Vehicles/BP_Cat745.BP_Cat745_C";
/// #     fn from_object(o: tempo_sim::unreal::ObjectRef) -> Self { Self(o) }
/// #     fn as_object(&self) -> &tempo_sim::unreal::ObjectRef { &self.0 }
/// #     fn hierarchy() -> &'static (dyn tempo_sim::unreal::Hierarchy + 'static) { &T }
/// # }
/// # struct Table;
/// # impl tempo_sim::unreal::Hierarchy for Table {
/// #     fn superclass(&self, _: &str) -> Option<&'static str> { None }
/// #     fn header(&self) -> tempo_sim::unreal::SchemaIdentity { Default::default() }
/// #     fn hashes(&self, _: tempo_sim::unreal::EntryKind)
/// #         -> &'static [(&'static str, &'static str)] { &[] }
/// #     fn content_hash(&self, _: tempo_sim::unreal::EntryKind, _: &str)
/// #         -> Option<&'static str> { None }
/// # }
/// # static T: Table = Table;
/// # impl BP_Cat745_C {
/// #     fn set_initial_terrain_height(&self, v: f64) -> Result<(), tempo_sim::TempoError> {
/// #         self.0.set("InitialTerrainHeight", v)
/// #     }
/// # }
/// # fn main() -> Result<(), tempo_sim::TempoError> {
/// let truck = world::spawn_actor_deferred::<BP_Cat745_C, _>(None, None, |truck| {
///     truck.set_initial_terrain_height(1.0)
/// })?;
/// # let _ = truck;
/// # Ok(())
/// # }
/// ```
///
/// Spawning is finished even if `configure` fails: a deferred actor left unfinished is in the
/// world but never initialized, which is worse than one configured only partway. The error from
/// `configure` is what comes back.
pub fn spawn_actor_deferred<T, F>(
    transform: Option<Transform>,
    relative_to_actor: Option<&ObjectRef>,
    configure: F,
) -> Result<T, TempoError>
where
    T: Class,
    F: FnOnce(&T) -> Result<(), TempoError>,
{
    note_origin::<T>();
    let response = crate::tempo_world::spawn_actor(
        T::PATH.to_string(),
        true,
        transform,
        optional_actor(relative_to_actor, "relative_to_actor")?,
    )?;
    let handle = T::from_object(ObjectRef::actor(&response.name).with_class(T::PATH));

    let configured = configure(&handle);
    let finished = crate::tempo_world::finish_spawning_actor(response.name);

    configured?;
    finished?;
    Ok(handle)
}

/// [`spawn_actor_deferred`], without blocking.
///
/// `configure` is given a handle of its own to the actor, because the future it returns has to own
/// what it uses across an await; what comes back is a handle to the same actor. Inside it, write
/// through the `_async` setters, or queue several in one [`batch_async`]:
///
/// ```no_run
/// # use tempo_sim::world;
/// # #[repr(transparent)]
/// # struct BP_Cat745_C(tempo_sim::unreal::ObjectRef);
/// # unsafe impl tempo_sim::unreal::Class for BP_Cat745_C {
/// #     const PATH: &'static str = "/Game/Vehicles/BP_Cat745.BP_Cat745_C";
/// #     fn from_object(o: tempo_sim::unreal::ObjectRef) -> Self { Self(o) }
/// #     fn as_object(&self) -> &tempo_sim::unreal::ObjectRef { &self.0 }
/// #     fn hierarchy() -> &'static (dyn tempo_sim::unreal::Hierarchy + 'static) { &T }
/// # }
/// # struct Table;
/// # impl tempo_sim::unreal::Hierarchy for Table {
/// #     fn superclass(&self, _: &str) -> Option<&'static str> { None }
/// #     fn header(&self) -> tempo_sim::unreal::SchemaIdentity { Default::default() }
/// #     fn hashes(&self, _: tempo_sim::unreal::EntryKind)
/// #         -> &'static [(&'static str, &'static str)] { &[] }
/// #     fn content_hash(&self, _: tempo_sim::unreal::EntryKind, _: &str)
/// #         -> Option<&'static str> { None }
/// # }
/// # static T: Table = Table;
/// # impl BP_Cat745_C {
/// #     fn set_initial_terrain_height(&self, v: f64) -> Result<(), tempo_sim::TempoError> {
/// #         self.0.set("InitialTerrainHeight", v)
/// #     }
/// # }
/// # async fn spawn() -> Result<(), tempo_sim::TempoError> {
/// let truck = world::spawn_actor_deferred_async::<BP_Cat745_C, _, _>(None, None, |truck| async move {
///     world::batch_async(|| truck.set_initial_terrain_height(1.0)).await
/// })
/// .await?;
/// # let _ = truck;
/// # Ok(())
/// # }
/// ```
///
/// As in the blocking form, spawning is finished even if `configure` fails, and its error is what
/// comes back. Dropped before it completes, this leaves the actor unfinished, as the blocking form
/// does if `configure` panics.
pub async fn spawn_actor_deferred_async<T, F, Fut>(
    transform: Option<Transform>,
    relative_to_actor: Option<&ObjectRef>,
    configure: F,
) -> Result<T, TempoError>
where
    T: Class,
    F: FnOnce(T) -> Fut,
    Fut: std::future::Future<Output = Result<(), TempoError>>,
{
    note_origin::<T>();
    let response = crate::tempo_world::spawn_actor_async(
        T::PATH.to_string(),
        true,
        transform,
        optional_actor(relative_to_actor, "relative_to_actor")?,
    )
    .await?;
    let object = ObjectRef::actor(&response.name).with_class(T::PATH);

    let configured = configure(T::from_object(object.clone())).await;
    let finished = crate::tempo_world::finish_spawning_actor_async(response.name).await;

    configured?;
    finished?;
    Ok(T::from_object(object))
}

// -- the rest of tempo_world, given handles ----------------------------------------------------
//
// Each is the `tempo_world` function of the same name, taking a handle where it takes a name and
// returning what it returns -- except that what creates a thing hands back a typed handle to it.
// SI throughout, as `tempo_world` is: meters, radians, right-handed. A scale is a ratio, and rides
// beside a transform rather than in it, as it does there.

fn actor_name(handle: &ObjectRef, role: &str) -> Result<String, TempoError> {
    if !handle.component_name().is_empty() || !handle.subsystem_class().is_empty() {
        return Err(TempoError::Protocol(format!("{} must be an actor, not {:?}", role, handle)));
    }
    Ok(handle.actor_name().to_string())
}

fn optional_actor(handle: Option<&ObjectRef>, role: &str) -> Result<String, TempoError> {
    handle.map_or(Ok(String::new()), |handle| actor_name(handle, role))
}

fn component_names(handle: &ObjectRef, role: &str) -> Result<(String, String), TempoError> {
    if handle.component_name().is_empty() {
        return Err(TempoError::Protocol(format!("{} must be a component, not {:?}", role, handle)));
    }
    Ok((handle.actor_name().to_string(), handle.component_name().to_string()))
}

fn optional_component(handle: Option<&ObjectRef>, role: &str) -> Result<String, TempoError> {
    handle.map_or(Ok(String::new()), |handle| Ok(component_names(handle, role)?.1))
}

/// `tempo_world::destroy_actor`, given a handle.
pub fn destroy_actor(actor: &impl Class) -> Result<(), TempoError> {
    crate::tempo_world::destroy_actor(actor_name(actor.as_object(), "actor")?)?;
    Ok(())
}

/// `tempo_world::destroy_actor`, given a handle, without blocking.
pub async fn destroy_actor_async(actor: &impl Class) -> Result<(), TempoError> {
    crate::tempo_world::destroy_actor_async(actor_name(actor.as_object(), "actor")?).await?;
    Ok(())
}

/// The actor's components of this class, or of a class derived from it, as handles of it.
///
/// `tempo_world::get_all_components`, filtered by class as [`get_actors`] is: matched on each
/// component's real class through the generated hierarchy, so a subclass counts and a name never
/// has to.
pub fn get_components<T: Class>(actor: &impl Class) -> Result<Vec<T>, TempoError> {
    note_origin::<T>();
    let response = crate::tempo_world::get_all_components(actor_name(actor.as_object(), "actor")?)?;
    Ok(components_of(response))
}

/// The actor's components of this class, without blocking.
pub async fn get_components_async<T: Class>(actor: &impl Class) -> Result<Vec<T>, TempoError> {
    note_origin::<T>();
    let response =
        crate::tempo_world::get_all_components_async(actor_name(actor.as_object(), "actor")?).await?;
    Ok(components_of(response))
}

fn components_of<T: Class>(response: crate::proto::tempo_world::GetAllComponentsResponse) -> Vec<T> {
    response
        .components
        .into_iter()
        .map(|d| ObjectRef::component(d.actor, d.name).with_class(d.class_path))
        .filter(|object| object.is_a(T::PATH))
        .map(T::from_object)
        .collect()
}

/// Add a component of this class to an actor and return a handle to it: `tempo_world::add_component`,
/// typed. The handle carries the name the sim gave it, which is `name` unless that was taken or
/// empty.
pub fn add_component<T: Class>(
    actor: &impl Class,
    name: &str,
    parent: Option<&ObjectRef>,
    transform: Option<Transform>,
    socket: &str,
) -> Result<T, TempoError> {
    note_origin::<T>();
    let owner = actor_name(actor.as_object(), "actor")?;
    let response = crate::tempo_world::add_component(
        T::PATH.to_string(),
        owner.clone(),
        name.to_string(),
        optional_component(parent, "parent")?,
        transform,
        socket.to_string(),
    )?;
    Ok(T::from_object(ObjectRef::component(owner, response.name).with_class(T::PATH)))
}

/// Add a component of this class to an actor, without blocking.
pub async fn add_component_async<T: Class>(
    actor: &impl Class,
    name: &str,
    parent: Option<&ObjectRef>,
    transform: Option<Transform>,
    socket: &str,
) -> Result<T, TempoError> {
    note_origin::<T>();
    let owner = actor_name(actor.as_object(), "actor")?;
    let response = crate::tempo_world::add_component_async(
        T::PATH.to_string(),
        owner.clone(),
        name.to_string(),
        optional_component(parent, "parent")?,
        transform,
        socket.to_string(),
    )
    .await?;
    Ok(T::from_object(ObjectRef::component(owner, response.name).with_class(T::PATH)))
}

macro_rules! component_call {
    ($(#[$doc:meta])* $name:ident, $async_name:ident) => {
        $(#[$doc])*
        pub fn $name(component: &impl Class) -> Result<(), TempoError> {
            let (actor, name) = component_names(component.as_object(), "component")?;
            crate::tempo_world::$name(actor, name)?;
            Ok(())
        }

        $(#[$doc])*
        /// Without blocking.
        pub async fn $async_name(component: &impl Class) -> Result<(), TempoError> {
            let (actor, name) = component_names(component.as_object(), "component")?;
            crate::tempo_world::$async_name(actor, name).await?;
            Ok(())
        }
    };
}

component_call!(
    /// `tempo_world::destroy_component`, given a handle.
    destroy_component, destroy_component_async);
component_call!(
    /// `tempo_world::activate_component`, given a handle.
    activate_component, activate_component_async);
component_call!(
    /// `tempo_world::deactivate_component`, given a handle.
    deactivate_component, deactivate_component_async);

/// `tempo_world::set_actor_transform`, given handles.
pub fn set_actor_transform(
    actor: &impl Class,
    transform: Transform,
    relative_to_actor: Option<&ObjectRef>,
    scale: Option<Vector>,
) -> Result<(), TempoError> {
    crate::tempo_world::set_actor_transform(
        actor_name(actor.as_object(), "actor")?,
        transform,
        optional_actor(relative_to_actor, "relative_to_actor")?,
        scale,
    )?;
    Ok(())
}

/// `tempo_world::set_actor_transform`, given handles, without blocking.
pub async fn set_actor_transform_async(
    actor: &impl Class,
    transform: Transform,
    relative_to_actor: Option<&ObjectRef>,
    scale: Option<Vector>,
) -> Result<(), TempoError> {
    crate::tempo_world::set_actor_transform_async(
        actor_name(actor.as_object(), "actor")?,
        transform,
        optional_actor(relative_to_actor, "relative_to_actor")?,
        scale,
    )
    .await?;
    Ok(())
}

macro_rules! actor_set {
    ($(#[$doc:meta])* $name:ident, $async_name:ident, $field:ident: $ty:ty) => {
        $(#[$doc])*
        pub fn $name(
            actor: &impl Class,
            $field: $ty,
            relative_to_actor: Option<&ObjectRef>,
        ) -> Result<(), TempoError> {
            crate::tempo_world::$name(
                actor_name(actor.as_object(), "actor")?,
                $field,
                optional_actor(relative_to_actor, "relative_to_actor")?,
            )?;
            Ok(())
        }

        $(#[$doc])*
        /// Without blocking.
        pub async fn $async_name(
            actor: &impl Class,
            $field: $ty,
            relative_to_actor: Option<&ObjectRef>,
        ) -> Result<(), TempoError> {
            crate::tempo_world::$async_name(
                actor_name(actor.as_object(), "actor")?,
                $field,
                optional_actor(relative_to_actor, "relative_to_actor")?,
            )
            .await?;
            Ok(())
        }
    };
}

actor_set!(
    /// `tempo_world::set_actor_location`, given handles.
    set_actor_location, set_actor_location_async, location: Vector);
actor_set!(
    /// `tempo_world::set_actor_rotation`, given handles.
    set_actor_rotation, set_actor_rotation_async, rotation: Rotation);

/// `tempo_world::set_actor_scale3d`, given a handle.
pub fn set_actor_scale3d(actor: &impl Class, scale: Vector) -> Result<(), TempoError> {
    crate::tempo_world::set_actor_scale3d(actor_name(actor.as_object(), "actor")?, scale)?;
    Ok(())
}

/// `tempo_world::set_actor_scale3d`, given a handle, without blocking.
pub async fn set_actor_scale3d_async(actor: &impl Class, scale: Vector) -> Result<(), TempoError> {
    crate::tempo_world::set_actor_scale3d_async(actor_name(actor.as_object(), "actor")?, scale)
        .await?;
    Ok(())
}

/// `tempo_world::set_component_transform`, given a handle.
pub fn set_component_transform(
    component: &impl Class,
    transform: Transform,
    relative_to_world: bool,
    scale: Option<Vector>,
) -> Result<(), TempoError> {
    let (actor, name) = component_names(component.as_object(), "component")?;
    crate::tempo_world::set_component_transform(actor, name, transform, relative_to_world, scale)?;
    Ok(())
}

/// `tempo_world::set_component_transform`, given a handle, without blocking.
pub async fn set_component_transform_async(
    component: &impl Class,
    transform: Transform,
    relative_to_world: bool,
    scale: Option<Vector>,
) -> Result<(), TempoError> {
    let (actor, name) = component_names(component.as_object(), "component")?;
    crate::tempo_world::set_component_transform_async(actor, name, transform, relative_to_world, scale)
        .await?;
    Ok(())
}

macro_rules! component_set {
    ($(#[$doc:meta])* $name:ident, $async_name:ident, $field:ident: $ty:ty) => {
        $(#[$doc])*
        pub fn $name(component: &impl Class, $field: $ty, relative_to_world: bool) -> Result<(), TempoError> {
            let (actor, name) = component_names(component.as_object(), "component")?;
            crate::tempo_world::$name(actor, name, $field, relative_to_world)?;
            Ok(())
        }

        $(#[$doc])*
        /// Without blocking.
        pub async fn $async_name(
            component: &impl Class,
            $field: $ty,
            relative_to_world: bool,
        ) -> Result<(), TempoError> {
            let (actor, name) = component_names(component.as_object(), "component")?;
            crate::tempo_world::$async_name(actor, name, $field, relative_to_world).await?;
            Ok(())
        }
    };
}

component_set!(
    /// `tempo_world::set_component_location`, given a handle.
    set_component_location, set_component_location_async, location: Vector);
component_set!(
    /// `tempo_world::set_component_rotation`, given a handle.
    set_component_rotation, set_component_rotation_async, rotation: Rotation);
component_set!(
    /// `tempo_world::set_component_scale3d`, given a handle.
    set_component_scale3d, set_component_scale3d_async, scale: Vector);

macro_rules! actor_get {
    ($(#[$doc:meta])* $name:ident, $async_name:ident, $response:ident) => {
        $(#[$doc])*
        pub fn $name(
            actor: &impl Class,
            relative_to_actor: Option<&ObjectRef>,
        ) -> Result<crate::proto::tempo_world::$response, TempoError> {
            crate::tempo_world::$name(
                actor_name(actor.as_object(), "actor")?,
                optional_actor(relative_to_actor, "relative_to_actor")?,
            )
        }

        $(#[$doc])*
        /// Without blocking.
        pub async fn $async_name(
            actor: &impl Class,
            relative_to_actor: Option<&ObjectRef>,
        ) -> Result<crate::proto::tempo_world::$response, TempoError> {
            crate::tempo_world::$async_name(
                actor_name(actor.as_object(), "actor")?,
                optional_actor(relative_to_actor, "relative_to_actor")?,
            )
            .await
        }
    };
}

actor_get!(
    /// `tempo_world::get_actor_transform`, given handles.
    get_actor_transform, get_actor_transform_async, GetTransformResponse);
actor_get!(
    /// `tempo_world::get_actor_location`, given handles.
    get_actor_location, get_actor_location_async, GetLocationResponse);
actor_get!(
    /// `tempo_world::get_actor_rotation`, given handles.
    get_actor_rotation, get_actor_rotation_async, GetRotationResponse);

/// `tempo_world::get_actor_scale3d`, given a handle.
pub fn get_actor_scale3d(
    actor: &impl Class,
) -> Result<crate::proto::tempo_world::GetScale3DResponse, TempoError> {
    crate::tempo_world::get_actor_scale3d(actor_name(actor.as_object(), "actor")?)
}

/// `tempo_world::get_actor_scale3d`, given a handle, without blocking.
pub async fn get_actor_scale3d_async(
    actor: &impl Class,
) -> Result<crate::proto::tempo_world::GetScale3DResponse, TempoError> {
    crate::tempo_world::get_actor_scale3d_async(actor_name(actor.as_object(), "actor")?).await
}

macro_rules! component_get {
    ($(#[$doc:meta])* $name:ident, $async_name:ident, $response:ident) => {
        $(#[$doc])*
        /// `frame` is a `ComponentFrame`.
        pub fn $name(
            component: &impl Class,
            frame: crate::proto::tempo_world::ComponentFrame,
            relative_to_component: Option<&ObjectRef>,
            socket: &str,
        ) -> Result<crate::proto::tempo_world::$response, TempoError> {
            let (actor, name) = component_names(component.as_object(), "component")?;
            crate::tempo_world::$name(
                actor,
                name,
                frame as i32,
                optional_component(relative_to_component, "relative_to_component")?,
                socket.to_string(),
            )
        }

        $(#[$doc])*
        /// Without blocking.
        pub async fn $async_name(
            component: &impl Class,
            frame: crate::proto::tempo_world::ComponentFrame,
            relative_to_component: Option<&ObjectRef>,
            socket: &str,
        ) -> Result<crate::proto::tempo_world::$response, TempoError> {
            let (actor, name) = component_names(component.as_object(), "component")?;
            crate::tempo_world::$async_name(
                actor,
                name,
                frame as i32,
                optional_component(relative_to_component, "relative_to_component")?,
                socket.to_string(),
            )
            .await
        }
    };
}

component_get!(
    /// `tempo_world::get_component_transform`, given handles.
    get_component_transform, get_component_transform_async, GetTransformResponse);
component_get!(
    /// `tempo_world::get_component_location`, given handles.
    get_component_location, get_component_location_async, GetLocationResponse);
component_get!(
    /// `tempo_world::get_component_rotation`, given handles.
    get_component_rotation, get_component_rotation_async, GetRotationResponse);
component_get!(
    /// `tempo_world::get_component_scale3d`, given handles.
    get_component_scale3d, get_component_scale3d_async, GetScale3DResponse);

/// `tempo_world::get_current_actor_state`, given a handle.
pub fn get_current_actor_state(
    actor: &impl Class,
    include_hidden_components: bool,
) -> Result<crate::proto::tempo_world::ActorState, TempoError> {
    crate::tempo_world::get_current_actor_state(
        actor_name(actor.as_object(), "actor")?,
        include_hidden_components,
    )
}

/// `tempo_world::get_current_actor_state`, given a handle, without blocking.
pub async fn get_current_actor_state_async(
    actor: &impl Class,
    include_hidden_components: bool,
) -> Result<crate::proto::tempo_world::ActorState, TempoError> {
    crate::tempo_world::get_current_actor_state_async(
        actor_name(actor.as_object(), "actor")?,
        include_hidden_components,
    )
    .await
}

/// `tempo_world::stream_actor_state`, given a handle: an `ActorState` per tick.
pub fn stream_actor_state(
    actor: &impl Class,
    include_hidden_components: bool,
) -> Result<
    impl Iterator<Item = Result<crate::proto::tempo_world::ActorState, tonic::Status>>,
    TempoError,
> {
    crate::tempo_world::stream_actor_state(
        actor_name(actor.as_object(), "actor")?,
        include_hidden_components,
    )
}

/// `tempo_world::stream_actor_state`, given a handle, without blocking: a `tonic::Streaming` of
/// `ActorState`s, one per tick, read with `.message().await`.
pub async fn stream_actor_state_async(
    actor: &impl Class,
    include_hidden_components: bool,
) -> Result<tonic::Streaming<crate::proto::tempo_world::ActorState>, TempoError> {
    crate::tempo_world::stream_actor_state_async(
        actor_name(actor.as_object(), "actor")?,
        include_hidden_components,
    )
    .await
}

/// `tempo_world::get_current_actor_states_near`, given a handle.
pub fn get_current_actor_states_near(
    near_actor: &impl Class,
    search_radius_m: f32,
    include_static: bool,
    include_hidden_actors: bool,
    include_hidden_components: bool,
) -> Result<crate::proto::tempo_world::ActorStates, TempoError> {
    crate::tempo_world::get_current_actor_states_near(
        actor_name(near_actor.as_object(), "near_actor")?,
        search_radius_m,
        include_static,
        include_hidden_actors,
        include_hidden_components,
    )
}

/// `tempo_world::get_current_actor_states_near`, given a handle, without blocking.
pub async fn get_current_actor_states_near_async(
    near_actor: &impl Class,
    search_radius_m: f32,
    include_static: bool,
    include_hidden_actors: bool,
    include_hidden_components: bool,
) -> Result<crate::proto::tempo_world::ActorStates, TempoError> {
    crate::tempo_world::get_current_actor_states_near_async(
        actor_name(near_actor.as_object(), "near_actor")?,
        search_radius_m,
        include_static,
        include_hidden_actors,
        include_hidden_components,
    )
    .await
}

/// `tempo_world::stream_actor_states_near`, given a handle: `ActorStates` per tick.
pub fn stream_actor_states_near(
    near_actor: &impl Class,
    search_radius_m: f32,
    include_static: bool,
    include_hidden_actors: bool,
    include_hidden_components: bool,
) -> Result<
    impl Iterator<Item = Result<crate::proto::tempo_world::ActorStates, tonic::Status>>,
    TempoError,
> {
    crate::tempo_world::stream_actor_states_near(
        actor_name(near_actor.as_object(), "near_actor")?,
        search_radius_m,
        include_static,
        include_hidden_actors,
        include_hidden_components,
    )
}

/// `tempo_world::stream_actor_states_near`, given a handle, without blocking: a `tonic::Streaming`
/// of `ActorStates`, one per tick.
pub async fn stream_actor_states_near_async(
    near_actor: &impl Class,
    search_radius_m: f32,
    include_static: bool,
    include_hidden_actors: bool,
    include_hidden_components: bool,
) -> Result<tonic::Streaming<crate::proto::tempo_world::ActorStates>, TempoError> {
    crate::tempo_world::stream_actor_states_near_async(
        actor_name(near_actor.as_object(), "near_actor")?,
        search_radius_m,
        include_static,
        include_hidden_actors,
        include_hidden_components,
    )
    .await
}

/// `tempo_world::stream_overlap_events`, given a handle: `OverlapEventResponse`s as they happen.
pub fn stream_overlap_events(
    actor: &impl Class,
) -> Result<
    impl Iterator<Item = Result<crate::proto::tempo_world::OverlapEventResponse, tonic::Status>>,
    TempoError,
> {
    crate::tempo_world::stream_overlap_events(actor_name(actor.as_object(), "actor")?)
}

/// `tempo_world::stream_overlap_events`, given a handle, without blocking: a `tonic::Streaming` of
/// `OverlapEventResponse`s as they happen.
pub async fn stream_overlap_events_async(
    actor: &impl Class,
) -> Result<tonic::Streaming<crate::proto::tempo_world::OverlapEventResponse>, TempoError> {
    crate::tempo_world::stream_overlap_events_async(actor_name(actor.as_object(), "actor")?).await
}

/// `tempo_world::raycast`, ignoring actors given as handles.
pub fn raycast(
    start: Vector,
    end: Vector,
    collision_channel: crate::proto::tempo_world::CollisionChannel,
    ignored_actors: &[&ObjectRef],
) -> Result<crate::proto::tempo_world::RaycastResponse, TempoError> {
    let ignored = ignored_actors
        .iter()
        .map(|actor| actor_name(actor, "ignored_actors"))
        .collect::<Result<Vec<_>, _>>()?;
    crate::tempo_world::raycast(start, end, collision_channel as i32, ignored)
}

/// `tempo_world::raycast`, ignoring actors given as handles, without blocking.
pub async fn raycast_async(
    start: Vector,
    end: Vector,
    collision_channel: crate::proto::tempo_world::CollisionChannel,
    ignored_actors: &[&ObjectRef],
) -> Result<crate::proto::tempo_world::RaycastResponse, TempoError> {
    let ignored = ignored_actors
        .iter()
        .map(|actor| actor_name(actor, "ignored_actors"))
        .collect::<Result<Vec<_>, _>>()?;
    crate::tempo_world::raycast_async(start, end, collision_channel as i32, ignored).await
}

/// Compare the linked generated classes against the sim's own reflection, class by class.
///
/// Classes generated from one build and used against another is silent wrongness: the compiler is
/// satisfied, and the first property access that disagrees fails for reasons that look like
/// anything but a version skew. A hash covers names, types, writability and signatures — not
/// documentation — so this answers "can these classes address this sim", not "is anything
/// different at all".
///
/// Compared per entry rather than per build, for two reasons. A crate may describe part of a build
/// by design: the published classes inside `tempo_sim` describe the modules every build ships (see
/// TYPED_WORLD_API_PLAN.md §16), so a whole-walk comparison would reject every one of them. And
/// for a whole-build crate it is simply a better report — one number says only that something
/// moved, where today a project that adds a single Blueprint invalidates every class it has.
///
/// Every linked crate is checked, because more than one can be linked and they describe different
/// things.
///
/// Not automatic on connect, and deliberately so: the sim computes its answer by walking
/// reflection, which is dominated by loading every Blueprint and takes the best part of a minute.
/// Paying that on every client start would be worse than the problem. Call it once from a test, or
/// at the start of a long run.
pub fn check_schema() -> Result<(), TempoError> {
    // These three options must match what `query_packaged_classes.py` asked when the linked
    // classes were stamped. The hashes cover function signatures, so a different request yields
    // different numbers for the same build.
    let reported = crate::tempo_world::describe_classes(Vec::new(), true, true, true)?;
    let assets = if has_catalog() {
        catalog(crate::tempo_world::get_assets(ANY_ASSET.to_string(), true, String::new())?)
    } else {
        Default::default()
    };
    compare(reported, assets)
}

/// Compare the linked generated classes against the sim's reflection, without blocking.
pub async fn check_schema_async() -> Result<(), TempoError> {
    let reported = crate::tempo_world::describe_classes_async(Vec::new(), true, true, true).await?;
    let assets = if has_catalog() {
        catalog(crate::tempo_world::get_assets_async(ANY_ASSET.to_string(), true, String::new()).await?)
    } else {
        Default::default()
    };
    compare(reported, assets)
}

/// The class every asset derives from, which asks the registry for all of them.
const ANY_ASSET: &str = "/Script/CoreUObject.Object";

/// Whether any linked crate has an asset catalog to compare, which is the only reason to ask the
/// sim for its assets.
fn has_catalog() -> bool {
    crate::unreal::hierarchies()
        .iter()
        .any(|hierarchy| !hierarchy.hashes(crate::unreal::EntryKind::Asset).is_empty())
}

/// The sim's assets as the catalog records them: path to class path.
fn catalog(
    response: crate::proto::tempo_world::GetAssetsResponse,
) -> std::collections::HashMap<String, String> {
    response.assets.into_iter().map(|asset| (asset.path, asset.class_path)).collect()
}

/// One entry the sim no longer agrees about.
struct Finding {
    kind: crate::unreal::EntryKind,
    name: String,
    /// False when the sim does not have this entry at all, rather than having a different one.
    present: bool,
}

impl std::fmt::Display for Finding {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{} {}", self.kind.label(), self.name)
    }
}

fn compare(
    reported: crate::proto::tempo_world::DescribeClassesResponse,
    reported_assets: std::collections::HashMap<String, String>,
) -> Result<(), TempoError> {
    use crate::unreal::EntryKind;

    let linked = crate::unreal::hierarchies();
    if linked.is_empty() {
        return Err(TempoError::SchemaMismatch(
            "no generated classes are linked, so there is nothing to check. Add this build's \
             project crate (e.g. `tempo-sample`, whose classes are its `reflected` module), which \
             ships in Packaged/API/Rust alongside the tempo-sim crate, and use its classes: a \
             dependency the program never names is not linked at all."
                .to_string(),
        ));
    }

    let header = reported.header.clone().unwrap_or_default();
    let classes: std::collections::HashMap<&str, &str> = reported
        .classes
        .iter()
        .map(|entry| (entry.class_path.as_str(), entry.content_hash.as_str()))
        .collect();
    let structs: std::collections::HashMap<&str, &str> = reported
        .structs
        .iter()
        .map(|entry| (entry.name.as_str(), entry.content_hash.as_str()))
        .collect();
    let enums: std::collections::HashMap<&str, &str> = reported
        .enums
        .iter()
        .map(|entry| (entry.name.as_str(), entry.content_hash.as_str()))
        .collect();

    let assets: std::collections::HashMap<&str, &str> = reported_assets
        .iter()
        .map(|(path, class)| (path.as_str(), class.as_str()))
        .collect();

    let mut findings: Vec<Finding> = Vec::new();
    for hierarchy in &linked {
        for (kind, current) in [
            (EntryKind::Class, &classes),
            (EntryKind::Struct, &structs),
            (EntryKind::Enum, &enums),
            (EntryKind::Asset, &assets),
        ] {
            for (name, linked_hash) in hierarchy.hashes(kind) {
                match current.get(*name) {
                    // An entry the sim has and the crate does not is deliberately not a finding: a
                    // published crate holds the modules every build ships and nothing else, so
                    // that is its normal condition rather than a problem with it.
                    None => findings.push(Finding {
                        kind,
                        name: (*name).to_string(),
                        present: false,
                    }),
                    Some(reported_hash) if !linked_hash.is_empty()
                        && reported_hash != linked_hash =>
                    {
                        findings.push(Finding {
                            kind,
                            name: (*name).to_string(),
                            present: true,
                        })
                    }
                    Some(_) => {}
                }
            }
        }
    }

    if findings.is_empty() {
        return Ok(());
    }

    let changed: Vec<&Finding> = findings.iter().filter(|f| f.present).collect();
    let absent: Vec<&Finding> = findings.iter().filter(|f| !f.present).collect();
    let mut detail = String::new();
    if !changed.is_empty() {
        detail.push_str(&format!("\n  {} changed ({})", changed.len(), examples(&changed)));
    }
    if !absent.is_empty() {
        detail.push_str(&format!(
            "\n  {} not in this sim ({})",
            absent.len(),
            examples(&absent)
        ));
    }

    Err(TempoError::SchemaMismatch(format!(
        "the linked generated classes do not match this sim.{}\n  \
         the sim reports: engine {}, schema {}\n\
         Regenerate the client against this build, or connect to the build the client came from.",
        detail, header.engine_version, header.schema_version
    )))
}

/// A few findings by name. Enough to recognise the change; the count carries the scale.
fn examples(findings: &[&Finding]) -> String {
    const LIMIT: usize = 5;
    let shown = findings
        .iter()
        .take(LIMIT)
        .map(|finding| finding.to_string())
        .collect::<Vec<_>>()
        .join(", ");
    if findings.len() > LIMIT {
        format!("{shown}, ...")
    } else {
        shown
    }
}

#[cfg(test)]
mod tests {
    //! What `check_schema` makes of a sim's answer.
    //!
    //! The live case is covered by the `typed` test group, which calls `check_schema` against the
    //! build its classes came from. These are the cases a live sim cannot produce on demand: a
    //! class whose hash moved, a class the sim does not have, and a class only the sim has.
    //!
    //! `tempo-sim` has no generated class crate linked, so the registry is empty here and a fake
    //! hierarchy can own it.

    use super::*;
    use crate::proto::tempo_world as pb;
    use crate::unreal::EntryKind;

    const WIDGET: &str = "/Script/Engine.Widget";

    // Not zero-sized, as a registered table must not be.
    struct Fake(#[allow(dead_code)] u8);

    static CLASSES: &[(&str, &str)] = &[(WIDGET, "WIDGET-HASH")];
    static STRUCTS: &[(&str, &str)] = &[("FWidgetSettings", "SETTINGS-HASH")];
    static ENUMS: &[(&str, &str)] = &[("EWidgetMode", "MODE-HASH")];

    impl crate::unreal::Hierarchy for Fake {
        fn superclass(&self, _class_path: &str) -> Option<&'static str> {
            None
        }

        fn header(&self) -> crate::unreal::SchemaIdentity {
            crate::unreal::SchemaIdentity {
                schema_version: "3".to_string(),
                engine_version: "5.8".to_string(),
                content_hash: "WHOLE-WALK".to_string(),
            }
        }

        fn hashes(&self, kind: EntryKind) -> &'static [(&'static str, &'static str)] {
            match kind {
                EntryKind::Class => CLASSES,
                EntryKind::Struct => STRUCTS,
                EntryKind::Enum => ENUMS,
                EntryKind::Asset => &[],
            }
        }

        fn content_hash(&self, kind: EntryKind, name: &str) -> Option<&'static str> {
            self.hashes(kind)
                .iter()
                .find(|(key, _)| *key == name)
                .map(|(_, hash)| *hash)
        }
    }

    static FAKE: Fake = Fake(0);

    /// Register the fake once, however many tests run. Registration is process-wide, and the
    /// registry already refuses a table it holds, so this only has to happen before the first
    /// comparison.
    fn linked() {
        crate::unreal::register_classes(&FAKE);
    }

    fn response(classes: &[(&str, &str)]) -> pb::DescribeClassesResponse {
        pb::DescribeClassesResponse {
            header: Some(pb::SchemaHeader {
                schema_version: "3".to_string(),
                engine_version: "5.8".to_string(),
                ..Default::default()
            }),
            classes: classes
                .iter()
                .map(|(path, hash)| pb::ClassDescriptor {
                    class_path: (*path).to_string(),
                    content_hash: (*hash).to_string(),
                    ..Default::default()
                })
                .collect(),
            structs: STRUCTS
                .iter()
                .map(|(name, hash)| pb::StructDescriptor {
                    name: (*name).to_string(),
                    content_hash: (*hash).to_string(),
                    ..Default::default()
                })
                .collect(),
            enums: ENUMS
                .iter()
                .map(|(name, hash)| pb::EnumDescriptor {
                    name: (*name).to_string(),
                    content_hash: (*hash).to_string(),
                    ..Default::default()
                })
                .collect(),
        }
    }

    fn message(result: Result<(), TempoError>) -> String {
        match result {
            Err(TempoError::SchemaMismatch(message)) => message,
            other => panic!("expected a schema mismatch, got {other:?}"),
        }
    }

    #[test]
    fn matching_hashes_pass() {
        linked();
        compare(response(CLASSES), Default::default()).expect("the same hashes should match");
    }

    #[test]
    fn a_changed_class_is_named() {
        linked();
        let text = message(compare(response(&[(WIDGET, "SOMETHING-ELSE")]), Default::default()));
        assert!(text.contains("1 changed"), "{text}");
        assert!(text.contains(WIDGET), "{text}");
    }

    #[test]
    fn a_class_the_sim_does_not_have_is_a_different_finding() {
        linked();
        let text = message(compare(response(&[]), Default::default()));
        assert!(text.contains("1 not in this sim"), "{text}");
        assert!(text.contains(WIDGET), "{text}");
    }

    #[test]
    fn a_class_only_the_sim_has_is_not_a_finding() {
        // The normal condition of a published crate, which describes part of a build by design --
        // and the case a whole-walk hash cannot express, since every entry the crate holds is
        // current while the one number still differs.
        linked();
        let mut reported = Vec::from(CLASSES);
        reported.push(("/Game/Blueprints/BP_Thing.BP_Thing_C", "A-PROJECT-CLASS"));
        compare(response(&reported), Default::default()).expect("a class only the sim has is not a problem");
    }
}
