// The 3D sample game: free flight through the Perspective, Gallery, Assemblies, Puzzle and
// Collection scenes and the benchmark scenes, or the orbit demo with `--orbit`.

#include "Interaction.hpp"
#include "SampleComponents.hpp"
#include "GameContract.hpp"

#include "Raven/Components/CameraComponent.hpp"
#include "Raven/Components/PhysicsComponents.hpp"
#include "Raven/Components/SpriteComponent.hpp"
#include "Raven/Components/StaticMeshComponent.hpp"
#include "Raven/Components/TransformComponent.hpp"
#include "Raven/Core/Api.hpp"
#include "Raven/Core/FrameTime.hpp"
#include "Raven/Core/GameSystems.hpp"
#include "Raven/Core/Log.hpp"
#include "Raven/Platform/Input.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>

namespace
{
	using namespace Raven;
	using namespace Samples;

	// The first drawable frame after a pause carries a zero delta; long stalls are capped.
	float Delta(const SystemContext& context)
	{
		return static_cast<float>(std::clamp(context.Time.DeltaSeconds, 0.0, 0.1));
	}

	bool FreeFlight(const SystemContext& context)
	{
		return !context.Services.HasOption("orbit");
	}

	ECS::Entity ActiveCamera(const SceneAccess& scene)
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

	float MovementAxis(const InputState& input, Key positive, Key negative)
	{
		return static_cast<float>(static_cast<int>(input.IsKeyDown(positive)) - static_cast<int>(input.IsKeyDown(negative)));
	}

	float KeyStep(const InputState& input, Key key, float tapStep, float heldStep)
	{
		return (input.WasKeyPressed(key) ? tapStep : 0.0f) + (input.IsKeyDown(key) ? heldStep : 0.0f);
	}

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

	// The orbit demo: arrows orbit the target, the wheel zooms, Home or R resets.
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
		const bool reset = input.WasKeyPressed(Key::R) || input.WasKeyPressed(Key::Home);
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

	// Hold T to rotate the controlled mesh in free flight, unless a rotator drives it.
	void MeshRotationSystem(SystemContext& context)
	{
		const auto& scene = context.World;
		const float delta = Delta(context);
		const auto mesh = First<ControlledMeshComponent>(scene);
		if (!FreeFlight(context) || delta <= 0.0f || !context.Input.IsKeyDown(Key::T) || mesh.IsNull() ||
			scene.TryGet<const RotatorComponent>(mesh))
			return;
		ECS::TransformComponent transform;
		if (!scene.Read(mesh, transform))
			return;
		constexpr float MeshRotationSpeed = 1.5f;
		transform.Rotation.Z = std::remainder(transform.Rotation.Z + MeshRotationSpeed * delta, 2.0f * std::numbers::pi_v<float>);
		scene.Write(mesh, transform);
	}

	// Fixed step: movers steer toward the next point of their circle (FD-47). The engine moves them,
	// so they slide along walls, climb the ramp and push crates.
	void PatrolSystem(SystemContext& context)
	{
		const float delta = Delta(context);
		if (delta <= 0.0f)
			return;
		context.World.View<PatrolComponent, ECS::MoverComponent, const ECS::TransformComponent>().Each(
			[delta](ECS::Entity, PatrolComponent& patrol, ECS::MoverComponent& mover, const ECS::TransformComponent& transform)
			{
				patrol.Angle = std::remainder(patrol.Angle + patrol.Speed / std::max(patrol.Radius, 0.1f) * delta,
					2.0f * std::numbers::pi_v<float>);
				const Vec3 target{ patrol.Center.X + patrol.Radius * std::cos(patrol.Angle),
					patrol.Center.Y + patrol.Radius * std::sin(patrol.Angle), 0.0f };
				const Vec3 toward{ target.X - transform.Position.X, target.Y - transform.Position.Y, 0.0f };
				const float distance = std::sqrt(Dot(toward, toward));
				const float speed = std::abs(patrol.Speed) * std::min(1.5f, distance + 0.5f);
				mover.DesiredVelocity = distance > 1e-4f ? toward * (speed / distance) : Vec3{};
			});
	}

	void InteractionSystem(SystemContext& context)
	{
		if (FreeFlight(context))
			UpdateInteraction(context, ActiveCamera(context.World), Delta(context));
	}

