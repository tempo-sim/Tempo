// Copyright Tempo Simulation, LLC. All Rights Reserved
//
// The integration group: the C++ RPC client against a running sim.

#include <cmath>

#include "harness.h"

#include "tempo/tempo_core.h"
#include "tempo/tempo_world.h"

namespace tw = tempo::tempo_world;

TEMPO_TEST(the_sim_answers) {
	REQUIRE(tempo_test::connect());
	auto level = tempo::tempo_core::get_current_level_name();
	REQUIRE_OK(level);
	CHECK(!level.value().level().empty());
}

TEMPO_TEST(an_actor_spawns_and_is_destroyed) {
	REQUIRE(tempo_test::connect());
	TempoCore::Transform transform;
	transform.mutable_location()->set_z(1.0);  // meters, as every RPC but the property ones speaks
	auto spawned = tw::spawn_actor({.actor_type = "/Script/Engine.StaticMeshActor", .transform = transform});
	REQUIRE_OK(spawned);
	const std::string name = spawned.value().name();

	auto state = tw::get_current_actor_state({.actor = name});
	REQUIRE_OK(state);
	CHECK(std::abs(state.value().transform().location().z() - 1.0) < 1e-3);

	REQUIRE_OK(tw::destroy_actor({.actor = name}));
	CHECK(!tw::get_current_actor_state({.actor = name}).ok());
}

TEMPO_TEST(a_transforms_scale_travels_in_it) {
	REQUIRE(tempo_test::connect());
	auto spawned = tw::spawn_actor({.actor_type = "/Script/Engine.StaticMeshActor"});
	REQUIRE_OK(spawned);
	const std::string name = spawned.value().name();

	// A StaticMeshActor spawns with Static mobility, which Unreal refuses to move at runtime.
	TempoWorld::Value movable;
	movable.set_enum_value("Movable");
	REQUIRE_OK(tw::set_property(
		{.actor = name, .component = "StaticMeshComponent0", .property = "Mobility", .value = movable}));

	TempoCore::Transform transform;
	transform.mutable_location()->set_x(2.0);
	transform.mutable_scale()->set_x(2.0);
	transform.mutable_scale()->set_y(3.0);
	transform.mutable_scale()->set_z(4.0);
	auto set = tw::set_actor_transform({.actor = name, .transform = transform});
	auto got = tw::get_actor_transform({.actor = name});
	(void)tw::destroy_actor({.actor = name});
	REQUIRE_OK(set);
	REQUIRE_OK(got);
	const TempoCore::Transform& reported = got.value().transform();
	REQUIRE(reported.has_scale());
	CHECK(std::abs(reported.scale().x() - 2) < 1e-6 && std::abs(reported.scale().y() - 3) < 1e-6 &&
		  std::abs(reported.scale().z() - 4) < 1e-6);
	CHECK(std::abs(reported.location().x() - 2) < 1e-3);
}

TEMPO_TEST(a_property_is_read_in_unreals_units) {
	REQUIRE(tempo_test::connect());
	TempoCore::Transform transform;
	transform.mutable_location()->set_x(1.0);  // a meter, in the API's units
	auto spawned = tw::spawn_actor({.actor_type = "/Script/Engine.StaticMeshActor", .transform = transform});
	REQUIRE_OK(spawned);
	const std::string name = spawned.value().name();

	auto pose = tw::get_property(
		{.actor = name, .component = "StaticMeshComponent0", .property = "RelativeLocation"});
	(void)tw::destroy_actor({.actor = name});
	REQUIRE_OK(pose);
	REQUIRE(pose.value().has_vector_value());
	CHECK(std::abs(pose.value().vector_value().x() - 100.0) < 1e-2);  // centimeters
}

TEMPO_TEST_MAIN
