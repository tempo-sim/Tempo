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

    /// The class table of the crate this type was generated into.
    ///
    /// Rust has no import side effects, so a generated crate cannot announce itself the way
    /// Python's `__init__.py` does. Instead every path that makes a handle registers through this,
    /// which means [`ObjectRef::is_a`] and `world::check_schema` work without the caller
    /// remembering to do anything.
    fn hierarchy() -> &'static (dyn Hierarchy + 'static);
}

/// Note the crate a handle came from, so the class table is available to ask about it later.
#[inline]
pub fn note_origin<T: Class>() {
    register_classes(T::hierarchy());
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

macro_rules! name_container {
    ($($case:ident, $set_case:ident, $wanted:literal, $wrapper:ident;)*) => { $(
        impl FromValue for Vec<$wrapper> {
            fn from_value(value: &pb::Value) -> Result<Self, TempoError> {
                let names = match variant(value)? {
                    pb::value::Value::$case(inner) => &inner.values,
                    pb::value::Value::$set_case(inner) => &inner.values,
                    _ => return Err(wrong(value, $wanted)),
                };
                Ok(names.iter().map(|name| $wrapper(name.clone())).collect())
            }
        }
        impl ToValue for Vec<$wrapper> {
            fn to_value(&self) -> pb::Value {
                pb::Value {
                    value: Some(pb::value::Value::$case(pb::StringArray {
                        values: self.iter().map(|reference| reference.0.clone()).collect(),
                    })),
                }
            }
        }
    )* };
}

name_container! {
    ClassArrayValue, ClassSetValue, "a class array", ClassRef;
    AssetArrayValue, AssetSetValue, "an asset array", AssetRef;
}

// -- struct values -------------------------------------------------------------------------------
//
// A struct with no first-class representation travels as a `StructValue`: its members by name.
// Generated code declares one Rust struct per Unreal struct that can travel that way, every field
// an `Option`, and implements `FromValue`/`ToValue` for it through the two helpers below -- so the
// encoding is written once, here, rather than once per generated struct.
//
// Every field is optional because a write is a merge: only the members present are written, and
// the rest of the destination keeps what it holds. A value built with one field is the same write
// as assigning that one member through the proxy.

/// A generated struct value, named as the sim names it.
pub trait StructType {
    /// The struct's C++ name, e.g. `"FHitResult"` -- what `StructValue.struct_name` carries.
    const NAME: &'static str;
}

/// The members of a `StructValue` the server sent, for a generated `FromValue` to read.
pub struct StructFields<'v> {
    fields: &'v [pb::StructField],
}

impl<'v> StructFields<'v> {
    /// The fields of a struct value, checking it is the struct expected.
    pub fn of(value: &'v pb::Value, name: &str) -> Result<Self, TempoError> {
        match variant(value)? {
            pb::value::Value::StructValue(inner) => {
                if !inner.struct_name.is_empty() && inner.struct_name != name {
                    return Err(TempoError::Protocol(format!(
                        "expected a {} from the server, got a {}",
                        name, inner.struct_name
                    )));
                }
                Ok(Self { fields: &inner.fields })
            }
            _ => Err(wrong(value, "a struct")),
        }
    }

    fn find(&self, name: &str) -> Option<&'v pb::Value> {
        self.fields.iter().find(|field| field.name == name).and_then(|field| field.value.as_ref())
    }

    /// One member, or `None` when the server did not send it.
    pub fn get<T: FromValue>(&self, name: &str) -> Result<Option<T>, TempoError> {
        self.find(name).map(T::from_value).transpose()
    }

    /// One member that refers to an actor or a component, as a typed handle.
    pub fn object<T: Class>(&self, name: &str) -> Result<Option<T>, TempoError> {
        note_origin::<T>();
        match self.find(name) {
            None => Ok(None),
            Some(value) => match variant(value)? {
                // An unset reference reads as an empty name, which is no handle at all.
                pb::value::Value::ActorValue(reference) | pb::value::Value::ComponentValue(reference)
                    if reference.is_empty() =>
                {
                    Ok(None)
                }
                _ => Ok(Some(T::from_object(object_reference(value)?))),
            },
        }
    }
}

/// A `StructValue` being built, for a generated `ToValue` to fill in.
pub struct StructBuilder {
    value: pb::StructValue,
}

