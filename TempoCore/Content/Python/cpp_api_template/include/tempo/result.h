// Copyright Tempo Simulation, LLC. All Rights Reserved
//
// Result<T> — variant-based success/error type, used as the return value of
// every Tempo API call. The wrapper layer never throws; consumers can compile
// against this header with -fno-exceptions.

#pragma once

#include <cassert>
#include <utility>
#include <variant>

#include "tempo/error.h"

namespace tempo {

template <typename T>
class Result {
public:
	Result(T value) : v_(std::move(value)) {}
	Result(TempoError error) : v_(std::move(error)) {}

	bool ok() const { return std::holds_alternative<T>(v_); }
	explicit operator bool() const { return ok(); }

	T& value() & { return std::get<T>(v_); }
	const T& value() const& { return std::get<T>(v_); }
	T&& value() && { return std::move(std::get<T>(v_)); }

	TempoError& error() & { return std::get<TempoError>(v_); }
	const TempoError& error() const& { return std::get<TempoError>(v_); }
	TempoError&& error() && { return std::move(std::get<TempoError>(v_)); }

private:
	std::variant<T, TempoError> v_;
};

}  // namespace tempo
