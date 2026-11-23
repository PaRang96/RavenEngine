#pragma once

#include "Assets/SettledFileWatch.hpp"
#include "Raven/Assets/Image.hpp"

namespace Raven
{
	ImageData DecodePngSource(std::span<const std::byte> bytes, const std::filesystem::path& path);

	// Watches one PNG. The consumer takes the latest decoded image on the host thread.
	class PngFileWatch
	{
	public:
		explicit PngFileWatch(std::filesystem::path path);
		std::optional<ImageData> TakeImage();
		const std::filesystem::path& GetPath() const noexcept { return m_Watch.GetPath(); }
		// After uploading a taken image: the H7 summary (docs/HotReloadDesign.md).
		std::string Published(std::chrono::duration<double, std::milli> publish) { return m_Watch.Published(publish); }

		PngFileWatch(const PngFileWatch&) = delete;
		PngFileWatch& operator=(const PngFileWatch&) = delete;

	private:
		SettledFileWatch<ImageData> m_Watch;
	};
}
