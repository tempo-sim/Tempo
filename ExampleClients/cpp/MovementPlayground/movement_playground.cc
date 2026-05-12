// Copyright Tempo Simulation, LLC. All Rights Reserved
//
// Tempo C++ Movement Playground example. Lists commandable and navigable
// pawns, then exercises each TempoMovement RPC against the first commandable
// pawn (or vehicle, for the driving command). Logging only — no GUI. Mirrors
// the Rust ExampleClients/Rust/MovementPlayground in spirit.

#include <cstdio>
#include <cstdlib>
#include <string>

#include <tempo.h>

namespace tm = tempo::tempo_movement;

namespace {

const char* result_string(TempoMovement::MoveToResult r) {
    switch (r) {
        case TempoMovement::MTR_SUCCESS:  return "SUCCESS";
        case TempoMovement::MTR_BLOCKED:  return "BLOCKED";
        case TempoMovement::MTR_OFF_PATH: return "OFF_PATH";
        case TempoMovement::MTR_ABORTED:  return "ABORTED";
        case TempoMovement::MTR_INVALID:  return "INVALID";
        default:                          return "UNKNOWN";
    }
}

}  // namespace

int main(int argc, char** argv) {
    const char* host = (argc >= 2) ? argv[1] : "localhost";
    int port = (argc >= 3) ? std::atoi(argv[2]) : 10001;

    tempo::set_server(host, static_cast<uint16_t>(port));
    std::printf("[MovementPlayground] connecting to %s:%d\n", host, port);

    auto commandable = tm::get_commandable_pawns();
    if (!commandable) {
        std::fprintf(stderr, "[MovementPlayground] get_commandable_pawns failed: %s\n",
                     commandable.error().what().c_str());
        return 1;
    }
    const auto& cmd_pawns = commandable.value().pawns();
    std::printf("[MovementPlayground] %d commandable pawn(s):\n", cmd_pawns.size());
    for (const auto& p : cmd_pawns) {
        std::printf("  - %s\n", p.c_str());
    }

    auto navigable = tm::get_navigable_pawns();
    if (!navigable) {
        std::fprintf(stderr, "[MovementPlayground] get_navigable_pawns failed: %s\n",
                     navigable.error().what().c_str());
        return 1;
    }
    const auto& nav_pawns = navigable.value().pawns();
    std::printf("[MovementPlayground] %d navigable pawn(s):\n", nav_pawns.size());
    for (const auto& p : nav_pawns) {
        std::printf("  - %s\n", p.c_str());
    }

    if (cmd_pawns.empty()) {
        std::printf("[MovementPlayground] no commandable pawns, exiting.\n");
        return 0;
    }
    const std::string target = cmd_pawns.Get(0);

    // 1) Normalized driving command (assumes target is a vehicle; a no-op for
    //    non-vehicle pawns, but the RPC will fail rather than crash).
    std::printf("[MovementPlayground] CommandVehicle(%s, accel=0.2, steer=0.0)\n", target.c_str());
    if (auto r = tm::command_vehicle(target, 0.2f, 0.0f); !r) {
        std::fprintf(stderr, "  command_vehicle failed: %s\n", r.error().what().c_str());
    }

    // 2) Closed-loop velocity command: 1 m/s forward.
    TempoCore::Twist twist;
    twist.mutable_linear()->set_x(1.0);
    twist.mutable_linear()->set_y(0.0);
    twist.mutable_linear()->set_z(0.0);
    twist.mutable_angular()->set_x(0.0);
    twist.mutable_angular()->set_y(0.0);
    twist.mutable_angular()->set_z(0.0);
    std::printf("[MovementPlayground] CommandVelocity(%s, linear=(1,0,0))\n", target.c_str());
    if (auto r = tm::command_velocity(target, twist); !r) {
        std::fprintf(stderr, "  command_velocity failed: %s\n", r.error().what().c_str());
    }

    // 3) Closed-loop acceleration command: 0.5 m/s^2 forward.
    TempoCore::Accel accel;
    accel.mutable_linear()->set_x(0.5);
    accel.mutable_linear()->set_y(0.0);
    accel.mutable_linear()->set_z(0.0);
    accel.mutable_angular()->set_x(0.0);
    accel.mutable_angular()->set_y(0.0);
    accel.mutable_angular()->set_z(0.0);
    std::printf("[MovementPlayground] CommandAcceleration(%s, linear=(0.5,0,0))\n", target.c_str());
    if (auto r = tm::command_acceleration(target, accel); !r) {
        std::fprintf(stderr, "  command_acceleration failed: %s\n", r.error().what().c_str());
    }

    // 4) Pawn move-to-location (relative): drive the first navigable pawn 5 m
    //    forward along world +X. Blocks until navigation completes.
    if (!nav_pawns.empty()) {
        const std::string nav_target = nav_pawns.Get(0);
        TempoCore::Vector location;
        location.set_x(5.0);
        location.set_y(0.0);
        location.set_z(0.0);
        std::printf("[MovementPlayground] PawnMoveToLocation(%s, relative=(5,0,0))\n",
                    nav_target.c_str());
        if (auto r = tm::pawn_move_to_location(nav_target, location, /*relative=*/true); !r) {
            std::fprintf(stderr, "  pawn_move_to_location failed: %s\n", r.error().what().c_str());
        } else {
            std::printf("  result: %s\n", result_string(r.value().result()));
        }
    }

    return 0;
}
