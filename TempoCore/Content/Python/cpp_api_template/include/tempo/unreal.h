// Copyright Tempo Simulation, LLC. All Rights Reserved
//
// The hand-written base of the typed World API: the handle every generated class derives from,
// the value types that carry Unreal's structs, and the machinery that turns a method call into a
// TempoWorld RPC. Mirrors src/unreal.rs in the Rust API.
//
// Everything in this header is in Unreal's own units -- centimeters, degrees, left-handed -- which
// is the one place in the Tempo API where that is true. Raw UPROPERTY values carry no semantic
// tagging, so converting them would be guesswork; the typed API therefore does not convert at all
// and says so here rather than per property.
//
// Which is why the value types here are Unreal's own, named and shaped as Unreal's are -- FVector,
// FRotator in pitch-yaw-roll order, FTransform with its quaternion and scale -- and not the
// TempoCore messages every other Tempo RPC speaks, which are meters, radians and right-handed.
// TempoCore::Vector is not an FVector. tempo/conversion.h converts between the two.
//
// Like the rest of the C++ client this never throws: every call that can fail returns a Result.

#pragma once

#include <concepts>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "tempo/error.h"
#include "tempo/result.h"
#include "tempo/streaming.h"

#include "TempoCore/Geometry.pb.h"
#include "TempoWorld/WorldControl.pb.h"

namespace tempo::unreal {

// -- value types --------------------------------------------------------------------------------

/// Unreal's FVector: a location or direction in centimeters, left-handed.
struct FVector {
	double x = 0.0;
	double y = 0.0;
	double z = 0.0;
	bool operator==(const FVector&) const = default;
};

/// Unreal's FVector2D.
struct FVector2D {
	double x = 0.0;
	double y = 0.0;
	bool operator==(const FVector2D&) const = default;
};

/// Unreal's FIntVector.
struct FIntVector {
	std::int32_t x = 0;
	std::int32_t y = 0;
	std::int32_t z = 0;
	bool operator==(const FIntVector&) const = default;
};

/// Unreal's FIntPoint.
struct FIntPoint {
	std::int32_t x = 0;
	std::int32_t y = 0;
	bool operator==(const FIntPoint&) const = default;
};

/// Unreal's FRotator: an orientation in degrees, left-handed, in Unreal's own order -- pitch, yaw,
/// roll -- so FRotator{10, 20, 30} means what it does in Unreal.
struct FRotator {
	double pitch = 0.0;
	double yaw = 0.0;
	double roll = 0.0;
	bool operator==(const FRotator&) const = default;
};

/// Unreal's FQuat: a rotation, left-handed.
struct FQuat {
	double x = 0.0;
	double y = 0.0;
	double z = 0.0;
	double w = 1.0;
	bool operator==(const FQuat&) const = default;
};

/// Unreal's FTransform: a rotation as a quaternion, a translation in centimeters and a 3D scale,
/// left-handed, in Unreal's own order. The default is the identity, scale 1.
struct FTransform {
	FQuat rotation;
	FVector translation;
	FVector scale3d{1.0, 1.0, 1.0};
	bool operator==(const FTransform&) const = default;
};

/// Unreal's FColor: 8-bit RGBA, sRGB. The default is opaque black.
struct FColor {
	std::uint8_t r = 0;
	std::uint8_t g = 0;
	std::uint8_t b = 0;
	std::uint8_t a = 255;
	bool operator==(const FColor&) const = default;
};

/// Unreal's FLinearColor: floating-point RGBA, linear. The default is opaque black.
struct FLinearColor {
	float r = 0.0f;
	float g = 0.0f;
	float b = 0.0f;
	float a = 1.0f;
	bool operator==(const FLinearColor&) const = default;
};

// -- the hierarchy a generated library carries ---------------------------------------------------

/// The things a generated library records an identity for.
enum class EntryKind {
	Class,
	Struct,
	Enum,
	/// An asset in the catalog, by path. Its "hash" is its class path: one that moved is absent,
	/// one whose class changed is a different asset under the same name.
	Asset,
};

/// What one entry of a kind is called, for a message that names a finding.
std::string_view label(EntryKind kind);

/// Identifies the build a generated library was made from.
struct SchemaIdentity {
	std::string schema_version;
	std::string engine_version;
	std::string content_hash;
};

/// One entry and what it hashed to.
using HashEntry = std::pair<std::string_view, std::string_view>;

/// What a generated library tells the runtime about its build.
///
/// Only what has to be data rather than code: which class derives from which (for is_a), and what
/// each entry hashed to (for world::check_schema). Everything else a generated method needs, it was
/// generated knowing.
class Hierarchy {
public:
	virtual ~Hierarchy() = default;

	/// The class a class derives from, or nullopt for a root or an unknown class.
	virtual std::optional<std::string_view> superclass(std::string_view class_path) const = 0;

	/// The schema this build's classes were generated from.
	virtual SchemaIdentity header() const = 0;

