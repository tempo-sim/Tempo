// Copyright Tempo Simulation, LLC. All Rights Reserved

//! The hand-written base of the typed World API: the handle every generated class wraps, the
//! value types that carry Unreal's structs, and the machinery that turns an attribute-shaped
//! call into a `TempoWorld` RPC.
//!
//! Everything in this module is in Unreal's own units — centimeters, degrees, left-handed — which
//! is the one place in the Tempo API where that is true. Raw `UPROPERTY` values carry no semantic
//! tagging, so converting them would be guesswork; the typed API therefore does not convert at all
//! and says so here rather than per property.
//!
//! **`Vector` here is not [`crate::proto::tempo_core::Vector`].** This one is centimeters and
//! left-handed, as Unreal stores it. That one is meters and right-handed, as every other Tempo RPC
//! speaks. They are not interchangeable; convert explicitly.
//!
//! Unlike the Python runtime, nothing here consults a metadata table. A generated Rust method
//! already knows its property's type, its `Value` variant and its `BlueprintSetter`, because all
//! three were decided when it was generated — so the lookups `tempo_sim.unreal` does at runtime
//! happen here at compile time instead, and a wrong one is a generator bug rather than a
//! `TempoError`.

use std::cell::RefCell;
use std::future::Future;

use crate::error::TempoError;
use crate::proto::tempo_world as pb;

// -- value types ---------------------------------------------------------------------------------

/// A location or direction in centimeters, left-handed.
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Vector {
    pub x: f64,
    pub y: f64,
    pub z: f64,
}

#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Vector2D {
    pub x: f64,
    pub y: f64,
}

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct IntVector {
    pub x: i32,
    pub y: i32,
    pub z: i32,
}

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct IntPoint {
    pub x: i32,
    pub y: i32,
}

/// An orientation in degrees, left-handed.
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Rotator {
    pub roll: f64,
    pub pitch: f64,
    pub yaw: f64,
}

/// A rotation, left-handed.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Quat {
    pub x: f64,
    pub y: f64,
    pub z: f64,
    pub w: f64,
}

impl Default for Quat {
    fn default() -> Self {
        Self { x: 0.0, y: 0.0, z: 0.0, w: 1.0 }
    }
}

/// A pose: centimeters and degrees, left-handed.
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Transform {
    pub location: Vector,
    pub rotation: Rotator,
}

/// 8-bit RGB.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct Color {
    pub r: u8,
    pub g: u8,
    pub b: u8,
}

// -- references ----------------------------------------------------------------------------------
//
// A reference-valued property travels as a name, and the name alone does not say what kind of
// thing it names -- a class, an asset, an actor and a component are four different `Value`
// variants carrying four strings. So each is its own Rust type, which is what lets the generated
// code pick the variant by passing a value rather than by naming one.

/// A reference to an Unreal class, by path (`/Script/Engine.StaticMeshActor`) or by name.
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct ClassRef(pub String);

/// A reference to an asset, by path or by name.
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct AssetRef(pub String);

impl ClassRef {
    /// The class a generated type stands for.
    pub fn of<T: Class>() -> Self {
        Self(T::PATH.to_string())
    }
}

macro_rules! string_ref {
    ($($t:ty),*) => { $(
        impl $t {
            pub fn new(name: impl Into<String>) -> Self { Self(name.into()) }
            pub fn as_str(&self) -> &str { &self.0 }
        }
        impl From<&str> for $t { fn from(name: &str) -> Self { Self(name.to_string()) } }
        impl From<String> for $t { fn from(name: String) -> Self { Self(name) } }
    )* };
}
string_ref!(ClassRef, AssetRef);

// -- the handle ----------------------------------------------------------------------------------

/// What a handle addresses, and the class it was found to have.
///
/// Addressed the way every `TempoWorld` RPC addresses things: an actor name, and a component name
/// when the handle is to a component rather than to the actor itself.
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct ObjectRef {
    actor: String,
    component: String,
    class_path: String,
}

impl ObjectRef {
    /// A handle to an actor.
    pub fn actor(name: impl Into<String>) -> Self {
        Self { actor: name.into(), component: String::new(), class_path: String::new() }
    }

    /// A handle to one of an actor's components.
    pub fn component(actor: impl Into<String>, component: impl Into<String>) -> Self {
        Self { actor: actor.into(), component: component.into(), class_path: String::new() }
    }

    /// The same handle, recording the class the thing really has.
    ///
    /// A query for a base class hands back handles typed as that base — which is what lets the
    /// call site type-check — so the handle's own Rust type is not the answer to what it points
    /// at. [`ObjectRef::is_a`] needs this to tell the difference.
    pub fn with_class(mut self, class_path: impl Into<String>) -> Self {
        self.class_path = class_path.into();
        self
    }

