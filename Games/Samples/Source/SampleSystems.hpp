#pragma once

// The sample game's systems, one file per object (FreeFly, OrbitCamera, Demo, Patrol, Interaction),
// and the helpers they share. SamplesGame.cpp only registers.

#include "SampleComponents.hpp"

#include "Raven/Components/CameraComponent.hpp"
#include "Raven/Core/FrameTime.hpp"
#include "Raven/Core/GameSystems.hpp"
#include "Raven/Platform/Input.hpp"

#include <algorithm>
#include <string_view>

namespace Samples
{
	using namespace Raven;

	// Mapped actions (FD-29), so a developer key never takes them silently. Space pauses the orbit
	// animation or, in free flight, the puzzle; O or F1 toggles the transparent order; G swaps the
	// mesh sources of the controlled mesh and the beacon (F6 is the developer's preview key). R
	// restarts the scene (in the orbit demo: resets the camera), and Enter continues to the next
	// scene once a goal is completed, also in a packaged player.
	inline constexpr std::string_view PlayContext = "Samples.Play";
	inline constexpr std::string_view PauseAction = "Pause";
	inline constexpr std::string_view OrderAction = "ToggleOrder";
	inline constexpr std::string_view SwapAction = "SwapMeshes";
	inline constexpr std::string_view RestartAction = "Restart";
	inline constexpr std::string_view ContinueAction = "Continue";
	inline constexpr ActionId Pause{ PauseAction };
	inline constexpr ActionId ToggleOrder{ OrderAction };
	inline constexpr ActionId SwapMeshes{ SwapAction };
	inline constexpr ActionId Restart{ RestartAction };
	inline constexpr ActionId Continue{ ContinueAction };

	// The first drawable frame after a pause carries a zero delta; long stalls are capped.
	inline float Delta(const SystemContext& context)
	{
		return static_cast<float>(std::clamp(context.Time.DeltaSeconds, 0.0, 0.1));
	}

	inline bool FreeFlight(const SystemContext& context)
	{
		return !context.Services.HasOption("orbit");
	}

	inline ECS::Entity ActiveCamera(const SceneAccess& scene)
	{
		ECS::Entity camera;
		scene.View<const ECS::ActiveCameraComponent, const ECS::CameraComponent>().Each(
			[&camera](ECS::Entity entity, const ECS::ActiveCameraComponent&, const ECS::CameraComponent&)
			{
				camera = entity;
				return false;
			});
		return camera;
	}

	template<typename Tag>
	ECS::Entity First(const SceneAccess& scene)
	{
		ECS::Entity found;
		scene.View<const Tag>().Each([&found](ECS::Entity entity, const Tag&)
		{
			found = entity;
			return false;
		});
		return found;
	}

	inline float MovementAxis(const InputState& input, Key positive, Key negative)
	{
		return static_cast<float>(static_cast<int>(input.IsKeyDown(positive)) - static_cast<int>(input.IsKeyDown(negative)));
	}

	inline float KeyStep(const InputState& input, Key key, float tapStep, float heldStep)
	{
		return (input.WasKeyPressed(key) ? tapStep : 0.0f) + (input.IsKeyDown(key) ? heldStep : 0.0f);
	}

	// Fixed step.
	void PatrolSystem(SystemContext& context);
	// Update.
	void FreeFlySystem(SystemContext& context);
	void OrbitCameraSystem(SystemContext& context);
	void MeshRotationSystem(SystemContext& context);
	void InteractionSystem(SystemContext& context);
	void DemoSystem(SystemContext& context);
	void TraceSystem(SystemContext& context);
}
