// Helpers shared by the arena's systems (ArenaSystems.hpp).

#include "ArenaSystems.hpp"

#include <exception>

namespace Arena
{
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
