// The energy cells the craft collects: spawning, and pickup from the previous step's sensor contacts.

#include "ArenaSystems.hpp"

#include "Raven/Components/PhysicsComponents.hpp"
#include "Raven/Components/StaticMeshComponent.hpp"
#include "Raven/Components/TransformComponent.hpp"
#include "Raven/Core/Log.hpp"

#include <format>

namespace Arena
{
	void SpawnCell(SystemContext& context, const ArenaRulesComponent& rules, Vec3 craft)
	{
		auto& commands = context.Commands;
		const auto cell = commands.Create();
		ECS::TransformComponent transform;
		transform.Position = FreePoint(context, rules, craft, 3.0f, 0.9f, 0.5f);
		transform.Scale = { 0.6f, 0.6f, 0.6f };
		commands.Add(cell, transform);
		ECS::StaticMeshComponent mesh;
		mesh.Mesh = rules.CellMesh;
		mesh.Tint = { 0.25f, 0.95f, 1.0f, 1.0f };
		commands.Add(cell, mesh);
		commands.Add(cell, Paint(false));
		ECS::ColliderComponent sensor;
		sensor.Shape = ECS::ColliderShape::Sphere;
		sensor.Sensor = true;
		sensor.Layer = CellLayer;
		commands.Add(cell, sensor);
		commands.Add(cell, CellComponent{});
	}

	// The previous step's sensor contacts: the craft collects a cell, and a new one appears.
	void CellSystem(SystemContext& context)
	{
		const auto& scene = context.World;
		auto* run = scene.Single<ArenaRunComponent>();
		const auto* rules = scene.Single<ArenaRulesComponent>();
		const auto craft = First<CraftComponent>(scene);
		if (!run || !rules || craft.IsNull() || run->State != RunState::Playing || run->Time <= 0.0f)
			return;
		const auto craftRef = scene.GetRef(craft);
		const Vec3 craftPosition = scene.TryGet<const ECS::TransformComponent>(craft)->Position;
		for (const auto& contact : context.Physics.GetContacts())
		{
			if (contact.Kind != ContactEventKind::Begin || !contact.Sensor)
				continue;
			const auto other = contact.A == craftRef ? contact.B : contact.B == craftRef ? contact.A : ECS::EntityRef{};
			const auto cell = scene.Resolve(other);
			const auto* value = scene.TryGet<const CellComponent>(cell);
			if (!value)
				continue;
			context.Commands.Destroy(other);
			run->Score += value->Value;
			if (const auto* place = scene.TryGet<const ECS::TransformComponent>(cell))
				context.Services.PlaySound(rules->PickupSound, { place->Position });
			SpawnCell(context, *rules, craftPosition);
			Log(LogLevel::Info, std::format("Cell collected: score {}", run->Score));
		}
	}
}
