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
//! Which is why the value types here are Unreal's own, named and shaped as Unreal's are --
//! [`FVector`], [`FRotator`] in pitch-yaw-roll order, [`FTransform`] with its quaternion and scale --
//! and not the TempoCore messages every other Tempo RPC speaks, which are meters, radians and
//! right-handed. [`crate::proto::tempo_core::Vector`] is not an `FVector`. [`crate::conversion`]
//! converts between the two.
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

/// Unreal's `FVector`: a location or direction in centimeters, left-handed.
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct FVector {
    pub x: f64,
    pub y: f64,
    pub z: f64,
}

/// Unreal's `FVector2D`.
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct FVector2D {
    pub x: f64,
    pub y: f64,
}

/// Unreal's `FIntVector`.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct FIntVector {
    pub x: i32,
    pub y: i32,
    pub z: i32,
}

/// Unreal's `FIntPoint`.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct FIntPoint {
    pub x: i32,
    pub y: i32,
}

/// Unreal's `FRotator`: an orientation in degrees, left-handed, in Unreal's own order -- pitch, yaw,
/// roll.
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct FRotator {
    pub pitch: f64,
    pub yaw: f64,
    pub roll: f64,
}

/// Unreal's `FQuat`: a rotation, left-handed.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct FQuat {
    pub x: f64,
    pub y: f64,
    pub z: f64,
    pub w: f64,
}

impl Default for FQuat {
    fn default() -> Self {
        Self { x: 0.0, y: 0.0, z: 0.0, w: 1.0 }
    }
}

/// Unreal's `FTransform`: a rotation as a quaternion, a translation in centimeters and a 3D scale,
/// left-handed. The default is the identity, scale 1, as Unreal's is.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct FTransform {
    pub rotation: FQuat,
    pub translation: FVector,
    pub scale3d: FVector,
}

impl Default for FTransform {
    fn default() -> Self {
        Self {
            rotation: FQuat::default(),
            translation: FVector::default(),
            scale3d: FVector { x: 1.0, y: 1.0, z: 1.0 },
        }
    }
}

/// Unreal's `FColor`: 8-bit RGBA, sRGB. The default is opaque black.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct FColor {
    pub r: u8,
    pub g: u8,
    pub b: u8,
    pub a: u8,
}

impl Default for FColor {
    fn default() -> Self {
        Self { r: 0, g: 0, b: 0, a: 255 }
    }
}

/// Unreal's `FLinearColor`: floating-point RGBA, linear. The default is opaque black.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct FLinearColor {
    pub r: f32,
    pub g: f32,
    pub b: f32,
    pub a: f32,
}

impl Default for FLinearColor {
    fn default() -> Self {
        Self { r: 0.0, g: 0.0, b: 0.0, a: 1.0 }
    }
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

/// A reference to a cooked asset, by path, typed by the asset's class.
///
/// The generated catalog holds one per asset the build cooked -- `assets::game::vehicles::SM_Truck`
/// is an `AssetRef<StaticMesh>` -- and an asset-valued property's setter takes the class it
/// accepts. So a moved or renamed asset is a name that no longer resolves, and a material where a
/// mesh is wanted does not compile.
///
/// It derefs to `AssetRef<Superclass>` along the same chain a class handle does, so `&M_Paint`, an
/// `&AssetRef<Material>`, coerces to the `&AssetRef<MaterialInterface>` a setter takes. Built from a
/// path, it takes whatever class the destination wants: that is the escape hatch for a path from
/// data, and it is visible where it is written.
#[repr(transparent)]
pub struct AssetRef<T = AnyClass> {
    path: std::borrow::Cow<'static, str>,
    class: std::marker::PhantomData<fn() -> T>,
}

/// The class of an asset reference whose class nothing knows -- the default for `AssetRef`.
pub enum AnyClass {}

impl<T> AssetRef<T> {
    /// A reference to the asset at this path, for the generated catalog's statics.
    pub const fn from_static(path: &'static str) -> Self {
        Self { path: std::borrow::Cow::Borrowed(path), class: std::marker::PhantomData }
    }

    /// A reference to the asset at a path known only at run time. Nothing checks it until the
    /// server looks it up.
    pub fn from_path(path: impl Into<String>) -> Self {
        Self { path: std::borrow::Cow::Owned(path.into()), class: std::marker::PhantomData }
    }

    pub fn path(&self) -> &str {
        &self.path
    }
}

// By hand rather than derived, because a derive would demand the same of the class parameter,
// which is a marker and has none of them.
impl<T> Clone for AssetRef<T> {
    fn clone(&self) -> Self {
        Self { path: self.path.clone(), class: std::marker::PhantomData }
    }
}

impl<T> std::fmt::Debug for AssetRef<T> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "AssetRef({:?})", self.path)
    }
}

impl<T> PartialEq for AssetRef<T> {
    fn eq(&self, other: &Self) -> bool {
        self.path == other.path
    }
}

impl<T> Eq for AssetRef<T> {}

impl<T> std::hash::Hash for AssetRef<T> {
    fn hash<H: std::hash::Hasher>(&self, state: &mut H) {
        self.path.hash(state);
    }
}

impl<T> ::core::ops::Deref for AssetRef<T>
where
    T: Class + ::core::ops::Deref,
    T::Target: Class + Sized,
{
    type Target = AssetRef<T::Target>;

    #[inline]
    fn deref(&self) -> &AssetRef<T::Target> {
        // SAFETY: AssetRef is `#[repr(transparent)]` over its path, the class being only a
        // `PhantomData`, so every AssetRef<_> has one layout and this reinterprets nothing but the
        // marker. The `T: Deref` bound is the class's own superclass link, which is what makes the
        // widened class true.
        unsafe { &*(self as *const Self as *const AssetRef<T::Target>) }
    }
}

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
string_ref!(ClassRef);

// -- the handle ----------------------------------------------------------------------------------

/// What a handle addresses, and the class it was found to have.
///
/// Addressed the way every `TempoWorld` RPC addresses things: an actor name, and a component name
/// when the handle is to a component rather than to the actor itself -- or, for a subsystem, its
/// class alone, since there is one per world (or game instance, or engine).
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct ObjectRef {
    actor: String,
    component: String,
    subsystem: String,
    class_path: String,
}

impl ObjectRef {
    /// A handle to an actor.
    pub fn actor(name: impl Into<String>) -> Self {
        Self { actor: name.into(), ..Self::default() }
    }

    /// A handle to one of an actor's components.
    pub fn component(actor: impl Into<String>, component: impl Into<String>) -> Self {
        Self { actor: actor.into(), component: component.into(), ..Self::default() }
    }

