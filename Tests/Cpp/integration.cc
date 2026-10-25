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
	transform.mutable_location()->set_z(1.0);  // meters, as every RPC speaks
	auto spawned = tw::spawn_actor("/Script/Engine.StaticMeshActor", false, transform, "");
	REQUIRE_OK(spawned);
	const std::string name = spawned.value().name();

	auto state = tw::get_current_actor_state(name, false);
	REQUIRE_OK(state);
	CHECK(std::abs(state.value().transform().location().z() - 1.0) < 1e-3);

	REQUIRE_OK(tw::destroy_actor(name));
	CHECK(!tw::get_current_actor_state(name, false).ok());
}

TEMPO_TEST_MAIN
