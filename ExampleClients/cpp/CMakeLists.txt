# Copyright Tempo Simulation, LLC. All Rights Reserved
#
# C++ example clients for the Tempo gRPC API. Mirrors the Rust and Python example clients, linked
# against the Tempo C++ client through its CMake package.
#
# By default against the client in this checkout, which a build with TEMPO_GEN_CPP_API=1 generates;
# point Tempo_DIR at a packaged one instead:
#
#   cmake -S . -B build -DTempo_DIR=<project>/Packaged/API/Cpp

cmake_minimum_required(VERSION 3.20)
project(tempo_examples CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Tempo CONFIG REQUIRED
    PATHS "${CMAKE_CURRENT_SOURCE_DIR}/../../TempoCore/Content/Cpp/API"
    NO_DEFAULT_PATH)

function(tempo_add_example name)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE tempo::tempo)
endfunction()

tempo_add_example(sensor_playground   SensorPlayground/sensor_playground.cc)
tempo_add_example(world_playground    WorldPlayground/world_playground.cc)
tempo_add_example(movement_playground MovementPlayground/movement_playground.cc)
