// Guards scene hierarchy and world-transform behavior that the M1 cached world
// transforms must preserve (docs/ECSDesign.md, D7) and that the M6 incremental
// hierarchy index rewrote (docs/DataModelDesign.md, FD-07). Runs on the CPU: a scene
// without an asset store needs no window or GPU (M6-5).

#include "Raven/Components/TransformComponent.hpp"
#include "Raven/Scene/Scene.hpp"
#include "Raven/ECS/Registry.hpp"
#include "Support/Check.hpp"

#include <cmath>
#include <numbers>

namespace
{
	using Raven::Mat4;
	using Raven::Scene;
	using Raven::Vec3;
	using Raven::ECS::Entity;
	using Raven::ECS::TransformComponent;
	using Raven::ECS::WorldTransformComponent;

	bool Near(const Mat4& left, const Mat4& right)
	{
		for (std::size_t row = 0; row < 4; ++row)
			for (std::size_t column = 0; column < 4; ++column)
				if (std::abs(left(row, column) - right(row, column)) > 1.0e-4f)
					return false;
		return true;
	}

	bool NearPosition(const Mat4& world, Vec3 expected)
	{
		return std::abs(world(0, 3) - expected.X) < 1.0e-4f && std::abs(world(1, 3) - expected.Y) < 1.0e-4f &&
			std::abs(world(2, 3) - expected.Z) < 1.0e-4f;
	}

	Mat4 CachedWorld(Raven::ECS::Registry& registry, Entity entity)
	{
		WorldTransformComponent world;
		RAVEN_CHECK(registry.Read(entity, world));
		return world.World;
	}

	TransformComponent MakeTransform(Vec3 position, Vec3 rotation = {}, Vec3 scale = { 1.0f, 1.0f, 1.0f })
	{
		TransformComponent transform;
		transform.Position = position;
		transform.Rotation = rotation;
		transform.Scale = scale;
		return transform;
	}

	void Hierarchy()
	{
		Raven::ECS::Registry registry;
		Scene scene(registry);

		const auto root = scene.CreateEntity("Root");
		const auto child = scene.CreateEntity("Child");
		const auto grandchild = scene.CreateEntity("Grandchild");
		auto rootLocal = MakeTransform({ 1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, std::numbers::pi_v<float> / 2.0f });
		const auto childLocal = MakeTransform({ 2.0f, 0.0f, 0.0f }, {}, { 2.0f, 1.0f, 1.0f });
		const auto grandchildLocal = MakeTransform({ 0.0f, 1.0f, 0.0f }, { 0.3f, 0.0f, 0.0f }, { 1.0f, -1.0f, 0.5f });
		RAVEN_CHECK(registry.Add(root, rootLocal));
		RAVEN_CHECK(registry.Add(child, childLocal));
		RAVEN_CHECK(registry.Add(grandchild, grandchildLocal));
		scene.SetParent(child, root);
		scene.SetParent(grandchild, child);
		RAVEN_CHECK(scene.GetParent(grandchild) == child);
		RAVEN_CHECK(scene.GetParent(root).IsNull());

		// Nested composition: parent * child * grandchild, including rotation and reflected scale.
		Mat4 expected = rootLocal.GetMatrix() * childLocal.GetMatrix() * grandchildLocal.GetMatrix();
		RAVEN_CHECK(Near(scene.GetWorldTransform(grandchild), expected));
		RAVEN_CHECK(NearPosition(scene.GetWorldTransform(child), { 1.0f, 2.0f, 0.0f }));

		// The cached world update matches direct composition and adds the component.
		RAVEN_CHECK(!registry.Has<WorldTransformComponent>(grandchild));
		scene.UpdateWorldTransforms();
		RAVEN_CHECK(Near(CachedWorld(registry, grandchild), expected));
		RAVEN_CHECK(Near(CachedWorld(registry, child), rootLocal.GetMatrix() * childLocal.GetMatrix()));

		// An unchanged pass keeps cached values and advances the change tick once.
		const auto beforeTick = registry.GetChangeTick();
		scene.UpdateWorldTransforms();
		RAVEN_CHECK(registry.GetChangeTick() == beforeTick + 1);
		RAVEN_CHECK(Near(CachedWorld(registry, grandchild), expected));

		// Moving a parent moves its descendants on the next query and the next update.
		rootLocal.Position = { 0.0f, 0.0f, 5.0f };
		RAVEN_CHECK(registry.Write(root, rootLocal));
		expected = rootLocal.GetMatrix() * childLocal.GetMatrix() * grandchildLocal.GetMatrix();
		RAVEN_CHECK(Near(scene.GetWorldTransform(grandchild), expected));
		scene.UpdateWorldTransforms();
		RAVEN_CHECK(Near(CachedWorld(registry, grandchild), expected));

		// Mutable view access marks the edit; the next update sees it.
		registry.View<TransformComponent>().Each([&](Entity entity, TransformComponent& transform)
		{
			if (entity == child)
				transform.Position.Z = 1.0f;
		});
		auto movedChild = childLocal;
		movedChild.Position.Z = 1.0f;
		scene.UpdateWorldTransforms();
		RAVEN_CHECK(Near(CachedWorld(registry, grandchild),
			rootLocal.GetMatrix() * movedChild.GetMatrix() * grandchildLocal.GetMatrix()));
		RAVEN_CHECK(registry.Write(child, childLocal));
		scene.UpdateWorldTransforms();
		RAVEN_CHECK(Near(CachedWorld(registry, grandchild), expected));

		// Reparenting and detaching keep the local transform.
		scene.SetParent(grandchild, root);
		TransformComponent local;
		RAVEN_CHECK(registry.Read(grandchild, local));
		RAVEN_CHECK(Near(local.GetMatrix(), grandchildLocal.GetMatrix()));
		RAVEN_CHECK(Near(scene.GetWorldTransform(grandchild), rootLocal.GetMatrix() * grandchildLocal.GetMatrix()));
		scene.UpdateWorldTransforms();
		RAVEN_CHECK(Near(CachedWorld(registry, grandchild), rootLocal.GetMatrix() * grandchildLocal.GetMatrix()));
		scene.SetParent(grandchild);
		RAVEN_CHECK(scene.GetParent(grandchild).IsNull());
		RAVEN_CHECK(Near(scene.GetWorldTransform(grandchild), grandchildLocal.GetMatrix()));

		// Cycles and self-parenting are rejected without changing the hierarchy.
		scene.SetParent(grandchild, child);
		RAVEN_CHECK_THROWS(scene.SetParent(root, grandchild));
		RAVEN_CHECK_THROWS(scene.SetParent(root, root));
		RAVEN_CHECK(scene.GetParent(root).IsNull());

		// Entities owned by another scene cannot join this hierarchy.
		Scene other(registry);
		const auto stranger = other.CreateEntity();
		RAVEN_CHECK_THROWS(scene.SetParent(stranger, root));
		RAVEN_CHECK_THROWS(scene.DestroyEntity(stranger));

		// Scene-scoped views and borrowed access skip other scenes and unowned entities.
		const auto foreign = registry.Create();
		RAVEN_CHECK(registry.Add(stranger, TransformComponent{}));
		RAVEN_CHECK(registry.Add(foreign, TransformComponent{}));
		const auto access = scene.GetAccess();
		std::size_t owned = 0;
		bool leaked = false;
		access.View<const TransformComponent>().Each([&](Entity entity, const TransformComponent&)
		{
			++owned;
			leaked = leaked || entity == stranger || entity == foreign;
		});
		RAVEN_CHECK(owned == 3 && !leaked);
		RAVEN_CHECK(access.Count<TransformComponent>() == 3);
		RAVEN_CHECK(access.TryGet<TransformComponent>(foreign) == nullptr);
		RAVEN_CHECK(access.TryGet<TransformComponent>(stranger) == nullptr);
		RAVEN_CHECK(access.TryGet<TransformComponent>(root) != nullptr);
		RAVEN_CHECK(access.Query<TransformComponent>().size() == 3);
		registry.Destroy(foreign);

		// Destroying a root destroys its whole subtree and its names.
		scene.DestroyEntity(root);
		RAVEN_CHECK(!registry.IsAlive(root));
		RAVEN_CHECK(!registry.IsAlive(child));
		RAVEN_CHECK(!registry.IsAlive(grandchild));
		RAVEN_CHECK(scene.FindEntity("Child").IsNull());
		RAVEN_CHECK(scene.GetEntities().empty());
		RAVEN_CHECK(registry.IsAlive(stranger));
	}
}

