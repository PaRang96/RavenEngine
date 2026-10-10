#pragma once

#include "Raven/Math/Transform.hpp"

namespace Raven
{
	struct PerspectiveCamera
	{
		Vec3 Position{ 0.0f, -7.0f, 3.5f };
		Vec3 Target{};
		Vec3 Up{ 0.0f, 0.0f, 1.0f };
		float VerticalFovRadians = std::numbers::pi_v<float> / 3.0f;
		float NearPlane = 0.1f;
		float FarPlane = 100.0f;

		Mat4 GetView() const
		{
			return LookAtRH(Position, Target, Up);
		}

		Mat4 GetProjection(float aspect) const
		{
			return PerspectiveVulkanRH(VerticalFovRadians, aspect, NearPlane, FarPlane);
		}
	};
}
