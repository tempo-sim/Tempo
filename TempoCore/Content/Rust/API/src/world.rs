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
    compare(reported)
}

/// Compare the linked generated classes against the sim's reflection, without blocking.
pub async fn check_schema_async() -> Result<(), TempoError> {
    let reported = crate::tempo_world::describe_classes_async(Vec::new(), true, true, true).await?;
    compare(reported)
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
) -> Result<(), TempoError> {
    use crate::unreal::EntryKind;

    let linked = crate::unreal::hierarchies();
    if linked.is_empty() {
        return Err(TempoError::SchemaMismatch(
            "no generated classes are linked, so there is nothing to check. Add this build's \
             generated class crate (e.g. `tempo-sample-classes`), which ships in \
             Packaged/API/Rust alongside the tempo-sim crate."
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

    let mut findings: Vec<Finding> = Vec::new();
    for hierarchy in &linked {
        for (kind, current) in [
            (EntryKind::Class, &classes),
            (EntryKind::Struct, &structs),
            (EntryKind::Enum, &enums),
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

    struct Fake;

    static CLASSES: &[(&str, &str)] = &[(WIDGET, "WIDGET-HASH")];
    static STRUCTS: &[(&str, &str)] = &[("FWidgetSettings", "SETTINGS-HASH")];
    static ENUMS: &[(&str, &str)] = &[("EWidgetMode", "MODE-HASH")];

    impl crate::unreal::Hierarchy for Fake {
        fn superclass(&self, _class_path: &str) -> Option<&'static str> {
            None
        }

        fn header(&self) -> crate::unreal::SchemaIdentity {
            crate::unreal::SchemaIdentity {
                schema_version: "2".to_string(),
                engine_version: "5.8".to_string(),
                content_hash: "WHOLE-WALK".to_string(),
            }
        }

        fn hashes(&self, kind: EntryKind) -> &'static [(&'static str, &'static str)] {
            match kind {
                EntryKind::Class => CLASSES,
                EntryKind::Struct => STRUCTS,
                EntryKind::Enum => ENUMS,
            }
        }

        fn content_hash(&self, kind: EntryKind, name: &str) -> Option<&'static str> {
            self.hashes(kind)
                .iter()
                .find(|(key, _)| *key == name)
                .map(|(_, hash)| *hash)
        }
    }

    static FAKE: Fake = Fake;

    /// Register the fake once, however many tests run. Registration is process-wide, and the
    /// registry already refuses a table it holds, so this only has to happen before the first
    /// comparison.
    fn linked() {
        crate::unreal::register_classes(&FAKE);
    }

    fn response(classes: &[(&str, &str)]) -> pb::DescribeClassesResponse {
        pb::DescribeClassesResponse {
            header: Some(pb::SchemaHeader {
                schema_version: "2".to_string(),
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
        compare(response(CLASSES)).expect("the same hashes should match");
    }

    #[test]
    fn a_changed_class_is_named() {
        linked();
        let text = message(compare(response(&[(WIDGET, "SOMETHING-ELSE")])));
        assert!(text.contains("1 changed"), "{text}");
        assert!(text.contains(WIDGET), "{text}");
    }

    #[test]
    fn a_class_the_sim_does_not_have_is_a_different_finding() {
        linked();
        let text = message(compare(response(&[])));
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
        compare(response(&reported)).expect("a class only the sim has is not a problem");
    }
}
