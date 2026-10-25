// Copyright Tempo Simulation, LLC. All Rights Reserved
//
// The typed group: the reflected C++ API against the build they were generated from. The cases
// are the Rust typed group's (Tests/Rust/typed/tests/typed.rs), on engine and Tempo classes every
// build has, so the suite runs against any project.
//
// Two things here are not in every build. TempoAgents' world subsystem stands in for "a Tempo
// subsystem", and its half of that case is compiled only when the generated header is there. The
// catalog case needs the engine's /Engine/BasicShapes/Cube cooked, which any project that places a
// basic shape does; there is no compile-time way to ask the catalog for one entry, so a host whose
// cook dropped it has to drop that case.

#include <cmath>

#include "harness.h"

#include "tempo/conversion.h"
#include "tempo/tempo_world.h"
#include "tempo/world.h"

#include "tempo/reflected/assets/engine.h"
#include "tempo/reflected/classes/engine/Actor.h"
#include "tempo/reflected/classes/engine/Pawn.h"
#include "tempo/reflected/classes/engine/ReplaySubsystem.h"
#include "tempo/reflected/classes/engine/StaticMeshActor.h"
#include "tempo/reflected/classes/engine/StaticMeshComponent.h"
#include "tempo/reflected/structs.h"

#if __has_include("tempo/reflected/classes/tempo_agents/TempoAgentsWorldSubsystem.h")
#include "tempo/reflected/classes/tempo_agents/TempoAgentsWorldSubsystem.h"
#define TEMPO_TESTS_HAS_TEMPO_AGENTS
#endif

using namespace tempo::unreal;
using tempo::reflected::classes::engine::Actor;
using tempo::reflected::classes::engine::StaticMeshActor;
using tempo::reflected::classes::engine::StaticMeshComponent;
namespace world = tempo::world;
namespace conversion = tempo::conversion;

namespace {

/// An actor spawned for one case, destroyed when the case is done. Moving one hands the actor over,
/// so only the last owner destroys it.
struct Spawned {
	explicit Spawned(StaticMeshActor spawned) : actor(std::move(spawned)) {}
	Spawned(Spawned&& other) noexcept : actor(other.actor), owns(other.owns) { other.owns = false; }
	Spawned(const Spawned&) = delete;
	Spawned& operator=(const Spawned&) = delete;
	~Spawned() {
		if (owns) (void)tempo::tempo_world::destroy_actor(actor.object().actor_name());
	}

	StaticMeshActor actor;
	bool owns = true;
};

// StaticMeshActor is an engine class, so it is present whatever the host project is, and it spawns
// with no mesh or collision so nothing moves it afterwards.
std::optional<Spawned> spawn() {
	auto actor = world::spawn_actor<StaticMeshActor>();
	if (!actor) {
		tempo_test::fail(__FILE__, __LINE__, "spawn_actor: " + actor.error().what());
		return std::nullopt;
	}
	return std::optional<Spawned>(std::in_place, std::move(actor).value());
}

/// A spawned StaticMeshActor's root is Static, which refuses to move.
bool make_movable(const Spawned& spawned) {
	return tempo::tempo_world::set_enum_property(spawned.actor.object().actor_name(), "StaticMeshComponent0",
												 "Mobility", "Movable")
		.ok();
}

bool close(double a, double b, double tolerance = 1e-3) { return std::abs(a - b) < tolerance; }

}  // namespace

TEMPO_TEST(the_linked_classes_match_the_sim) {
	REQUIRE(tempo_test::connect());
	// The content hash covers names, types and writability. If this fails, every other case here is
	// testing classes that describe a different build.
	REQUIRE_OK(world::check_schema());
}

TEMPO_TEST(a_property_round_trips_through_an_inherited_member) {
	REQUIRE(tempo_test::connect());
	auto spawned = spawn();
	REQUIRE(spawned);
	// bCanBeDamaged is declared on Actor, and the handle is a StaticMeshActor.
	REQUIRE_OK(spawned->actor.set_b_can_be_damaged(false));
	auto damaged = spawned->actor.b_can_be_damaged();
	REQUIRE_OK(damaged);
	CHECK(!damaged.value());
}

