#include "Raven/Core/Log.hpp"

#include <iostream>
#include <mutex>

namespace Raven
{
	void Log(
		LogLevel level, 
		std::string_view message, 
		std::source_location location)
	{
		static std::mutex mutex;
		std::lock_guard lock(mutex);

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
	}
}

