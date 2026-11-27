#include "Platform/PlatformProcess.hpp"

#include <stdexcept>

namespace Raven::Platform
{
	// The fallback until an OS implements background build processes (M4).
	struct BuildProcess
	{
	};


	BuildProcess* StartBuildProcess(const std::filesystem::path&, const std::vector<std::string>&,
		const std::filesystem::path&)
	{
		throw std::runtime_error("Background build processes are not implemented on this platform yet");
	}

	bool PollBuildProcess(BuildProcess&, int&)
	{
		return false;
	}

	void DestroyBuildProcess(BuildProcess* process) noexcept
	{
		delete process;
	}
}