impl StructBuilder {
    pub fn new(name: &str) -> Self {
        Self { value: pb::StructValue { struct_name: name.to_string(), fields: Vec::new() } }
    }

    fn push(&mut self, name: &str, value: pb::Value) {
        self.value.fields.push(pb::StructField { name: name.to_string(), value: Some(value) });
    }

    /// One member, when it has been given. An unset field is not sent, which leaves the
    /// destination's own value in place.
    pub fn field<T: ToValue>(&mut self, name: &str, value: &Option<T>) {
        if let Some(value) = value {
            self.push(name, value.to_value());
        }
    }

    /// One member referring to an actor.
    pub fn actor<T: Class>(&mut self, name: &str, value: &Option<T>) {
        if let Some(value) = value {
            self.push(name, ActorName(value.as_object().reference_name()).to_value());
        }
    }

    /// One member referring to a component.
    pub fn component<T: Class>(&mut self, name: &str, value: &Option<T>) {
        if let Some(value) = value {
            self.push(name, ComponentName(value.as_object().reference_name()).to_value());
        }
    }

    pub fn finish(self) -> pb::Value {
        pb::Value { value: Some(pb::value::Value::StructValue(self.value)) }
    }
}

// An array of structs. Here rather than per generated struct because the orphan rule forbids a
// generated crate implementing a tempo-sim trait for a `Vec` of its own type; a blanket impl over
// `StructType` is how it is allowed at all.
impl<T: StructType + FromValue> FromValue for Vec<T> {
    fn from_value(value: &pb::Value) -> Result<Self, TempoError> {
        match variant(value)? {
            pb::value::Value::StructArrayValue(inner) => inner
                .values
                .iter()
                .map(|element| {
                    T::from_value(&pb::Value {
                        value: Some(pb::value::Value::StructValue(element.clone())),
                    })
                })
                .collect(),
            _ => Err(wrong(value, "an array of structs")),
        }
    }
}

impl<T: StructType + ToValue> ToValue for Vec<T> {
    fn to_value(&self) -> pb::Value {
        let values = self
            .iter()
            .filter_map(|element| match element.to_value().value {
                Some(pb::value::Value::StructValue(inner)) => Some(inner),
                _ => None,
            })
            .collect();
        pb::Value { value: Some(pb::value::Value::StructArrayValue(pb::StructArray { values })) }
    }
}

// -- batching ------------------------------------------------------------------------------------