	/// Every entry of one kind and what it hashed to, sorted by name.
	virtual std::span<const HashEntry> hashes(EntryKind kind) const = 0;
};

/// Make a generated library's hierarchy available. Idempotent, and called by every path that makes
/// a handle, because C++ offers no import side effect that a static library's linker would keep.
void register_classes(const Hierarchy& hierarchy);

/// Every registered hierarchy, in registration order. Empty when no generated classes are linked.
std::vector<const Hierarchy*> hierarchies();

/// What a class derives from, according to whichever registered hierarchy knows it.
std::optional<std::string_view> superclass_of(std::string_view class_path);

// -- the handle ---------------------------------------------------------------------------------

template <typename T>
class PropertyStream;
class StructRef;

/// What a handle addresses, and the class it was found to have.
///
/// Addressed the way every TempoWorld RPC addresses things: an actor name, and a component name
/// when the handle is to a component rather than to the actor itself -- or, for a subsystem, its
/// class alone, since there is one per world (or game instance, or engine).
class ObjectRef {
public:
	ObjectRef() = default;

	/// A handle to an actor.
	static ObjectRef actor(std::string name);

	/// A handle to one of an actor's components.
	static ObjectRef component(std::string actor, std::string component);

	/// A handle to a subsystem, by its class path. See world::get_subsystem.
	static ObjectRef subsystem(std::string class_path);

	/// Parse the name a reference-valued property carried back: "Actor" or "Actor:Component".
	static ObjectRef from_reference_name(std::string_view name);

	/// The same handle, recording the class the thing really has.
	///
	/// A query for a base class hands back handles typed as that base -- which is what lets the
	/// call site compile -- so the handle's own C++ type is not the answer to what it points at.
	/// is_a needs this to tell the difference.
	ObjectRef with_class(std::string class_path) const;

	const std::string& actor_name() const { return actor_; }
	const std::string& component_name() const { return component_; }
	const std::string& subsystem_class() const { return subsystem_; }

	/// The class the thing was found to have, or empty when whoever built the handle did not know.
	const std::string& class_path() const { return class_path_; }

	/// How a reference-valued property names this.
	std::string reference_name() const;

	/// Whether the thing this handle addresses is `ancestor`, or derives from it. A question about
	/// the simulation, answered from the registered hierarchies, so it costs no call.
	bool is_a(std::string_view ancestor) const;

	/// Two handles are equal when they address the same thing. The class is left out: the same actor
	/// found through a query for a base class and read from a reference-valued property records two
	/// different classes, and is one actor.
	bool operator==(const ObjectRef& other) const {
		return actor_ == other.actor_ && component_ == other.component_ && subsystem_ == other.subsystem_;
	}

	// The string-keyed surface the generated methods call. Usable directly, as the escape hatch:
	// nothing about a name passed here is checked until the server looks it up.

	template <typename T> Result<T> get(std::string_view property) const;
	template <typename T> Result<void> set(std::string_view property, const T& value) const;

	/// Read through a BlueprintGetter, which may be the only thing that computes the value.
	template <typename T> Result<T> get_via(std::string_view getter) const;

	/// Write through a BlueprintSetter, which may be the only thing that clamps the value or marks
	/// render state dirty. `argument` is what the setter calls its parameter: CallFunction matches
	/// arguments by name.
	template <typename T>
	Result<void> set_via(std::string_view setter, std::string_view argument, const T& value) const;

	/// Call a function, and ignore what it returns.
	Result<void> call(std::string_view function, std::vector<TempoWorld::FunctionArg> args) const;

	/// Call a function, and read its return value.
	template <typename T>
	Result<T> call_for(std::string_view function, std::vector<TempoWorld::FunctionArg> args) const;

	/// Call a function, and hand back every result -- for a method with out parameters.
	Result<TempoWorld::CallFunctionResponse> call_raw(
		std::string_view function, std::vector<TempoWorld::FunctionArg> args) const;

	/// One named result of a call: an out parameter, or "ReturnValue".
	template <typename T>
	static Result<T> result_of(const TempoWorld::CallFunctionResponse& response, std::string_view name);

	/// Yield this property's value once per simulation tick, until the caller stops asking.
	template <typename T> Result<PropertyStream<T>> watch(std::string_view property) const;

	/// A struct-valued property, for a generated proxy to reach into.
	StructRef struct_ref(std::string_view property) const;

	/// Point an actor- or component-valued property at something, or an array of them.
	Result<void> set_actor_ref(std::string_view property, const ObjectRef& value) const;
	Result<void> set_component_ref(std::string_view property, const ObjectRef& value) const;
	Result<void> set_actor_refs(std::string_view property, const std::vector<ObjectRef>& values) const;
	Result<void> set_component_refs(std::string_view property, const std::vector<ObjectRef>& values) const;

private:
	friend class StructRef;

