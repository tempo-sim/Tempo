// Copyright Tempo Simulation, LLC. All Rights Reserved
//
// Entry points for the typed World API: how a scenario gets a handle it can address by method.
// Mirrors src/world.rs in the Rust API.
//
// The form to reach for is discovery by type:
//
//     #include <tempo/world.h>
//     #include <tempo/reflected/classes/game/vehicles/BP_Cat745_C.h>
//
//     auto trucks = tempo::world::get_actors<tempo::reflected::classes::game::vehicles::BP_Cat745_C>();
//     if (!trucks) return 1;
//     for (const auto& truck : trucks.value()) truck.set_max_speed(1200.0);
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

#include "TempoCore/Geometry.pb.h"

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
						  const std::optional<Transform>& transform, std::string_view relative_to);

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

/// Spawn one actor of a class and return a handle to it. The transform is SI, as tempo_world's is.
template <unreal::Class T>
Result<T> spawn_actor(const std::optional<Transform>& transform = std::nullopt,
					  std::string_view relative_to = "") {
	unreal::note_origin<T>();
	auto name = detail::spawn(T::kPath, false, transform, relative_to);
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
							   std::string_view relative_to, F&& configure) {
	unreal::note_origin<T>();
	auto name = detail::spawn(T::kPath, true, transform, relative_to);
	if (!name) return std::move(name).error();
	T handle(unreal::ObjectRef::actor(name.value()).with_class(std::string(T::kPath)));
	Result<void> configured = std::forward<F>(configure)(handle);
	Result<void> finished = detail::finish_spawning(name.value());
	if (!configured) return std::move(configured).error();
	if (!finished) return std::move(finished).error();
	return handle;
}

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
