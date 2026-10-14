#pragma once

#include "Raven/Math/Transform.hpp"

namespace Raven
{
	struct OrthographicCamera
	{
		Vec3 Position{ 0.0f, -10.0f, 0.0f };
		Vec3 Target{};
		Vec3 Up{ 0.0f, 0.0f, 1.0f };
		// Full visible height; width follows the drawable framebuffer's aspect ratio.
		float VerticalSize = 6.0f;
		float NearPlane = 0.1f;
		float FarPlane = 100.0f;

		Mat4 GetView() const
		{
			return LookAtRH(Position, Target, Up);
		}

		Mat4 GetProjection(float aspect) const
		{
			return OrthographicVulkanRH(VerticalSize, aspect, NearPlane, FarPlane);
		}
	};
}
