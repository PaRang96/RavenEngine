#pragma once

#include "Raven/Core/Log.hpp"

#include <chrono>
#include <string>
#include <vector>

namespace Raven
{
	// Recent warnings and errors, for the on-screen diagnostics (docs/UIDesign.md, "Other users of the
	// path"). The log keeps the last 32; any thread may log, readers copy.
	struct LoggedProblem
	{
		LogLevel Level = LogLevel::Warning;
		std::string Message;
		std::chrono::steady_clock::time_point Time;
	};

	std::vector<LoggedProblem> GetRecentProblems(std::chrono::steady_clock::duration window);
}