/// One queued write.
enum Queued {
    /// A plain write, which `SetProperties` can carry.
    Set { object: ObjectRef, property: String, value: pb::Value },
    /// A write through a `BlueprintSetter`, which it cannot.
    Call { object: ObjectRef, function: String, argument: String, value: pb::Value },
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
            Queued::Call { object, function, argument, value } => {
                send_writes(std::mem::take(&mut pending))?;
                call_setter(&object, &function, &argument, value)?;
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

fn call_setter(
    object: &ObjectRef,
    function: &str,
    argument: &str,
    value: pb::Value,
) -> Result<(), TempoError> {
    crate::tempo_world::call_function(
        object.actor.clone(),
        object.component.clone(),
        function.to_string(),
        vec![pb::FunctionArg { name: argument.to_string(), value: Some(value) }],
        true,
    )?;
    Ok(())
}

async fn call_setter_async(
    object: &ObjectRef,
    function: &str,
    argument: &str,
    value: pb::Value,
) -> Result<(), TempoError> {
    crate::tempo_world::call_function_async(
        object.actor.clone(),
        object.component.clone(),
        function.to_string(),
        vec![pb::FunctionArg { name: argument.to_string(), value: Some(value) }],
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
    /// `argument` is what the setter calls its parameter. `CallFunction` rejects an unnamed
    /// argument outright and matches the name against the function's own parameters, so the
    /// generator resolves it from the schema; a property whose setter cannot be named is not
    /// generated as writable at all.
    pub fn set_via<T: ToValue>(
        &self,
        setter: &str,
        argument: &str,
        value: T,
    ) -> Result<(), TempoError> {
        let item = Queued::Call {
            object: self.clone(),
            function: setter.to_string(),
            argument: argument.to_string(),
            value: value.to_value(),
        };
        match enqueue(item) {
            None => Ok(()),
            Some(Queued::Call { function, argument, value, .. }) => {
                call_setter(self, &function, &argument, value)
            }
            Some(Queued::Set { .. }) => unreachable!("a Call cannot come back as a Set"),
        }
    }

    /// Write one property through its `BlueprintSetter`, without blocking.
    pub fn set_via_async<T: ToValue>(
        &self,
        setter: &str,
        argument: &str,
        value: T,
    ) -> impl Future<Output = Result<(), TempoError>> + '_ {
        let setter = setter.to_string();
        let argument = argument.to_string();
        let value = value.to_value();
        async move { call_setter_async(self, &setter, &argument, value).await }
    }

    /// Read a reference-valued property as a typed handle.
    pub fn get_object<T: Class>(&self, property: &str) -> Result<T, TempoError> {
        note_origin::<T>();
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
        note_origin::<T>();
        async move { Ok(T::from_object(object_reference(&request.await?)?)) }
    }

    /// Read an array or set of actor or component references as typed handles.
    ///
    /// Separate from [`ObjectRef::get`] for the same reason [`ObjectRef::get_enums`] is: a `Vec`
    /// of a generated class cannot implement [`FromValue`] in the crate that generated it.
    pub fn get_objects<T: Class>(&self, property: &str) -> Result<Vec<T>, TempoError> {
        note_origin::<T>();
        let value = crate::tempo_world::get_property(
            self.actor.clone(),
            self.component.clone(),
            property.to_string(),
            true,
        )?;
        Ok(reference_names(&value)?
            .iter()
            .map(|name| T::from_object(ObjectRef::from_reference_name(name)))
            .collect())
    }

    /// Read an array or set of references as typed handles, without blocking.
    pub fn get_objects_async<T: Class>(
        &self,
        property: &str,
    ) -> impl Future<Output = Result<Vec<T>, TempoError>> + '_ {
        note_origin::<T>();
        let request = crate::tempo_world::get_property_async(
            self.actor.clone(),
            self.component.clone(),
            property.to_string(),
            true,
        );
        async move {
            let value = request.await?;
            Ok(reference_names(&value)?
                .iter()
                .map(|name| T::from_object(ObjectRef::from_reference_name(name)))
                .collect())
        }
    }

    /// Point an actor-valued array at some actors.
    pub fn set_actor_refs<T: Class>(
        &self,
        property: &str,
        values: &[T],
    ) -> Result<(), TempoError> {
        self.set(property, ActorNames(reference_list(values)))
    }

    /// Point a component-valued array at some components.
    pub fn set_component_refs<T: Class>(
        &self,
        property: &str,
        values: &[T],
    ) -> Result<(), TempoError> {
        self.set(property, ComponentNames(reference_list(values)))
    }

    /// Point an actor-valued array at some actors, without blocking.
    pub fn set_actor_refs_async<'s, T: Class>(
        &'s self,
        property: &str,
        values: &[T],
    ) -> impl Future<Output = Result<(), TempoError>> + 's {
        self.set_async(property, ActorNames(reference_list(values)))
    }

    /// Point a component-valued array at some components, without blocking.
    pub fn set_component_refs_async<'s, T: Class>(
        &'s self,
        property: &str,
        values: &[T],
    ) -> impl Future<Output = Result<(), TempoError>> + 's {
        self.set_async(property, ComponentNames(reference_list(values)))
    }

    /// Point an actor-valued property at an actor.
    pub fn set_actor_ref<T: Class>(&self, property: &str, value: &T) -> Result<(), TempoError> {
        self.set(property, ActorName(value.as_object().reference_name()))
    }

    /// Point a component-valued property at a component.
    pub fn set_component_ref<T: Class>(&self, property: &str, value: &T) -> Result<(), TempoError> {
        self.set(property, ComponentName(value.as_object().reference_name()))
    }

    /// Point an actor-valued property at an actor, without blocking.
    pub fn set_actor_ref_async<'s, T: Class>(
        &'s self,
        property: &str,
        value: &T,
    ) -> impl Future<Output = Result<(), TempoError>> + 's {
        self.set_async(property, ActorName(value.as_object().reference_name()))
    }

    /// Point a component-valued property at a component, without blocking.
    pub fn set_component_ref_async<'s, T: Class>(
        &'s self,
        property: &str,
        value: &T,
    ) -> impl Future<Output = Result<(), TempoError>> + 's {
        self.set_async(property, ComponentName(value.as_object().reference_name()))
    }

    /// One of this actor's components, addressed under its own name.
    ///
    /// A component accessor is read-only: the attribute names a component that exists, and
    /// replacing one is not an assignment. Add and destroy components through the `TempoWorld`
    /// API.
    pub fn component_handle<T: Class>(&self, name: &str) -> T {
        note_origin::<T>();
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

    /// Call a function whose result is an actor or a component.
    pub fn call_for_object<T: Class>(
        &self,
        function: &str,
        args: Vec<pb::FunctionArg>,
    ) -> Result<T, TempoError> {
        note_origin::<T>();
        let response = self.call_raw(function, args)?;
        Self::object_result_of(&response, "ReturnValue")
    }

    /// Call a function whose result is an actor or a component, without blocking.
    pub fn call_for_object_async<T: Class>(
        &self,
        function: &str,
        args: Vec<pb::FunctionArg>,
    ) -> impl Future<Output = Result<T, TempoError>> + '_ {
        note_origin::<T>();
        let request = self.call_raw_async(function, args);
        async move { Self::object_result_of(&request.await?, "ReturnValue") }
    }

    /// One named result of a call, as a typed handle.
    pub fn object_result_of<T: Class>(
        response: &pb::CallFunctionResponse,
        name: &str,
    ) -> Result<T, TempoError> {
        note_origin::<T>();
        let value = response
            .results
            .iter()
            .find(|result| result.name == name)
            .and_then(|result| result.typed_value.as_ref())
            .ok_or_else(|| {
                TempoError::Protocol(format!("the function returned no result named {:?}", name))
            })?;
        Ok(T::from_object(object_reference(value)?))
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
            match superclass_of(current) {
                Some(parent) if hops < 128 => {
                    current = parent;
                    hops += 1;
                }
                _ => return false,
            }
        }
    }
}