	std::string actor_;
	std::string component_;
	std::string subsystem_;
	std::string class_path_;
};

/// The base of every generated class: a handle, and nothing a scenario can mistake for a member.
///
/// Generated classes derive from it through Unreal's own single inheritance, so an inherited member
/// is inherited and a derived handle is a base one, by the language's rules. Copying one copies the
/// address, not the thing.
class Handle {
public:
	explicit Handle(ObjectRef object) : object_(std::move(object)) {}

	/// The address underneath, and the string-keyed escape hatch.
	const ObjectRef& object() const { return object_; }

	/// Whether the thing this addresses is `ancestor`, or derives from it.
	bool is_a(std::string_view ancestor) const { return object_.is_a(ancestor); }

	template <typename T>
	bool is_a() const { return object_.is_a(T::kPath); }

	bool operator==(const Handle&) const = default;

protected:
	ObjectRef object_;
};

/// What a generated class is: a Handle that knows its Unreal class path and its library's
/// hierarchy. world::get_actors<T> and the rest take only these, so a type that is not a class does
/// not compile.
template <typename T>
concept Class = std::derived_from<T, Handle> && std::constructible_from<T, ObjectRef> && requires {
	{ T::kPath } -> std::convertible_to<std::string_view>;
	{ T::hierarchy() } -> std::same_as<const Hierarchy&>;
};

/// Note the library a handle came from, so its hierarchy is there to ask about later.
template <Class T>
void note_origin() { register_classes(T::hierarchy()); }

/// A reference to an Unreal class, by path (/Script/Engine.StaticMeshActor) or by name.
struct ClassRef {
	std::string path;

	/// The class a generated type stands for.
	template <Class T>
	static ClassRef of() { return ClassRef{std::string(T::kPath)}; }

	bool operator==(const ClassRef&) const = default;
};

/// The class of an asset reference whose class nothing knows -- the default for AssetRef.
struct AnyClass {};

/// A reference to a cooked asset, by path, typed by the asset's class.
///
/// The generated catalog holds one per asset the build cooked, and an asset-valued setter takes the
/// class it accepts. So a moved or renamed asset is a name that no longer resolves, and a material
/// where a mesh is wanted does not compile. One converts to an AssetRef of any superclass, which is
/// what lets a Material go where a MaterialInterface is wanted. Built from a path, it is whatever
/// class it is written as: the escape hatch for a path from data, visible where it is written.
template <typename T = AnyClass>
class AssetRef {
public:
	AssetRef() = default;
	explicit AssetRef(std::string path) : path_(std::move(path)) {}

	template <typename U>
		requires(!std::is_same_v<T, U> && (std::is_same_v<T, AnyClass> || std::is_base_of_v<T, U>))
	AssetRef(const AssetRef<U>& other) : path_(other.path()) {}

	const std::string& path() const { return path_; }

