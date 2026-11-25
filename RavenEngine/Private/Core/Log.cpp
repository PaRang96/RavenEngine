#include "Raven/Core/Log.hpp"
#include "Core/LogHistory.hpp"

#include <deque>
#include <string>

#include <iostream>
#include <mutex>

namespace Raven
{
	namespace
	{
		std::mutex& LogMutex()
		{
			static std::mutex mutex;
			return mutex;
		}

		std::deque<LoggedProblem>& History()
		{
			static std::deque<LoggedProblem> history;
			return history;
		}
	}

	void Log(
		LogLevel level, 
		std::string_view message, 
		std::source_location location)
	{
		std::lock_guard lock(LogMutex());

		const char* label = "UNKNOWN";
		switch (level)
		{
		case Raven::LogLevel::Info:
			label = "INFO";
			break;
		case Raven::LogLevel::Warning:
			label = "WARNING";
			break;
		case Raven::LogLevel::Error:
			label = "ERROR";
			break;
		}

		std::cerr << '[' << label << "] "
			<< location.file_name() << ':' << location.line()
			<< ": " << message << '\n';
		if (level != LogLevel::Info)
		{
			auto& history = History();
			if (history.size() == 32)
				history.pop_front();
			history.push_back({ level, std::string(message), std::chrono::steady_clock::now() });
		}
	}

	std::vector<LoggedProblem> GetRecentProblems(std::chrono::steady_clock::duration window)
	{
		std::lock_guard lock(LogMutex());
		const auto now = std::chrono::steady_clock::now();
		std::vector<LoggedProblem> result;
		for (const auto& problem : History())
			if (now - problem.Time <= window)
				result.push_back(problem);
		return result;
	}
}

