#pragma once

#include <filesystem>
#include <string>

namespace Raven
{
	inline std::string PathToUtf8(const std::filesystem::path& path)
	{
		const auto text = path.u8string();
		return std::string(reinterpret_cast<const char*>(text.data()), text.size());
	}
}