    /// The name of the actor this handle addresses.
    pub fn actor_name(&self) -> &str {
        &self.actor
    }

    /// The name of the component this handle addresses, or `""` for the actor itself.
    pub fn component_name(&self) -> &str {
        &self.component
    }

    /// The class the thing was found to have, or `""` when whoever built the handle did not know.
    pub fn class_path(&self) -> &str {
        &self.class_path
    }

    /// How a reference-valued property names this.
    pub fn reference_name(&self) -> String {
        if self.component.is_empty() {
            self.actor.clone()
        } else {
            format!("{}:{}", self.actor, self.component)
        }
    }

    /// Parse the name a reference-valued property carried back.
    fn from_reference_name(name: &str) -> Self {
        match name.split_once(':') {
            Some((actor, component)) => Self::component(actor, component),
            None => Self::actor(name),
        }
    }
}

/// Class identity for a generated type.
///
/// # Safety
///
/// Every implementor must be a `#[repr(transparent)]` newtype whose only field is an
/// [`ObjectRef`]. [`upcast`] reinterprets a reference on that basis, which is how a generated
/// class reaches its superclass's members. Only `gen_rust_classes.py` implements this.
pub unsafe trait Class: Sized {
    /// The Unreal class path this type stands for — a path rather than a name, because two
    /// Blueprints in different folders can share a name.
    const PATH: &'static str;

    fn from_object(object: ObjectRef) -> Self;

    fn as_object(&self) -> &ObjectRef;
}

/// View an object as one of its superclasses.
///
/// This is the whole of the `unsafe` in the typed Rust surface. Generated code calls it from its
/// `Deref` impl, so that `lidar.relative_location()` reaches `SceneComponent` however many classes
/// lie between them.
#[inline]
pub fn upcast<T: Class>(object: &ObjectRef) -> &T {
    // SAFETY: by the `Class` contract, `T` is a `#[repr(transparent)]` newtype over `ObjectRef`,
    // so it has the same size, alignment and address as the value behind this reference.
    unsafe { &*(object as *const ObjectRef as *const T) }
}

/// An Unreal enum, which travels as its authored name rather than its numeric value.
pub trait UnrealEnum: Sized {
    /// The enumerator's name as Unreal declares it.
    fn unreal_name(&self) -> &'static str;

    fn from_unreal_name(name: &str) -> Result<Self, TempoError>;
}

// -- Value conversion ----------------------------------------------------------------------------

/// A Rust type one `Value` variant can be read into.
pub trait FromValue: Sized {
    fn from_value(value: &pb::Value) -> Result<Self, TempoError>;
}

/// A Rust type that can be written into the `Value` variant its property expects.
pub trait ToValue {
    fn to_value(&self) -> pb::Value;
}

fn variant(value: &pb::Value) -> Result<&pb::value::Value, TempoError> {
    value
        .value
        .as_ref()
        .ok_or_else(|| TempoError::Protocol("the server sent a Value carrying nothing".into()))
}

fn wrong(value: &pb::Value, wanted: &str) -> TempoError {
    TempoError::Protocol(format!(
        "expected {} from the server, got {}",
        wanted,
        match &value.value {
            None => "nothing".to_string(),
            Some(other) => format!("{:?}", std::mem::discriminant(other)),
        }
    ))
}

macro_rules! scalar_value {
    ($($rust:ty => $case:ident, $wanted:literal;)*) => { $(
        impl FromValue for $rust {
            fn from_value(value: &pb::Value) -> Result<Self, TempoError> {
                match variant(value)? {
                    pb::value::Value::$case(inner) => Ok(inner.clone()),
                    _ => Err(wrong(value, $wanted)),
                }
            }
        }
        impl ToValue for $rust {
            fn to_value(&self) -> pb::Value {
                pb::Value { value: Some(pb::value::Value::$case(self.clone())) }
            }
        }
    )* };
}

scalar_value! {
    bool => BoolValue, "a bool";
    i32 => IntValue, "an int";
    i64 => Int64Value, "an int64";
    String => StringValue, "a string";
}

// Unreal's float and double properties both travel in `float_value`, which is 32-bit on the wire.
// The typed surface says `f64` for both, because that is what Unreal stores and what every other
// value type here uses; the narrowing is the wire's, not this crate's.
impl FromValue for f64 {
    fn from_value(value: &pb::Value) -> Result<Self, TempoError> {
        match variant(value)? {
            pb::value::Value::FloatValue(inner) => Ok(*inner as f64),
            _ => Err(wrong(value, "a float")),
        }
    }
}

