#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace Raven
{
	inline std::filesystem::path PathFromUtf8(std::string_view text)
	{
		return std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(text.data()), text.size()));
	}

	inline std::string PathToUtf8(const std::filesystem::path& path)
	{
		const auto text = path.u8string();
		return std::string(reinterpret_cast<const char*>(text.data()), text.size());
	}
}
