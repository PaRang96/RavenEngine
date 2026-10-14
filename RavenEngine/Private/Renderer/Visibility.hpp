#pragma once

#include "Renderer/MeshBounds.hpp"

#include <array>
#include <cmath>
#include <stdexcept>

namespace Raven
{
	inline MeshBounds TransformBounds(const MeshBounds& local, const Mat4& model)
	{
		for (const auto& column : model.Columns)
			for (const float value : column)
				if (!std::isfinite(value))
					throw std::invalid_argument("Draw model matrices must be finite");
		if (model(3, 0) != 0.0f || model(3, 1) != 0.0f || model(3, 2) != 0.0f || model(3, 3) != 1.0f)
			throw std::invalid_argument("Draw model matrices must be affine transforms");

		const Vec4 center = model * Vec4{ local.Center.X, local.Center.Y, local.Center.Z, 1.0f };
		const auto extent = [&model, &local](std::size_t row)
		{
			return std::abs(model(row, 0)) * local.HalfExtent.X +
				std::abs(model(row, 1)) * local.HalfExtent.Y +
				std::abs(model(row, 2)) * local.HalfExtent.Z;
		};
		MeshBounds world{ { center.X, center.Y, center.Z }, { extent(0), extent(1), extent(2) } };
		if (!std::isfinite(center.X) || !std::isfinite(center.Y) || !std::isfinite(center.Z) ||
			!std::isfinite(world.HalfExtent.X) || !std::isfinite(world.HalfExtent.Y) ||
			!std::isfinite(world.HalfExtent.Z))
			throw std::invalid_argument("Transformed mesh bounds exceed the supported numeric range");
		return world;
	}

	class ViewFrustum
	{
	public:
		explicit ViewFrustum(const Mat4& viewProjection)
		{
			// Vulkan clip volume: -w <= x,y <= w and 0 <= z <= w.
			for (std::size_t column = 0; column < 4; ++column)
			{
				const double w = viewProjection(3, column);
				m_Planes[0][column] = w + viewProjection(0, column);
				m_Planes[1][column] = w - viewProjection(0, column);
				m_Planes[2][column] = w + viewProjection(1, column);
				m_Planes[3][column] = w - viewProjection(1, column);
				m_Planes[4][column] = viewProjection(2, column);
				m_Planes[5][column] = w - viewProjection(2, column);
			}
			for (auto& plane : m_Planes)
			{
				const double length = std::sqrt(plane[0] * plane[0] + plane[1] * plane[1] + plane[2] * plane[2]);
				if (!std::isfinite(length) || !std::isfinite(plane[3]))
					throw std::invalid_argument("Camera frustum planes must be finite");
				// A very distant far plane can round to an unbounded plane in float projection math.
				// Preserve the GPU clip volume conservatively instead of rejecting a valid camera.
				if (length == 0.0)
					continue;
				for (double& value : plane)
					value /= length;
			}
		}

		bool Intersects(const MeshBounds& world) const
		{
			for (const auto& plane : m_Planes)
			{
				const double distance = plane[0] * world.Center.X + plane[1] * world.Center.Y +
					plane[2] * world.Center.Z + plane[3];
				const double radius = std::abs(plane[0]) * world.HalfExtent.X +
					std::abs(plane[1]) * world.HalfExtent.Y + std::abs(plane[2]) * world.HalfExtent.Z;
				// Keep touching/intersecting bounds, including float rounding near a plane.
				const double tolerance = 1.0e-4 * std::max({ 1.0, std::abs(distance), radius });
				if (distance + radius < -tolerance)
					return false;
			}
			return true;
		}

	private:
		std::array<std::array<double, 4>, 6> m_Planes{};
	};
}
