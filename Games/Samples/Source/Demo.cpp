// The Perspective scene's controlled mesh and beacon: the orbit animation (paused with Space), T
// rotating the controlled mesh in free flight, the transparent-order toggle (O or F1) and the mesh
// swap (G).

#include "SampleSystems.hpp"

#include "Raven/Components/SpriteComponent.hpp"
#include "Raven/Components/StaticMeshComponent.hpp"
#include "Raven/Components/TransformComponent.hpp"
#include "Raven/Core/Log.hpp"

#include <cmath>
#include <numbers>
#include <utility>

namespace Samples
{
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

	void DemoSystem(SystemContext& context)
	{
		const auto& scene = context.World;
		auto& actions = context.Actions;
		auto* demo = scene.Single<DemoComponent>();
		// In free flight, Space pauses the puzzle instead (Interaction.cpp).
		if (!FreeFlight(context) && actions.WasPressed(Pause) && demo)
			demo->Animate = !demo->Animate;
		if (actions.WasPressed(ToggleOrder))
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
		if (actions.WasPressed(SwapMeshes))
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
}
