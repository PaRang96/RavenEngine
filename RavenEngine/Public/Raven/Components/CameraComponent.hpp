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
	// Stored in scene files: values are pinned and only appended (docs/DataModelDesign.md, FD-10).
	enum class CameraProjectionMode : std::uint8_t
	{
		Perspective = 0,
		Orthographic = 1,
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

	// Marks the camera the engine draws the scene from (docs/DataModelDesign.md, FD-12). Moving it
	// to another entity switches the view; without one, the scene's first camera is used.
	struct ActiveCameraComponent : Component
	{
		RAVEN_COMPONENT(Raven::ECS::ActiveCameraComponent);
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
