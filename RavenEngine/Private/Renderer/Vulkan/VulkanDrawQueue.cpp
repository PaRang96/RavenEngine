#include "Renderer/Vulkan/VulkanDrawQueue.hpp"

#include <cmath>

namespace Raven
{
	namespace
	{
		// Solves View(3x3) * position = -translation, so any invertible affine view works.
		Vec4 CameraPosition(const Mat4& view)
		{
			const auto m = [&view](std::size_t row, std::size_t column) { return view(row, column); };
			const float determinant =
				m(0, 0) * (m(1, 1) * m(2, 2) - m(1, 2) * m(2, 1)) -
				m(0, 1) * (m(1, 0) * m(2, 2) - m(1, 2) * m(2, 0)) +
				m(0, 2) * (m(1, 0) * m(2, 1) - m(1, 1) * m(2, 0));
			if (!std::isfinite(determinant) || determinant == 0.0f)
				return { 0.0f, 0.0f, 0.0f, 1.0f };
			const float b0 = -m(0, 3), b1 = -m(1, 3), b2 = -m(2, 3);
			const float x = (b0 * (m(1, 1) * m(2, 2) - m(1, 2) * m(2, 1)) -
				m(0, 1) * (b1 * m(2, 2) - m(1, 2) * b2) + m(0, 2) * (b1 * m(2, 1) - m(1, 1) * b2)) / determinant;
			const float y = (m(0, 0) * (b1 * m(2, 2) - m(1, 2) * b2) -
				b0 * (m(1, 0) * m(2, 2) - m(1, 2) * m(2, 0)) + m(0, 2) * (m(1, 0) * b2 - b1 * m(2, 0))) / determinant;
			const float z = (m(0, 0) * (m(1, 1) * b2 - b1 * m(2, 1)) -
				m(0, 1) * (m(1, 0) * b2 - b1 * m(2, 0)) + b0 * (m(1, 0) * m(2, 1) - m(1, 1) * m(2, 0))) / determinant;
			return { x, y, z, 1.0f };
		}
	}

	void BuildVulkanDrawQueue(VulkanDrawQueue& queue, const Mat4& view, const Mat4& viewProjection,
		const SceneLighting& lighting, float seconds, float delta)
	{
		lighting.Validate();
		const auto direction = Normalize(lighting.Direction);
		queue.Frame = FrameUniformData{ viewProjection, view, CameraPosition(view),
			{ lighting.Ambient.X, lighting.Ambient.Y, lighting.Ambient.Z, 0.0f },
			{ direction.X, direction.Y, direction.Z, 0.0f },
			{ lighting.Color.X, lighting.Color.Y, lighting.Color.Z, 0.0f }, { seconds, delta, 0.0f, 0.0f } };
	}
}
