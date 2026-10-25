# Copyright Tempo Simulation, LLC. All Rights Reserved
#
# The C++ client's tests, against a packaged client (Packaged/API/Cpp), one executable per group:
# contract (no sim), integration (the RPC client) and typed (the typed classes). Scripts/TestCppAPI.sh
# configures this with Tempo_DIR pointed at the package, launches a sim, and runs a group.

cmake_minimum_required(VERSION 3.20)
project(tempo_cpp_tests CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Tempo CONFIG REQUIRED)

function(tempo_add_group name library)
    add_executable(${name} ${name}.cc)
    target_link_libraries(${name} PRIVATE ${library})
    if(WIN32)
        # The shared gRPC library has to sit beside the executable to be found.
        add_custom_command(TARGET ${name} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:tempo::grpc> $<TARGET_FILE_DIR:${name}>)
    endif()
endfunction()

if(TARGET tempo::reflected)
    tempo_add_group(contract tempo::reflected)
    target_compile_definitions(contract PRIVATE TEMPO_TESTS_REFLECTED)
    tempo_add_group(typed tempo::reflected)
else()
    tempo_add_group(contract tempo::tempo)
endif()
tempo_add_group(integration tempo::tempo)
