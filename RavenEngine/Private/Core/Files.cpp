#include "Raven/Core/Files.hpp"

#include "Core/PathText.hpp"
#include "Platform/PlatformFiles.hpp"

#include <atomic>
#include <chrono>

#include <cstdint>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace Raven::Files
{
	std::vector<std::byte> ReadBinary(const std::filesystem::path& path)
	{
		return ReadBinary(path, std::numeric_limits<std::size_t>::max());
	}

	std::vector<std::byte> ReadBinary(const std::filesystem::path& path, std::size_t maxBytes)
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
		if (static_cast<std::uintmax_t>(size) > maxBytes)
			throw std::runtime_error("File exceeds the " + std::to_string(maxBytes) +
				"-byte read limit: " + PathToUtf8(attemptedPath));
		bytes.resize(static_cast<std::size_t>(size));
		file.seekg(0, std::ios::beg);
		if (!file || (!bytes.empty() &&
			!file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size))))
			throw std::runtime_error("Cannot read file: " + PathToUtf8(attemptedPath));
		return bytes;
	}

	void WriteAtomic(const std::filesystem::path& path, std::span<const std::byte> bytes)
	{
		if (path.empty() || bytes.size() > static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max()))
			throw std::invalid_argument("Invalid atomic write path or size");
		const auto target = std::filesystem::absolute(path);
		static std::atomic<std::uint64_t> sequence = 0;
		std::filesystem::path temporary;
		std::ofstream file;
		for (int attempt = 0; attempt < 16; ++attempt)
		{
			temporary = target;
			temporary += ".raven-save-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
				"-" + std::to_string(sequence.fetch_add(1, std::memory_order_relaxed));
			file.open(temporary, std::ios::binary | std::ios::out | std::ios::noreplace);
			if (file) break;
			if (!std::filesystem::exists(temporary)) throw std::runtime_error("Cannot create atomic save temporary file");
			file.clear();
		}
		if (!file) throw std::runtime_error("Cannot reserve atomic save temporary file");
		try
		{
			if (!bytes.empty()) file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
			file.flush();
			if (!file) throw std::runtime_error("Cannot write atomic save temporary file");
			file.close();
			if (!file) throw std::runtime_error("Cannot close atomic save temporary file");
			Platform::ReplaceSavedFile(temporary, target);
		}
		catch (...)
		{
			file.close();
			std::error_code ignored;
			std::filesystem::remove(temporary, ignored);
			throw;
		}
	}

	std::string ReadText(const std::filesystem::path& path)
	{
		const auto bytes = ReadBinary(path);
		if (bytes.empty())
			return {};
		return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
	}
}
