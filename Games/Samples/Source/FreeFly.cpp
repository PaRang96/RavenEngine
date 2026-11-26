// The free-flight camera: WASD moves, Q and E sink and rise, the right mouse button looks, the wheel
// zooms.

#include "SampleSystems.hpp"

#include "Raven/Components/TransformComponent.hpp"

#include <cmath>
#include <numbers>

namespace Samples
{
	void FreeFlySystem(SystemContext& context)
	{
		const auto& scene = context.World;
		const float delta = Delta(context);
		const auto entity = ActiveCamera(scene);
		if (!FreeFlight(context) || delta <= 0.0f || entity.IsNull())
			return;
		ECS::TransformComponent transform;
		ECS::CameraComponent camera;
		if (!scene.Read(entity, transform) || !scene.Read(entity, camera))
			return;
		const auto& input = context.Input;
		const Vec3 movement{ MovementAxis(input, Key::D, Key::A), MovementAxis(input, Key::W, Key::S),
			MovementAxis(input, Key::E, Key::Q) };
		const bool move = movement.X != 0.0f || movement.Y != 0.0f || movement.Z != 0.0f;
		const ButtonState look = input.GetMouseButtonState(MouseButton::Right);
		const bool turn = look.Down && !look.Pressed && input.HasCursorPosition &&
			(input.CursorDeltaX != 0 || input.CursorDeltaY != 0);
		if (turn)
		{
			constexpr float MouseSensitivity = 0.0025f;
			transform.Rotation.X = std::clamp(transform.Rotation.X -
				static_cast<float>(input.CursorDeltaY) * MouseSensitivity, -1.55f, 1.55f);
			transform.Rotation.Z = std::remainder(transform.Rotation.Z -
				static_cast<float>(input.CursorDeltaX) * MouseSensitivity, 2.0f * std::numbers::pi_v<float>);
		}
		if (move)
		{
			constexpr float FreeFlySpeed = 4.0f;
			const Mat4 view = ViewFromPoseRH({}, transform.Rotation);
			const Vec3 right{ view(0, 0), view(0, 1), view(0, 2) };
			const Vec3 forward{ -view(2, 0), -view(2, 1), -view(2, 2) };
			const Vec3 direction = right * movement.X + forward * movement.Y + Vec3{ 0.0f, 0.0f, movement.Z };
			if (Dot(direction, direction) > 1.0e-12f)
				transform.Position = transform.Position + Normalize(direction) * (FreeFlySpeed * delta);
		}
		if (turn || move)
			scene.Write(entity, transform);
		if (input.ScrollY != 0.0f && camera.ProjectionMode == ECS::CameraProjectionMode::Perspective)
		{
			camera.VerticalFovRadians = std::clamp(camera.VerticalFovRadians - input.ScrollY * 0.08f, 0.2f, 2.6f);
			scene.Write(entity, camera);
		}
	}
}
