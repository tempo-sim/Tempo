// Copyright Tempo Simulation, LLC. All Rights Reserved
//
// Entry points for the reflected API: how a scenario gets a handle it can address by method.
// Mirrors src/world.rs in the Rust API.
//
// The form to reach for is discovery by type:
//
//     #include <tempo/world.h>
//     #include <tempo/reflected/classes/game/vehicles/BP_Truck_C.h>
//
//     auto trucks = tempo::world::get_actors<tempo::reflected::classes::game::vehicles::BP_Truck_C>();
//     if (!trucks) return 1;
//     for (const auto& truck : trucks.value()) {
//         if (auto set = truck.set_max_speed(1200.0); !set) return 1;
//     }
//
// There is no unchecked string in that at all, which is the point. Looking an actor up by name is
// available and sometimes necessary, but a name is decided at runtime and cannot be verified by a
// compiler -- and in a packaged build it is not even the name the editor showed, so a hard-coded
// one is its own kind of drift.

#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "tempo/result.h"
#include "tempo/unreal.h"

#include "tempo/streaming.h"

#include "TempoCore/Geometry.pb.h"
#include "TempoWorld/WorldControl.pb.h"
#include "TempoWorld/WorldState.pb.h"

namespace tempo::world {

using unreal::batch;
using unreal::defer;
using unreal::Deferred;

// The SI types, re-exported: this namespace mirrors tempo_world, and takes and returns what it does
// -- meters, radians and right-handed -- with handles where tempo_world takes names. The generated
// classes are what speak Unreal's own units and types; tempo/conversion.h moves between the two.
using Rotation = TempoCore::Rotation;
using Transform = TempoCore::Transform;
using Vector = TempoCore::Vector;
using Vector2D = TempoCore::Vector2D;

namespace detail {

/// Every actor of a class or a subclass of it, each carrying its real class.
Result<std::vector<unreal::ObjectRef>> actors_of_class(std::string_view class_path);

/// One actor by name, checked to be of a class.
Result<unreal::ObjectRef> actor_of_class(std::string_view class_path, std::string_view name);

/// Ask the sim to resolve a subsystem, so one that declined to exist fails here.
Result<void> resolve_subsystem(std::string_view class_path);

/// Spawn one actor and return its name.
Result<std::string> spawn(std::string_view class_path, bool deferred,
						  const std::optional<Transform>& transform, const unreal::Handle* relative_to_actor);

/// An actor's components of a class or a subclass of it, each carrying its real class.
Result<std::vector<unreal::ObjectRef>> components_of_class(const unreal::Handle& actor, std::string_view class_path);

/// Add a component and return its address.
Result<unreal::ObjectRef> add(std::string_view class_path, const unreal::Handle& actor, std::string_view name,
							  const unreal::Handle* parent, const std::optional<Transform>& transform,
							  std::string_view socket);

Result<void> finish_spawning(const std::string& name);

}  // namespace detail

/// Every actor of this class, or of a class derived from it.
///
/// Filtered by the server, which walks the actors it already has rather than describing all of
/// them for the client to discard. Matched on the class path, because two Blueprints in different
/// folders can generate classes with the same name.
template <unreal::Class T>
Result<std::vector<T>> get_actors() {
	unreal::note_origin<T>();
	auto objects = detail::actors_of_class(T::kPath);
	if (!objects) return std::move(objects).error();
	std::vector<T> actors;
	for (auto& object : objects.value()) actors.emplace_back(std::move(object));
	return actors;
}

/// One actor by name, checked to really be of this class. A name is a runtime string, so without
/// the check a typo or a renamed actor would surface later as a confusing failure on first use.
template <unreal::Class T>
Result<T> get_actor(std::string_view name) {
	unreal::note_origin<T>();
	auto object = detail::actor_of_class(T::kPath, name);
	if (!object) return std::move(object).error();
	return T(std::move(object).value());
}

/// The subsystem of one class: a world, game instance, engine or local player subsystem, belonging
/// to the sim's game world. There is one of each, so the class is the whole address.
template <unreal::Class T>
Result<T> get_subsystem() {
	unreal::note_origin<T>();
	auto resolved = detail::resolve_subsystem(T::kPath);
	if (!resolved) return std::move(resolved).error();
	return T(unreal::ObjectRef::subsystem(std::string(T::kPath)).with_class(std::string(T::kPath)));
}

/// One actor by name, with no class to check it against. Every property is addressed by string
/// and checked only by the server: the escape hatch, as a separate function so that taking it is
/// visible where it was taken.
Result<unreal::ObjectRef> find_actor(std::string_view name);

/// Spawn one actor of a class and return a handle to it: tempo_world::spawn_actor, typed. The
/// transform is SI, as tempo_world's is.
template <unreal::Class T>
Result<T> spawn_actor(const std::optional<Transform>& transform = std::nullopt,
					  const unreal::Handle* relative_to_actor = nullptr) {
	unreal::note_origin<T>();
	auto name = detail::spawn(T::kPath, false, transform, relative_to_actor);
	if (!name) return std::move(name).error();
	return T(unreal::ObjectRef::actor(std::move(name).value()).with_class(std::string(T::kPath)));
}

/// Spawn an actor, configure it, and only then let it begin.
///
/// What `configure` sets lands before the construction script and BeginPlay run, which is the only
/// way to influence anything that reads it. Spawning is finished even if `configure` fails: a
/// deferred actor left unfinished is in the world but never initialized, which is worse than one
/// configured only partway. The error from `configure` is what comes back.
template <unreal::Class T, typename F>
	requires std::is_invocable_r_v<Result<void>, F, const T&>
Result<T> spawn_actor_deferred(const std::optional<Transform>& transform,
							   const unreal::Handle* relative_to_actor, F&& configure) {
	unreal::note_origin<T>();
	auto name = detail::spawn(T::kPath, true, transform, relative_to_actor);
	if (!name) return std::move(name).error();
	T handle(unreal::ObjectRef::actor(name.value()).with_class(std::string(T::kPath)));
	Result<void> configured = std::forward<F>(configure)(handle);
	Result<void> finished = detail::finish_spawning(name.value());
	if (!configured) return std::move(configured).error();
	if (!finished) return std::move(finished).error();
	return handle;
}

// -- the rest of tempo_world, given handles ----------------------------------------------------
//
// Each is the tempo_world function of the same name, taking a handle where it takes a name and
// returning what it returns -- except that what creates a thing hands back a typed handle to it. An
// optional handle is a pointer, null for none. SI throughout, as tempo_world is: meters, radians,
// right-handed. A scale is a ratio, and rides beside a transform rather than in it, as it does there.

Result<void> destroy_actor(const unreal::Handle& actor);

/// The actor's components of this class, or of a class derived from it, as handles of it:
/// tempo_world::get_all_components, filtered by class as get_actors is.
template <unreal::Class T>
Result<std::vector<T>> get_components(const unreal::Handle& actor) {
	unreal::note_origin<T>();
	auto objects = detail::components_of_class(actor, T::kPath);
	if (!objects) return std::move(objects).error();
	std::vector<T> components;
	for (auto& object : objects.value()) components.emplace_back(std::move(object));
	return components;
}

/// Add a component of this class to an actor and return a handle to it: tempo_world::add_component,
/// typed. The handle carries the name the sim gave it, which is `name` unless that was taken or empty.
template <unreal::Class T>
Result<T> add_component(const unreal::Handle& actor, std::string_view name = "",
						const unreal::Handle* parent = nullptr,
						const std::optional<Transform>& transform = std::nullopt, std::string_view socket = "") {
	unreal::note_origin<T>();
	auto object = detail::add(T::kPath, actor, name, parent, transform, socket);
	if (!object) return std::move(object).error();
	return T(std::move(object).value());
}

Result<void> destroy_component(const unreal::Handle& component);
Result<void> activate_component(const unreal::Handle& component);
Result<void> deactivate_component(const unreal::Handle& component);

Result<void> set_actor_transform(const unreal::Handle& actor, const Transform& transform,
								 const unreal::Handle* relative_to_actor = nullptr,
								 const std::optional<Vector>& scale = std::nullopt);
Result<void> set_actor_location(const unreal::Handle& actor, const Vector& location,
								const unreal::Handle* relative_to_actor = nullptr);
Result<void> set_actor_rotation(const unreal::Handle& actor, const Rotation& rotation,
								const unreal::Handle* relative_to_actor = nullptr);
Result<void> set_actor_scale3d(const unreal::Handle& actor, const Vector& scale);

Result<void> set_component_transform(const unreal::Handle& component, const Transform& transform,
									 bool relative_to_world = false,
									 const std::optional<Vector>& scale = std::nullopt);
Result<void> set_component_location(const unreal::Handle& component, const Vector& location,
									bool relative_to_world = false);
Result<void> set_component_rotation(const unreal::Handle& component, const Rotation& rotation,
									bool relative_to_world = false);
Result<void> set_component_scale3d(const unreal::Handle& component, const Vector& scale,
								   bool relative_to_world = false);

Result<TempoWorld::GetTransformResponse> get_actor_transform(const unreal::Handle& actor,
															 const unreal::Handle* relative_to_actor = nullptr);
Result<TempoWorld::GetLocationResponse> get_actor_location(const unreal::Handle& actor,
														   const unreal::Handle* relative_to_actor = nullptr);
Result<TempoWorld::GetRotationResponse> get_actor_rotation(const unreal::Handle& actor,
														   const unreal::Handle* relative_to_actor = nullptr);
Result<TempoWorld::GetScale3DResponse> get_actor_scale3d(const unreal::Handle& actor);

Result<TempoWorld::GetTransformResponse> get_component_transform(
	const unreal::Handle& component, TempoWorld::ComponentFrame frame = TempoWorld::CF_WORLD,
	const unreal::Handle* relative_to_component = nullptr, std::string_view socket = "");
Result<TempoWorld::GetLocationResponse> get_component_location(
	const unreal::Handle& component, TempoWorld::ComponentFrame frame = TempoWorld::CF_WORLD,
	const unreal::Handle* relative_to_component = nullptr, std::string_view socket = "");
Result<TempoWorld::GetRotationResponse> get_component_rotation(
	const unreal::Handle& component, TempoWorld::ComponentFrame frame = TempoWorld::CF_WORLD,
	const unreal::Handle* relative_to_component = nullptr, std::string_view socket = "");
Result<TempoWorld::GetScale3DResponse> get_component_scale3d(
	const unreal::Handle& component, TempoWorld::ComponentFrame frame = TempoWorld::CF_WORLD,
	const unreal::Handle* relative_to_component = nullptr, std::string_view socket = "");

Result<TempoWorld::ActorState> get_current_actor_state(const unreal::Handle& actor,
													   bool include_hidden_components = false);
Result<ServerStream<TempoWorld::ActorState>> stream_actor_state(const unreal::Handle& actor,
																bool include_hidden_components = false);
Result<TempoWorld::ActorStates> get_current_actor_states_near(const unreal::Handle& near_actor,
															  float search_radius_m, bool include_static = false,
															  bool include_hidden_actors = false,
															  bool include_hidden_components = false);
Result<ServerStream<TempoWorld::ActorStates>> stream_actor_states_near(
	const unreal::Handle& near_actor, float search_radius_m, bool include_static = false,
	bool include_hidden_actors = false, bool include_hidden_components = false);
Result<ServerStream<TempoWorld::OverlapEventResponse>> stream_overlap_events(const unreal::Handle& actor);

/// tempo_world::raycast, ignoring actors given as handles.
Result<TempoWorld::RaycastResponse> raycast(const Vector& start, const Vector& end,
											TempoWorld::CollisionChannel collision_channel = {},
											const std::vector<const unreal::Handle*>& ignored_actors = {});

/// Compare the linked generated classes against the sim's own reflection, entry by entry.
///
/// Classes generated from one build and used against another is silent wrongness: the compiler is
/// satisfied, and the first access that disagrees fails for reasons that look like anything but a
/// version skew. Not automatic on connect: the sim walks its whole reflection to answer, which
/// takes the best part of a minute. Call it once from a test, or at the start of a long run.
Result<void> check_schema();

namespace detail {

/// What check_schema makes of a sim's answer, apart from asking: the part a test can exercise.
Result<void> compare(const TempoWorld::DescribeClassesResponse& reported,
					 const std::vector<std::pair<std::string, std::string>>& reported_assets);

}  // namespace detail

}  // namespace tempo::world