	bool operator==(const AssetRef&) const = default;

private:
	std::string path_;
};

// -- Value conversion ---------------------------------------------------------------------------

/// How a C++ type is read out of and written into a TempoWorld::Value. Specialized below for the
/// runtime's own types; generated enums and struct values come in through EnumTraits and
/// StructTraits.
template <typename T, typename Enable = void>
struct ValueTraits;

/// An Unreal enum, which travels as its authored name rather than its numeric value, because that
/// is what survives someone reordering the enumerators. Specialized for each generated enum.
template <typename E>
struct EnumTraits;

template <typename E>
concept UnrealEnum = std::is_enum_v<E> && requires(E value, std::string_view name) {
	{ EnumTraits<E>::kName } -> std::convertible_to<std::string_view>;
	{ EnumTraits<E>::name_of(value) } -> std::convertible_to<std::string_view>;
	{ EnumTraits<E>::from_name(name) } -> std::same_as<std::optional<E>>;
};

class StructFields;
class StructBuilder;

/// A generated struct value: its C++ name, and how its members are read and written. Specialized
/// for each generated value type.
template <typename T>
struct StructTraits;

template <typename T>
concept StructType = requires(const T& value, const StructFields& fields, StructBuilder& builder) {
	{ StructTraits<T>::kName } -> std::convertible_to<std::string_view>;
	{ StructTraits<T>::from(fields) } -> std::same_as<Result<T>>;
	StructTraits<T>::to(value, builder);
};

namespace detail {

TempoError wrong(const TempoWorld::Value& value, std::string_view wanted);

/// The type tag a deferred result is checked against. RTTI is off in every consumer -- the vendored
/// protobuf ships without typeinfo -- so a tag's address stands in for typeid.
template <typename T>
const void* tag() {
	static const char unique = 0;
	return &unique;
}

}  // namespace detail

#define TEMPO_SCALAR_VALUE(Type, Case, Field, Wanted)                                              \
	template <>                                                                                    \
	struct ValueTraits<Type> {                                                                     \
		static Result<Type> from(const TempoWorld::Value& value) {                                 \
			if (value.value_case() != TempoWorld::Value::Case) return detail::wrong(value, Wanted); \
			return static_cast<Type>(value.Field());                                               \
		}                                                                                          \
		static TempoWorld::Value to(const Type& v) {                                               \
			TempoWorld::Value value;                                                               \
			value.set_##Field(v);                                                                  \
			return value;                                                                          \
		}                                                                                          \
	};

TEMPO_SCALAR_VALUE(bool, kBoolValue, bool_value, "a bool")
TEMPO_SCALAR_VALUE(std::int32_t, kIntValue, int_value, "an int")
TEMPO_SCALAR_VALUE(std::int64_t, kInt64Value, int64_value, "an int64")
TEMPO_SCALAR_VALUE(std::string, kStringValue, string_value, "a string")

#undef TEMPO_SCALAR_VALUE

// Unreal's float and double properties both travel in float_value, which is 32-bit on the wire. The
// typed surface says double for both, because that is what Unreal stores; the narrowing is the
// wire's.
template <>
struct ValueTraits<double> {
	static Result<double> from(const TempoWorld::Value& value);
	static TempoWorld::Value to(const double& v);
};

#define TEMPO_DECLARE_VALUE(Type)                                \
	template <>                                                  \
	struct ValueTraits<Type> {                                   \
		static Result<Type> from(const TempoWorld::Value& value); \
		static TempoWorld::Value to(const Type& v);              \
	};

TEMPO_DECLARE_VALUE(FVector)
TEMPO_DECLARE_VALUE(FVector2D)
TEMPO_DECLARE_VALUE(FIntVector)
TEMPO_DECLARE_VALUE(FIntPoint)
TEMPO_DECLARE_VALUE(FRotator)
TEMPO_DECLARE_VALUE(FQuat)
TEMPO_DECLARE_VALUE(FTransform)
TEMPO_DECLARE_VALUE(FColor)
TEMPO_DECLARE_VALUE(FLinearColor)
TEMPO_DECLARE_VALUE(ClassRef)
TEMPO_DECLARE_VALUE(std::vector<bool>)
TEMPO_DECLARE_VALUE(std::vector<std::int32_t>)
TEMPO_DECLARE_VALUE(std::vector<std::int64_t>)
TEMPO_DECLARE_VALUE(std::vector<double>)
TEMPO_DECLARE_VALUE(std::vector<std::string>)
TEMPO_DECLARE_VALUE(std::vector<ClassRef>)

#undef TEMPO_DECLARE_VALUE

namespace detail {

Result<std::string> asset_path(const TempoWorld::Value& value);
TempoWorld::Value asset_value(const std::string& path);
Result<std::vector<std::string>> asset_paths(const TempoWorld::Value& value);
TempoWorld::Value asset_array_value(const std::vector<std::string>& paths);
Result<std::string> enum_name(const TempoWorld::Value& value, std::string_view enum_type);
TempoWorld::Value enum_value(std::string_view name);
Result<std::vector<std::string>> enum_names(const TempoWorld::Value& value);
TempoWorld::Value enum_array_value(const std::vector<std::string>& names);
Result<ObjectRef> object_reference(const TempoWorld::Value& value);
Result<std::vector<ObjectRef>> object_references(const TempoWorld::Value& value);
Result<std::vector<TempoWorld::StructValue>> struct_values(const TempoWorld::Value& value);
TempoWorld::Value struct_array_value(std::vector<TempoWorld::Value> values);

}  // namespace detail

template <typename T>
struct ValueTraits<AssetRef<T>> {
	static Result<AssetRef<T>> from(const TempoWorld::Value& value) {
		auto path = detail::asset_path(value);
		if (!path) return std::move(path).error();
		return AssetRef<T>(std::move(path).value());
	}
	static TempoWorld::Value to(const AssetRef<T>& v) { return detail::asset_value(v.path()); }
};

template <typename T>
struct ValueTraits<std::vector<AssetRef<T>>> {
	static Result<std::vector<AssetRef<T>>> from(const TempoWorld::Value& value) {
		auto paths = detail::asset_paths(value);
		if (!paths) return std::move(paths).error();
		std::vector<AssetRef<T>> assets;
		for (auto& path : paths.value()) assets.emplace_back(std::move(path));
		return assets;
	}
	static TempoWorld::Value to(const std::vector<AssetRef<T>>& v) {
		std::vector<std::string> paths;
		for (const auto& asset : v) paths.push_back(asset.path());
		return detail::asset_array_value(paths);
	}
};

template <UnrealEnum E>
struct ValueTraits<E> {
	static Result<E> from(const TempoWorld::Value& value) {
		auto name = detail::enum_name(value, EnumTraits<E>::kName);
		if (!name) return std::move(name).error();
		if (auto found = EnumTraits<E>::from_name(name.value())) return *found;
		return TempoError::protocol("the sim reported \"" + name.value() + "\" for " +
									std::string(EnumTraits<E>::kName) +
									", which is not one of its enumerators");
	}
	static TempoWorld::Value to(const E& v) { return detail::enum_value(EnumTraits<E>::name_of(v)); }
};

template <UnrealEnum E>
struct ValueTraits<std::vector<E>> {
	static Result<std::vector<E>> from(const TempoWorld::Value& value) {
		auto names = detail::enum_names(value);
		if (!names) return std::move(names).error();
		std::vector<E> values;
		for (const auto& name : names.value()) {
			auto found = EnumTraits<E>::from_name(name);
			if (!found) {
				return TempoError::protocol("the sim reported \"" + name + "\" for " +
											std::string(EnumTraits<E>::kName) +
											", which is not one of its enumerators");
			}
			values.push_back(*found);
		}
		return values;
	}
	static TempoWorld::Value to(const std::vector<E>& v) {
		std::vector<std::string> names;
		for (const auto& value : v) names.emplace_back(EnumTraits<E>::name_of(value));
		return detail::enum_array_value(names);
	}
};

/// A reference-valued property read back as a typed handle. The wire carries a name and nothing
/// else, so the handle records the property's declared class, as a component accessor does: it is
/// what the type system already says the pointee is, and without it is_a would deny the handle is
/// even a T.
template <Class T>
struct ValueTraits<T> {
	static Result<T> from(const TempoWorld::Value& value) {
		note_origin<T>();
		auto object = detail::object_reference(value);
		if (!object) return std::move(object).error();
		return T(object.value().with_class(std::string(T::kPath)));
	}
};

template <Class T>
struct ValueTraits<std::vector<T>> {
	static Result<std::vector<T>> from(const TempoWorld::Value& value) {
		note_origin<T>();
		auto objects = detail::object_references(value);
		if (!objects) return std::move(objects).error();
		std::vector<T> handles;
		for (const auto& object : objects.value()) handles.emplace_back(object.with_class(std::string(T::kPath)));
		return handles;
	}
};

// -- struct values -------------------------------------------------------------------------------
//
// A struct with no first-class representation travels as a StructValue: its members by name.
// Generated code declares one C++ struct per Unreal struct that can travel that way, every member a
// std::optional, and specializes StructTraits for it through the two helpers below -- so the
// encoding is written once, here, rather than once per generated struct.
//
// Every member is optional because a write is a merge: only the members present are written, and
// the rest of the destination keeps what it holds.

/// The members of a StructValue the server sent, for a generated StructTraits to read.
class StructFields {
public:
	/// The fields of a struct value, checking it is the struct expected.
	static Result<StructFields> of(const TempoWorld::Value& value, std::string_view name);

