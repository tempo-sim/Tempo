// Copyright Tempo Simulation, LLC. All Rights Reserved

//! Entry points for the typed World API: how a scenario gets a handle it can address by method.
//!
//! The form to reach for is discovery by type:
//!
//! ```no_run
//! use tempo_sim::world;
//! # mod my_project_classes { pub mod game { pub mod vehicles {
//! #     pub struct BP_Cat745_C(pub tempo_sim::unreal::ObjectRef);
//! #     unsafe impl tempo_sim::unreal::Class for BP_Cat745_C {
//! #         const PATH: &'static str = "/Game/Vehicles/BP_Cat745.BP_Cat745_C";
//! #         fn from_object(o: tempo_sim::unreal::ObjectRef) -> Self { Self(o) }
//! #         fn as_object(&self) -> &tempo_sim::unreal::ObjectRef { &self.0 }
//! #         fn hierarchy() -> &'static (dyn tempo_sim::unreal::Hierarchy + 'static) { &T }
//! #     }
//! # struct Table;
//! # impl tempo_sim::unreal::Hierarchy for Table {
//! #     fn superclass(&self, _: &str) -> Option<&str> { None }
//! #     fn header(&self) -> tempo_sim::unreal::SchemaIdentity { Default::default() }
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
use crate::unreal::{note_origin, Class, ObjectRef, Transform};

pub use crate::unreal::batch;

/// A `Transform` as `SpawnActor` takes it.
///
/// `SpawnActor` converts: it reads meters, radians and right-handed, unlike the typed property
/// API, which is Unreal-native throughout. So a native-unit `Transform` has to be converted on the
/// way in, rather than passed through.
fn spawn_transform(transform: Option<Transform>) -> crate::proto::tempo_core::Transform {
    let transform = transform.unwrap_or_default();
    crate::proto::tempo_core::Transform {
        // cm -> m, and Unreal's left-handed Y is the API's right-handed -Y.
        location: Some(crate::proto::tempo_core::Vector {
            x: transform.location.x / 100.0,
            y: -transform.location.y / 100.0,
            z: transform.location.z / 100.0,
        }),
        rotation: Some(crate::proto::tempo_core::Rotation {
            r: transform.rotation.roll.to_radians(),
            p: -transform.rotation.pitch.to_radians(),
            y: -transform.rotation.yaw.to_radians(),
        }),
    }
}

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

/// Spawn one actor of a class and return a handle to it.
///
/// Typed for free: the RPC already takes the class, so there is nothing to infer.
pub fn spawn_actor<T: Class>(
    transform: Option<Transform>,
    relative_to: &str,
) -> Result<T, TempoError> {
    note_origin::<T>();
    let response = crate::tempo_world::spawn_actor(
        T::PATH.to_string(),
        false,
        spawn_transform(transform),
        relative_to.to_string(),
    )?;
    Ok(T::from_object(ObjectRef::actor(response.name).with_class(T::PATH)))
}

/// Spawn one actor of a class, without blocking.
pub async fn spawn_actor_async<T: Class>(
    transform: Option<Transform>,
    relative_to: &str,
) -> Result<T, TempoError> {
    note_origin::<T>();
    let response = crate::tempo_world::spawn_actor_async(
        T::PATH.to_string(),
        false,
        spawn_transform(transform),
        relative_to.to_string(),
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
/// # struct BP_Cat745_C(tempo_sim::unreal::ObjectRef);
/// # unsafe impl tempo_sim::unreal::Class for BP_Cat745_C {
/// #     const PATH: &'static str = "/Game/Vehicles/BP_Cat745.BP_Cat745_C";
/// #     fn from_object(o: tempo_sim::unreal::ObjectRef) -> Self { Self(o) }
/// #     fn as_object(&self) -> &tempo_sim::unreal::ObjectRef { &self.0 }
/// #     fn hierarchy() -> &'static (dyn tempo_sim::unreal::Hierarchy + 'static) { &T }
/// # }
/// # struct Table;
/// # impl tempo_sim::unreal::Hierarchy for Table {
/// #     fn superclass(&self, _: &str) -> Option<&str> { None }
/// #     fn header(&self) -> tempo_sim::unreal::SchemaIdentity { Default::default() }
/// # }
/// # static T: Table = Table;
/// # impl BP_Cat745_C {
/// #     fn set_initial_terrain_height(&self, v: f64) -> Result<(), tempo_sim::TempoError> {
/// #         self.0.set("InitialTerrainHeight", v)
/// #     }
/// # }
/// # fn main() -> Result<(), tempo_sim::TempoError> {
/// let truck = world::spawn_actor_deferred::<BP_Cat745_C, _>(None, "", |truck| {
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
    relative_to: &str,
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
        spawn_transform(transform),
        relative_to.to_string(),
    )?;
    let handle = T::from_object(ObjectRef::actor(&response.name).with_class(T::PATH));

    let configured = configure(&handle);
    let finished = crate::tempo_world::finish_spawning_actor(response.name);

    configured?;
    finished?;
    Ok(handle)
}

/// Compare the linked generated classes against the sim's own reflection.
///
/// Classes generated from one build and used against another is silent wrongness: the compiler is
/// satisfied, and the first property access that disagrees fails for reasons that look like
/// anything but a version skew. The content hash covers names, types and writability — not
/// documentation — so this answers "can these classes address this sim", not "is anything
/// different at all".
///
/// Not automatic on connect, and deliberately so: the sim computes its hash by walking reflection,
/// which is dominated by loading every Blueprint and takes the best part of a minute. Paying that
/// on every client start would be worse than the problem. Call it once from a test, or at the
/// start of a long run.
pub fn check_schema() -> Result<(), TempoError> {
    // These three options must match what `query_packaged_classes.py` asked when the linked
    // classes were stamped. The hash covers function signatures, so a different request yields a
    // different number for the same build.
    let reported = crate::tempo_world::describe_classes(Vec::new(), true, true, true)?;
    compare(reported)
}

/// Compare the linked generated classes against the sim's reflection, without blocking.
pub async fn check_schema_async() -> Result<(), TempoError> {
    let reported = crate::tempo_world::describe_classes_async(Vec::new(), true, true, true).await?;
    compare(reported)
}

fn compare(
    reported: crate::proto::tempo_world::DescribeClassesResponse,
) -> Result<(), TempoError> {
    let installed = crate::unreal::hierarchy().header();
    if installed.content_hash.is_empty() {
        return Err(TempoError::SchemaMismatch(
            "no generated classes are linked, so there is nothing to check. Add this build's \
             generated class crate (e.g. `tempo-sample-classes`), which ships in \
             Packaged/API/Rust alongside the tempo-sim crate."
                .to_string(),
        ));
    }

    let header = reported.header.unwrap_or_default();
    if header.content_hash == installed.content_hash {
        return Ok(());
    }

    Err(TempoError::SchemaMismatch(format!(
        "the linked generated classes do not match this sim.\n  \
         classes were generated from: engine {}, schema {}, hash {}\n  \
         the sim reports:             engine {}, schema {}, hash {}\n\
         Regenerate the client against this build, or connect to the build the client came from.",
        installed.engine_version,
        installed.schema_version,
        installed.content_hash,
        header.engine_version,
        header.schema_version,
        header.content_hash
    )))
}
