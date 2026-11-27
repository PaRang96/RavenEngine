#include "Platform/PlatformFiles.hpp"

namespace Raven::Platform
{
	void ReplaceSavedFile(const std::filesystem::path& temporary, const std::filesystem::path& target)
	{
		std::filesystem::rename(temporary, target);
	}
}
