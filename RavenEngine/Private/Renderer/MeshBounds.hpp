#pragma once

#include "Raven/Math/Transform.hpp"

#include <algorithm>
#include <cstdint>
#include <numeric>
#include <span>
#include <stdexcept>

namespace Raven
{
	struct MeshBounds
	{
		Vec3 Center;
		Vec3 HalfExtent;
	};

	// Call after validating nonempty triangle indices. Only referenced vertices contribute.
	template<typename Vertex>
	MeshBounds CalculateMeshBounds(std::span<const Vertex> vertices,
		std::span<const std::uint16_t> indices)
	{
		const auto& first = vertices[indices.front()].Position;
		float minimum[3]{ first[0], first[1], first[2] };
		float maximum[3]{ first[0], first[1], first[2] };
		for (const auto index : indices)
			for (std::size_t axis = 0; axis < 3; ++axis)
			{
				const float value = vertices[index].Position[axis];
				if (!std::isfinite(value))
					throw std::invalid_argument("Mesh positions must be finite");
				minimum[axis] = std::min(minimum[axis], value);
				maximum[axis] = std::max(maximum[axis], value);
			}
		return
		{
			{ std::midpoint(minimum[0], maximum[0]), std::midpoint(minimum[1], maximum[1]),
				std::midpoint(minimum[2], maximum[2]) },
			{ maximum[0] * 0.5f - minimum[0] * 0.5f, maximum[1] * 0.5f - minimum[1] * 0.5f,
				maximum[2] * 0.5f - minimum[2] * 0.5f }
		};
	}
}
