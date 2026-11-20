#include "Raven/Renderer/Vulkan/VulkanContext.hpp"
#include "Renderer/Vulkan/VulkanMesh.hpp"
#include "Renderer/Visibility.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Raven
{
	std::optional<float> VulkanContext::IntersectMesh(MeshHandle handle, const Mat4& model,
													  const Ray& input) const
	{
		const auto* mesh = ResolveMesh(handle);
		if (!mesh)
			return std::nullopt;
		const Ray ray{input.Origin, Normalize(input.Direction)};
		if (!std::isfinite(ray.Origin.X) || !std::isfinite(ray.Origin.Y) || !std::isfinite(ray.Origin.Z))
			throw std::invalid_argument("Picking ray origin must be finite");
		const auto& data = *mesh->GetData();
		const auto bounds = TransformBounds(data.LocalBounds, model);
		double near = 0.0, far = std::numeric_limits<double>::infinity();
		for (const auto& axis :
			 {std::array{ray.Origin.X, ray.Direction.X, bounds.Center.X, bounds.HalfExtent.X},
			  std::array{ray.Origin.Y, ray.Direction.Y, bounds.Center.Y, bounds.HalfExtent.Y},
			  std::array{ray.Origin.Z, ray.Direction.Z, bounds.Center.Z, bounds.HalfExtent.Z}})
		{
			const double low = static_cast<double>(axis[2]) - axis[3],
						 high = static_cast<double>(axis[2]) + axis[3];
			if (axis[1] == 0.0f)
			{
				if (axis[0] < low || axis[0] > high)
					return std::nullopt;
			}
			else
			{
				const double a = (low - axis[0]) / axis[1], b = (high - axis[0]) / axis[1];
				near = std::max(near, std::min(a, b));
				far = std::min(far, std::max(a, b));
				if (near > far)
					return std::nullopt;
			}
		}
		using D = std::array<double, 3>;
		const auto subtract = [](D a, D b) -> D { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; };
		const auto cross = [](D a, D b) -> D
		{ return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]}; };
		const auto dot = [](D a, D b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; };
		const auto point = [&model, &data](std::uint16_t index) -> D
		{
			const auto p = data.PickVertices[index];
			const auto v = model * Vec4{p.X, p.Y, p.Z, 1};
			return {v.X, v.Y, v.Z};
		};
		const D origin{ray.Origin.X, ray.Origin.Y, ray.Origin.Z},
			direction{ray.Direction.X, ray.Direction.Y, ray.Direction.Z};
		double closest = std::numeric_limits<double>::infinity();
		for (std::size_t i = 0; i < data.PickIndices.size(); i += 3)
		{
			const auto a = point(data.PickIndices[i]);
			const auto e1 = subtract(point(data.PickIndices[i + 1]), a),
					   e2 = subtract(point(data.PickIndices[i + 2]), a);
			const auto p = cross(direction, e2);
			const double determinant = dot(e1, p);
			if (determinant == 0.0 || !std::isfinite(determinant))
				continue;
			const auto t = subtract(origin, a);
			const double u = dot(t, p) / determinant;
			if (u < 0.0 || u > 1.0)
				continue;
			const auto q = cross(t, e1);
			const double v = dot(direction, q) / determinant;
			if (v < 0.0 || u + v > 1.0)
				continue;
			const double distance = dot(e2, q) / determinant;
			if (distance >= 0.0 && std::isfinite(distance))
				closest = std::min(closest, distance);
		}
		if (closest > std::numeric_limits<float>::max())
			return std::nullopt;
		return static_cast<float>(closest);
	}
}
