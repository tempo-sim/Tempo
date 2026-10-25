// Copyright Tempo Simulation, LLC. All Rights Reserved
//
// The contract group: what a packaged C++ client promises without a sim to talk to. That it links
// -- the RPC client, the shared tempogrpc library and, when it was built, the typed classes -- that
// the conversions apply the server's rules, and that the typed classes' compiled-in hierarchy
// answers is_a.

#include <cmath>
#include <numbers>

#include "harness.h"

#include "tempo/conversion.h"
#include "tempo/unreal.h"
#include "tempo/tempo_world.h"

#ifdef TEMPO_TESTS_REFLECTED
#include "tempo/reflected/classes/engine/Actor.h"
#include "tempo/reflected/classes/engine/StaticMeshActor.h"
#endif

using namespace tempo::unreal;
namespace conversion = tempo::conversion;

TEMPO_TEST(the_conversions_apply_the_servers_rule) {
	TempoCore::Vector si = conversion::ue_to_si(FVector{100, 50, -25});
	CHECK(si.x() == 1 && si.y() == -0.5 && si.z() == -0.25);
	CHECK(conversion::ue_to_si(250.0) == 2.5);
	TempoCore::Rotation r = conversion::ue_to_si(FRotator{90, 45, 180});
	CHECK(std::abs(r.r() - std::numbers::pi) < 1e-12);
	CHECK(std::abs(r.p() + std::numbers::pi / 2) < 1e-12);
	CHECK(std::abs(r.y() + std::numbers::pi / 4) < 1e-12);
	FRotator back = conversion::si_to_ue(r);
	CHECK(std::abs(back.pitch - 90) < 1e-9 && std::abs(back.yaw - 45) < 1e-9 && std::abs(back.roll - 180) < 1e-9);
}

TEMPO_TEST(the_value_types_have_unreals_shapes) {
	CHECK(FTransform{}.scale3d == (FVector{1, 1, 1}));
	CHECK(FColor{}.a == 255);
	FRotator rotator{10, 20, 30};
	TempoWorld::Value wire = ValueTraits<FRotator>::to(rotator);
	CHECK(wire.rotator_value().p() == 10 && wire.rotator_value().y() == 20 && wire.rotator_value().r() == 30);
}

TEMPO_TEST(a_handle_compares_by_identity_not_class) {
	// The same actor, found through a base-class query and read from a reference-valued property,
	// records two classes and is one actor.
	CHECK(ObjectRef::actor("A") == ObjectRef::actor("A").with_class("/Script/Engine.Pawn"));
	CHECK(ObjectRef::actor("A") != ObjectRef::actor("B"));
	CHECK(ObjectRef::actor("A") != ObjectRef::component("A", "Mesh"));
	CHECK(ObjectRef::component("A", "Mesh") == ObjectRef::from_reference_name("A:Mesh"));
	CHECK(ObjectRef::subsystem("/Script/Engine.ReplaySubsystem") != ObjectRef::actor("A"));
}

TEMPO_TEST(an_unreachable_sim_is_an_error_not_a_crash) {
	// The RPC client linked and ran: nothing listens on this port, so the call fails cleanly.
	tempo::set_server("localhost", 1);
	auto actors = tempo::tempo_world::get_all_actors();
	CHECK(!actors.ok());
}

#ifdef TEMPO_TESTS_REFLECTED
TEMPO_TEST(the_typed_classes_link_and_know_their_hierarchy) {
	using tempo::reflected::classes::engine::Actor;
	using tempo::reflected::classes::engine::StaticMeshActor;
	note_origin<StaticMeshActor>();
	StaticMeshActor actor(ObjectRef::actor("Somewhere").with_class(std::string(StaticMeshActor::kPath)));
	CHECK(actor.is_a<Actor>());
	CHECK(!actor.is_a("/Script/Engine.Pawn"));
	CHECK(!tempo::reflected::hierarchy().hashes(EntryKind::Class).empty());
}

TEMPO_TEST(a_reference_read_as_a_class_is_that_class) {
	using tempo::reflected::classes::engine::Actor;
	using tempo::reflected::classes::engine::StaticMeshActor;
	// What a property typed StaticMeshActor hands back: the wire carries a name alone, so the handle
	// records the declared class, and is_a agrees with the type system.
	TempoWorld::Value one;
	one.set_actor_value("Somewhere");
	auto actor = ValueTraits<StaticMeshActor>::from(one);
	REQUIRE_OK(actor);
	CHECK(actor.value().is_a<StaticMeshActor>());
	CHECK(actor.value().is_a<Actor>());
	CHECK(!actor.value().is_a("/Script/Engine.Pawn"));
	CHECK(actor.value().object() == ObjectRef::actor("Somewhere"));

	TempoWorld::Value several;
	several.mutable_actor_array_value()->add_values("One");
	several.mutable_actor_array_value()->add_values("Two");
	auto actors = ValueTraits<std::vector<StaticMeshActor>>::from(several);
	REQUIRE_OK(actors);
	REQUIRE(actors.value().size() == 2);
	CHECK(actors.value()[0].is_a<Actor>() && actors.value()[1].is_a<Actor>());
	CHECK(actors.value()[1].object().actor_name() == "Two");
}
#endif

TEMPO_TEST_MAIN