	/// One member, or nullopt when the server did not send it.
	template <typename T>
	Result<std::optional<T>> get(std::string_view name) const {
		const TempoWorld::Value* found = find(name);
		if (found == nullptr) return std::optional<T>();
		// An unset reference reads as an empty name, which is no handle at all.
		if constexpr (Class<T>) {
			if ((found->value_case() == TempoWorld::Value::kActorValue && found->actor_value().empty()) ||
				(found->value_case() == TempoWorld::Value::kComponentValue &&
				 found->component_value().empty())) {
				return std::optional<T>();
			}
		}
		auto value = ValueTraits<T>::from(*found);
		if (!value) return std::move(value).error();
		return std::optional<T>(std::move(value).value());
	}

private:
	explicit StructFields(const TempoWorld::StructValue* value) : value_(value) {}
	const TempoWorld::Value* find(std::string_view name) const;

	const TempoWorld::StructValue* value_;
};

/// A StructValue being built, for a generated StructTraits to fill in.
class StructBuilder {
public:
	explicit StructBuilder(std::string_view name);

	/// One member, when it has been given. An unset member is not sent, which leaves the
	/// destination's own value in place.
	template <typename T>
	void field(std::string_view name, const std::optional<T>& value) {
		if (value) push(name, ValueTraits<T>::to(*value));
	}

	/// One member referring to an actor.
	template <Class T>
	void actor(std::string_view name, const std::optional<T>& value) {
		if (value) push_actor(name, value->object());
	}

	/// One member referring to a component.
	template <Class T>
	void component(std::string_view name, const std::optional<T>& value) {
		if (value) push_component(name, value->object());
	}

	TempoWorld::Value finish() &&;

private:
	void push(std::string_view name, TempoWorld::Value value);
	void push_actor(std::string_view name, const ObjectRef& object);
	void push_component(std::string_view name, const ObjectRef& object);

