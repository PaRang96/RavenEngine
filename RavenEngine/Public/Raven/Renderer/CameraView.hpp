#pragma once

#include "Raven/Math/Transform.hpp"

#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>
#include <optional>
#include <stdexcept>

namespace Raven
{
	// Derived values for one rendered view.
	struct CameraView
	{
		Mat4 View;
		Mat4 Projection;
	};

	struct Ray
	{
		Vec3 Origin;
		Vec3 Direction;
	};

	// Limits a normalized world ray to the rendered camera's explicit far distance.
	inline std::optional<float> RayDistanceToFarPlane(const CameraView& camera, const Ray& ray, float farPlane)
	{
		const auto& view = camera.View;
		const double forward = -(static_cast<double>(view(2, 0)) * ray.Direction.X +
			static_cast<double>(view(2, 1)) * ray.Direction.Y + static_cast<double>(view(2, 2)) * ray.Direction.Z);
		const double origin = -(static_cast<double>(view(2, 0)) * ray.Origin.X +
			static_cast<double>(view(2, 1)) * ray.Origin.Y + static_cast<double>(view(2, 2)) * ray.Origin.Z + view(2, 3));
		if (!(forward > 0.0) || !std::isfinite(farPlane))
			return std::nullopt;
		const double distance = (farPlane - origin) / forward;
		if (!(distance >= 0.0 && distance <= std::numeric_limits<float>::max()))
			return std::nullopt;
		return static_cast<float>(distance);
	}

	// Uses a rigid view and a symmetric Raven Vulkan projection. Pixel coordinates
	// start at the top left. The ray starts on the near plane and has unit length direction.
	inline std::optional<Ray> ScreenPointToRay(const CameraView& camera,
		std::uint32_t width, std::uint32_t height, double pixelX, double pixelY)
	{
		if (!(pixelX >= 0.0 && pixelX < width && pixelY >= 0.0 && pixelY < height))
			return std::nullopt;

		const Mat4& projection = camera.Projection;
		if (projection(0, 0) == 0.0f || projection(1, 1) == 0.0f || projection(2, 2) == 0.0f)
			return std::nullopt;

		// Clip Z is zero on the near plane. Clip W changes with depth in perspective only.
		const float nearZ = -projection(2, 3) / projection(2, 2);
		const float nearW = projection(3, 2) * nearZ + projection(3, 3);
		const float x = static_cast<float>(2.0 * pixelX / width - 1.0) / projection(0, 0);
		const float y = static_cast<float>(2.0 * pixelY / height - 1.0) / projection(1, 1);
		const float wPerForward = -projection(3, 2);

		// The rows of the view rotation are the camera axes in world space.
		const Mat4& view = camera.View;
		const Vec3 right{ view(0, 0), view(0, 1), view(0, 2) };
		const Vec3 up{ view(1, 0), view(1, 1), view(1, 2) };
		const Vec3 back{ view(2, 0), view(2, 1), view(2, 2) };
		const Vec3 origin = right * (x * nearW - view(0, 3)) +
			up * (y * nearW - view(1, 3)) + back * (nearZ - view(2, 3));
		if (!std::isfinite(origin.X) || !std::isfinite(origin.Y) || !std::isfinite(origin.Z))
			return std::nullopt;

		const Vec3 direction = right * (x * wPerForward) +
			up * (y * wPerForward) - back;
		try
		{
			return Ray{ origin, Normalize(direction) };
		}
		catch (const std::invalid_argument&)
		{
			return std::nullopt;
		}
	}

	inline Mat4 LookAtRH(Vec3 eye, Vec3 target, Vec3 up)
	{
		const Vec3 forward = Normalize(target - eye);
		const Vec3 right = Normalize(Cross(forward, up));
		const Vec3 cameraUp = Cross(right, forward);
		Mat4 result = Mat4::Identity();
		result(0, 0) = right.X;
		result(0, 1) = right.Y;
		result(0, 2) = right.Z;
		result(0, 3) = -Dot(right, eye);
		result(1, 0) = cameraUp.X;
		result(1, 1) = cameraUp.Y;
		result(1, 2) = cameraUp.Z;
		result(1, 3) = -Dot(cameraUp, eye);
		result(2, 0) = -forward.X;
		result(2, 1) = -forward.Y;
		result(2, 2) = -forward.Z;
		result(2, 3) = Dot(forward, eye);
		return result;
	}

