#pragma once

#include "Raven/Core/Paths.hpp"

#include <filesystem>
#include <stdexcept>

namespace Raven
{
	inline std::filesystem::path ResolveAssetPath(const std::filesystem::path& root,
		const std::filesystem::path& relative)
	{
		(void)Paths::GetContentPath(relative);
		const auto canonicalRoot = std::filesystem::weakly_canonical(root);
		const auto result = std::filesystem::weakly_canonical(canonicalRoot / relative);
		auto source = result.begin();
		for (auto part = canonicalRoot.begin(); part != canonicalRoot.end(); ++part, ++source)
			if (source == result.end() || *source != *part)
				throw std::invalid_argument("Asset source escapes the content root");
		return result;
	}
}