/// An argument naming an actor. Its own function because the handle's type cannot say whether the
/// server should look for an actor or a component.
pub fn argument_actor<T: Class>(name: &str, value: &T) -> pb::FunctionArg {
    argument(name, &ActorName(value.as_object().reference_name()))
}

/// An argument naming a component.
pub fn argument_component<T: Class>(name: &str, value: &T) -> pb::FunctionArg {
    argument(name, &ComponentName(value.as_object().reference_name()))
}

/// One argument of a call, named the way the function declares it.
///
/// Generated code names every argument rather than relying on order, so adding a parameter to a
/// Blueprint cannot silently shift the others onto the wrong ones.
pub fn argument<T: ToValue>(name: &str, value: &T) -> pb::FunctionArg {
    pb::FunctionArg { name: name.to_string(), value: Some(value.to_value()) }
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
/// The server has always resolved a nested address, so reaching into a struct is a matter of
/// accumulating the path: `PostProcessSettings` becomes `PostProcessSettings.BloomIntensity` on the
/// way out. A struct that can also travel as one value is loaded and stored whole through
/// [`StructRef::load`] and [`StructRef::store`].
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

    /// The whole struct this addresses, as a value.
    pub fn load<T: FromValue>(&self) -> Result<T, TempoError> {
        self.owner.get(&self.address)
    }

    /// The whole struct this addresses, as a value, without blocking.
    pub fn load_async<'s, T: FromValue + 's>(&'s self) -> impl Future<Output = Result<T, TempoError>> + 's {
        self.owner.get_async(&self.address)
    }

    /// Write a struct value here: its given fields, merged into what is there.
    pub fn store<T: ToValue>(&self, value: T) -> Result<(), TempoError> {
        self.owner.set(&self.address, value)
    }

    /// Write a struct value here, without blocking.
    pub fn store_async<'s, T: ToValue + 's>(
        &'s self,
        value: T,
    ) -> impl Future<Output = Result<(), TempoError>> + 's {
        self.owner.set_async(&self.address, value)
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
    ///
    /// `'static` because the table is compiled into the generated crate, which is what lets a
    /// caller walk the chain across crates without borrowing the registry.
    fn superclass(&self, class_path: &str) -> Option<&'static str>;

    /// The schema this build's classes were generated from: engine version, schema version, hash.
    fn header(&self) -> SchemaIdentity;

    /// Every entry of one kind and what it hashed to, sorted by name.
    fn hashes(&self, kind: EntryKind) -> &'static [(&'static str, &'static str)];

    /// What one entry hashed to, or `None` when these classes do not have it.
    fn content_hash(&self, kind: EntryKind, name: &str) -> Option<&'static str>;
}

/// The three things a generated crate records a hash for.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum EntryKind {
    Class,
    Struct,
    Enum,
}