impl ToValue for f64 {
    fn to_value(&self) -> pb::Value {
        pb::Value { value: Some(pb::value::Value::FloatValue(*self as f32)) }
    }
}

impl FromValue for Vector {
    fn from_value(value: &pb::Value) -> Result<Self, TempoError> {
        match variant(value)? {
            pb::value::Value::VectorValue(v) => Ok(Vector { x: v.x, y: v.y, z: v.z }),
            _ => Err(wrong(value, "a vector")),
        }
    }
}

impl ToValue for Vector {
    fn to_value(&self) -> pb::Value {
        pb::Value {
            value: Some(pb::value::Value::VectorValue(crate::proto::tempo_core::Vector {
                x: self.x,
                y: self.y,
                z: self.z,
            })),
        }
    }
}

impl FromValue for Vector2D {
    fn from_value(value: &pb::Value) -> Result<Self, TempoError> {
        match variant(value)? {
            pb::value::Value::Vector2dValue(v) => Ok(Vector2D { x: v.x, y: v.y }),
            _ => Err(wrong(value, "a 2D vector")),
        }
    }
}

impl ToValue for Vector2D {
    fn to_value(&self) -> pb::Value {
        pb::Value {
            value: Some(pb::value::Value::Vector2dValue(crate::proto::tempo_core::Vector2D {
                x: self.x,
                y: self.y,
            })),
        }
    }
}

impl FromValue for IntVector {
    fn from_value(value: &pb::Value) -> Result<Self, TempoError> {
        match variant(value)? {
            pb::value::Value::IntVectorValue(v) => Ok(IntVector { x: v.x, y: v.y, z: v.z }),
            _ => Err(wrong(value, "an int vector")),
        }
    }
}

impl ToValue for IntVector {
    fn to_value(&self) -> pb::Value {
        pb::Value {
            value: Some(pb::value::Value::IntVectorValue(pb::IntVector {
                x: self.x,
                y: self.y,
                z: self.z,
            })),
        }
    }
}

impl FromValue for IntPoint {
    fn from_value(value: &pb::Value) -> Result<Self, TempoError> {
        match variant(value)? {
            pb::value::Value::IntPointValue(v) => Ok(IntPoint { x: v.x, y: v.y }),
            _ => Err(wrong(value, "an int point")),
        }
    }
}

impl ToValue for IntPoint {
    fn to_value(&self) -> pb::Value {
        pb::Value {
            value: Some(pb::value::Value::IntPointValue(pb::IntPoint { x: self.x, y: self.y })),
        }
    }
}

// Carried in a message whose fields are named for radians and right-handed degrees, because that
// is what every other RPC puts there. With `native_units` set the server does not convert, so
// these are Unreal's own degrees and r/p/y map straight onto roll/pitch/yaw.
impl FromValue for Rotator {
    fn from_value(value: &pb::Value) -> Result<Self, TempoError> {
        match variant(value)? {
            pb::value::Value::RotatorValue(v) => {
                Ok(Rotator { roll: v.r, pitch: v.p, yaw: v.y })
            }
            _ => Err(wrong(value, "a rotator")),
        }
    }
}

impl ToValue for Rotator {
    fn to_value(&self) -> pb::Value {
        pb::Value {
            value: Some(pb::value::Value::RotatorValue(crate::proto::tempo_core::Rotation {
                r: self.roll,
                p: self.pitch,
                y: self.yaw,
            })),
        }
    }
}

impl FromValue for Quat {
    fn from_value(value: &pb::Value) -> Result<Self, TempoError> {
        match variant(value)? {
            pb::value::Value::QuatValue(v) => Ok(Quat {
                x: v.x as f64,
                y: v.y as f64,
                z: v.z as f64,
                w: v.w as f64,
            }),
            _ => Err(wrong(value, "a quaternion")),
        }
    }
}

impl ToValue for Quat {
    fn to_value(&self) -> pb::Value {
        pb::Value {
            value: Some(pb::value::Value::QuatValue(pb::Quat {
                x: self.x as f32,
                y: self.y as f32,
                z: self.z as f32,
                w: self.w as f32,
            })),
        }
    }
}

impl FromValue for Transform {
    fn from_value(value: &pb::Value) -> Result<Self, TempoError> {
        match variant(value)? {
            pb::value::Value::TransformValue(t) => Ok(Transform {
                location: t
                    .location
                    .as_ref()
                    .map(|l| Vector { x: l.x, y: l.y, z: l.z })
                    .unwrap_or_default(),
                rotation: t
                    .rotation
                    .as_ref()
                    .map(|r| Rotator { roll: r.r, pitch: r.p, yaw: r.y })
                    .unwrap_or_default(),
            }),
            _ => Err(wrong(value, "a transform")),
        }
    }
}

