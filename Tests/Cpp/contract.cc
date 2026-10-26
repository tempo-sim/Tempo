// Copyright Tempo Simulation, LLC. All Rights Reserved
//
// The contract group: what a packaged C++ client promises without a sim to talk to. That it links
// -- the RPC client, the shared tempogrpc library and, when it was built, the reflected API -- that
// the conversions apply the server's rules, that the wrappers take their requests' fields by name,
// and that the reflected API's compiled-in hierarchy answers is_a.

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

TEMPO_TEST(a_quaternion_converts_exactly) {
	// TempoConversion.h's rule: x and z negated, y and w kept, with nothing lost on the way.
	const FQuat q{0.1, -0.2, 0.3, std::sqrt(1 - 0.01 - 0.04 - 0.09)};
	TempoWorld::Quat si = conversion::ue_to_si(q);
	CHECK(si.x() == -q.x && si.y() == q.y && si.z() == -q.z && si.w() == q.w);
	CHECK(conversion::si_to_ue(si) == q);

	// The quaternion and the rotation describe one orientation: Unreal's yaw of 90 degrees is a
	// right-handed turn of -90 about z, either way it is expressed.
	const FQuat quarter = conversion::quat_of(FRotator{0, 90, 0});
	TempoWorld::Quat turned = conversion::ue_to_si(quarter);
	CHECK(std::abs(turned.z() + std::sqrt(0.5)) < 1e-12 && std::abs(turned.w() - std::sqrt(0.5)) < 1e-12);
	TempoCore::Rotation rotation = conversion::ue_to_si(conversion::rotator_of(quarter));
	CHECK(std::abs(rotation.y() + std::numbers::pi / 2) < 1e-12);
}

TEMPO_TEST(a_transform_carries_its_scale_unconverted) {
	const FTransform pose{conversion::quat_of(FRotator{5, 6, 4}), FVector{100, 200, 300}, FVector{2, -3, 4}};
	TempoCore::Transform si = conversion::ue_to_si(pose);
	REQUIRE(si.has_scale());
	CHECK(si.scale().x() == 2 && si.scale().y() == -3 && si.scale().z() == 4);  // no unit, no handedness
	CHECK(si.location().x() == 1 && si.location().y() == -2 && si.location().z() == 3);

	FTransform back = conversion::si_to_ue(si);
	CHECK(back.scale3d == pose.scale3d);
	CHECK(std::abs(back.translation.y - 200) < 1e-9);
	FRotator turned = conversion::rotator_of(back.rotation);
	CHECK(std::abs(turned.pitch - 5) < 1e-9 && std::abs(turned.yaw - 6) < 1e-9 && std::abs(turned.roll - 4) < 1e-9);

	// An unset scale is 1, as it is on the wire; a set one is used even when it is zero.
	TempoCore::Transform unscaled;
	unscaled.mutable_location()->set_x(1);
	CHECK(conversion::si_to_ue(unscaled).scale3d == (FVector{1, 1, 1}));
	CHECK(conversion::si_to_ue(TempoCore::Transform()) == FTransform{});
	TempoCore::Transform flattened;
	flattened.mutable_scale()->set_x(1);
	CHECK(conversion::si_to_ue(flattened).scale3d == (FVector{1, 0, 0}));
}

TEMPO_TEST(the_value_types_have_unreals_shapes) {
	CHECK(FTransform{}.scale3d == (FVector{1, 1, 1}));
	CHECK(FColor{}.a == 255);
	FRotator rotator{10, 20, 30};
	TempoWorld::Value wire = ValueTraits<FRotator>::to(rotator);
	CHECK(wire.rotator_value().p() == 10 && wire.rotator_value().y() == 20 && wire.rotator_value().r() == 30);

	const FQuat q{0.1, 0.2, 0.3, 0.9};  // a double on the wire, so nothing is narrowed
	CHECK(ValueTraits<FQuat>::from(ValueTraits<FQuat>::to(q)).value() == q);
	const FTransform scaled{q, FVector{1, 2, 3}, FVector{2, 2, 2}};
	TempoWorld::Value transform = ValueTraits<FTransform>::to(scaled);
	CHECK(transform.has_native_transform_value());
	CHECK(ValueTraits<FTransform>::from(transform).value() == scaled);

	TempoWorld::Value opaque;  // a color written without its alpha means opaque
	opaque.mutable_color_value()->set_r(1);
	CHECK(ValueTraits<FColor>::from(opaque).value() == (FColor{1, 0, 0, 255}));
	CHECK(ValueTraits<FColor>::to(FColor{1, 2, 3, 4}).color_value().a() == 4);
}

TEMPO_TEST(a_wrapper_takes_its_requests_fields_by_name) {
	// Every field may be left out, so a field a later release adds breaks no call.
	tempo::tempo_world::SetPropertyArgs set{.actor = "A", .property = "P", .value = TempoWorld::Value()};
	CHECK(set.component.empty() && set.subsystem.empty() && set.value.has_value());
	tempo::tempo_world::GetPropertyArgs unset{};
	CHECK(unset.actor.empty());
	tempo::tempo_world::SetActorTransformArgs move{.actor = "A"};
	CHECK(!move.transform.has_value());
	tempo::tempo_world::ActorStatesNearArgs near{.near_actor = "A", .search_radius_m = 5};
	CHECK(near.search_radius_m == 5 && !near.include_static);
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
	CHECK(!tempo::tempo_world::get_property({.actor = "A", .property = "P"}).ok());
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