	inline Mat4 ViewFromPoseRH(Vec3 position, Vec3 rotation)
	{
		if (!std::isfinite(position.X) || !std::isfinite(position.Y) || !std::isfinite(position.Z) ||
			!std::isfinite(rotation.X) || !std::isfinite(rotation.Y) || !std::isfinite(rotation.Z))
			throw std::invalid_argument("Invalid camera position or rotation");

		const Mat4 orientation = RotationZ(rotation.Z) * RotationY(rotation.Y) * RotationX(rotation.X);

		const Vec3 forward{
			orientation(0, 1), orientation(1, 1), orientation(2, 1)
		};

		const Vec3 up {
			orientation(0, 2), orientation(1, 2), orientation(2, 2)
		};

		const Mat4 view = LookAtRH({}, forward, up) *
			Translation({ -position.X, -position.Y, -position.Z });

		for (const auto& column : view.Columns)
			for (const float value : column)
				if (!std::isfinite(value))
					throw std::invalid_argument("Camera pose exceeds the supported numeric range");

		return view;
	}

	// Symmetric view region: verticalSize is the full visible height in world units.
	inline Mat4 OrthographicVulkanRH(float verticalSize, float aspect,
		float nearPlane, float farPlane)
	{
		if (!std::isfinite(verticalSize) || !std::isfinite(aspect) ||
			!std::isfinite(nearPlane) || !std::isfinite(farPlane) ||
			verticalSize <= 0.0f || aspect <= 0.0f || nearPlane < 0.0f || farPlane <= nearPlane)
			throw std::invalid_argument("Invalid orthographic size, aspect, or clipping planes");

		// Use double intermediates so large finite dimensions do not overflow before division.
		const double depthRange = static_cast<double>(farPlane) - nearPlane;
		const auto toFloat = [](double value)
		{
			if (!std::isfinite(value) || std::abs(value) > std::numeric_limits<float>::max())
				throw std::invalid_argument("Orthographic parameters exceed the supported numeric range");
			return static_cast<float>(value);
		};
		Mat4 result = Mat4::Identity();
		result(0, 0) = toFloat(2.0 / (static_cast<double>(verticalSize) * aspect));
		// Match the perspective path's positive-height Vulkan viewport.
		result(1, 1) = toFloat(-2.0 / verticalSize);
		result(2, 2) = toFloat(-1.0 / depthRange);
		result(2, 3) = toFloat(-static_cast<double>(nearPlane) / depthRange);
		if (result(0, 0) == 0.0f || result(1, 1) == 0.0f || result(2, 2) == 0.0f)
			throw std::invalid_argument("Orthographic parameters produce a degenerate projection");
		return result;
	}

	inline Mat4 PerspectiveVulkanRH(float verticalFovRadians, float aspect,
		float nearPlane, float farPlane)
	{
		if (!std::isfinite(verticalFovRadians) || !std::isfinite(aspect) ||
			!std::isfinite(nearPlane) || !std::isfinite(farPlane) ||
			verticalFovRadians <= 0.0f || verticalFovRadians >= std::numbers::pi_v<float> ||
			aspect <= 0.0f || nearPlane <= 0.0f || farPlane <= nearPlane)
			throw std::invalid_argument("Invalid perspective field of view, aspect, or clipping planes");

		const float focalScale = 1.0f / std::tan(verticalFovRadians * 0.5f);
		Mat4 result;
		result(0, 0) = focalScale / aspect;
		// Positive-height Vulkan viewport: world/camera up must map toward the screen top.
		result(1, 1) = -focalScale;
		result(2, 2) = farPlane / (nearPlane - farPlane);
		result(2, 3) = nearPlane * result(2, 2);
		result(3, 2) = -1.0f;
		for (const auto& column : result.Columns)
			for (const float value : column)
				if (!std::isfinite(value))
					throw std::invalid_argument("Perspective parameters exceed the supported numeric range");
		return result;
	}
}