impl ToValue for Transform {
    fn to_value(&self) -> pb::Value {
        pb::Value {
            value: Some(pb::value::Value::TransformValue(crate::proto::tempo_core::Transform {
                location: Some(crate::proto::tempo_core::Vector {
                    x: self.location.x,
                    y: self.location.y,
                    z: self.location.z,
                }),
                rotation: Some(crate::proto::tempo_core::Rotation {
                    r: self.rotation.roll,
                    p: self.rotation.pitch,
                    y: self.rotation.yaw,
                }),
            })),
        }
    }
}

impl FromValue for Color {
    fn from_value(value: &pb::Value) -> Result<Self, TempoError> {
        match variant(value)? {
            pb::value::Value::ColorValue(c) => {
                Ok(Color { r: c.r as u8, g: c.g as u8, b: c.b as u8 })
            }
            _ => Err(wrong(value, "a color")),
        }
    }
}

impl ToValue for Color {
    fn to_value(&self) -> pb::Value {
        pb::Value {
            value: Some(pb::value::Value::ColorValue(pb::Color {
                r: self.r as i32,
                g: self.g as i32,
                b: self.b as i32,
            })),
        }
    }
}

impl FromValue for ClassRef {
    fn from_value(value: &pb::Value) -> Result<Self, TempoError> {
        match variant(value)? {
            pb::value::Value::ClassValue(name) => Ok(ClassRef(name.clone())),
            _ => Err(wrong(value, "a class reference")),
        }
    }
}

impl ToValue for ClassRef {
    fn to_value(&self) -> pb::Value {
        pb::Value { value: Some(pb::value::Value::ClassValue(self.0.clone())) }
    }
}

impl FromValue for AssetRef {
    fn from_value(value: &pb::Value) -> Result<Self, TempoError> {
        match variant(value)? {
            pb::value::Value::AssetValue(name) => Ok(AssetRef(name.clone())),
            _ => Err(wrong(value, "an asset reference")),
        }
    }
}

impl ToValue for AssetRef {
    fn to_value(&self) -> pb::Value {
        pb::Value { value: Some(pb::value::Value::AssetValue(self.0.clone())) }
    }
}

macro_rules! container_value {
    ($($case:ident, $wanted:literal, $message:ident, $element:ty;)*) => { $(
        impl FromValue for Vec<$element> {
            fn from_value(value: &pb::Value) -> Result<Self, TempoError> {
                match variant(value)? {
                    pb::value::Value::$case(inner) => Ok(inner.values.clone()),
                    _ => Err(wrong(value, $wanted)),
                }
            }
        }
        impl ToValue for Vec<$element> {
            fn to_value(&self) -> pb::Value {
                pb::Value {
                    value: Some(pb::value::Value::$case(pb::$message { values: self.clone() })),
                }
            }
        }
    )* };
}

// Arrays only. A `TSet` travels in its own variants, which carry the same payload, so the
// generated code reads a set with `get_set` rather than through a second `Vec` impl -- Rust cannot
// carry two `FromValue` impls for one type.
container_value! {
    BoolArrayValue, "a bool array", BoolArray, bool;
    IntArrayValue, "an int array", IntArray, i32;
    Int64ArrayValue, "an int64 array", Int64Array, i64;
    StringArrayValue, "a string array", StringArray, String;
}

impl FromValue for Vec<f64> {
    fn from_value(value: &pb::Value) -> Result<Self, TempoError> {
        match variant(value)? {
            pb::value::Value::FloatArrayValue(inner) => {
                Ok(inner.values.iter().map(|v| *v as f64).collect())
            }
            _ => Err(wrong(value, "a float array")),
        }
    }
}

impl ToValue for Vec<f64> {
    fn to_value(&self) -> pb::Value {
        pb::Value {
            value: Some(pb::value::Value::FloatArrayValue(pb::FloatArray {
                values: self.iter().map(|v| *v as f32).collect(),
            })),
        }
    }
}

// -- batching ------------------------------------------------------------------------------------

/// One queued write.
enum Queued {
    /// A plain write, which `SetProperties` can carry.
    Set { object: ObjectRef, property: String, value: pb::Value },
    /// A write through a `BlueprintSetter`, which it cannot.
    Call { object: ObjectRef, function: String, value: pb::Value },
}

thread_local! {
    // Thread-local because the client is not thread-safe anyway -- the gRPC channel is bound to
    // the loop that created it -- so a batch opened on one thread has no business capturing writes
    // made on another.
    static BATCH: RefCell<Option<Vec<Queued>>> = const { RefCell::new(None) };
}

