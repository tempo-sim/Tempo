// Copyright Tempo Simulation, LLC. All Rights Reserved
//
// Tempo C++ TempoMCP example. Looks up an actor by name and prints its
// transform and bounds. Logging only. Mirrors the Rust
// ExampleClients/Rust/TempoMCP in spirit.

#include <cstdio>
#include <cstdlib>
#include <string>

#include <tempo.h>

namespace tw = tempo::tempo_world;

int main(int argc, char** argv) {
    const char* host = (argc >= 2) ? argv[1] : "localhost";
    int port = (argc >= 3) ? std::atoi(argv[2]) : 10001;
    const std::string actor_name = (argc >= 4) ? argv[3] : "Vehicle";

    tempo::set_server(host, static_cast<uint16_t>(port));
    std::printf("[TempoMCP] connecting to %s:%d\n", host, port);

    auto state = tw::get_current_actor_state(actor_name, /*include_hidden_components=*/false);
    if (!state) {
        std::fprintf(stderr, "[TempoMCP] get_current_actor_state(%s) failed: %s\n",
                     actor_name.c_str(), state.error().what().c_str());
        return 1;
    }

    const auto& s = state.value();
    std::printf("[TempoMCP] actor %s @ t=%.3f\n", s.name().c_str(), s.timestamp_s());
    if (s.has_transform()) {
        const auto& t = s.transform();
        std::printf("  transform: location=(%.2f, %.2f, %.2f)\n",
                    t.location().x(), t.location().y(), t.location().z());
    }
    if (s.has_velocity() && s.velocity().has_linear()) {
        const auto& v = s.velocity().linear();
        std::printf("  linear velocity: (%.2f, %.2f, %.2f)\n", v.x(), v.y(), v.z());
    }
    if (s.has_bounds()) {
        const auto& b = s.bounds();
        std::printf("  bounds min=(%.2f, %.2f, %.2f) max=(%.2f, %.2f, %.2f)\n",
                    b.min().x(), b.min().y(), b.min().z(),
                    b.max().x(), b.max().y(), b.max().z());
    }
    return 0;
}