TEMPO_TEST(a_property_with_a_blueprint_setter_is_written_through_it) {
	REQUIRE(tempo_test::connect());
	auto spawned = spawn();
	REQUIRE(spawned);
	REQUIRE_OK(spawned->actor.set_net_update_frequency(50.0));
	auto frequency = spawned->actor.net_update_frequency();
	REQUIRE_OK(frequency);
	CHECK(frequency.value() == 50.0);
}

TEMPO_TEST(a_batch_defers_reads_and_sends_nothing_when_it_fails) {
	REQUIRE(tempo_test::connect());
	auto spawned = spawn();
	REQUIRE(spawned);
	const StaticMeshActor& actor = spawned->actor;

	std::optional<world::Deferred<bool>> damaged;
	auto batched = world::batch([&]() -> tempo::Result<void> {
		if (auto r = actor.set_b_can_be_damaged(false); !r) return r;
		if (auto r = actor.set_b_always_relevant(true); !r) return r;
		auto deferred = world::defer([&] { return actor.b_can_be_damaged(); });
		if (!deferred) return std::move(deferred).error();
		damaged.emplace(std::move(deferred).value());
		return {};
	});
	REQUIRE_OK(batched);
	REQUIRE(damaged);
	auto value = damaged->take();
	REQUIRE_OK(value);
	CHECK(!value.value());

	REQUIRE_OK(actor.set_b_always_relevant(false));
	auto abandoned = world::batch([&]() -> tempo::Result<void> {
		if (auto r = actor.set_b_always_relevant(true); !r) return r;
		return tempo::TempoError::protocol("the scenario changed its mind");
	});
	CHECK(!abandoned.ok());
	auto relevant = actor.b_always_relevant();
	REQUIRE_OK(relevant);
	CHECK(!relevant.value());  // a batch abandoned by an error sent nothing
}

TEMPO_TEST(discovery_finds_and_checks_by_class) {
	REQUIRE(tempo_test::connect());
	auto spawned = spawn();
	REQUIRE(spawned);
	const std::string name = spawned->actor.object().actor_name();

	auto found = world::get_actors<StaticMeshActor>();
	REQUIRE_OK(found);
	bool present = false;
	for (const auto& candidate : found.value()) present = present || candidate.object().actor_name() == name;
	CHECK(present);
	CHECK(spawned->actor.is_a<Actor>());
	CHECK(!spawned->actor.is_a<tempo::reflected::classes::engine::Pawn>());
	CHECK(world::get_actor<StaticMeshActor>(name).ok());
	CHECK(!world::get_actor<tempo::reflected::classes::engine::Pawn>(name).ok());
}

TEMPO_TEST(a_struct_argument_and_result_round_trip) {
	REQUIRE(tempo_test::connect());
	auto spawned = spawn();
	REQUIRE(spawned);
	StaticMeshComponent component = spawned->actor.static_mesh_component0();

	tempo::reflected::structs::FWalkableSlopeOverride slope;
	slope.walkable_slope_behavior = tempo::reflected::enums::EWalkableSlopeBehavior::WalkableSlope_Decrease;
	slope.walkable_slope_angle = 12.0;
	REQUIRE_OK(component.set_walkable_slope_override(slope));
	auto back = component.get_walkable_slope_override();
	REQUIRE_OK(back);
	CHECK(back.value().walkable_slope_behavior == slope.walkable_slope_behavior);
	CHECK(back.value().walkable_slope_angle && close(*back.value().walkable_slope_angle, 12.0));
}

TEMPO_TEST(an_out_struct_comes_back_with_the_return_value) {
	REQUIRE(tempo_test::connect());
	auto spawned = spawn();
	REQUIRE(spawned);
	REQUIRE(make_movable(*spawned));
	auto moved = spawned->actor.k2_set_actor_location(FVector{100, 0, 0}, false, true);
	REQUIRE_OK(moved);
	auto [returned, hit] = moved.value();
	CHECK(returned);
	CHECK(hit.b_blocking_hit == std::optional<bool>(false));
}