	// Space pauses the orbit animation, O or F1 toggles the transparent order, F6 swaps the mesh
	// sources of the controlled mesh and the beacon.
	void DemoSystem(SystemContext& context)
	{
		const auto& scene = context.World;
		const auto& input = context.Input;
		auto* demo = scene.Single<DemoComponent>();
		if (input.WasKeyPressed(Key::Space) && demo)
			demo->Animate = !demo->Animate;
		if (input.WasKeyPressed(Key::O) || input.WasKeyPressed(Key::F1))
		{
			const auto entity = First<OrderToggleComponent>(scene);
			if (auto* sprite = scene.TryGet<ECS::SpriteComponent>(entity))
			{
				sprite->SortOrder = sprite->SortOrder == 0 ? 1 : 0;
				Log(LogLevel::Info, sprite->SortOrder == 0 ? "Transparent order: automatic camera depth" :
					"Transparent order: blue-tinted quad composited last");
			}
		}
		const auto controlled = First<ControlledMeshComponent>(scene);
		const auto beacon = First<BeaconComponent>(scene);
		if (input.WasKeyPressed(Key::F6))
		{
			auto* left = scene.TryGet<ECS::StaticMeshComponent>(controlled);
			auto* right = scene.TryGet<ECS::StaticMeshComponent>(beacon);
			if (left && right)
			{
				std::swap(left->Mesh, right->Mesh);
				Log(LogLevel::Info, "Swapped Perspective mesh sources");
			}
		}
		if (FreeFlight(context) || !demo || !demo->OrbitAnimation || !demo->Animate)
			return;
		demo->Angle = static_cast<float>(std::fmod(demo->Angle + context.Time.DeltaSeconds * 0.7,
			2.0 * std::numbers::pi_v<double>));
		const float angle = demo->Angle;
		if (const auto* state = scene.TryGet<const ControlledMeshComponent>(controlled))
			if (auto* transform = scene.TryGet<ECS::TransformComponent>(controlled))
				transform->Rotation = state->InitialRotation + Vec3{ angle * 0.37f, 0.0f, angle };
		if (const auto* state = scene.TryGet<const BeaconComponent>(beacon))
			if (auto* transform = scene.TryGet<ECS::TransformComponent>(beacon))
				transform->Rotation = state->InitialRotation + Vec3{ 0.0f, 0.0f, -angle * 0.65f };
	}

	// Benchmarks read the first rotor's phase from the frame trace (scripts/Benchmark-Windows3D.py).
	void TraceSystem(SystemContext& context)
	{
		if (const auto* rotor = context.World.TryGet<const ECS::TransformComponent>(context.World.Find("benchmark-rotor-0")))
			context.Services.SetTracePhase(rotor->Rotation.Y);
	}
}

// These two C-linkage signatures stay fixed. The host checks the contract before
// registering systems, and keeps them only while this module generation is loaded.
extern "C" RAVEN_GAME_API const char* RavenGetGameContract() noexcept
{
	return Raven::GameBuild::Contract;
}

extern "C" RAVEN_GAME_API void RavenRegisterSystems(Raven::SystemTable& systems)
{
	using Raven::SystemPhase;
	systems.Component<SwitchComponent>();
	systems.Component<RotatorComponent>();
	systems.Component<DoorComponent>();
	systems.Component<GoalComponent>();
	systems.Component<PickupComponent>();
	systems.Component<AttemptComponent>();
	systems.Component<OrbitCameraComponent>();
	systems.Component<ControlledMeshComponent>();
	systems.Component<BeaconComponent>();
	systems.Component<DemoComponent>();
	systems.Component<OrderToggleComponent>();
	systems.Component<PatrolComponent>();
	systems.Add({ "Patrol", SystemPhase::FixedUpdate, 0, &PatrolSystem });
	systems.Add({ "FreeFly", SystemPhase::Update, 0, &FreeFlySystem });
	systems.Add({ "OrbitCamera", SystemPhase::Update, 0, &OrbitCameraSystem });
	systems.Add({ "MeshRotation", SystemPhase::Update, 10, &MeshRotationSystem });
	systems.Add({ "Interaction", SystemPhase::Update, 20, &InteractionSystem });
	systems.Add({ "Demo", SystemPhase::Update, 30, &DemoSystem });
	systems.Add({ "Trace", SystemPhase::Update, 100, &TraceSystem });
}
