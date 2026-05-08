// Copyright Tempo Simulation, LLC. All Rights Reserved
//
// Tempo C++ Greeter Loop example. Calls the Greet RPC once a second and
// prints the response. Mirrors the Rust ExampleClients/Rust/GreeterLoop.

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

#include <tempo.h>

int main(int argc, char** argv) {
    const char* host = (argc >= 2) ? argv[1] : "localhost";
    int port = (argc >= 3) ? std::atoi(argv[2]) : 10001;

    tempo::set_server(host, static_cast<uint16_t>(port));
    std::printf("[GreeterLoop] connecting to %s:%d\n", host, port);

    int n = 0;
    for (;;) {
        const std::string message = "Tempo " + std::to_string(n);
        auto result = tempo::greeter::greet(message);
        if (!result) {
            std::fprintf(stderr, "[GreeterLoop] greet failed: %s\n",
                         result.error().what().c_str());
        } else {
            std::printf("[GreeterLoop] %s -> %s\n",
                        message.c_str(),
                        result.value().message().c_str());
        }
        ++n;
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    return 0;
}