impl EntryKind {
    /// What one entry of this kind is called, for a message that names a finding.
    pub fn label(self) -> &'static str {
        match self {
            EntryKind::Class => "class",
            EntryKind::Struct => "struct",
            EntryKind::Enum => "enum",
        }
    }
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
    fn superclass(&self, _class_path: &str) -> Option<&'static str> {
        None
    }

    fn header(&self) -> SchemaIdentity {
        SchemaIdentity::default()
    }

    fn hashes(&self, _kind: EntryKind) -> &'static [(&'static str, &'static str)] {
        &[]
    }

    fn content_hash(&self, _kind: EntryKind, _name: &str) -> Option<&'static str> {
        None
    }
}

/// Every linked generated class crate, in registration order.
///
/// A list and not one slot, because more than one can be linked and they do not describe the same
/// thing: a project's own crate describes its whole build, while the published classes inside
/// `tempo_sim` describe the modules every build ships (TYPED_WORLD_API_PLAN.md §16). One slot
/// would make whichever registered second invisible — no `is_a`, no dynamic path — and silently,
/// since the loser's own generated methods would go on working.
///
/// There is no overlap to arbitrate: a project's crate re-exports the published classes it matches
/// rather than redefining them.
static HIERARCHIES: std::sync::RwLock<Vec<&'static (dyn Hierarchy + 'static)>> =
    std::sync::RwLock::new(Vec::new());

/// Called by a generated class crate to make its hierarchy available.
///
/// Where Python's equivalent reads JSON out of the installed package at import time, this is a
/// table compiled into the generated crate — so it cannot go missing, and it costs no I/O.
pub fn register_classes(hierarchy: &'static (dyn Hierarchy + 'static)) {
    if let Ok(mut registered) = HIERARCHIES.write() {
        // Every handle made through a crate registers it, so this runs constantly for the same
        // table. Compared by address, since each crate has exactly one.
        if !registered
            .iter()
            .any(|existing| std::ptr::eq(*existing as *const _, hierarchy as *const _))
        {
            registered.push(hierarchy);
        }
    }
}

/// Every linked hierarchy, in registration order. Empty when no generated classes are linked.
pub fn hierarchies() -> Vec<&'static (dyn Hierarchy + 'static)> {
    HIERARCHIES.read().map(|h| h.clone()).unwrap_or_default()
}

/// What a class derives from, according to whichever linked crate knows it.
///
/// Searched across all of them, because the chain can cross between crates: a project's Blueprint
/// derives from an engine class the published crate declares.
pub fn superclass_of(class_path: &str) -> Option<&'static str> {
    let registered = HIERARCHIES.read().ok()?;
    registered
        .iter()
        .find_map(|hierarchy| hierarchy.superclass(class_path))
}

/// The first linked hierarchy, or an empty one when none is.
///
/// Kept for the single-crate case, which is every project that has not linked the published
/// classes beside its own. [`hierarchies`] is what a lookup across all of them wants.
pub fn hierarchy() -> &'static (dyn Hierarchy + 'static) {
    HIERARCHIES
        .read()
        .ok()
        .and_then(|registered| registered.first().copied())
        .unwrap_or(&NoClasses)
}

/// Whether a generated class crate has registered itself.
pub fn classes_registered() -> bool {
    HIERARCHIES.read().map(|h| !h.is_empty()).unwrap_or(false)
}

impl ObjectRef {
    /// Read an array or set of enums.
    ///
    /// Separate from [`ObjectRef::get`] because a `Vec` of a generated enum cannot implement
    /// [`FromValue`]: in the generated crate both `Vec` and the trait are foreign, and the orphan
    /// rule wants a local type that is not merely a parameter of a foreign one. Reading through
    /// `UnrealEnum` sidesteps that, and keeps the conversion in one place.
    pub fn get_enums<T: UnrealEnum>(&self, property: &str) -> Result<Vec<T>, TempoError> {
        let value = crate::tempo_world::get_property(
            self.actor.clone(),
            self.component.clone(),
            property.to_string(),
            true,
        )?;
        enum_names(&value)?.iter().map(|name| T::from_unreal_name(name)).collect()
    }

