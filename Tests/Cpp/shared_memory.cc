// Copyright Tempo Simulation, LLC. All Rights Reserved
//
// The C++ client's shared-memory transport against a rendering sim on this machine: streams one camera
// through the generated wrapper, which asks for shared memory, and through the raw stub, which does
// not, and compares the captures both received byte for byte. Not one of TestCppAPI.sh's groups, since
// those run the sim without rendering; run it against a sim started with -RenderOffScreen.

#include <grpcpp/client_context.h>

#include "harness.h"

#include "TempoSensors/Sensors.grpc.pb.h"
#include "tempo/context.h"
#include "tempo/tempo_core.h"
#include "tempo/tempo_sensors.h"
#include "tempo/tempo_world.h"

namespace ts = tempo::tempo_sensors;

TEMPO_TEST(shared_memory_images_match_inline_ones) {
	REQUIRE(tempo_test::connect());
	auto spawned = tempo::tempo_world::spawn_actor({.actor_type = "BP_SensorRig"});
	REQUIRE_OK(spawned);
	const std::string owner = spawned.value().name();
	auto sensors = ts::get_available_sensors();
	REQUIRE_OK(sensors);
	std::string camera;
	for (const auto& sensor : sensors.value().available_sensors()) {
		if (sensor.owner() == owner) camera = sensor.name();
	}
	REQUIRE(!camera.empty());

	REQUIRE_OK(tempo::tempo_core::set_console_variable({.name = "Tempo.WriteStats", .value = "1"}));
	auto shared = ts::stream_color_images({.owner = owner, .sensor = camera});
	REQUIRE_OK(shared);

	auto channel = tempo::TempoContext::instance().channel();
	REQUIRE_OK(channel);
	auto stub = TempoSensors::SensorService::NewStub(channel.value());
	TempoSensors::ColorImageRequest request;
	request.set_owner(owner);
	request.set_sensor(camera);
	grpc::ClientContext inline_context;
	auto inline_reader = stub->StreamColorImages(&inline_context, request);

	// Read them in turn, so neither stream falls behind the other.
	std::vector<TempoSensors::ColorImage> shared_images(6), inline_images(6);
	for (size_t i = 0; i < shared_images.size(); ++i) {
		REQUIRE(shared.value().read(&shared_images[i]));
		REQUIRE(inline_reader->Read(&inline_images[i]));
	}
	shared.value().cancel();
	inline_context.TryCancel();
	auto stats = tempo::tempo_core::exec_console_command({.command = "Tempo.WriteStats.Dump"});
	(void)tempo::tempo_core::set_console_variable({.name = "Tempo.WriteStats", .value = "0"});
	(void)tempo::tempo_world::destroy_actor({.actor = owner});

	int compared = 0;
	for (auto& image : shared_images) {
		for (auto& other : inline_images) {
			if (other.header().sequence_id() != image.header().sequence_id()) continue;
			// When each stream's response was placed on the wire may differ; nothing else may.
			image.mutable_header()->clear_transmission_time_s();
			other.mutable_header()->clear_transmission_time_s();
			CHECK(image.SerializeAsString() == other.SerializeAsString());
			++compared;
		}
	}
	CHECK(compared > 0);
	REQUIRE_OK(stats);
	CHECK(stats.value().output().find("\"shm_messages\":0,") == std::string::npos);
}

TEMPO_TEST_MAIN