    /// A handle to a subsystem, by its class path. See [`crate::world::get_subsystem`].
    pub fn subsystem(class_path: impl Into<String>) -> Self {
        Self { subsystem: class_path.into(), ..Self::default() }
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

    /// The class of the subsystem this handle addresses, or `""` for an actor or a component.
    pub fn subsystem_class(&self) -> &str {
        &self.subsystem
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
///
/// `Send`, as every value the typed API reads is: a deferred read's result crosses the await of
/// [`batch_async`], which a multi-threaded runtime may resume on another thread.
pub unsafe trait Class: Sized + Send {
    /// The Unreal class path this type stands for — a path rather than a name, because two
    /// Blueprints in different folders can share a name.
    const PATH: &'static str;

    fn from_object(object: ObjectRef) -> Self;

    fn as_object(&self) -> &ObjectRef;

    /// The class table of the crate this type was generated into.
    ///
    /// A generated crate registers its table before `main`, from the platform's static
    /// initializers. Every path that makes a handle registers through this as well, so a table
    /// whose initializer a linker dropped is still there before [`ObjectRef::is_a`] needs it.
    fn hierarchy() -> &'static (dyn Hierarchy + 'static);
}

/// Note the crate a handle came from, so the class table is available to ask about it later.
#[inline]
pub fn note_origin<T: Class>() {
    register_classes(T::hierarchy());
}

/// View an object as one of its superclasses.
///
/// One of the two `unsafe` reinterpretations in the typed Rust surface -- the other is
/// [`AssetRef`]'s `Deref` -- and the one the [`Class`] contract exists for. Generated code contains
/// no `unsafe` blocks; its one `unsafe` is the `unsafe impl Class` stating the newtype contract.
/// It calls this from its `Deref` impl, so that `lidar.relative_location()` reaches
/// `SceneComponent` however many classes lie between them.
#[inline]
pub fn upcast<T: Class>(object: &ObjectRef) -> &T {
    // SAFETY: by the `Class` contract, `T` is a `#[repr(transparent)]` newtype over `ObjectRef`,
    // so it has the same size, alignment and address as the value behind this reference.
    unsafe { &*(object as *const ObjectRef as *const T) }
}

/// An Unreal enum, which travels as its authored name rather than its numeric value.
pub trait UnrealEnum: Sized + Send {
    /// The enumerator's name as Unreal declares it.
    fn unreal_name(&self) -> &'static str;

    fn from_unreal_name(name: &str) -> Result<Self, TempoError>;
}

// -- Value conversion ----------------------------------------------------------------------------

/// A Rust type one `Value` variant can be read into. `Send` for the reason [`Class`] is.
pub trait FromValue: Sized + Send {
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
            None => "nothing",
            Some(other) => variant_name(other),
        }
    ))
}

/// The variant a `Value` carries, by its name in `WorldControl.proto`, for an error that says what
/// arrived. Exhaustive on purpose: a variant added to the proto has to be named here, where it will
/// also need a `FromValue`.
fn variant_name(value: &pb::value::Value) -> &'static str {
    use pb::value::Value as V;
    match value {
        V::BoolValue(_) => "bool_value",
        V::IntValue(_) => "int_value",
        V::Int64Value(_) => "int64_value",
        V::FloatValue(_) => "float_value",
        V::StringValue(_) => "string_value",
        V::EnumValue(_) => "enum_value",
        V::VectorValue(_) => "vector_value",
        V::Vector2dValue(_) => "vector2d_value",
        V::IntVectorValue(_) => "int_vector_value",
        V::IntPointValue(_) => "int_point_value",
        V::RotatorValue(_) => "rotator_value",
        V::QuatValue(_) => "quat_value",
        V::TransformValue(_) => "transform_value",
        V::ColorValue(_) => "color_value",
        V::ClassValue(_) => "class_value",
        V::AssetValue(_) => "asset_value",
        V::ActorValue(_) => "actor_value",
        V::ComponentValue(_) => "component_value",
        V::BoolArrayValue(_) => "bool_array_value",
        V::StringArrayValue(_) => "string_array_value",
        V::EnumArrayValue(_) => "enum_array_value",
        V::IntArrayValue(_) => "int_array_value",
        V::Int64ArrayValue(_) => "int64_array_value",
        V::FloatArrayValue(_) => "float_array_value",
        V::ClassArrayValue(_) => "class_array_value",
        V::AssetArrayValue(_) => "asset_array_value",
        V::ActorArrayValue(_) => "actor_array_value",
        V::ComponentArrayValue(_) => "component_array_value",
        V::BoolSetValue(_) => "bool_set_value",
        V::StringSetValue(_) => "string_set_value",
        V::EnumSetValue(_) => "enum_set_value",
        V::IntSetValue(_) => "int_set_value",
        V::Int64SetValue(_) => "int64_set_value",
        V::FloatSetValue(_) => "float_set_value",
        V::ClassSetValue(_) => "class_set_value",
        V::AssetSetValue(_) => "asset_set_value",
        V::ActorSetValue(_) => "actor_set_value",
        V::ComponentSetValue(_) => "component_set_value",
        V::StructValue(_) => "struct_value",
        V::StructArrayValue(_) => "struct_array_value",
        V::NativeTransformValue(_) => "native_transform_value",
        V::LinearColorValue(_) => "linear_color_value",
    }
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

impl FromValue for FVector {
    fn from_value(value: &pb::Value) -> Result<Self, TempoError> {
        match variant(value)? {
            pb::value::Value::VectorValue(v) => Ok(FVector { x: v.x, y: v.y, z: v.z }),
            _ => Err(wrong(value, "a vector")),
        }
    }
}