    /// Read an array or set of enums, without blocking.
    pub fn get_enums_async<T: UnrealEnum>(
        &self,
        property: &str,
    ) -> impl Future<Output = Result<Vec<T>, TempoError>> + '_ {
        let request = crate::tempo_world::get_property_async(
            self.actor.clone(),
            self.component.clone(),
            property.to_string(),
            true,
        );
        async move {
            let value = request.await?;
            enum_names(&value)?.iter().map(|name| T::from_unreal_name(name)).collect()
        }
    }

    /// Write an array of enums, or queue it when a [`batch`] is open.
    pub fn set_enums<T: UnrealEnum>(
        &self,
        property: &str,
        values: &[T],
    ) -> Result<(), TempoError> {
        self.set(property, EnumNames(values.iter().map(|v| v.unreal_name().to_string()).collect()))
    }

    /// Write an array of enums, without blocking.
    pub fn set_enums_async<'s, T: UnrealEnum>(
        &'s self,
        property: &str,
        values: &[T],
    ) -> impl Future<Output = Result<(), TempoError>> + 's {
        let names = EnumNames(values.iter().map(|v| v.unreal_name().to_string()).collect());
        self.set_async(property, names)
    }
}

/// A reference's name, in the variant its property expects. Two types rather than one, because
/// the name alone does not say whether the server should look for an actor or a component.
struct ActorName(String);

struct ComponentName(String);

impl ToValue for ActorName {
    fn to_value(&self) -> pb::Value {
        pb::Value { value: Some(pb::value::Value::ActorValue(self.0.clone())) }
    }
}

impl ToValue for ComponentName {
    fn to_value(&self) -> pb::Value {
        pb::Value { value: Some(pb::value::Value::ComponentValue(self.0.clone())) }
    }
}

struct ActorNames(Vec<String>);

struct ComponentNames(Vec<String>);

impl ToValue for ActorNames {
    fn to_value(&self) -> pb::Value {
        pb::Value {
            value: Some(pb::value::Value::ActorArrayValue(pb::StringArray {
                values: self.0.clone(),
            })),
        }
    }
}

impl ToValue for ComponentNames {
    fn to_value(&self) -> pb::Value {
        pb::Value {
            value: Some(pb::value::Value::ComponentArrayValue(pb::StringArray {
                values: self.0.clone(),
            })),
        }
    }
}

fn reference_list<T: Class>(values: &[T]) -> Vec<String> {
    values.iter().map(|value| value.as_object().reference_name()).collect()
}

fn reference_names(value: &pb::Value) -> Result<&Vec<String>, TempoError> {
    match variant(value)? {
        pb::value::Value::ActorArrayValue(inner) => Ok(&inner.values),
        pb::value::Value::ActorSetValue(inner) => Ok(&inner.values),
        pb::value::Value::ComponentArrayValue(inner) => Ok(&inner.values),
        pb::value::Value::ComponentSetValue(inner) => Ok(&inner.values),
        _ => Err(wrong(value, "an actor or component array")),
    }
}

/// The names an enum container carries on the wire. Its own type, so that writing one picks the
/// enum array variant rather than the string array variant a `Vec<String>` would.
struct EnumNames(Vec<String>);

impl ToValue for EnumNames {
    fn to_value(&self) -> pb::Value {
        pb::Value {
            value: Some(pb::value::Value::EnumArrayValue(pb::StringArray {
                values: self.0.clone(),
            })),
        }
    }
}

fn enum_names(value: &pb::Value) -> Result<&Vec<String>, TempoError> {
    match variant(value)? {
        pb::value::Value::EnumArrayValue(inner) => Ok(&inner.values),
        pb::value::Value::EnumSetValue(inner) => Ok(&inner.values),
        _ => Err(wrong(value, "an enum array")),
    }
}

// -- generated declarations ----------------------------------------------------------------------