/// Collect the writes made inside `body` and send them together.
///
/// ```no_run
/// # use tempo_sim::unreal::batch;
/// # fn configure() -> Result<(), tempo_sim::TempoError> {
/// # let lidar: &() = &(); let _ = lidar;
/// batch(|| {
///     // lidar.set_horizontal_beams(1024)?;
///     // lidar.set_vertical_beams(64)?;
///     Ok(())
/// })
/// # }
/// ```
///
/// Each typed write is one round trip, which is the cost of making it look like a field. Setting
/// twenty properties on an actor should not be twenty calls, so a batch collects them and flushes
/// once.
///
/// Order is preserved exactly. A property with a `BlueprintSetter` has to be written by calling
/// its accessor, which `SetProperties` cannot carry, so the queue holds both kinds and the flush
/// sends each contiguous run of plain writes as one call with the accessor calls in between. The
/// common case — no accessors — is a single call.
///
/// Nothing is sent if `body` returns an error: a half-applied configuration is harder to reason
/// about than one that never started.
pub fn batch<F>(body: F) -> Result<(), TempoError>
where
    F: FnOnce() -> Result<(), TempoError>,
{
    let outer = BATCH.with(|slot| slot.borrow_mut().replace(Vec::new()));
    let result = body();
    let queued = BATCH.with(|slot| {
        let mut slot = slot.borrow_mut();
        let mine = slot.take().unwrap_or_default();
        *slot = outer;
        mine
    });

    match result {
        Ok(()) => flush(queued),
        Err(error) => Err(error),
    }
}

/// Queue a write if a batch is open, reporting whether it was taken.
fn enqueue(item: Queued) -> Option<Queued> {
    BATCH.with(|slot| match slot.borrow_mut().as_mut() {
        Some(queue) => {
            queue.push(item);
            None
        }
        None => Some(item),
    })
}

fn flush(queued: Vec<Queued>) -> Result<(), TempoError> {
    let mut pending: Vec<(ObjectRef, String, pb::Value)> = Vec::new();
    for item in queued {
        match item {
            Queued::Set { object, property, value } => pending.push((object, property, value)),
            Queued::Call { object, function, value } => {
                send_writes(std::mem::take(&mut pending))?;
                call_setter(&object, &function, value)?;
            }
        }
    }
    send_writes(pending)
}

fn send_writes(writes: Vec<(ObjectRef, String, pb::Value)>) -> Result<(), TempoError> {
    if writes.is_empty() {
        return Ok(());
    }
    let count = writes.len();
    let mut builder = crate::tempo_world::batch();
    for (object, property, value) in writes {
        // The generic op, which is the only one carrying native_units.
        builder = builder.set_generic_property(
            object.actor.clone(),
            object.component.clone(),
            property,
            value,
            true,
        );
    }
    let response = builder.execute()?;
    if let Some(first) = response.failures.first() {
        return Err(TempoError::Protocol(format!(
            "batched write {} of {} failed: {}",
            first.op_index, count, first.error
        )));
    }
    Ok(())
}

fn call_setter(object: &ObjectRef, function: &str, value: pb::Value) -> Result<(), TempoError> {
    crate::tempo_world::call_function(
        object.actor.clone(),
        object.component.clone(),
        function.to_string(),
        vec![pb::FunctionArg { name: String::new(), value: Some(value) }],
        true,
    )?;
    Ok(())
}

async fn call_setter_async(
    object: &ObjectRef,
    function: &str,
    value: pb::Value,
) -> Result<(), TempoError> {
    crate::tempo_world::call_function_async(
        object.actor.clone(),
        object.component.clone(),
        function.to_string(),
        vec![pb::FunctionArg { name: String::new(), value: Some(value) }],
        true,
    )
    .await?;
    Ok(())
}

// -- property access -----------------------------------------------------------------------------
//
// What the generated methods call. Each takes the property's name as a literal and its type as a
// type parameter, both fixed when the method was generated, so there is nothing to look up and
// nothing that can disagree with the stub.

impl ObjectRef {
    /// Read one property.
    pub fn get<T: FromValue>(&self, property: &str) -> Result<T, TempoError> {
        T::from_value(&crate::tempo_world::get_property(
            self.actor.clone(),
            self.component.clone(),
            property.to_string(),
            true,
        )?)
    }

