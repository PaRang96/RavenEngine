#pragma once

#include <cstdio>
#include <exception>

// Minimal assertion helpers for tracked tests. A failed check prints its location
// and the test executable returns nonzero from Finish().
namespace RavenTests
{
	inline int& FailureCount() noexcept
	{
		static int count = 0;
		return count;
	}

	inline void Check(bool condition, const char* expression, const char* file, int line) noexcept
	{
		if (condition)
			return;
		std::fprintf(stderr, "%s(%d): check failed: %s\n", file, line, expression);
		++FailureCount();
	}

	inline int Finish(const char* name) noexcept
	{
		if (FailureCount() == 0)
		{
			std::printf("%s: all checks passed\n", name);
			return 0;
		}
		std::fprintf(stderr, "%s: %d check(s) failed\n", name, FailureCount());
		return 1;
	}

	// Runs a test body and reports an escaped exception as a failure.
	template<typename Body>
	int Run(const char* name, Body&& body) noexcept
	{
		try
		{
			body();
		}
		catch (const std::exception& error)
		{
			std::fprintf(stderr, "%s: unexpected exception: %s\n", name, error.what());
			++FailureCount();
		}
		catch (...)
		{
			std::fprintf(stderr, "%s: unexpected unknown exception\n", name);
			++FailureCount();
		}
		return Finish(name);
	}
}

#define RAVEN_CHECK(expression) \
	::RavenTests::Check(static_cast<bool>(expression), #expression, __FILE__, __LINE__)

#define RAVEN_CHECK_THROWS(expression) \
	do \
	{ \
		bool ravenThrown = false; \
		try { static_cast<void>(expression); } \
		catch (const std::exception&) { ravenThrown = true; } \
		::RavenTests::Check(ravenThrown, "throws: " #expression, __FILE__, __LINE__); \
	} while (false)
