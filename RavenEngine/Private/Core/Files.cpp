#include "Raven/Core/Files.hpp"

#include "Core/PathText.hpp"

#include <cstdint>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace Raven::Files
{
	std::vector<std::byte> ReadBinary(const std::filesystem::path& path)
	{
		if (path.empty())
			throw std::invalid_argument("Cannot read an empty file path");
		const auto attemptedPath = std::filesystem::absolute(path);
		std::ifstream file(attemptedPath, std::ios::binary | std::ios::ate);
		if (!file)
			throw std::runtime_error("Cannot open file: " + PathToUtf8(attemptedPath));

		const std::streamoff size = file.tellg();
		std::vector<std::byte> bytes;
		if (size < 0 || size > std::numeric_limits<std::streamsize>::max() ||
			static_cast<std::uintmax_t>(size) > bytes.max_size())
			throw std::runtime_error("Invalid file size: " + PathToUtf8(attemptedPath));
		bytes.resize(static_cast<std::size_t>(size));
		file.seekg(0, std::ios::beg);
		if (!file || (!bytes.empty() &&
			!file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size))))
			throw std::runtime_error("Cannot read file: " + PathToUtf8(attemptedPath));
		return bytes;
	}

	std::string ReadText(const std::filesystem::path& path)
	{
		const auto bytes = ReadBinary(path);
		if (bytes.empty())
			return {};
		return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
	}
}
