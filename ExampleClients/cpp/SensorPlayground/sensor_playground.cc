// Copyright Tempo Simulation, LLC. All Rights Reserved
//
// Tempo C++ Sensor Playground example. Lists available sensors, then if any
// lidar is available, streams 5 scans and logs basic stats. Logging only —
// no GUI. Mirrors the Rust ExampleClients/Rust/SensorPlayground in spirit.

#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>

#include <tempo.h>

namespace ts = tempo::tempo_sensors;

int main(int argc, char** argv) {
    const char* host = (argc >= 2) ? argv[1] : "localhost";
    int port = (argc >= 3) ? std::atoi(argv[2]) : 10001;
    int max_frames = (argc >= 4) ? std::atoi(argv[3]) : 5;

    tempo::set_server(host, static_cast<uint16_t>(port));
    std::printf("[SensorPlayground] connecting to %s:%d\n", host, port);

    auto avail = ts::get_available_sensors();
    if (!avail) {
        std::fprintf(stderr, "[SensorPlayground] get_available_sensors failed: %s\n",
                     avail.error().what().c_str());
        return 1;
    }

    const auto& sensors = avail.value().available_sensors();
    std::printf("[SensorPlayground] %d sensor(s) available:\n", sensors.size());

    std::string lidar_owner;
    std::string lidar_name;
    for (const auto& s : sensors) {
        std::printf("  - %s/%s @ %.1f Hz\n",
                    s.owner().c_str(), s.name().c_str(), s.rate_hz());
        for (int i = 0; i < s.measurement_types_size(); ++i) {
            int m = s.measurement_types(i);
            if (m == TempoSensors::MT_LIDAR_SCAN && lidar_owner.empty()) {
                lidar_owner = s.owner();
                lidar_name = s.name();
            }
        }
    }

    if (lidar_owner.empty()) {
        std::printf("[SensorPlayground] no lidar sensors found, exiting.\n");
        return 0;
    }

    std::printf("[SensorPlayground] streaming %d lidar scan(s) from %s/%s\n",
                max_frames, lidar_owner.c_str(), lidar_name.c_str());

    auto stream_result = ts::stream_lidar_scans(lidar_owner, lidar_name);
    if (!stream_result) {
        std::fprintf(stderr, "[SensorPlayground] stream_lidar_scans failed: %s\n",
                     stream_result.error().what().c_str());
        return 1;
    }
    auto stream = std::move(stream_result).value();

    TempoSensors::LidarScanSegment segment;
    int frames = 0;
    while (frames < max_frames && stream.read(&segment)) {
        std::printf("  frame %d: %d points (%dx%d), distances=%d, intensities=%d\n",
                    frames, segment.scan_count(),
                    segment.vertical_beams(), segment.horizontal_beams(),
                    segment.distances_m_size(), segment.intensities_size());
        ++frames;
    }
    stream.cancel();
    auto status = stream.finish();
    std::printf("[SensorPlayground] stream finished: %s\n",
                status.ok() ? "ok" : status.error_message().c_str());
    return 0;
}
