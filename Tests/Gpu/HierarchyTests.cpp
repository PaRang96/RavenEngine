// Guards scene hierarchy and world-transform behavior that the M1 cached world
// transforms must preserve (docs/ECSDesign.md, D7). GPU-labeled only because
// Scene requires an AssetStore, which requires a renderer.

#include "Raven/Components/TransformComponent.hpp"
#include "Raven/Scene/Scene.hpp"
#include "Support/Check.hpp"
#include "Support/TestWorld.hpp"

#include <cmath>
#include <numbers>

namespace
{
	using Raven::Mat4;
	using Raven::Scene;
	using Raven::Vec3;
	using Raven::ECS::Entity;
	using Raven::ECS::TransformComponent;

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

	TransformComponent MakeTransform(Vec3 position, Vec3 rotation = {}, Vec3 scale = { 1.0f, 1.0f, 1.0f })
	{
		TransformComponent transform;
		transform.Position = position;
		transform.Rotation = rotation;
		transform.Scale = scale;
		return transform;
	}

	void Hierarchy(RavenTests::TestWorld& world)
	{
		auto& registry = world.Registry;
		Scene scene(registry, world.Store);

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

		// The draw-time pass matches direct composition.
		Scene::WorldTransformStorage storage;
		{
			Scene::WorldTransformPass pass(scene, storage);
			RAVEN_CHECK(Near(pass.Get(grandchild, grandchildLocal), expected));
			RAVEN_CHECK(Near(pass.Get(child, childLocal), rootLocal.GetMatrix() * childLocal.GetMatrix()));
		}

		// Moving a parent moves its descendants on the next query and the next pass.
		rootLocal.Position = { 0.0f, 0.0f, 5.0f };
		RAVEN_CHECK(registry.Write(root, rootLocal));
		expected = rootLocal.GetMatrix() * childLocal.GetMatrix() * grandchildLocal.GetMatrix();
		RAVEN_CHECK(Near(scene.GetWorldTransform(grandchild), expected));
		{
			Scene::WorldTransformPass pass(scene, storage);
			RAVEN_CHECK(Near(pass.Get(grandchild, grandchildLocal), expected));
		}

		// Reparenting and detaching keep the local transform.
		scene.SetParent(grandchild, root);
		TransformComponent local;
		RAVEN_CHECK(registry.Read(grandchild, local));
		RAVEN_CHECK(Near(local.GetMatrix(), grandchildLocal.GetMatrix()));
		RAVEN_CHECK(Near(scene.GetWorldTransform(grandchild), rootLocal.GetMatrix() * grandchildLocal.GetMatrix()));
		scene.SetParent(grandchild);
		RAVEN_CHECK(scene.GetParent(grandchild).IsNull());
		RAVEN_CHECK(Near(scene.GetWorldTransform(grandchild), grandchildLocal.GetMatrix()));

		// Cycles and self-parenting are rejected without changing the hierarchy.
		scene.SetParent(grandchild, child);
		RAVEN_CHECK_THROWS(scene.SetParent(root, grandchild));
		RAVEN_CHECK_THROWS(scene.SetParent(root, root));
		RAVEN_CHECK(scene.GetParent(root).IsNull());

		// Entities owned by another scene cannot join this hierarchy.
		Scene other(registry, world.Store);
		const auto stranger = other.CreateEntity();
		RAVEN_CHECK_THROWS(scene.SetParent(stranger, root));
		RAVEN_CHECK_THROWS(scene.DestroyEntity(stranger));

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

int main()
{
	return RavenTests::Run("HierarchyTests", []
	{
		RavenTests::TestWorld world("RavenEngine HierarchyTests");
		Hierarchy(world);
	});
}
