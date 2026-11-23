#pragma once

#include <chrono>
#include <cstdlib>
#include <string>
#include <string_view>

// Reload timing (docs/HotReloadDesign.md, H5 and H7). Every reload logs one summary line;
// per-phase detail is opt-in through the RAVEN_RELOAD_TRACE environment variable.
namespace Raven::ReloadTrace
{
	// Spans start when the engine observes a change. File write times are not used: on some
	// volumes they run ahead of the system clock (1.5 s measured on the test laptop's D: drive).
	using Time = std::chrono::steady_clock::time_point;

	inline bool Enabled() noexcept
	{
		static const bool enabled = []
		{
			const char* value = std::getenv("RAVEN_RELOAD_TRACE");
			return value && *value && std::string_view(value) != "0";
		}();
		return enabled;
	}

	inline Time Now() noexcept
	{
		return std::chrono::steady_clock::now();
	}

	// Milliseconds between two recorded points, or -1 when either is missing.
	inline long long Milliseconds(Time from, Time to) noexcept
	{
		if (from == Time{} || to == Time{})
			return -1;
		return std::chrono::duration_cast<std::chrono::milliseconds>(to - from).count();
	}

	inline std::string Span(std::string_view label, Time from, Time to)
	{
		const auto value = Milliseconds(from, to);
		return std::string(label) + ' ' + (value < 0 ? std::string("-") : std::to_string(value));
	}
}