	TempoWorld::StructValue value_;
};

template <StructType T>
struct ValueTraits<T> {
	static Result<T> from(const TempoWorld::Value& value) {
		auto fields = StructFields::of(value, StructTraits<T>::kName);
		if (!fields) return std::move(fields).error();
		return StructTraits<T>::from(fields.value());
	}
	static TempoWorld::Value to(const T& v) {
		StructBuilder builder(StructTraits<T>::kName);
		StructTraits<T>::to(v, builder);
		return std::move(builder).finish();
	}
};

template <StructType T>
struct ValueTraits<std::vector<T>> {
	static Result<std::vector<T>> from(const TempoWorld::Value& value) {
		auto elements = detail::struct_values(value);
		if (!elements) return std::move(elements).error();
		std::vector<T> values;
		for (auto& element : elements.value()) {
			TempoWorld::Value wrapped;
			*wrapped.mutable_struct_value() = std::move(element);
			auto decoded = ValueTraits<T>::from(wrapped);
			if (!decoded) return std::move(decoded).error();
			values.push_back(std::move(decoded).value());
		}
		return values;
	}
	static TempoWorld::Value to(const std::vector<T>& v) {
		std::vector<TempoWorld::Value> values;
		for (const auto& element : v) values.push_back(ValueTraits<T>::to(element));
		return detail::struct_array_value(std::move(values));
	}
};

// -- arguments ----------------------------------------------------------------------------------

/// One argument of a call, named the way the function declares it. Generated code names every
/// argument rather than relying on order, so adding a parameter to a Blueprint cannot silently
/// shift the others onto the wrong ones.
template <typename T>
TempoWorld::FunctionArg argument(std::string_view name, const T& value) {
	TempoWorld::FunctionArg arg;
	arg.set_name(std::string(name));
	*arg.mutable_value() = ValueTraits<T>::to(value);
	return arg;
}

/// An argument naming an actor. Its own function, because a handle's type cannot say whether the
/// server should look for an actor or a component.
TempoWorld::FunctionArg argument_actor(std::string_view name, const Handle& value);

/// An argument naming a component.
TempoWorld::FunctionArg argument_component(std::string_view name, const Handle& value);

// -- batching -----------------------------------------------------------------------------------

namespace detail {

template <typename T>
struct Slot {
	std::optional<Result<T>> value;
};

struct Captured {
	std::shared_ptr<void> slot;
	const void* tag;
};

using Resolver = std::function<void(Result<TempoWorld::BatchOpResult>)>;

bool batch_open();
void enqueue(TempoWorld::BatchOp op, std::string description, Resolver resolve = {});

/// Whether a defer() is collecting reads, taking this one if so.
bool capture(std::shared_ptr<void> slot, const void* tag);

/// Swap the reads defer() is collecting, returning what was there. nullopt is "not collecting".
std::optional<std::vector<Captured>> swap_capture(std::optional<std::vector<Captured>> captured);

/// Send everything queued in the open batch now.
Result<void> flush_open_batch();

/// Run one op whose result is wanted: as its own RPC when no batch is open, and through the batch
/// otherwise -- queued and deferred inside defer(), or sent at once with everything before it.
template <typename T>
Result<T> perform(TempoWorld::BatchOp op, std::string description,
				  std::function<Result<T>(const TempoWorld::BatchOpResult&)> decode,
				  const std::function<Result<T>()>& direct) {
	if (!batch_open()) return direct();

	auto slot = std::make_shared<Slot<T>>();
	enqueue(std::move(op), std::move(description),
			[slot, decode = std::move(decode)](Result<TempoWorld::BatchOpResult> outcome) {
				if (!outcome) {
					slot->value.emplace(std::move(outcome).error());
				} else {
					slot->value.emplace(decode(outcome.value()));
				}
			});
	if (capture(slot, tag<T>())) return TempoError::deferred();

	Result<void> flushed = flush_open_batch();
	if (slot->value) {
		if (slot->value->ok() || flushed.ok()) return std::move(*slot->value);
		// The op that failed first, which may be an earlier one this op depended on.
		return std::move(flushed).error();
	}
	if (!flushed) return std::move(flushed).error();
	return TempoError::protocol("the server answered the batch without this op");
}

TempoWorld::BatchOp get_op(const ObjectRef& object, std::string_view property, std::string* description);
TempoWorld::BatchOp call_op(const ObjectRef& object, std::string_view function,
							std::vector<TempoWorld::FunctionArg> args, std::string* description);
Result<TempoWorld::Value> read_value(const TempoWorld::BatchOpResult& result);
Result<TempoWorld::CallFunctionResponse> call_results(const TempoWorld::BatchOpResult& result);
Result<TempoWorld::Value> returned(const TempoWorld::CallFunctionResponse& response);
Result<TempoWorld::Value> get_property(const ObjectRef& object, std::string_view property);
Result<TempoWorld::CallFunctionResponse> call_function(const ObjectRef& object, std::string_view function,
														  std::vector<TempoWorld::FunctionArg> args);
Result<void> write(const ObjectRef& object, std::string_view property, TempoWorld::Value value);
Result<void> write_via(const ObjectRef& object, std::string_view setter, std::string_view argument,
					   TempoWorld::Value value);
Result<ServerStream<TempoWorld::Value>> stream_property(const ObjectRef& object, std::string_view property);

}  // namespace detail

/// Send what `body` does as one RunBatch: in order, and between two ticks of the simulation.
///
///     tempo::world::batch([&]() -> tempo::Result<void> {
///         if (auto r = lidar.set_horizontal_beams(1024); !r) return r;
///         if (auto r = lidar.set_vertical_beams(64); !r) return r;
///         return {};
///     });
///
/// Each typed write is one round trip, which is the cost of making it look like a field. A batch
/// collects them and flushes once, and since the server runs the whole batch between two ticks,
/// nothing simulates against a half-configured actor. A write through a BlueprintSetter is a call,
/// and travels in the same batch.
///
/// A read or call inside the batch comes back one of two ways. Through defer(), it is queued with
/// everything else and its result is available from the Deferred once the batch has run. Made
/// directly, it goes out at once with everything queued before it and returns its real result --
/// so it sees those writes -- at the price of a round trip of its own.
///
/// The server stops at the first failure, and this reports it, naming the op. Nothing is sent if
/// `body` fails before a read forced a flush: a half-applied configuration is harder to reason about
/// than one that never started.
Result<void> batch(const std::function<Result<void>()>& body);

/// A read or call queued in a batch, available once the batch has run.
template <typename T>
class Deferred {
public:
	explicit Deferred(std::shared_ptr<detail::Slot<T>> slot) : slot_(std::move(slot)) {}