    /// Read one property, without blocking.
    pub fn get_async<T: FromValue>(
        &self,
        property: &str,
    ) -> impl Future<Output = Result<T, TempoError>> + '_ {
        let request = crate::tempo_world::get_property_async(
            self.actor.clone(),
            self.component.clone(),
            property.to_string(),
            true,
        );
        async move { T::from_value(&request.await?) }
    }

    /// Read one property through its `BlueprintGetter`.
    pub fn get_via<T: FromValue>(&self, getter: &str) -> Result<T, TempoError> {
        let response = crate::tempo_world::call_function(
            self.actor.clone(),
            self.component.clone(),
            getter.to_string(),
            Vec::new(),
            true,
        )?;
        T::from_value(returned(&response)?)
    }

    /// Read one property through its `BlueprintGetter`, without blocking.
    pub fn get_via_async<T: FromValue>(
        &self,
        getter: &str,
    ) -> impl Future<Output = Result<T, TempoError>> + '_ {
        let request = crate::tempo_world::call_function_async(
            self.actor.clone(),
            self.component.clone(),
            getter.to_string(),
            Vec::new(),
            true,
        );
        async move { T::from_value(returned(&request.await?)?) }
    }

    /// Write one property, or queue it when a [`batch`] is open.
    pub fn set<T: ToValue>(&self, property: &str, value: T) -> Result<(), TempoError> {
        let item = Queued::Set {
            object: self.clone(),
            property: property.to_string(),
            value: value.to_value(),
        };
        match enqueue(item) {
            None => Ok(()),
            Some(Queued::Set { property, value, .. }) => {
                crate::tempo_world::set_property(
                    self.actor.clone(),
                    self.component.clone(),
                    property,
                    value,
                    true,
                )?;
                Ok(())
            }
            Some(Queued::Call { .. }) => unreachable!("a Set cannot come back as a Call"),
        }
    }

    /// Write one property, without blocking.
    ///
    /// A batch is not consulted: queuing a write and handing back a future that resolves before
    /// the write happens would be a lie about when it took effect. Await it, or use [`batch`].
    pub fn set_async<T: ToValue>(
        &self,
        property: &str,
        value: T,
    ) -> impl Future<Output = Result<(), TempoError>> + '_ {
        let request = crate::tempo_world::set_property_async(
            self.actor.clone(),
            self.component.clone(),
            property.to_string(),
            value.to_value(),
            true,
        );
        async move {
            request.await?;
            Ok(())
        }
    }

    /// Write one property through its `BlueprintSetter`.
    ///
    /// Writing such a property directly would write its memory and skip the setter, which may be
    /// the only thing that clamps the value or marks render state dirty — so the accessor is
    /// called instead. The generator knows which properties have one; a cooked build could not
    /// have told it they exist.
    pub fn set_via<T: ToValue>(&self, setter: &str, value: T) -> Result<(), TempoError> {
        let item = Queued::Call {
            object: self.clone(),
            function: setter.to_string(),
            value: value.to_value(),
        };
        match enqueue(item) {
            None => Ok(()),
            Some(Queued::Call { function, value, .. }) => call_setter(self, &function, value),
            Some(Queued::Set { .. }) => unreachable!("a Call cannot come back as a Set"),
        }
    }

    /// Write one property through its `BlueprintSetter`, without blocking.
    pub fn set_via_async<T: ToValue>(
        &self,
        setter: &str,
        value: T,
    ) -> impl Future<Output = Result<(), TempoError>> + '_ {
        let setter = setter.to_string();
        let value = value.to_value();
        async move { call_setter_async(self, &setter, value).await }
    }

    /// Read a reference-valued property as a typed handle.
    pub fn get_object<T: Class>(&self, property: &str) -> Result<T, TempoError> {
        Ok(T::from_object(object_reference(&crate::tempo_world::get_property(
            self.actor.clone(),
            self.component.clone(),
            property.to_string(),
            true,
        )?)?))
    }

    /// Read a reference-valued property as a typed handle, without blocking.
    pub fn get_object_async<T: Class>(
        &self,
        property: &str,
    ) -> impl Future<Output = Result<T, TempoError>> + '_ {
        let request = crate::tempo_world::get_property_async(
            self.actor.clone(),
            self.component.clone(),
            property.to_string(),
            true,
        );
        async move { Ok(T::from_object(object_reference(&request.await?)?)) }
    }

    /// One of this actor's components, addressed under its own name.
    ///
    /// A component accessor is read-only: the attribute names a component that exists, and
    /// replacing one is not an assignment. Add and destroy components through the `TempoWorld`
    /// API.
    pub fn component_handle<T: Class>(&self, name: &str) -> T {
        T::from_object(ObjectRef::component(&self.actor, name).with_class(T::PATH))
    }

    /// Call a function and ignore what it returns.
    pub fn call(&self, function: &str, args: Vec<pb::FunctionArg>) -> Result<(), TempoError> {
        crate::tempo_world::call_function(
            self.actor.clone(),
            self.component.clone(),
            function.to_string(),
            args,
            true,
        )?;
        Ok(())
    }

    /// Call a function and ignore what it returns, without blocking.
    pub fn call_async(
        &self,
        function: &str,
        args: Vec<pb::FunctionArg>,
    ) -> impl Future<Output = Result<(), TempoError>> + '_ {
        let request = crate::tempo_world::call_function_async(
            self.actor.clone(),
            self.component.clone(),
            function.to_string(),
            args,
            true,
        );
        async move {
            request.await?;
            Ok(())
        }
    }

    /// Call a function and read its return value.
    pub fn call_for<T: FromValue>(
        &self,
        function: &str,
        args: Vec<pb::FunctionArg>,
    ) -> Result<T, TempoError> {
        let response = crate::tempo_world::call_function(
            self.actor.clone(),
            self.component.clone(),
            function.to_string(),
            args,
            true,
        )?;
        T::from_value(returned(&response)?)
    }

    /// Call a function and read its return value, without blocking.
    pub fn call_for_async<T: FromValue>(
        &self,
        function: &str,
        args: Vec<pb::FunctionArg>,
    ) -> impl Future<Output = Result<T, TempoError>> + '_ {
        let request = crate::tempo_world::call_function_async(
            self.actor.clone(),
            self.component.clone(),
            function.to_string(),
            args,
            true,
        );
        async move { T::from_value(returned(&request.await?)?) }
    }

    /// Yield this property's value once per simulation tick, until the caller stops asking.
    ///
    /// Dynamically typed in the property, unavoidably: it is named by a string here, so there is
    /// nothing for the compiler to work from. The generated classes offer a `watch_<property>`
    /// for each readable property, which is the checked form.
    pub fn watch<T: FromValue>(
        &self,
        property: &str,
    ) -> Result<impl Iterator<Item = Result<T, TempoError>>, TempoError> {
        let stream = crate::tempo_world::stream_property(
            self.actor.clone(),
            self.component.clone(),
            property.to_string(),
            true,
        )?;
        Ok(stream.map(|message| match message {
            Ok(value) => T::from_value(&value),
            Err(status) => Err(TempoError::Rpc(status)),
        }))
    }

    /// Whether the thing this handle addresses is `ancestor`, or derives from it.
    ///
    /// A question about the simulation, not about the handle. `world::get_actors::<Pawn>()` hands
    /// back handles typed `Pawn`, because that is what makes the call site check, but each one
    /// points at an actor of whatever class it really is — so answering from the handle's own type
    /// would do no more than repeat the question that was asked.
    ///
    /// Answered from the generated hierarchy table, which is compiled in, so this costs nothing
    /// and works without a running sim. Unlike Python's, it is not `Deref`-based: a handle's Rust
    /// type is the class that was *asked for*, and the hierarchy is what knows the rest.
    pub fn is_a(&self, ancestor: &str) -> bool {
        if self.class_path.is_empty() {
            return false;
        }
        let mut current: &str = &self.class_path;
        let mut hops = 0;
        loop {
            if current == ancestor {
                return true;
            }
            match hierarchy().superclass(current) {
                Some(parent) if hops < 128 => {
                    current = parent;
                    hops += 1;
                }
                _ => return false,
            }
        }
    }
}