namespace
{
	// Change ticks decide which cached worlds are recomputed (docs/ECSDesign.md, D4/D7). Commands,
	// the hierarchy index and field migration rely on these rules: Add, Write, mutable TryGet and
	// mutable views stamp a changed element; const access does not.
	void ChangeTicks()
	{
		Raven::ECS::Registry registry;
		Scene scene(registry);
		const auto entity = scene.CreateEntity();
		RAVEN_CHECK(registry.Add(entity, MakeTransform({ 1.0f, 0.0f, 0.0f })));
		scene.UpdateWorldTransforms();
		RAVEN_CHECK(NearPosition(CachedWorld(registry, entity), { 1.0f, 0.0f, 0.0f }));

		// A change made through const access is not stamped, so the cache keeps the old value.
		const_cast<TransformComponent*>(registry.TryGet<const TransformComponent>(entity))->Position.X = 2.0f;
		scene.UpdateWorldTransforms();
		RAVEN_CHECK(NearPosition(CachedWorld(registry, entity), { 1.0f, 0.0f, 0.0f }));

		registry.TryGet<TransformComponent>(entity)->Position.X = 3.0f;
		scene.UpdateWorldTransforms();
		RAVEN_CHECK(NearPosition(CachedWorld(registry, entity), { 3.0f, 0.0f, 0.0f }));

		registry.View<TransformComponent>().Each([](Entity, TransformComponent& transform) { transform.Position.X = 4.0f; });
		scene.UpdateWorldTransforms();
		RAVEN_CHECK(NearPosition(CachedWorld(registry, entity), { 4.0f, 0.0f, 0.0f }));

		registry.View<const TransformComponent>().Each([](Entity, const TransformComponent&) {});
		const_cast<TransformComponent*>(registry.TryGet<const TransformComponent>(entity))->Position.X = 5.0f;
		scene.UpdateWorldTransforms();
		RAVEN_CHECK(NearPosition(CachedWorld(registry, entity), { 4.0f, 0.0f, 0.0f }));

		RAVEN_CHECK(registry.Write(entity, MakeTransform({ 6.0f, 0.0f, 0.0f })));
		const auto before = registry.GetChangeTick();
		RAVEN_CHECK(registry.AdvanceChangeTick() == before && registry.GetChangeTick() > before);
		scene.UpdateWorldTransforms();
		RAVEN_CHECK(NearPosition(CachedWorld(registry, entity), { 6.0f, 0.0f, 0.0f }));
	}
}

int main()
{
	return RavenTests::Run("HierarchyTests", []
	{
		Hierarchy();
		ChangeTicks();
	});
}
