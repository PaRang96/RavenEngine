#include "Raven/Core/Api.hpp"
#include "Raven/Components/CameraComponent.hpp"
#include "Raven/Components/TransformComponent.hpp"
#include "Raven/Core/ApplicationClient.hpp"
#include "Raven/Core/FrameTime.hpp"
#include "Raven/Core/GameInstance.hpp"
#include "Raven/Core/Log.hpp"
#include "Raven/Platform/Input.hpp"
#include "GameComponents.hpp"
#include "InteractionGame.hpp"
#include "GameContract.hpp"

#include <algorithm>
#include <cmath>
#include <exception>
#include <numbers>

namespace
{
	void MoveSprites(Raven::ECS::Registry& registry, float delta)
	{
		constexpr float MovementScale = 1.0f;
		for (const auto entity : registry.GetEntities())
		{
			Raven::ECS::TransformComponent transform;
			Raven::SampleGame::Velocity velocity;
			if (!registry.Read(entity, velocity) || !registry.Read(entity, transform))
				continue;

			transform.Position = transform.Position + velocity.Linear * (delta * MovementScale);
			if (transform.Position.X > 3.0f || transform.Position.X < -3.0f)
			{
				transform.Position.X = std::clamp(transform.Position.X, -3.0f, 3.0f);
				velocity.Linear.X = -velocity.Linear.X;
				registry.Write(entity, velocity);
			}
			registry.Write(entity, transform);
		}
	}

	float MovementAxis(const Raven::InputState& input, Raven::Key positive, Raven::Key negative)
	{
		const int positiveDown = input.IsKeyDown(positive);
		const int negativeDown = input.IsKeyDown(negative);
		return static_cast<float>(positiveDown - negativeDown);
	}

	float PlayerAxis(const Raven::InputState& input, Raven::Key positive, Raven::Key negative)
	{
		// A press and release between two ticks still gives one tick of movement.
		const int positiveActive = input.IsKeyDown(positive) || input.WasKeyPressed(positive);
		const int negativeActive = input.IsKeyDown(negative) || input.WasKeyPressed(negative);
		return static_cast<float>(positiveActive - negativeActive);
	}

	Raven::Vec3 GetPlayerDisplacement(const Raven::InputState& input, float delta)
	{
		using namespace Raven;

		const Vec3 direction{ PlayerAxis(input, Key::D, Key::A), 0.0f,
			PlayerAxis(input, Key::W, Key::S) };
		if (direction.X == 0.0f && direction.Z == 0.0f)
			return {};

		constexpr float PlayerSpeed = 3.0f;
		return Normalize(direction) * (PlayerSpeed * delta);
	}

	void UpdateFreeFly(const Raven::SceneAccess& scene, Raven::ECS::Entity entity,
		const Raven::InputState& input, float delta)
	{
		using namespace Raven;

		// The first drawable frame after a pause carries a zero delta.
		if (delta <= 0.0f)
			return;

		ECS::TransformComponent transform;
		ECS::CameraComponent camera;
		if (!scene.Read(entity, transform) || !scene.Read(entity, camera))
			return;

		const Vec3 movement
		{
			MovementAxis(input, Key::D, Key::A),
			MovementAxis(input, Key::W, Key::S),
			MovementAxis(input, Key::E, Key::Q)
		};
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
			const Vec3 direction = right * movement.X + forward * movement.Y +
				Vec3{ 0.0f, 0.0f, movement.Z };
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

	void RotateMesh(const Raven::SceneAccess& scene, Raven::ECS::Entity entity,
		const Raven::InputState& input, float delta)
	{
		using namespace Raven;

		if (delta <= 0.0f || !input.IsKeyDown(Key::T))
			return;

		ECS::TransformComponent transform;
		if (!scene.Read(entity, transform))
			return;

		constexpr float MeshRotationSpeed = 1.5f;
		transform.Rotation.Z = std::remainder(transform.Rotation.Z + MeshRotationSpeed * delta,
			2.0f * std::numbers::pi_v<float>);
		scene.Write(entity, transform);
	}

	Raven::GameUpdateResult Update(Raven::GameInstance& instance, const Raven::InputState& input,
		const Raven::FrameTime& frame, Raven::GameBindings bindings) noexcept
	{
		try
		{
			const float delta = static_cast<float>(std::clamp(frame.DeltaSeconds, 0.0, 0.1));
			if (bindings.Camera.IsNull())
				MoveSprites(instance.GetRegistry(), delta);
			else
				UpdateFreeFly(bindings.World, bindings.Camera, input, delta);
			if (!bindings.Mesh.IsNull())
				RotateMesh(bindings.World, bindings.Mesh, input, delta);
			Raven::SampleGame::UpdateInteractionGame(bindings, input, delta);
			if (!bindings.Player.IsNull() && delta > 0.0f)
				return { GetPlayerDisplacement(input, delta) };
		}
		catch (const std::exception& error)
		{
			Raven::Log(Raven::LogLevel::Error, error.what());
		}
		catch (...)
		{
			Raven::Log(Raven::LogLevel::Error, "Unknown game update failure");
		}
		return {};
	}
}

// These two C-linkage signatures stay fixed. Check the contract before fetching Update.
extern "C" RAVEN_GAME_API const char* RavenGetGameContract() noexcept
{
	return Raven::GameBuild::Contract;
}

extern "C" RAVEN_GAME_API Raven::GameUpdateFunction RavenGetGameUpdate() noexcept
{
	return &Update;
}