impl ToValue for FVector {
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

impl FromValue for FVector2D {
    fn from_value(value: &pb::Value) -> Result<Self, TempoError> {
        match variant(value)? {
            pb::value::Value::Vector2dValue(v) => Ok(FVector2D { x: v.x, y: v.y }),
            _ => Err(wrong(value, "a 2D vector")),
        }
    }
}

impl ToValue for FVector2D {
    fn to_value(&self) -> pb::Value {
        pb::Value {
            value: Some(pb::value::Value::Vector2dValue(crate::proto::tempo_core::Vector2D {
                x: self.x,
                y: self.y,
            })),
        }
    }
}

impl FromValue for FIntVector {
    fn from_value(value: &pb::Value) -> Result<Self, TempoError> {
        match variant(value)? {
            pb::value::Value::IntVectorValue(v) => Ok(FIntVector { x: v.x, y: v.y, z: v.z }),
            _ => Err(wrong(value, "an int vector")),
        }
    }
}

impl ToValue for FIntVector {
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

impl FromValue for FIntPoint {
    fn from_value(value: &pb::Value) -> Result<Self, TempoError> {
        match variant(value)? {
            pb::value::Value::IntPointValue(v) => Ok(FIntPoint { x: v.x, y: v.y }),
            _ => Err(wrong(value, "an int point")),
        }
    }
}

impl ToValue for FIntPoint {
    fn to_value(&self) -> pb::Value {
        pb::Value {
            value: Some(pb::value::Value::IntPointValue(pb::IntPoint { x: self.x, y: self.y })),
        }
    }
}

// Carried in a message whose fields are named for radians and right-handed degrees, because that
// is what every other RPC puts there. With `native_units` set the server does not convert, so
// these are Unreal's own degrees, and r/p/y are roll, pitch and yaw by name.
impl FromValue for FRotator {
    fn from_value(value: &pb::Value) -> Result<Self, TempoError> {
        match variant(value)? {
            pb::value::Value::RotatorValue(v) => Ok(FRotator { pitch: v.p, yaw: v.y, roll: v.r }),
            _ => Err(wrong(value, "a rotator")),
        }
    }
}

impl ToValue for FRotator {
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

fn quat_from(v: &pb::Quat) -> FQuat {
    FQuat { x: v.x as f64, y: v.y as f64, z: v.z as f64, w: v.w as f64 }
}

fn quat_to(q: &FQuat) -> pb::Quat {
    pb::Quat { x: q.x as f32, y: q.y as f32, z: q.z as f32, w: q.w as f32 }
}

impl FromValue for FQuat {
    fn from_value(value: &pb::Value) -> Result<Self, TempoError> {
        match variant(value)? {
            pb::value::Value::QuatValue(v) => Ok(quat_from(v)),
            _ => Err(wrong(value, "a quaternion")),
        }
    }
}

impl ToValue for FQuat {
    fn to_value(&self) -> pb::Value {
        pb::Value { value: Some(pb::value::Value::QuatValue(quat_to(self))) }
    }
}

fn vector_from(v: &Option<crate::proto::tempo_core::Vector>) -> FVector {
    v.as_ref().map(|v| FVector { x: v.x, y: v.y, z: v.z }).unwrap_or_default()
}

fn vector_to(v: &FVector) -> Option<crate::proto::tempo_core::Vector> {
    Some(crate::proto::tempo_core::Vector { x: v.x, y: v.y, z: v.z })
}

// Unreal's own shape, which TempoCore's Transform does not have room for: see NativeTransform.
impl FromValue for FTransform {
    fn from_value(value: &pb::Value) -> Result<Self, TempoError> {
        match variant(value)? {
            pb::value::Value::NativeTransformValue(t) => Ok(FTransform {
                rotation: t.rotation.as_ref().map(quat_from).unwrap_or_default(),
                translation: vector_from(&t.translation),
                scale3d: vector_from(&t.scale3d),
            }),
            _ => Err(wrong(value, "a transform")),
        }
    }
}

impl ToValue for FTransform {
    fn to_value(&self) -> pb::Value {
        pb::Value {
            value: Some(pb::value::Value::NativeTransformValue(pb::NativeTransform {
                rotation: Some(quat_to(&self.rotation)),
                translation: vector_to(&self.translation),
                scale3d: vector_to(&self.scale3d),
            })),
        }
    }
}

impl FromValue for FColor {
    fn from_value(value: &pb::Value) -> Result<Self, TempoError> {
        match variant(value)? {
            pb::value::Value::ColorValue(c) => Ok(FColor {
                r: c.r as u8,
                g: c.g as u8,
                b: c.b as u8,
                a: c.a as u8,
            }),
            _ => Err(wrong(value, "a color")),
        }
    }
}

impl ToValue for FColor {
    fn to_value(&self) -> pb::Value {
        pb::Value {
            value: Some(pb::value::Value::ColorValue(pb::Color {
                r: self.r as i32,
                g: self.g as i32,
                b: self.b as i32,
                a: self.a as i32,
            })),
        }
    }
}

impl FromValue for FLinearColor {
    fn from_value(value: &pb::Value) -> Result<Self, TempoError> {
        match variant(value)? {
            pb::value::Value::LinearColorValue(c) => Ok(FLinearColor { r: c.r, g: c.g, b: c.b, a: c.a }),
            _ => Err(wrong(value, "a linear color")),
        }
    }
}

impl ToValue for FLinearColor {
    fn to_value(&self) -> pb::Value {
        pb::Value {
            value: Some(pb::value::Value::LinearColorValue(pb::LinearColor {
                r: self.r,
                g: self.g,
                b: self.b,
                a: self.a,
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

impl<T> FromValue for AssetRef<T> {
    fn from_value(value: &pb::Value) -> Result<Self, TempoError> {
        match variant(value)? {
            pb::value::Value::AssetValue(path) => Ok(AssetRef::from_path(path.clone())),
            _ => Err(wrong(value, "an asset reference")),
        }
    }
}

impl<T> ToValue for AssetRef<T> {
    fn to_value(&self) -> pb::Value {
        pb::Value { value: Some(pb::value::Value::AssetValue(self.path.to_string())) }
    }
}

impl<T> FromValue for Vec<AssetRef<T>> {
    fn from_value(value: &pb::Value) -> Result<Self, TempoError> {
        let paths = match variant(value)? {
            pb::value::Value::AssetArrayValue(inner) => &inner.values,
            pb::value::Value::AssetSetValue(inner) => &inner.values,
            _ => return Err(wrong(value, "an asset array or set")),
        };
        Ok(paths.iter().map(|path| AssetRef::from_path(path.clone())).collect())
    }
}

impl<T> ToValue for Vec<AssetRef<T>> {
    fn to_value(&self) -> pb::Value {
        pb::Value {
            value: Some(pb::value::Value::AssetArrayValue(pb::StringArray {
                values: self.iter().map(|asset| asset.path.to_string()).collect(),
            })),
        }
    }
}

macro_rules! container_value {
    ($($case:ident, $set_case:ident, $wanted:literal, $message:ident, $element:ty;)*) => { $(
        impl FromValue for Vec<$element> {
            fn from_value(value: &pb::Value) -> Result<Self, TempoError> {
                match variant(value)? {
                    pb::value::Value::$case(inner) | pb::value::Value::$set_case(inner) => {
                        Ok(inner.values.clone())
                    }
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

// A `TSet` travels in variants of its own that carry the same payload as the array's, and the
// generated code gives both containers the one Rust shape, `Vec<T>` -- so a read accepts either
// variant. A write sends the array variant, and the server stores it into whichever container the
// property is.
container_value! {
    BoolArrayValue, BoolSetValue, "a bool array or set", BoolArray, bool;
    IntArrayValue, IntSetValue, "an int array or set", IntArray, i32;
    Int64ArrayValue, Int64SetValue, "an int64 array or set", Int64Array, i64;
    StringArrayValue, StringSetValue, "a string array or set", StringArray, String;
}

impl FromValue for Vec<f64> {
    fn from_value(value: &pb::Value) -> Result<Self, TempoError> {
        match variant(value)? {
            pb::value::Value::FloatArrayValue(inner) | pb::value::Value::FloatSetValue(inner) => {
                Ok(inner.values.iter().map(|v| *v as f64).collect())
            }
            _ => Err(wrong(value, "a float array or set")),
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
    ClassArrayValue, ClassSetValue, "a class array or set", ClassRef;
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
        match self.find(name) {
            None => Ok(None),
            Some(value) => match variant(value)? {
                // An unset reference reads as an empty name, which is no handle at all.
                pb::value::Value::ActorValue(reference) | pb::value::Value::ComponentValue(reference)
                    if reference.is_empty() =>
                {
                    Ok(None)
                }
                _ => Ok(Some(object_of(value)?)),
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

/// What a queued op's result is delivered to, once the batch carrying it has run. `Send`, because
/// an awaited batch carries it across the await, and a multi-threaded runtime may resume that on
/// another thread.
type Resolver = Box<dyn FnOnce(Result<pb::BatchOpResult, TempoError>) + Send>;

/// Where a deferred result lands: shared between the [`Deferred`] and the op's resolver, and
/// behind a lock rather than a cell for the same reason the resolver is `Send`.
type Slot<T> = std::sync::Arc<std::sync::Mutex<Option<Result<T, String>>>>;

fn lock<T>(slot: &Slot<T>) -> std::sync::MutexGuard<'_, Option<Result<T, String>>> {
    // Nothing panics while holding it, but a poisoned slot still holds a usable value.
    slot.lock().unwrap_or_else(std::sync::PoisonError::into_inner)
}

/// One op queued in a batch.
struct Queued {
    op: pb::BatchOp,
    /// What the op was, for the error that names it if it fails.
    description: String,
    resolve: Option<Resolver>,
}

thread_local! {
    // Thread-local because a batch is collected synchronously, by the closure handed to `batch` or
    // `batch_async`, so it is the closure's thread that owns it -- and a batch opened on one thread
    // has no business capturing writes made on another.
    static BATCH: RefCell<Option<Vec<Queued>>> = const { RefCell::new(None) };
    // Whether the open batch is `batch_async`'s, where nothing may block.
    static ASYNC: RefCell<bool> = const { RefCell::new(false) };
    // Set while `defer` runs its closure: the read the closure makes is queued, and its Deferred
    // left here for `defer` to hand back.
    static CAPTURE: RefCell<Option<Vec<Box<dyn std::any::Any>>>> = const { RefCell::new(None) };
}

/// A thread-local's value for one scope, the previous one put back when this drops -- which a panic
/// unwinding through the scope does as surely as a return. Without it a panic inside [`batch`] or
/// [`defer`], caught by a runtime or by `catch_unwind`, would leave its thread in a batch for good:
/// every later write queued and never sent, every later read refused.
struct Scoped<T: 'static> {
    slot: &'static std::thread::LocalKey<RefCell<T>>,
    previous: Option<T>,
}

impl<T: 'static> Scoped<T> {
    fn set(slot: &'static std::thread::LocalKey<RefCell<T>>, value: T) -> Self {
        let previous = slot.with(|cell| std::mem::replace(&mut *cell.borrow_mut(), value));
        Self { slot, previous: Some(previous) }
    }

    /// Take the scope's value out, putting the previous one back now rather than at the drop.
    fn finish(mut self) -> T {
        let previous = self.previous.take().expect("a scope finishes once");
        self.slot.with(|cell| std::mem::replace(&mut *cell.borrow_mut(), previous))
    }
}

impl<T: 'static> Drop for Scoped<T> {
    fn drop(&mut self) {
        if let Some(previous) = self.previous.take() {
            self.slot.with(|cell| *cell.borrow_mut() = previous);
        }
    }
}

/// grpc's ABORTED: what `RunBatch` reports for an op it did not run because an earlier one failed.
const ABORTED: i32 = 10;

/// Send what `body` does as one `RunBatch`: in order, and between two ticks of the simulation.
///
/// ```no_run
/// # use tempo_sim::unreal::{batch, defer};
/// # fn configure() -> Result<(), tempo_sim::TempoError> {
/// batch(|| {
///     // lidar.set_horizontal_beams(1024)?;
///     // let beams = defer(|| lidar.horizontal_beams())?;   // Deferred<i32>
///     Ok(())
/// })
/// # }
/// ```
///
/// Each typed write is one round trip, which is the cost of making it look like a field. Setting
/// twenty properties on an actor should not be twenty calls, so a batch collects them and flushes
/// once -- and since the server runs the whole batch between two ticks, nothing simulates against
/// the half-configured actor. A write through a `BlueprintSetter` is a call, and travels in the
/// same batch.
///
/// A read or call inside the batch comes back one of two ways. Through [`defer`], it is queued with
/// everything else and its result is available from the returned [`Deferred`] once the batch has
/// run. Made directly, it goes out at once with everything queued before it and returns its real
/// result -- so it sees those writes -- at the price of a round trip of its own.
///
/// The server stops at the first failure, and this reports it, naming the op. Nothing is sent if
/// `body` returns an error before a read forced a flush: a half-applied configuration is harder to
/// reason about than one that never started.
///
/// A batch opened inside another joins it, so the whole of it is still one flush, in order.
pub fn batch<F>(body: F) -> Result<(), TempoError>
where
    F: FnOnce() -> Result<(), TempoError>,
{
    flush(collect(body, false)?)
}

/// [`batch`], sent without blocking -- the form for async code, where the blocking one's flush
/// would panic inside the runtime.
///
/// ```no_run
/// # use tempo_sim::unreal::{batch_async, defer};
/// # async fn configure() -> Result<(), tempo_sim::TempoError> {
/// batch_async(|| {
///     // lidar.set_horizontal_beams(1024)?;
///     // let beams = defer(|| lidar.horizontal_beams())?;
///     Ok(())
/// })
/// .await
/// # }
/// ```
///
/// `body` is synchronous, and runs before this returns. That is the design rather than a
/// limitation: inside a batch a write only queues, so there is nothing in it to wait for, and the
/// queue belongs to this thread, which an `.await` in the body could leave. A read or call is
/// deferred with [`defer`]; one made directly would have to block, and is refused. The future
/// holds only what was queued, so it is `Send` and can be awaited in a spawned task.
pub fn batch_async<F>(body: F) -> BatchFuture
where
    F: FnOnce() -> Result<(), TempoError>,
{
    let queued = collect(body, true);
    Box::pin(async move { flush_async(queued?).await })
}

/// What [`batch_async`] returns. Boxed rather than `impl Future`, which would capture the body's
/// type and so keep everything the body borrowed borrowed until the batch was awaited -- including
/// the `Option` a [`Deferred`] was put in, which is then unreadable.
pub type BatchFuture = std::pin::Pin<Box<dyn Future<Output = Result<(), TempoError>> + Send>>;

/// Run `body` with a batch open and hand back what it queued: nothing, when a batch was already
/// open, since the ops then joined that one.
fn collect<F>(body: F, asynchronous: bool) -> Result<Vec<Queued>, TempoError>
where
    F: FnOnce() -> Result<(), TempoError>,
{
    if batch_open() {
        // Joined rather than nested: a flush of its own would send these ahead of the outer
        // batch's earlier ops.
        return body().map(|()| Vec::new());
    }
    let queue = Scoped::set(&BATCH, Some(Vec::new()));
    let asynchronous = Scoped::set(&ASYNC, asynchronous);
    let result = body();
    drop(asynchronous);
    let queued = queue.finish().unwrap_or_default();
    result.map(|()| queued)
}

/// A read or call queued in a [`batch`], available once the batch has run.
pub struct Deferred<T> {
    slot: Slot<T>,
}

impl<T> Deferred<T> {
    fn ready(value: T) -> Self {
        Self { slot: std::sync::Arc::new(std::sync::Mutex::new(Some(Ok(value)))) }
    }

    /// Whether the batch carrying this has run.
    pub fn is_done(&self) -> bool {
        lock(&self.slot).is_some()
    }

    /// Take the result out. The op's own error if it failed; an error if the batch has not run.
    pub fn take(&self) -> Result<T, TempoError> {
        match lock(&self.slot).take() {
            Some(Ok(value)) => Ok(value),
            Some(Err(error)) => Err(TempoError::Batch(error)),
            None => Err(TempoError::Protocol(
                "the batch has not run yet, or this result was already taken".into(),
            )),
        }
    }
}

/// Defer one read or call into the open [`batch`], typed by what it reads.
///
/// `defer(|| truck.max_speed())?` is a `Deferred<f64>`: the closure runs, but the read it makes
/// is queued rather than sent, and its value arrives with the rest of the batch. The closure must
/// make exactly one read or call and return its result unchanged. Outside a batch the read simply
/// happens, and the `Deferred` is ready at once.
///
/// **Do nothing to the value inside the closure.** There is no value yet: the read returns early
/// through its `?`, so `defer(|| Ok(truck.max_speed()? * 2.0))` defers the plain speed and the
/// doubling never happens. A change of type is caught and refused; one that keeps the type cannot
/// be told apart from the read itself. Transform the result after [`Deferred::take`].
///
/// A generated method with out parameters cannot be deferred: it decodes its tuple after the call
/// returns, and there is nothing yet to decode. Call it directly instead.
pub fn defer<T: Send + 'static>(
    read: impl FnOnce() -> Result<T, TempoError>,
) -> Result<Deferred<T>, TempoError> {
    let capture = Scoped::set(&CAPTURE, Some(Vec::new()));
    let returned = read();
    let captured = capture.finish().unwrap_or_default();

    match (returned, captured.len()) {
        // Answered without a request, or outside a batch: there is nothing to wait for.
        (Ok(value), 0) => Ok(Deferred::ready(value)),
        (Err(TempoError::Deferred), 1) => captured
            .into_iter()
            .next()
            .and_then(|deferred| deferred.downcast::<Deferred<T>>().ok())
            .map(|deferred| *deferred)
            .ok_or_else(|| {
                TempoError::Protocol(
                    "a deferred closure must return what its one read or call returns, unchanged \
                     (a method with out parameters cannot be deferred)"
                        .into(),
                )
            }),
        (Err(error), 0) => Err(error),
        (_, made) => Err(TempoError::Protocol(format!(
            "a deferred closure must make exactly one read or call; this one made {}",
            made
        ))),
    }
}

fn batch_open() -> bool {
    BATCH.with(|slot| slot.borrow().is_some())
}

/// Queue an op if a batch is open, reporting whether it was taken.
fn enqueue(op: pb::BatchOp, description: String) -> bool {
    BATCH.with(|slot| match slot.borrow_mut().as_mut() {
        Some(queue) => {
            queue.push(Queued { op, description, resolve: None });
            true
        }
        None => false,
    })
}

/// Run one op whose result is wanted: as its own RPC when no batch is open, and through the batch
/// otherwise -- queued and deferred inside [`defer`], or sent at once with everything before it.
fn perform<T: Send + 'static>(
    op: pb::BatchOp,
    description: String,
    decode: impl FnOnce(pb::BatchOpResult) -> Result<T, TempoError> + Send + 'static,
    direct: impl FnOnce() -> Result<T, TempoError>,
) -> Result<T, TempoError> {
    if !batch_open() {
        return direct();
    }

    let capturing = CAPTURE.with(|capture| capture.borrow().is_some());
    if !capturing && ASYNC.with(|flag| *flag.borrow()) {
        return Err(TempoError::Protocol(format!(
            "{} was made directly inside batch_async, where nothing is sent until the batch is \
             awaited. Defer it with `defer(|| ...)`, or await its `_async` form outside the batch.",
            description
        )));
    }

    let slot: Slot<T> = std::sync::Arc::new(std::sync::Mutex::new(None));
    let sink = slot.clone();
    let resolve: Resolver = Box::new(move |outcome| {
        *lock(&sink) = Some(outcome.and_then(decode).map_err(|error| error.to_string()));
    });
    BATCH.with(|batch| {
        if let Some(queue) = batch.borrow_mut().as_mut() {
            queue.push(Queued { op, description, resolve: Some(resolve) });
        }
    });

    if capturing {
        CAPTURE.with(|capture| {
            if let Some(captured) = capture.borrow_mut().as_mut() {
                captured.push(Box::new(Deferred::<T> { slot: slot.clone() }));
            }
        });
        return Err(TempoError::Deferred);
    }

    let queued = BATCH.with(|batch| batch.borrow_mut().as_mut().map(std::mem::take).unwrap_or_default());
    let flushed = flush(queued);
    let outcome = lock(&slot).take();
    match outcome {
        Some(Ok(value)) => Ok(value),
        // The op that failed first, which may be an earlier one this op depended on.
        Some(Err(error)) => Err(flushed.err().unwrap_or(TempoError::Batch(error))),
        None => Err(flushed.err().unwrap_or_else(|| {
            TempoError::Protocol("the server answered the batch without this op".into())
        })),
    }
}

fn flush(queued: Vec<Queued>) -> Result<(), TempoError> {
    if queued.is_empty() {
        return Ok(());
    }
    let ops = queued.iter().map(|queued| queued.op.clone()).collect();
    settle(queued, crate::tempo_world::run_batch(ops, true))
}

async fn flush_async(queued: Vec<Queued>) -> Result<(), TempoError> {
    if queued.is_empty() {
        return Ok(());
    }
    let ops = queued.iter().map(|queued| queued.op.clone()).collect();
    settle(queued, crate::tempo_world::run_batch_async(ops, true).await)
}

/// Hand each queued op its result, and report the first failure.
fn settle(
    queued: Vec<Queued>,
    response: Result<pb::RunBatchResponse, TempoError>,
) -> Result<(), TempoError> {
    let count = queued.len();
    let response = match response {
        Ok(response) => response,
        Err(error) => {
            let message = error.to_string();
            for queued in queued {
                if let Some(resolve) = queued.resolve {
                    resolve(Err(TempoError::Batch(message.clone())));
                }
            }
            return Err(error);
        }
    };

    let mut first_failure = None;
    let mut results = response.results.into_iter();
    for (index, queued) in queued.into_iter().enumerate() {
        let outcome = match results.next() {
            Some(result) if result.code == 0 => Ok(result),
            Some(result) => {
                let error = format!(
                    "batched op {} of {} ({}) failed: {}",
                    index, count, queued.description, result.error
                );
                // The op that failed, not the ones the server then declined to run.
                if first_failure.is_none() && result.code != ABORTED {
                    first_failure = Some(error.clone());
                }
                Err(TempoError::Batch(error))
            }
            None => Err(TempoError::Protocol("the server answered the batch short".into())),
        };
        if let Some(resolve) = queued.resolve {
            resolve(outcome);
        }
    }
    match first_failure {
        Some(error) => Err(TempoError::Batch(error)),
        None => Ok(()),
    }
}

fn where_of(object: &ObjectRef, member: &str) -> String {
    if !object.subsystem.is_empty() {
        format!("{}.{}", object.subsystem, member)
    } else if object.component.is_empty() {
        format!("{}.{}", object.actor, member)
    } else {
        format!("{}:{}.{}", object.actor, object.component, member)
    }
}

fn get_op(object: &ObjectRef, property: &str) -> (pb::BatchOp, String) {
    (
        pb::BatchOp {
            op: Some(pb::batch_op::Op::Get(pb::GetPropertyRequest {
                actor: object.actor.clone(),
                component: object.component.clone(),
                property: property.to_string(),
                native_units: true,
                subsystem: object.subsystem.clone(),
            })),
        },
        format!("read {}", where_of(object, property)),
    )
}

fn call_op(object: &ObjectRef, function: &str, args: Vec<pb::FunctionArg>) -> (pb::BatchOp, String) {
    (
        pb::BatchOp {
            op: Some(pb::batch_op::Op::Call(pb::CallFunctionRequest {
                actor: object.actor.clone(),
                component: object.component.clone(),
                function: function.to_string(),
                args,
                native_units: true,
                subsystem: object.subsystem.clone(),
            })),
        },
        format!("call {}", where_of(object, function)),
    )
}

/// The value a get op read.
fn read_value(result: pb::BatchOpResult) -> Result<pb::Value, TempoError> {
    match result.result {
        Some(pb::batch_op_result::Result::Value(value)) => Ok(value),
        _ => Err(TempoError::Protocol("a batched read came back without a value".into())),
    }
}

/// The results a call op handed back.
fn call_results(result: pb::BatchOpResult) -> Result<pb::CallFunctionResponse, TempoError> {
    match result.result {
        Some(pb::batch_op_result::Result::Call(response)) => Ok(response),
        _ => Err(TempoError::Protocol("a batched call came back without results".into())),
    }
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
        object.subsystem.clone(),
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
        object.subsystem.clone(),
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
    /// Read one property -- through the open [`batch`] when there is one, so it sees the writes
    /// queued before it.
    pub fn get<T: FromValue + 'static>(&self, property: &str) -> Result<T, TempoError> {
        self.read_as(property, T::from_value)
    }

    /// Read one property and decode it into what the caller wants. The decoder is applied where
    /// the value arrives, which inside [`defer`] is after the batch runs -- so a deferred read is
    /// typed as what it decodes to, not as the raw value.
    fn read_as<T: Send + 'static>(
        &self,
        property: &str,
        decode: fn(&pb::Value) -> Result<T, TempoError>,
    ) -> Result<T, TempoError> {
        let (op, description) = get_op(self, property);
        perform(op, description, move |result| decode(&read_value(result)?), || {
            decode(&crate::tempo_world::get_property(
                self.actor.clone(),
                self.component.clone(),
                property.to_string(),
                true,
                self.subsystem.clone(),
            )?)
        })
    }

    /// Call a function and decode its return value, as `read_as` does a property.
    fn call_as<T: Send + 'static>(
        &self,
        function: &str,
        args: Vec<pb::FunctionArg>,
        decode: fn(&pb::Value) -> Result<T, TempoError>,
    ) -> Result<T, TempoError> {
        let (op, description) = call_op(self, function, args.clone());
        perform(
            op,
            description,
            move |result| decode(returned(&call_results(result)?)?),
            || {
                let response = crate::tempo_world::call_function(
                    self.actor.clone(),
                    self.component.clone(),
                    function.to_string(),
                    args,
                    true,
                    self.subsystem.clone(),
                )?;
                decode(returned(&response)?)
            },
        )
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
            self.subsystem.clone(),
        );
        async move { T::from_value(&request.await?) }
    }

    /// Read one property through its `BlueprintGetter`.
    pub fn get_via<T: FromValue + 'static>(&self, getter: &str) -> Result<T, TempoError> {
        self.call_as(getter, Vec::new(), T::from_value)
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
            self.subsystem.clone(),
        );
        async move { T::from_value(returned(&request.await?)?) }
    }

    /// Write one property, or queue it when a [`batch`] is open.
    pub fn set<T: ToValue>(&self, property: &str, value: T) -> Result<(), TempoError> {
        let request = pb::SetPropertyRequest {
            actor: self.actor.clone(),
            component: self.component.clone(),
            property: property.to_string(),
            value: Some(value.to_value()),
            native_units: true,
            subsystem: self.subsystem.clone(),
        };
        if batch_open() {
            enqueue(
                pb::BatchOp { op: Some(pb::batch_op::Op::Set(request)) },
                format!("write {}", where_of(self, property)),
            );
            return Ok(());
        }
        crate::tempo_world::set_property(
            request.actor,
            request.component,
            request.property,
            request.value.unwrap_or_default(),
            true,
            request.subsystem,
        )?;
        Ok(())
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
            self.subsystem.clone(),
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
        let value = value.to_value();
        if batch_open() {
            let (op, description) = call_op(
                self,
                setter,
                vec![pb::FunctionArg { name: argument.to_string(), value: Some(value) }],
            );
            enqueue(op, description);
            return Ok(());
        }
        call_setter(self, setter, argument, value)
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
    pub fn get_object<T: Class + 'static>(&self, property: &str) -> Result<T, TempoError> {
        self.read_as(property, object_of::<T>)
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
            self.subsystem.clone(),
        );
        async move { object_of(&request.await?) }
    }

    /// Read a reference-valued property through its `BlueprintGetter`, as [`ObjectRef::get_via`]
    /// reads a plain one: the getter may be the only thing that resolves the reference.
    pub fn get_object_via<T: Class + 'static>(&self, getter: &str) -> Result<T, TempoError> {
        self.call_as(getter, Vec::new(), object_of::<T>)
    }

    /// Read a reference-valued property through its `BlueprintGetter`, without blocking.
    pub fn get_object_via_async<T: Class>(
        &self,
        getter: &str,
    ) -> impl Future<Output = Result<T, TempoError>> + '_ {
        let request = self.call_raw_async(getter, Vec::new());
        async move { object_of(returned(&request.await?)?) }
    }

    /// Read an array or set of actor or component references as typed handles.
    ///
    /// Separate from [`ObjectRef::get`] for the same reason [`ObjectRef::get_enums`] is: a `Vec`
    /// of a generated class cannot implement [`FromValue`] in the crate that generated it.
    pub fn get_objects<T: Class + 'static>(&self, property: &str) -> Result<Vec<T>, TempoError> {
        self.read_as(property, objects_of::<T>)
    }

    /// Read an array or set of references as typed handles, without blocking.
    pub fn get_objects_async<T: Class>(
        &self,
        property: &str,
    ) -> impl Future<Output = Result<Vec<T>, TempoError>> + '_ {
        let request = crate::tempo_world::get_property_async(
            self.actor.clone(),
            self.component.clone(),
            property.to_string(),
            true,
            self.subsystem.clone(),
        );
        async move { objects_of(&request.await?) }
    }

