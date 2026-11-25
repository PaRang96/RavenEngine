#include "Physics/JoltSupport.hpp"

#include "Raven/Core/Log.hpp"

#include <Jolt/Core/Factory.h>
#include <Jolt/RegisterTypes.h>

#include <cstdarg>
#include <cstdio>
#include <format>
#include <mutex>
#include <stdexcept>

namespace Raven::Physics
{
	namespace
	{
		std::mutex JoltMutex;
		int JoltUsers = 0;

		void TraceToLog(const char* format, ...)
		{
			char buffer[1024];
			va_list arguments;
			va_start(arguments, format);
			std::vsnprintf(buffer, sizeof(buffer), format, arguments);
			va_end(arguments);
			Log(LogLevel::Info, std::string("Jolt: ") + buffer);
		}

#ifdef JPH_ENABLE_ASSERTS
		bool AssertToLog(const char* expression, const char* message, const char* file, JPH::uint line)
		{
			Log(LogLevel::Error, std::format("Jolt assert failed: {} {} ({}:{})", expression, message ? message : "",
				file, line));
			// Continue: every value is validated before it reaches Jolt, so a failed assert is logged
			// instead of stopping the host.
			return false;
		}
#endif
	}

	void AcquireJolt()
	{
		std::lock_guard lock(JoltMutex);
		if (JoltUsers == 0)
		{
			JPH::RegisterDefaultAllocator();
			JPH::Trace = TraceToLog;
			JPH_IF_ENABLE_ASSERTS(JPH::AssertFailed = AssertToLog;)
			// The library and RavenCore must agree on every option that changes Jolt's layout.
			if (!JPH::VerifyJoltVersionID())
				throw std::logic_error("Jolt was built with different settings than RavenCore");
			JPH::Factory::sInstance = new JPH::Factory();
			JPH::RegisterTypes();
		}
		++JoltUsers;
	}

	void ReleaseJolt() noexcept
	{
		std::lock_guard lock(JoltMutex);
		if (--JoltUsers == 0)
		{
			JPH::UnregisterTypes();
			delete JPH::Factory::sInstance;
			JPH::Factory::sInstance = nullptr;
		}
	}
}
