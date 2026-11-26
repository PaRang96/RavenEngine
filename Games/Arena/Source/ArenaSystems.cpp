// Helpers shared by the arena's systems (ArenaSystems.hpp).

#include "ArenaSystems.hpp"

#include "Raven/Core/Log.hpp"

#include <exception>

namespace Arena
{
	std::optional<CraftState> FindCraft(const SceneAccess& scene)
	{
		// Reported once until a complete craft is found again; a trivial static, so module unload
		// runs no destructor.
		static bool reported = false;
		const auto entity = First<CraftComponent>(scene);
		if (entity.IsNull())
			return std::nullopt;
		CraftState state{ entity, scene.TryGet<const CraftComponent>(entity),
			scene.TryGet<const ECS::TransformComponent>(entity), scene.TryGet<const ECS::MoverComponent>(entity) };
		if (!state.Craft || !state.Transform || !state.Mover)
		{
			if (!std::exchange(reported, true))
				Log(LogLevel::Warning, "Arena: the craft needs a TransformComponent and a MoverComponent; the run, "
					"drones, cells and hits wait until the scene has them");
			return std::nullopt;
		}
		reported = false;
		return state;
	}

	UserDataStore* Profiles(SystemContext& context)
	{
		try
		{
			return &context.Services.GetUserData();
		}
		catch (const std::exception&)
		{
			return nullptr;
		}
	}

	Vec3 RandomPoint(SystemContext& context, const ArenaRulesComponent& rules, float margin, float z)
	{
		const float extent = std::max(rules.HalfSize - margin, 1.0f);
		const float x = context.Random.Range(-extent, extent);
		const float y = context.Random.Range(-extent, extent);
		return { x, y, z };
	}

	Vec3 FreePoint(SystemContext& context, const ArenaRulesComponent& rules, Vec3 avoid, float distance, float z,
		float radius)
	{
		Vec3 point{};
		for (int attempt = 0; attempt < 16; ++attempt)
		{
			point = RandomPoint(context, rules, 1.5f, z);
			if (FlatLength(point - avoid) < distance)
				continue;
			PhysicsShape probe;
			probe.Size = { radius, radius, radius };
			probe.Position = point;
			if (context.Physics.Overlap(probe, 1u << WorldLayer).empty())
				break;
		}
		return point;
	}

	ECS::MaterialComponent Paint(bool lit)
	{
		ECS::MaterialComponent material;
		material.Lit = lit;
		return material;
	}
}