	template <typename V>
	static Deferred ready(V value) {
		auto slot = std::make_shared<detail::Slot<T>>();
		slot->value.emplace(std::move(value));
		return Deferred(std::move(slot));
	}

	/// Whether the batch carrying this has run.
	bool is_done() const { return slot_->value.has_value(); }

	/// Take the result out: the value, the op's own error if it failed, or an error if the batch
	/// has not run.
	Result<T> take() {
		if (!slot_->value) {
			return TempoError::protocol("the batch has not run yet, or this result was already taken");
		}
		Result<T> result = std::move(*slot_->value);
		slot_->value.reset();
		return result;
	}

private:
	std::shared_ptr<detail::Slot<T>> slot_;
};

namespace detail {

template <typename R>
struct ResultValue;

template <typename T>
struct ResultValue<Result<T>> {
	using type = T;
};

}  // namespace detail

/// Defer one read or call into the open batch, typed by what it reads.
///
///     auto beams = tempo::world::defer([&] { return lidar.horizontal_beams(); });   // Deferred<int32_t>
///
/// The lambda runs, but the read it makes is queued rather than sent, and its value arrives with
/// the rest of the batch. It must make exactly one read or call and return its result unchanged.
/// Outside a batch the read simply happens, and the Deferred is ready at once.
///
/// Do nothing to the value inside the lambda. There is no value yet: the read returns an error that
/// only defer() understands, so working on a result has nothing to work on. A change of type is
/// caught and refused. Transform the result after Deferred::take. A generated method with out
/// parameters cannot be deferred, because it decodes its results after the call returns; call it
/// directly.
template <typename F>
auto defer(F&& read) -> Result<Deferred<typename detail::ResultValue<std::invoke_result_t<F>>::type>> {
	using T = typename detail::ResultValue<std::invoke_result_t<F>>::type;
	auto previous = detail::swap_capture(std::vector<detail::Captured>());
	auto returned = std::forward<F>(read)();
	auto captured = detail::swap_capture(std::move(previous)).value_or(std::vector<detail::Captured>());

	if (returned.ok() && captured.empty()) {
		// Answered without a request, or outside a batch: there is nothing to wait for.
		if constexpr (std::is_void_v<T>) {
			auto slot = std::make_shared<detail::Slot<T>>();
			slot->value.emplace();
			return Deferred<T>(std::move(slot));
		} else {
			return Deferred<T>::ready(std::move(returned).value());
		}
	}
	if (!returned.ok() && returned.error().kind() == ErrorKind::Deferred && captured.size() == 1) {
		if (captured.front().tag != detail::tag<T>()) {
			return TempoError::protocol(
				"a deferred lambda must return what its one read or call returns, unchanged "
				"(a method with out parameters cannot be deferred)");
		}
		return Deferred<T>(std::static_pointer_cast<detail::Slot<T>>(captured.front().slot));
	}
	if (!returned.ok() && captured.empty()) return std::move(returned).error();
	return TempoError::protocol("a deferred lambda must make exactly one read or call; this one made " +
								std::to_string(captured.size()));
}

// -- property streams ---------------------------------------------------------------------------

/// A property's value once per simulation tick, for as long as the caller keeps asking.
template <typename T>
class PropertyStream {
public:
	explicit PropertyStream(ServerStream<TempoWorld::Value> stream) : stream_(std::move(stream)) {}

	// By hand, because a moved-from stream must not be finished again by its destructor.
	PropertyStream(PropertyStream&& other) noexcept
		: stream_(std::move(other.stream_)), done_(other.done_), cancelled_(other.cancelled_) {
		other.stream_.reset();
	}
	PropertyStream& operator=(PropertyStream&&) = delete;
	PropertyStream(const PropertyStream&) = delete;
	PropertyStream& operator=(const PropertyStream&) = delete;

	~PropertyStream() {
		if (stream_ && !done_) {
			// gRPC wants a call finished before it is torn down, and an open subscription only
			// finishes once it is cancelled.
			stream_->cancel();
			TempoWorld::Value discard;
			while (stream_->read(&discard)) {}
			stream_->finish();
		}
	}