fn returned(response: &pb::CallFunctionResponse) -> Result<&pb::Value, TempoError> {
    // A Blueprint usually hands back more than one thing, and position would be guesswork, so the
    // return value is the result named "ReturnValue" -- which is what UHT calls it.
    response
        .results
        .iter()
        .find(|result| result.name == "ReturnValue")
        .or_else(|| response.results.first())
        .and_then(|result| result.typed_value.as_ref())
        .ok_or_else(|| TempoError::Protocol("the function returned no value".into()))
}

fn object_reference(value: &pb::Value) -> Result<ObjectRef, TempoError> {
    match variant(value)? {
        pb::value::Value::ActorValue(name) => Ok(ObjectRef::actor(name)),
        pb::value::Value::ComponentValue(name) => Ok(ObjectRef::from_reference_name(name)),
        _ => Err(wrong(value, "an actor or component reference")),
    }
}

// -- struct-valued properties -------------------------------------------------------------------

/// A handle to a struct-valued property, addressed a member at a time.
///
/// A struct has no single `Value` that can carry it, but the server has always resolved a nested
/// address, so reaching into one is a matter of accumulating the path: `PostProcessSettings`
/// becomes `PostProcessSettings.BloomIntensity` on the way out.
///
/// Generated code wraps this in a typed proxy per Unreal struct, so the members are checked too.
#[derive(Clone, Debug)]
pub struct StructRef<'a> {
    owner: &'a ObjectRef,
    address: String,
}

impl<'a> StructRef<'a> {
    /// The address of one member, for a generated proxy to read or write.
    fn member(&self, name: &str) -> String {
        format!("{}.{}", self.address, name)
    }

