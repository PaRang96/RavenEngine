#include "Raven/Assets/Image.hpp"

#include "Raven/Core/Files.hpp"
#include "Raven/Core/Log.hpp"
#include "Core/PathText.hpp"

#include <bit>
#include <cstring>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <system_error>

#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_FAILURE_USERMSG
#define STBI_MAX_DIMENSIONS 16384
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

namespace Raven::Images
{
	namespace
	{
		constexpr std::size_t MaxEncodedBytes = 64 * 1024 * 1024;
		constexpr std::uint64_t MaxDecodedBytes = 256 * 1024 * 1024;

		std::runtime_error ImageError(const std::filesystem::path& path, const std::string& reason)
		{
			return std::runtime_error("Cannot load image: " + PathToUtf8(path) + ": " + reason);
		}

		std::string DecoderError()
		{
			const char* reason = stbi_failure_reason();
			return reason ? reason : "PNG/JPEG decoding failed";
		}
	}

	ImageData Load(const std::filesystem::path& path)
	{
		return Load(path, ImageLoadOptions{});
	}

	ImageData Load(const std::filesystem::path& path, ImageLoadOptions options)
	{
		if (path.empty())
			throw std::invalid_argument("Cannot load an empty image path");
		const auto attemptedPath = std::filesystem::absolute(path);
		// Avoid reading an already oversized file. Check again after reading for changed files.
		std::error_code sizeError;
		const auto encodedSize = std::filesystem::file_size(attemptedPath, sizeError);
		if (!sizeError && encodedSize > MaxEncodedBytes)
			throw ImageError(attemptedPath, "Encoded image exceeds the 64 MiB limit");
		const auto bytes = Files::ReadBinary(attemptedPath);
		if (bytes.empty())
			throw ImageError(attemptedPath, "Image file is empty");
		if (bytes.size() > MaxEncodedBytes || bytes.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
			throw ImageError(attemptedPath, "Encoded image exceeds the 64 MiB limit");

		const auto* encoded = reinterpret_cast<const stbi_uc*>(bytes.data());
		const int encodedLength = static_cast<int>(bytes.size());
		int width = 0;
		int height = 0;
		if (!stbi_info_from_memory(encoded, encodedLength, &width, &height, nullptr))
			throw ImageError(attemptedPath, DecoderError());
		if (width <= 0 || height <= 0 || width > STBI_MAX_DIMENSIONS || height > STBI_MAX_DIMENSIONS)
			throw ImageError(attemptedPath, "Image dimensions must be between 1 and 16384");
		const auto pixelBytes = std::uint64_t{ static_cast<std::uint32_t>(width) } *
			static_cast<std::uint32_t>(height) * 4;
		if (pixelBytes > MaxDecodedBytes || pixelBytes > std::numeric_limits<std::size_t>::max())
			throw ImageError(attemptedPath, "Decoded RGBA8 image exceeds the 256 MiB limit");
		if (stbi_is_16_bit_from_memory(encoded, encodedLength))
			throw ImageError(attemptedPath, "16-bit images are not supported by the RGBA8 loader");

		// Keep decoder options local to this call's thread, including CgBI PNG normalization.
		stbi_set_flip_vertically_on_load_thread(0);
		stbi_convert_iphone_png_to_rgb_thread(1);
		stbi_set_unpremultiply_on_load_thread(1);
		int decodedWidth = 0;
		int decodedHeight = 0;
		const std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> decoded(
			stbi_load_from_memory(encoded, encodedLength, &decodedWidth, &decodedHeight, nullptr, STBI_rgb_alpha),
			&stbi_image_free);
		if (!decoded)
			throw ImageError(attemptedPath, DecoderError());
		if (decodedWidth != width || decodedHeight != height)
			throw ImageError(attemptedPath, "Decoded dimensions differ from the image header");

		ImageData image;
		image.Width = static_cast<std::uint32_t>(width);
		image.Height = static_cast<std::uint32_t>(height);
		image.Pixels.resize(static_cast<std::size_t>(pixelBytes));
		std::memcpy(image.Pixels.data(), decoded.get(), image.Pixels.size());
#if defined(RAVEN_DEVELOPMENT)
		if (options.WarnIfNonPowerOfTwo &&
			(!std::has_single_bit(image.Width) || !std::has_single_bit(image.Height)))
		{
			Log(LogLevel::Warning, "Non-power-of-two source image: " + PathToUtf8(attemptedPath) +
				" (" + std::to_string(image.Width) + " x " + std::to_string(image.Height) + ").");
		}
#else
		(void)options;
#endif
		return image;
	}
}
