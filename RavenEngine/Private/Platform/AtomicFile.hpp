#pragma once
#include <filesystem>

namespace Raven::Platform
{
	void ReplaceSavedFile(const std::filesystem::path& temporary, const std::filesystem::path& target);
}