    pub fn get<T: FromValue>(&self, member: &str) -> Result<T, TempoError> {
        self.owner.get(&self.member(member))
    }

    pub fn get_async<'s, T: FromValue + 's>(
        &'s self,
        member: &str,
    ) -> impl Future<Output = Result<T, TempoError>> + 's {
        let address = self.member(member);
        async move { self.owner.get_async(&address).await }
    }

    pub fn set<T: ToValue>(&self, member: &str, value: T) -> Result<(), TempoError> {
        self.owner.set(&self.member(member), value)
    }

    pub fn set_async<'s, T: ToValue + 's>(
        &'s self,
        member: &str,
        value: T,
    ) -> impl Future<Output = Result<(), TempoError>> + 's {
        let address = self.member(member);
        async move { self.owner.set_async(&address, value).await }
    }

    /// A struct inside this one.
    pub fn nested(&self, member: &str) -> StructRef<'a> {
        StructRef { owner: self.owner, address: self.member(member) }
    }
}

impl ObjectRef {
    /// A struct-valued property of this object, for a generated proxy to reach into.
    pub fn struct_ref(&self, property: &str) -> StructRef<'_> {
        StructRef { owner: self, address: property.to_string() }
    }

    /// One named result of a call -- an out parameter, or `"ReturnValue"`.
    ///
    /// A Blueprint usually hands back more than one thing, and position would be guesswork, so a
    /// generated method with out parameters reads each result by the name it was declared with.
    pub fn result_of<T: FromValue>(
        response: &pb::CallFunctionResponse,
        name: &str,
    ) -> Result<T, TempoError> {
        let value = response
            .results
            .iter()
            .find(|result| result.name == name)
            .and_then(|result| result.typed_value.as_ref())
            .ok_or_else(|| {
                TempoError::Protocol(format!("the function returned no result named {:?}", name))
            })?;
        T::from_value(value)
    }

    /// Call a function and hand back every result, for a generated method with out parameters.
    pub fn call_raw(
        &self,
        function: &str,
        args: Vec<pb::FunctionArg>,
    ) -> Result<pb::CallFunctionResponse, TempoError> {
        crate::tempo_world::call_function(
            self.actor.clone(),
            self.component.clone(),
            function.to_string(),
            args,
            true,
        )
    }

    /// Call a function and hand back every result, without blocking.
    pub fn call_raw_async(
        &self,
        function: &str,
        args: Vec<pb::FunctionArg>,
    ) -> impl Future<Output = Result<pb::CallFunctionResponse, TempoError>> + '_ {
        crate::tempo_world::call_function_async(
            self.actor.clone(),
            self.component.clone(),
            function.to_string(),
            args,
            true,
        )
    }
}

// -- the generated hierarchy ---------------------------------------------------------------------

/// What a generated class package tells the runtime about this build.
///
/// Only three things need to come from generated data rather than from generated code: which class
/// derives from which (for [`ObjectRef::is_a`]), which Rust type a class path maps to (for the
/// dynamic path), and the schema header the classes were generated from (for
/// `world::check_schema`). Everything else a generated method needs, it already knows statically.
pub trait Hierarchy: Send + Sync {
    /// The class a class derives from, or `None` for a root or an unknown class.
    fn superclass(&self, class_path: &str) -> Option<&str>;

    /// The schema this build's classes were generated from: engine version, schema version, hash.
    fn header(&self) -> SchemaIdentity;
}

/// Identifies the build a generated class package was made from.
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct SchemaIdentity {
    pub schema_version: String,
    pub engine_version: String,
    pub content_hash: String,
}

struct NoClasses;

impl Hierarchy for NoClasses {
    fn superclass(&self, _class_path: &str) -> Option<&str> {
        None
    }

    fn header(&self) -> SchemaIdentity {
        SchemaIdentity::default()
    }
}

static HIERARCHY: std::sync::OnceLock<&'static (dyn Hierarchy + 'static)> =
    std::sync::OnceLock::new();

/// Called by a generated class crate to make its hierarchy available.
///
/// Where Python's equivalent reads JSON out of the installed package at import time, this is a
/// table compiled into the generated crate — so it cannot go missing, and it costs no I/O.
pub fn register_classes(hierarchy: &'static (dyn Hierarchy + 'static)) {
    let _ = HIERARCHY.set(hierarchy);
}

/// The registered hierarchy, or an empty one when no generated classes are linked.
pub fn hierarchy() -> &'static (dyn Hierarchy + 'static) {
    *HIERARCHY.get_or_init(|| &NoClasses)
}

/// Whether a generated class crate has registered itself.
pub fn classes_registered() -> bool {
    !hierarchy().header().content_hash.is_empty()
}