	/// The next value; nullopt once the stream has ended; an error if it failed or a value did
	/// not decode.
	std::optional<Result<T>> next() {
		if (!stream_ || done_) return std::nullopt;
		TempoWorld::Value value;
		if (stream_->read(&value)) return ValueTraits<T>::from(value);
		done_ = true;
		grpc::Status status = stream_->finish();
		if (!status.ok() && !cancelled_) return Result<T>(TempoError::from_status(status));
		return std::nullopt;
	}

	/// Stop the subscription. The next call to next() reports the end.
	void cancel() {
		cancelled_ = true;
		if (stream_) stream_->cancel();
	}

private:
	std::optional<ServerStream<TempoWorld::Value>> stream_;
	bool done_ = false;
	bool cancelled_ = false;
};

// -- struct-valued properties -------------------------------------------------------------------

/// A handle to a struct-valued property, addressed a member at a time.
///
/// The server has always resolved a nested address, so reaching into a struct is a matter of
/// accumulating the path: PostProcessSettings becomes PostProcessSettings.BloomIntensity on the way
/// out. A struct that can also travel as one value is loaded and stored whole. Generated code wraps
/// this in a typed proxy per Unreal struct, so the members are checked too.
class StructRef {
public:
	StructRef(ObjectRef owner, std::string address) : owner_(std::move(owner)), address_(std::move(address)) {}

	template <typename T>
	Result<T> get(std::string_view member) const { return owner_.get<T>(this->member(member)); }

	template <typename T>
	Result<void> set(std::string_view member, const T& value) const {
		return owner_.set(this->member(member), value);
	}

	/// A struct inside this one.
	StructRef nested(std::string_view member) const { return StructRef(owner_, this->member(member)); }

	/// The whole struct this addresses, as a value.
	template <typename T>
	Result<T> load() const { return owner_.get<T>(address_); }

	/// Write a struct value here: its given members, merged into what is there.
	template <typename T>
	Result<void> store(const T& value) const { return owner_.set(address_, value); }

private:
	std::string member(std::string_view name) const { return address_ + "." + std::string(name); }

	ObjectRef owner_;
	std::string address_;
};

// -- ObjectRef's templates -----------------------------------------------------------------------

template <typename T>
Result<T> ObjectRef::get(std::string_view property) const {
	std::string description;
	TempoWorld::BatchOp op = detail::get_op(*this, property, &description);
	return detail::perform<T>(
		std::move(op), std::move(description),
		[](const TempoWorld::BatchOpResult& result) -> Result<T> {
			auto value = detail::read_value(result);
			if (!value) return std::move(value).error();
			return ValueTraits<T>::from(value.value());
		},
		[&]() -> Result<T> {
			auto value = detail::get_property(*this, property);
			if (!value) return std::move(value).error();
			return ValueTraits<T>::from(value.value());
		});
}

template <typename T>
Result<void> ObjectRef::set(std::string_view property, const T& value) const {
	return detail::write(*this, property, ValueTraits<T>::to(value));
}

template <typename T>
Result<T> ObjectRef::call_for(std::string_view function, std::vector<TempoWorld::FunctionArg> args) const {
	std::string description;
	TempoWorld::BatchOp op = detail::call_op(*this, function, args, &description);
	return detail::perform<T>(
		std::move(op), std::move(description),
		[](const TempoWorld::BatchOpResult& result) -> Result<T> {
			auto response = detail::call_results(result);
			if (!response) return std::move(response).error();
			auto value = detail::returned(response.value());
			if (!value) return std::move(value).error();
			return ValueTraits<T>::from(value.value());
		},
		[&]() -> Result<T> {
			auto response = detail::call_function(*this, function, args);
			if (!response) return std::move(response).error();
			auto value = detail::returned(response.value());
			if (!value) return std::move(value).error();
			return ValueTraits<T>::from(value.value());
		});
}

template <typename T>
Result<T> ObjectRef::get_via(std::string_view getter) const {
	return call_for<T>(getter, {});
}

template <typename T>
Result<void> ObjectRef::set_via(std::string_view setter, std::string_view argument, const T& value) const {
	return detail::write_via(*this, setter, argument, ValueTraits<T>::to(value));
}

template <typename T>
Result<T> ObjectRef::result_of(const TempoWorld::CallFunctionResponse& response, std::string_view name) {
	for (const auto& result : response.results()) {
		if (result.name() == name && result.has_typed_value()) return ValueTraits<T>::from(result.typed_value());
	}
	return TempoError::protocol("the function returned no result named \"" + std::string(name) + "\"");
}

template <typename T>
Result<PropertyStream<T>> ObjectRef::watch(std::string_view property) const {
	auto stream = detail::stream_property(*this, property);
	if (!stream) return std::move(stream).error();
	return PropertyStream<T>(std::move(stream).value());
}

}  // namespace tempo::unreal
