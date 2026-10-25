// Copyright Tempo Simulation, LLC. All Rights Reserved
//
// A test harness small enough to read: cases register themselves, CHECK records a failure and goes
// on, REQUIRE records one and leaves the case. main() runs every case, or those whose names contain
// the argument, and exits non-zero if any failed. No framework to fetch, so the suite builds
// against a packaged client with nothing else.

#pragma once

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

#include "tempo/context.h"
#include "tempo/tempo_core.h"

namespace tempo_test {

struct Case {
	const char* name;
	void (*body)();
};

inline std::vector<Case>& cases() {
	static std::vector<Case> registered;
	return registered;
}

inline std::vector<std::string>& failures() {
	static std::vector<std::string> recorded;
	return recorded;
}

struct Register {
	Register(const char* name, void (*body)()) { cases().push_back({name, body}); }
};

inline void fail(const char* file, int line, const std::string& what) {
	failures().push_back(std::string(file) + ":" + std::to_string(line) + ": " + what);
}

/// Point the client where the runner put the sim, and wait for it to answer.
inline bool connect() {
	const char* socket = std::getenv("TEMPO_SERVER_SOCKET");
	const char* port = std::getenv("TEMPO_SERVER_PORT");
	if (socket != nullptr && *socket != '\0') {
		tempo::set_socket(socket);
	} else {
		tempo::set_server("localhost", static_cast<uint16_t>(port ? std::atoi(port) : 10001));
	}
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(120);
	while (std::chrono::steady_clock::now() < deadline) {
		if (tempo::tempo_core::get_current_level_name()) return true;
		std::this_thread::sleep_for(std::chrono::milliseconds(500));
	}
	return false;
}

inline int run(int argc, char** argv) {
	const std::string filter = argc > 1 ? argv[1] : "";
	int ran = 0, failed = 0;
	for (const Case& test : cases()) {
		if (!filter.empty() && std::string(test.name).find(filter) == std::string::npos) continue;
		failures().clear();
		test.body();
		++ran;
		if (failures().empty()) {
			std::printf("ok   %s\n", test.name);
		} else {
			++failed;
			std::printf("FAIL %s\n", test.name);
			for (const auto& failure : failures()) std::printf("     %s\n", failure.c_str());
		}
	}
	std::printf("\n%d run, %d failed\n", ran, failed);
	return failed == 0 && ran > 0 ? 0 : 1;
}

}  // namespace tempo_test

#define TEMPO_TEST(name)                                                       \
	static void name();                                                        \
	static ::tempo_test::Register name##_registered(#name, &name);             \
	static void name()

#define CHECK(condition)                                                       \
	do {                                                                       \
		if (!(condition)) ::tempo_test::fail(__FILE__, __LINE__, "CHECK(" #condition ")"); \
	} while (0)

#define REQUIRE(condition)                                                     \
	do {                                                                       \
		if (!(condition)) {                                                    \
			::tempo_test::fail(__FILE__, __LINE__, "REQUIRE(" #condition ")"); \
			return;                                                            \
		}                                                                      \
	} while (0)

/// A Result that must have succeeded, naming the error when it did not.
#define REQUIRE_OK(result)                                                     \
	do {                                                                       \
		if (const auto& tempo_test_result = (result); !tempo_test_result) {    \
			::tempo_test::fail(__FILE__, __LINE__, #result ": " + tempo_test_result.error().what()); \
			return;                                                            \
		}                                                                      \
	} while (0)

#define TEMPO_TEST_MAIN                                                        \
	int main(int argc, char** argv) { return ::tempo_test::run(argc, argv); }