/// Declare an Unreal enum and everything the typed API needs to carry one.
///
/// An enum travels as its authored name rather than its numeric value, because that is what
/// survives someone reordering the enumerators. The conversions are here rather than in the
/// generated crate so that all 866 of them cannot drift from each other.
///
/// ```
/// tempo_sim::unreal_enum! {
///     ETempoReturnMode { Strongest = "Strongest", Last = "Last" }
/// }
/// # fn main() {}
/// ```
#[macro_export]
macro_rules! unreal_enum {
    ($name:ident { $($variant:ident = $authored:literal),* $(,)? }) => {
        #[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
        pub enum $name { $($variant,)* }

        impl $crate::unreal::UnrealEnum for $name {
            fn unreal_name(&self) -> &'static str {
                match self { $(Self::$variant => $authored,)* }
            }

            fn from_unreal_name(name: &str) -> ::core::result::Result<Self, $crate::TempoError> {
                match name {
                    $($authored => ::core::result::Result::Ok(Self::$variant),)*
                    other => ::core::result::Result::Err($crate::TempoError::Protocol(format!(
                        "the sim reported {:?} for {}, which is not one of its enumerators",
                        other,
                        ::core::stringify!($name),
                    ))),
                }
            }
        }

        impl $crate::unreal::FromValue for $name {
            fn from_value(
                value: &$crate::proto::tempo_world::Value,
            ) -> ::core::result::Result<Self, $crate::TempoError> {
                match &value.value {
                    ::core::option::Option::Some(
                        $crate::proto::tempo_world::value::Value::EnumValue(name),
                    ) => <Self as $crate::unreal::UnrealEnum>::from_unreal_name(name),
                    _ => ::core::result::Result::Err($crate::TempoError::Protocol(format!(
                        "expected an enum from the server for {}",
                        ::core::stringify!($name),
                    ))),
                }
            }
        }

        impl $crate::unreal::ToValue for $name {
            fn to_value(&self) -> $crate::proto::tempo_world::Value {
                $crate::proto::tempo_world::Value {
                    value: ::core::option::Option::Some(
                        $crate::proto::tempo_world::value::Value::EnumValue(
                            <Self as $crate::unreal::UnrealEnum>::unreal_name(self).to_string(),
                        ),
                    ),
                }
            }
        }

    };
}

#[cfg(test)]
mod tests {
    //! The struct encoding, through a value type written the way `gen_rust_classes.py` writes one.

    use super::*;

    #[derive(Clone, Debug, Default, PartialEq)]
    struct Trim {
        offset: Option<Vector>,
        label: Option<String>,
    }

    impl StructType for Trim {
        const NAME: &'static str = "FTrim";
    }

    impl FromValue for Trim {
        fn from_value(value: &pb::Value) -> Result<Self, TempoError> {
            let fields = StructFields::of(value, "FTrim")?;
            Ok(Self { offset: fields.get("Offset")?, label: fields.get("Label")? })
        }
    }

    impl ToValue for Trim {
        fn to_value(&self) -> pb::Value {
            let mut builder = StructBuilder::new("FTrim");
            builder.field("Offset", &self.offset);
            builder.field("Label", &self.label);
            builder.finish()
        }
    }

    #[test]
    fn a_struct_value_round_trips() {
        let trim = Trim { offset: Some(Vector { x: 1.0, y: 2.0, z: 3.0 }), label: Some("a".into()) };
        assert_eq!(Trim::from_value(&trim.to_value()).unwrap(), trim);
    }

    #[test]
    fn an_unset_field_is_not_sent() {
        // A write is a merge, so sending a default would clobber what the destination holds.
        let value = Trim { offset: None, label: Some("only".into()) }.to_value();
        match value.value {
            Some(pb::value::Value::StructValue(inner)) => {
                assert_eq!(inner.struct_name, "FTrim");
                let names: Vec<_> = inner.fields.iter().map(|field| field.name.as_str()).collect();
                assert_eq!(names, ["Label"]);
            }
            other => panic!("expected a struct value, got {:?}", other),
        }
    }

    #[test]
    fn a_different_struct_is_refused() {
        let mut value = Trim::default().to_value();
        if let Some(pb::value::Value::StructValue(inner)) = value.value.as_mut() {
            inner.struct_name = "FSomethingElse".into();
        }
        assert!(Trim::from_value(&value).is_err());
    }

    #[test]
    fn an_array_of_structs_round_trips() {
        let trims = vec![Trim { label: Some("a".into()), ..Default::default() }, Trim::default()];
        assert_eq!(Vec::<Trim>::from_value(&trims.to_value()).unwrap(), trims);
    }
}
