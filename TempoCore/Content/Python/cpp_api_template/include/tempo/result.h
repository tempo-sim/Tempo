// Copyright Tempo Simulation, LLC. All Rights Reserved
//
// Result<T> — variant-based success/error type, used as the return value of
// every Tempo API call. The wrapper layer never throws; consumers can compile
// against this header with -fno-exceptions.
//
// A Result left unexamined is an error dropped on the floor, so the type is [[nodiscard]]: a setter
// called as a statement is a warning. An outcome that really does not matter is cast to void.

#pragma once

#include <cassert>
#include <optional>
#include <utility>
#include <variant>

#include "tempo/error.h"

namespace tempo {

template <typename T>
class [[nodiscard]] Result {
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

/// A call that succeeds with nothing to hand back, or fails.
template <>
class [[nodiscard]] Result<void> {
public:
	Result() = default;
	Result(TempoError error) : error_(std::move(error)) {}

	bool ok() const { return !error_.has_value(); }
	explicit operator bool() const { return ok(); }

	TempoError& error() & { return *error_; }
	const TempoError& error() const& { return *error_; }
	TempoError&& error() && { return std::move(*error_); }

private:
	std::optional<TempoError> error_;
};

}  // namespace tempo