TEMPO_TEST(a_transform_travels_in_unreals_shape) {
	REQUIRE(tempo_test::connect());
	auto spawned = spawn();
	REQUIRE(spawned);
	REQUIRE(make_movable(*spawned));
	FTransform wanted{conversion::quat_of(FRotator{0, 90, 0}), FVector{100, 0, 50}, FVector{2, 3, 4}};
	auto set = spawned->actor.k2_set_actor_transform(wanted, false, true);
	REQUIRE_OK(set);
	auto got = spawned->actor.get_transform();
	REQUIRE_OK(got);
	const FTransform& pose = got.value();
	CHECK(close(pose.scale3d.x, 2) && close(pose.scale3d.y, 3) && close(pose.scale3d.z, 4));
	CHECK(close(pose.translation.x, 100) && close(pose.translation.z, 50));
	CHECK(close(conversion::rotator_of(pose.rotation).yaw, 90));
}

TEMPO_TEST(the_world_mirror_works_on_handles) {
	REQUIRE(tempo_test::connect());
	auto spawned = spawn();
	REQUIRE(spawned);
	REQUIRE(make_movable(*spawned));

	auto added = world::add_component<StaticMeshComponent>(spawned->actor, "MirrorMesh");
	REQUIRE_OK(added);
	auto components = world::get_components<StaticMeshComponent>(spawned->actor);
	REQUIRE_OK(components);
	bool present = false;
	for (const auto& c : components.value()) {
		present = present || c.object().component_name() == added.value().object().component_name();
	}
	CHECK(present);

	world::Vector location;
	location.set_x(2.0);
	location.set_y(1.0);
	location.set_z(0.5);  // meters, right-handed, as tempo_world takes it
	REQUIRE_OK(world::set_actor_location(spawned->actor, location));
	auto back = world::get_actor_location(spawned->actor);
	REQUIRE_OK(back);
	CHECK(close(back.value().location().x(), 2.0, 1e-4) && close(back.value().location().y(), 1.0, 1e-4));
	auto unreal = spawned->actor.k2_get_actor_location();  // the same place in Unreal's terms
	REQUIRE_OK(unreal);
	CHECK(close(unreal.value().y, -100.0, 1e-2));

	REQUIRE_OK(world::destroy_component(added.value()));
}

TEMPO_TEST(a_catalog_asset_goes_through_a_typed_setter) {
	REQUIRE(tempo_test::connect());
	auto spawned = spawn();
	REQUIRE(spawned);
	REQUIRE(make_movable(*spawned));
	StaticMeshComponent component = spawned->actor.static_mesh_component0();
	const auto& cube = tempo::reflected::assets::engine::basic_shapes::Cube;
	auto set = component.set_static_mesh(cube);
	REQUIRE_OK(set);
	CHECK(set.value());
	auto mesh = component.static_mesh();
	REQUIRE_OK(mesh);
	CHECK(mesh.value() == cube);
}

TEMPO_TEST(a_subsystem_is_reached_by_its_class) {
	REQUIRE(tempo_test::connect());
#ifdef TEMPO_TESTS_HAS_TEMPO_AGENTS
	auto agents = world::get_subsystem<tempo::reflected::classes::tempo_agents::TempoAgentsWorldSubsystem>();
	REQUIRE_OK(agents);
	auto before = agents.value().brightness_meter_lateral_offset();
	REQUIRE_OK(before);
	REQUIRE_OK(agents.value().set_brightness_meter_lateral_offset(456.0));
	auto after = agents.value().brightness_meter_lateral_offset();
	REQUIRE_OK(agents.value().set_brightness_meter_lateral_offset(before.value()));
	REQUIRE_OK(after);
	CHECK(after.value() == 456.0);
#endif

	auto replay = world::get_subsystem<tempo::reflected::classes::engine::ReplaySubsystem>();
	REQUIRE_OK(replay);
	auto playing = replay.value().is_playing();
	REQUIRE_OK(playing);
	CHECK(!playing.value());
	CHECK(!world::get_subsystem<StaticMeshActor>().ok());  // not a subsystem at all
}

TEMPO_TEST_MAIN