    /// Read an array or set of references through its `BlueprintGetter`.
    pub fn get_objects_via<T: Class + 'static>(&self, getter: &str) -> Result<Vec<T>, TempoError> {
        self.call_as(getter, Vec::new(), objects_of::<T>)
    }

    /// Read an array or set of references through its `BlueprintGetter`, without blocking.
    pub fn get_objects_via_async<T: Class>(
        &self,
        getter: &str,
    ) -> impl Future<Output = Result<Vec<T>, TempoError>> + '_ {
        let request = self.call_raw_async(getter, Vec::new());
        async move { objects_of(returned(&request.await?)?) }
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
        self.call_raw(function, args)?;
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
            self.subsystem.clone(),
        );
        async move {
            request.await?;
            Ok(())
        }
    }

    /// Call a function and read its return value.
    pub fn call_for<T: FromValue + 'static>(
        &self,
        function: &str,
        args: Vec<pb::FunctionArg>,
    ) -> Result<T, TempoError> {
        self.call_as(function, args, T::from_value)
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
            self.subsystem.clone(),
        );
        async move { T::from_value(returned(&request.await?)?) }
    }

    /// Call a function whose result is an actor or a component.
    pub fn call_for_object<T: Class + 'static>(
        &self,
        function: &str,
        args: Vec<pb::FunctionArg>,
    ) -> Result<T, TempoError> {
        self.call_as(function, args, object_of::<T>)
    }

    /// Call a function whose result is an actor or a component, without blocking.
    pub fn call_for_object_async<T: Class>(
        &self,
        function: &str,
        args: Vec<pb::FunctionArg>,
    ) -> impl Future<Output = Result<T, TempoError>> + '_ {
        let request = self.call_raw_async(function, args);
        async move { Self::object_result_of(&request.await?, "ReturnValue") }
    }

    /// One named result of a call, as a typed handle.
    pub fn object_result_of<T: Class>(
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
        object_of(value)
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
            self.subsystem.clone(),
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

/// A handle to what a reference-valued `Value` names, typed as the property or result declares.
///
/// The wire carries a name and nothing else, so the handle is stamped with the declared class:
/// the type system has just said this is a `T`, and [`ObjectRef::is_a`] should agree rather than
/// answer `false` for want of a class. The thing may really be of a subclass, which only a query
/// that reports classes -- `world::get_actors`, `world::get_components` -- can say.
fn object_of<T: Class>(value: &pb::Value) -> Result<T, TempoError> {
    note_origin::<T>();
    Ok(declared::<T>(object_reference(value)?))
}

/// Handles to what an array or set of references names, each typed as [`object_of`] types one.
fn objects_of<T: Class>(value: &pb::Value) -> Result<Vec<T>, TempoError> {
    note_origin::<T>();
    Ok(reference_names(value)?
        .iter()
        .map(|name| declared::<T>(ObjectRef::from_reference_name(name)))
        .collect())
}

/// `object` as a `T`, carrying `T`'s class when nothing said what the class really is.
fn declared<T: Class>(object: ObjectRef) -> T {
    if object.class_path.is_empty() {
        T::from_object(object.with_class(T::PATH))
    } else {
        T::from_object(object)
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

    pub fn get<T: FromValue + 'static>(&self, member: &str) -> Result<T, TempoError> {
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
    pub fn load<T: FromValue + 'static>(&self) -> Result<T, TempoError> {
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
        let (op, description) = call_op(self, function, args.clone());
        perform(op, description, call_results, || {
            crate::tempo_world::call_function(
                self.actor.clone(),
                self.component.clone(),
                function.to_string(),
                args,
                true,
                self.subsystem.clone(),
            )
        })
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
            self.subsystem.clone(),
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
///
/// An implementor must not be zero-sized: the registry tells tables apart by address, and a
/// zero-sized static need not have an address of its own.
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

    /// Another table these classes lean on, registered with them: a project's types re-export the
    /// published ones they share, and leave those entries to the published table.
    fn depends_on(&self) -> Option<&'static (dyn Hierarchy + 'static)> {
        None
    }
}

/// The things a generated crate records an identity for.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum EntryKind {
    Class,
    Struct,
    Enum,
    /// An asset in the catalog, by path. Its "hash" is its class path: one that moved is absent,
    /// one whose class changed is a different asset under the same name.
    Asset,
}

