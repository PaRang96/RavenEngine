#include "Scene/TransformInterpolator.hpp"

#include "Raven/Components/PhysicsComponents.hpp"
#include "Raven/Math/Transform.hpp"
#include "Raven/Scene/Scene.hpp"

#include <cstring>
#include <vector>

namespace Raven
{
	namespace
	{
		bool Same(const ECS::TransformComponent& left, const ECS::TransformComponent& right) noexcept
		{
			return std::memcmp(&left, &right, sizeof(left)) == 0;
		}

		ECS::TransformComponent Blend(const ECS::TransformComponent& from, const ECS::TransformComponent& to, float t)
		{
			ECS::TransformComponent result = to;
			result.Position = from.Position + (to.Position - from.Position) * t;
			result.Scale = from.Scale + (to.Scale - from.Scale) * t;
			result.Rotation = EulerFromQuat(Nlerp(QuatFromEuler(from.Rotation), QuatFromEuler(to.Rotation), t), to.Rotation);
			return result;
		}
	}

	template<typename Function>
	void TransformInterpolator::ForEachMoving(Scene& scene, Function&& function)
	{
		const auto access = scene.GetAccess();
		access.View<const ECS::InterpolateComponent, const ECS::TransformComponent>().Each(
			[&](ECS::Entity entity, const ECS::InterpolateComponent&, const ECS::TransformComponent& transform)
			{
				function(entity, transform);
			});
		// Dynamic and kinematic bodies and movers are interpolated without the tag (FD-32, FD-47).
		access.View<const ECS::RigidBodyComponent, const ECS::TransformComponent>().Each(
			[&](ECS::Entity entity, const ECS::RigidBodyComponent& body, const ECS::TransformComponent& transform)
			{
				if (body.Motion != ECS::MotionType::Static && !access.TryGet<const ECS::InterpolateComponent>(entity))
					function(entity, transform);
			});
		access.View<const ECS::MoverComponent, const ECS::TransformComponent>().Each(
			[&](ECS::Entity entity, const ECS::MoverComponent&, const ECS::TransformComponent& transform)
			{
				if (!access.TryGet<const ECS::InterpolateComponent>(entity))
					function(entity, transform);
			});
	}

	void TransformInterpolator::BeforeStep(Scene& scene)
	{
		ForEachMoving(scene, [&](ECS::Entity entity, const ECS::TransformComponent& transform)
			{
				auto& entry = m_Entries[scene.GetRef(entity).Id];
				entry.Previous = transform;
				entry.HasPrevious = true;
				entry.Seen = true;
			});
	}

	void TransformInterpolator::AfterSteps(Scene& scene)
	{
		for (auto& [key, entry] : m_Entries)
			entry.Seen = false;
		ForEachMoving(scene, [&](ECS::Entity entity, const ECS::TransformComponent& transform)
			{
				auto& entry = m_Entries[scene.GetRef(entity).Id];
				entry.Current = transform;
				entry.HasCurrent = true;
				// A new entity has nothing to blend from yet.
				if (!entry.HasPrevious)
				{
					entry.Previous = transform;
					entry.HasPrevious = true;
				}
				entry.Seen = true;
			});
		std::erase_if(m_Entries, [](const auto& item) { return !item.second.Seen; });
	}

	bool TransformInterpolator::Apply(Scene& scene, float alpha)
	{
		bool written = false;
		const auto access = scene.GetAccess();
		for (auto& [key, entry] : m_Entries)
		{
			entry.Applied = false;
			if (!entry.HasCurrent || Same(entry.Previous, entry.Current))
				continue;
			const auto entity = scene.Resolve(ECS::EntityRef{ key });
			const auto* live = entity.IsNull() ? nullptr : access.TryGet<const ECS::TransformComponent>(entity);
			// Moved after the steps: drawn where it is, and blended from there after the next step.
			if (!live || !Same(*live, entry.Current))
			{
				entry.Previous = entry.Current = live ? *live : entry.Current;
				continue;
			}
			entry.Applied = access.Write(entity, Blend(entry.Previous, entry.Current, alpha));
			written |= entry.Applied;
		}
		return written;
	}

	void TransformInterpolator::Restore(Scene& scene)
	{
		const auto access = scene.GetAccess();
		for (auto& [key, entry] : m_Entries)
			if (entry.Applied)
			{
				access.Write(scene.Resolve(ECS::EntityRef{ key }), entry.Current);
				entry.Applied = false;
			}
	}

	void TransformInterpolator::Snap() noexcept
	{
		for (auto& [key, entry] : m_Entries)
			if (entry.HasCurrent)
				entry.Previous = entry.Current;
	}
}
