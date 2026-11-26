// The orbit demo's camera (`--orbit`): arrows orbit the target, the wheel zooms, Home or R (the
// Restart action) resets.

#include "SampleSystems.hpp"

#include "Raven/Components/TransformComponent.hpp"

#include <cmath>
#include <numbers>

namespace Samples
{
	void OrbitCameraSystem(SystemContext& context)
	{
		if (FreeFlight(context))
			return;
		const auto& scene = context.World;
		const auto& input = context.Input;
		const auto entity = ActiveCamera(scene);
		const auto* orbit = scene.TryGet<const OrbitCameraComponent>(entity);
		ECS::TransformComponent transform;
		if (!orbit || !scene.Read(entity, transform))
			return;
		const float delta = Delta(context);
		const bool reset = context.Actions.WasPressed(Restart) || input.WasKeyPressed(Key::Home);
		const float yawStep = KeyStep(input, Key::Right, 0.1f, delta) - KeyStep(input, Key::Left, 0.1f, delta);
		const float pitchStep = KeyStep(input, Key::Up, 0.1f, delta) - KeyStep(input, Key::Down, 0.1f, delta);
		if (!reset && yawStep == 0.0f && pitchStep == 0.0f && input.ScrollY == 0.0f)
			return;
		if (reset)
		{
			transform.Position = orbit->HomePosition;
			transform.Rotation = orbit->HomeRotation;
		}
		if (yawStep != 0.0f || pitchStep != 0.0f || input.ScrollY != 0.0f)
		{
			const Vec3 offset = transform.Position - orbit->Target;
			const float horizontal = std::hypot(offset.X, offset.Y);
			const float radius = std::hypot(horizontal, offset.Z);
			if (!std::isfinite(radius) || radius == 0.0f)
				return;
			const float yaw = std::remainder(std::atan2(offset.Y, offset.X) + yawStep, 2.0f * std::numbers::pi_v<float>);
			const float pitch = std::clamp(std::atan2(offset.Z, horizontal) + pitchStep, -1.3f, 1.3f);
			const float distance = std::clamp(radius - input.ScrollY * 0.5f, 2.5f, 25.0f);
			const float flat = distance * std::cos(pitch);
			transform.Position = orbit->Target + Vec3{ flat * std::cos(yaw), flat * std::sin(yaw), distance * std::sin(pitch) };
			transform.Rotation = { -pitch, 0.0f, yaw + std::numbers::pi_v<float> / 2.0f };
		}
		scene.Write(entity, transform);
	}
}