impl EntryKind {
    /// What one entry of this kind is called, for a message that names a finding.
    pub fn label(self) -> &'static str {
        match self {
            EntryKind::Class => "class",
            EntryKind::Struct => "struct",
            EntryKind::Enum => "enum",
            EntryKind::Asset => "asset",
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
    let mut next = Some(hierarchy);
    while let Some(hierarchy) = next {
        if let Ok(mut registered) = HIERARCHIES.write() {
            // Every handle made through a crate registers it, so this runs constantly for the same
            // table. Compared by address, since each crate has exactly one -- the table's address
            // alone, not the vtable a `dyn` pointer also carries, since each crate that names a
            // table may instantiate a vtable for it of its own.
            if registered.iter().any(|existing| {
                std::ptr::addr_eq(*existing as *const dyn Hierarchy, hierarchy as *const dyn Hierarchy)
            }) {
                return;
            }
            registered.push(hierarchy);
        }
        next = hierarchy.depends_on();
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
    pub fn get_enums<T: UnrealEnum + 'static>(&self, property: &str) -> Result<Vec<T>, TempoError> {
        self.read_as(property, enums_of::<T>)
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
            self.subsystem.clone(),
        );
        async move { enums_of(&request.await?) }
    }

    /// Read an array or set of enums through its `BlueprintGetter`.
    pub fn get_enums_via<T: UnrealEnum + 'static>(&self, getter: &str) -> Result<Vec<T>, TempoError> {
        self.call_as(getter, Vec::new(), enums_of::<T>)
    }

    /// Read an array or set of enums through its `BlueprintGetter`, without blocking.
    pub fn get_enums_via_async<T: UnrealEnum>(
        &self,
        getter: &str,
    ) -> impl Future<Output = Result<Vec<T>, TempoError>> + '_ {
        let request = self.call_raw_async(getter, Vec::new());
        async move { enums_of(returned(&request.await?)?) }
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
        _ => Err(wrong(value, "an actor or component array or set")),
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
        _ => Err(wrong(value, "an enum array or set")),
    }
}

