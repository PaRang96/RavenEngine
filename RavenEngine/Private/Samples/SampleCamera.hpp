#pragma once

#include "Raven/Components/CameraComponent.hpp"
#include "Raven/Core/Log.hpp"
#include "Raven/ECS/Registry.hpp"
#include "Raven/Platform/Input.hpp"

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string_view>

namespace Raven
{
	inline void ReportCameraUnavailable(bool& unavailable, std::string_view reason)
	{
		if (!unavailable)
			Log(LogLevel::Warning, reason);
		unavailable = true;
	}

	inline std::optional<CameraView> PrepareSampleCameraView(const ECS::Registry& registry,
		ECS::Entity entity, std::uint32_t width, std::uint32_t height,
		bool& unavailable, std::string_view missingCameraMessage)
	{
		ECS::TransformComponent transform;
		ECS::CameraComponent camera;
		if (!registry.Read(entity, transform) || !registry.Read(entity, camera))
		{
			ReportCameraUnavailable(unavailable, missingCameraMessage);
			return std::nullopt;
		}

		try
		{
			const float aspect = static_cast<float>(width) / static_cast<float>(height);
			const CameraView view = ECS::GetCameraView(transform, camera, aspect);
			unavailable = false;
			return view;
		}
		catch (const std::invalid_argument& error)
		{
			ReportCameraUnavailable(unavailable, error.what());
			return std::nullopt;
		}
	}

	inline float CameraKeyStep(const InputState& input, Key key, float tapStep, float heldStep)
	{
		float step = 0.0f;
		if (input.WasKeyPressed(key))
			step += tapStep;
		if (input.IsKeyDown(key))
			step += heldStep;
		return step;
	}
}
