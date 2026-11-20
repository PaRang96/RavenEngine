#pragma once

#include "Raven/ECS/Component.hpp"
#include "Raven/Math/Transform.hpp"
#include "Raven/Components/TransformComponent.hpp"
#include "Raven/Renderer/CameraView.hpp"

#include <cstdint>
#include <numbers>
#include <stdexcept>

namespace Raven::ECS
{
	enum class CameraProjectionMode : std::uint8_t
	{
		Perspective,
		Orthographic
	};

	struct CameraComponent : Component
	{
		CameraProjectionMode ProjectionMode = CameraProjectionMode::Perspective;

		float VerticalFovRadians = std::numbers::pi_v<float> / 3.0f;
		// Full visible height in world units.
		float OrthographicHeight = 6.0f;

		float NearPlane = 0.1f;
		float FarPlane = 100.0f;

		RAVEN_COMPONENT(Raven::ECS::CameraComponent,
			ProjectionMode, VerticalFovRadians, OrthographicHeight,
			NearPlane, FarPlane);

		Mat4 GetProjection(float aspect) const
		{
			switch (ProjectionMode)
			{
			case CameraProjectionMode::Perspective:
				return PerspectiveVulkanRH(VerticalFovRadians, aspect, NearPlane, FarPlane);
			case CameraProjectionMode::Orthographic:
				return OrthographicVulkanRH(OrthographicHeight, aspect, NearPlane, FarPlane);
			}
			throw std::invalid_argument("Unknown camera projection mode");
		}
	};

	inline CameraView GetCameraView(
		const TransformComponent& transform,
		const CameraComponent& camera,
		float aspect)
	{
		const Mat4 view = ViewFromPoseRH(transform.Position, transform.Rotation);
		return CameraView{ view, camera.GetProjection(aspect) };
	}
}