fn enums_of<T: UnrealEnum>(value: &pb::Value) -> Result<Vec<T>, TempoError> {
    enum_names(value)?.iter().map(|name| T::from_unreal_name(name)).collect()
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
        offset: Option<FVector>,
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
        let trim = Trim { offset: Some(FVector { x: 1.0, y: 2.0, z: 3.0 }), label: Some("a".into()) };
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

    /// A batch abandoned by its body sends nothing, which is what lets these run without a sim.
    fn abandoned<F: FnOnce()>(body: F) {
        let outcome = batch(|| {
            body();
            Err(TempoError::Protocol("abandoned".into()))
        });
        assert!(outcome.is_err());
    }

    #[test]
    fn a_deferred_read_is_queued_not_sent() {
        let object = ObjectRef::actor("Somewhere");
        abandoned(|| {
            let deferred = defer(|| object.get::<bool>("bHidden")).expect("defer");
            assert!(!deferred.is_done(), "nothing has been sent, so nothing has come back");
            assert!(deferred.take().is_err(), "a pending result is not a value");
            let queued = BATCH.with(|slot| slot.borrow().as_ref().map(Vec::len));
            assert_eq!(queued, Some(1), "the read waits in the batch");
        });
    }

    #[test]
    fn a_deferred_closure_makes_exactly_one_read() {
        let object = ObjectRef::actor("Somewhere");
        abandoned(|| {
            let both = defer(|| Ok((object.get::<bool>("A")?, object.get::<bool>("B")?)));
            // The first read short-circuits the closure, so its result is not the read's own.
            assert!(both.is_err());
        });
    }

    #[test]
    fn a_result_turned_into_another_type_cannot_be_deferred() {
        let object = ObjectRef::actor("Somewhere");
        abandoned(|| {
            let fast = defer(|| Ok(object.get::<f64>("Speed")? > 1.0));
            assert!(fast.is_err(), "the deferred value would be the read's f64, not the closure's bool");
        });
    }

    #[test]
    fn outside_a_batch_a_deferred_value_is_ready_at_once() {
        let ready = defer(|| Ok(7)).expect("defer");
        assert!(ready.is_done());
        assert_eq!(ready.take().expect("value"), 7);
    }

    #[test]
    fn a_write_inside_a_batch_is_queued() {
        let object = ObjectRef::actor("Somewhere");
        abandoned(|| {
            object.set("bHidden", true).expect("queued, not sent");
            object.set_via("SetScale", "InScale", 2.0).expect("queued, not sent");
            let kinds: Vec<_> = BATCH.with(|slot| {
                slot.borrow()
                    .as_ref()
                    .map(|queue| {
                        queue
                            .iter()
                            .map(|queued| match &queued.op.op {
                                Some(pb::batch_op::Op::Set(_)) => "set",
                                Some(pb::batch_op::Op::Call(_)) => "call",
                                _ => "other",
                            })
                            .collect()
                    })
                    .unwrap_or_default()
            });
            assert_eq!(kinds, ["set", "call"], "an accessor write travels in the same batch");
        });
    }

    #[test]
    fn an_array_of_structs_round_trips() {
        let trims = vec![Trim { label: Some("a".into()), ..Default::default() }, Trim::default()];
        assert_eq!(Vec::<Trim>::from_value(&trims.to_value()).unwrap(), trims);
    }

    fn value(inner: pb::value::Value) -> pb::Value {
        pb::Value { value: Some(inner) }
    }

    fn strings(values: &[&str]) -> pb::StringArray {
        pb::StringArray { values: values.iter().map(|s| s.to_string()).collect() }
    }

    #[test]
    fn a_set_reads_as_the_vec_an_array_does() {
        // The server encodes a TSet in a variant of its own, and the generated code types both
        // containers as Vec -- so the set variants have to read.
        use pb::value::Value as V;
        let ints: Vec<i32> = Vec::from_value(&value(V::IntSetValue(pb::IntArray { values: vec![1, 2] })))
            .expect("an int set");
        assert_eq!(ints, [1, 2]);
        let longs: Vec<i64> =
            Vec::from_value(&value(V::Int64SetValue(pb::Int64Array { values: vec![1 << 40] })))
                .expect("an int64 set");
        assert_eq!(longs, [1 << 40]);
        let floats: Vec<f64> =
            Vec::from_value(&value(V::FloatSetValue(pb::FloatArray { values: vec![0.5] })))
                .expect("a float set");
        assert_eq!(floats, [0.5]);
        let bools: Vec<bool> =
            Vec::from_value(&value(V::BoolSetValue(pb::BoolArray { values: vec![true] })))
                .expect("a bool set");
        assert_eq!(bools, [true]);
        let names: Vec<String> =
            Vec::from_value(&value(V::StringSetValue(strings(&["a", "b"])))).expect("a string set");
        assert_eq!(names, ["a", "b"]);
        let classes: Vec<ClassRef> =
            Vec::from_value(&value(V::ClassSetValue(strings(&["/Script/Engine.Pawn"]))))
                .expect("a class set");
        assert_eq!(classes, [ClassRef::new("/Script/Engine.Pawn")]);
        let assets: Vec<AssetRef> =
            Vec::from_value(&value(V::AssetSetValue(strings(&["/Game/M_Paint"])))).expect("an asset set");
        assert_eq!(assets, [AssetRef::from_path("/Game/M_Paint")]);
    }

    #[test]
    fn a_vec_is_written_as_an_array() {
        // Whichever container the property is: the server stores into the one it has.
        match vec![1, 2].to_value().value {
            Some(pb::value::Value::IntArrayValue(inner)) => assert_eq!(inner.values, [1, 2]),
            other => panic!("expected an int array, got {:?}", other),
        }
    }

    #[test]
    fn a_wrong_variant_is_named_in_the_error() {
        let error = Vec::<String>::from_value(&value(pb::value::Value::IntSetValue(pb::IntArray {
            values: vec![1],
        })))
        .expect_err("an int set is not a string container")
        .to_string();
        assert!(error.contains("a string array or set"), "{error}");
        assert!(error.contains("int_set_value"), "{error}");
    }

    #[test]
    fn a_panic_inside_a_batch_leaves_the_thread_usable() {
        let object = ObjectRef::actor("Somewhere");

        // A panic the body does not catch unwinds through `batch`, as one does through a tokio task
        // or `catch_unwind`. The thread has to come out of it in no batch at all, or every later
        // write would queue for good and every later read be refused.
        let caught = std::panic::catch_unwind(|| {
            batch(|| -> Result<(), TempoError> {
                object.set("bHidden", true)?;
                panic!("the body panicked")
            })
        });
        assert!(caught.is_err());
        assert!(!batch_open(), "the batch the panic unwound through is closed");

        let caught = std::panic::catch_unwind(|| {
            batch_async(|| -> Result<(), TempoError> { panic!("the body panicked") })
        });
        assert!(caught.is_err());
        assert!(!batch_open());
        assert!(!ASYNC.with(|flag| *flag.borrow()), "the async flag is back to what it was");

        abandoned(|| {
            let caught = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
                defer(|| -> Result<bool, TempoError> { panic!("the read panicked") })
            }));
            assert!(caught.is_err());
            assert!(CAPTURE.with(|slot| slot.borrow().is_none()), "no longer capturing");
            assert!(batch_open(), "the batch around the deferred read stays open");
            let deferred = defer(|| object.get::<bool>("bHidden")).expect("a later defer works");
            assert!(!deferred.is_done());
        });
        assert!(!batch_open());

        // A fresh batch starts from nothing the panicked one queued.
        abandoned(|| {
            object.set("bHidden", false).expect("queued, not sent");
            assert_eq!(BATCH.with(|slot| slot.borrow().as_ref().map(Vec::len)), Some(1));
        });
    }

    #[repr(transparent)]
    struct Pawn(ObjectRef);

    // SAFETY: a transparent newtype over ObjectRef, as the generator writes one.
    unsafe impl Class for Pawn {
        const PATH: &'static str = "/Script/Engine.Pawn";
        fn from_object(object: ObjectRef) -> Self {
            Self(object)
        }
        fn as_object(&self) -> &ObjectRef {
            &self.0
        }
        fn hierarchy() -> &'static (dyn Hierarchy + 'static) {
            &PAWN_TABLE
        }
    }

    // Not zero-sized, as a registered table must not be.
    struct PawnTable(#[allow(dead_code)] u8);

    static PAWN_TABLE: PawnTable = PawnTable(0);

    impl Hierarchy for PawnTable {
        fn superclass(&self, class_path: &str) -> Option<&'static str> {
            match class_path {
                "/Script/Engine.Pawn" => Some("/Script/Engine.Actor"),
                "/Script/Engine.Actor" => Some("/Script/CoreUObject.Object"),
                _ => None,
            }
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

    #[test]
    fn a_reference_read_from_a_property_is_typed_as_declared() {
        // The wire names the actor and nothing else, so the handle carries the declared class --
        // the type system has just said this is a Pawn, and is_a agrees.
        let truck: Pawn = object_of(&value(pb::value::Value::ActorValue("Truck".into())))
            .expect("an actor reference");
        assert_eq!(truck.as_object().actor_name(), "Truck");
        assert_eq!(truck.as_object().class_path(), Pawn::PATH);
        assert!(truck.as_object().is_a(Pawn::PATH));
        assert!(
            truck.as_object().is_a("/Script/Engine.Actor"),
            "answered from the table the handle's crate registered on the way"
        );
        assert!(!truck.as_object().is_a("/Script/Engine.Character"));

        let handles: Vec<Pawn> =
            objects_of(&value(pb::value::Value::ActorSetValue(strings(&["A", "B:Mesh"]))))
                .expect("a set of references");
        assert_eq!(handles.len(), 2);
        assert!(handles.iter().all(|handle| handle.as_object().is_a("/Script/Engine.Actor")));
        assert_eq!(handles[1].as_object().component_name(), "Mesh");
    }
}
