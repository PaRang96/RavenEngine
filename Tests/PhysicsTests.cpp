// Guards the physics world's contract with the components (docs/GameServicesDesign.md, FD-32):
// components as the source of truth, write-back that is never read as a game edit, teleports and
// velocity edits, ordered contact events with ends for removed bodies, queries, and the rejection
// rules. Picking (C5) and the movers' character backend (C6) run on Jolt too.

#include "../RavenEngine/Private/Physics/PhysicsWorld.hpp"
#include "../RavenEngine/Private/Physics/PickWorld.hpp"
#include "Raven/Components/PhysicsComponents.hpp"
#include "Raven/Components/StaticMeshComponent.hpp"
#include "Raven/Components/TransformComponent.hpp"
#include "Raven/ECS/Registry.hpp"
#include "Raven/Scene/Scene.hpp"
#include "Raven/Scene/SceneProject.hpp"
#include "Support/Check.hpp"

#include <cmath>
#include <memory>
#include <string>
#include <optional>
#include <vector>

namespace RavenTests
{
	using namespace Raven;

	constexpr double Step = 1.0 / 60.0;

	ECS::Entity AddBody(Scene& scene, ECS::Registry& registry, const char* name, Vec3 position,
		ECS::ColliderComponent collider, std::optional<ECS::RigidBodyComponent> body)
	{
		const auto entity = scene.CreateEntity(name);
		ECS::TransformComponent transform;
		transform.Position = position;
		RAVEN_CHECK(registry.Add(entity, transform));
		RAVEN_CHECK(registry.Add(entity, collider));
		if (body)
			RAVEN_CHECK(registry.Add(entity, *body));
		return entity;
	}

	ECS::ColliderComponent Box(Vec3 half)
	{
		ECS::ColliderComponent collider;
		collider.Size = half;
		return collider;
	}

	Vec3 PositionOf(ECS::Registry& registry, ECS::Entity entity)
	{
		return registry.TryGet<const ECS::TransformComponent>(entity)->Position;
	}

	std::size_t CountContacts(const PhysicsWorld& world, ContactEventKind kind)
	{
		std::size_t count = 0;
		for (const auto& contact : world.GetContacts())
			count += contact.Kind == kind ? 1 : 0;
		return count;
	}

	void FallRestAndEdit()
	{
		ECS::Registry registry;
		Scene scene(registry);
		PhysicsWorld world(PhysicsLimits{});
		const auto ground = AddBody(scene, registry, "ground", { 0.0f, 0.0f, -0.5f }, Box({ 20.0f, 20.0f, 0.5f }), {});
		const auto crate = AddBody(scene, registry, "crate", { 0.0f, 0.0f, 2.0f }, Box({ 0.5f, 0.5f, 0.5f }),
			ECS::RigidBodyComponent{});
		world.SetFixedPhase(true);
		std::size_t begins = 0;
		for (int step = 0; step < 180; ++step)
		{
			world.Step(scene, registry, Step);
			begins += CountContacts(world, ContactEventKind::Begin);
			for (const auto& contact : world.GetContacts())
				RAVEN_CHECK(contact.A.Id < contact.B.Id);
		}
		// It fell under Z-up gravity and rests on the ground.
		RAVEN_CHECK(std::abs(PositionOf(registry, crate).Z - 0.5f) < 0.02f);
		RAVEN_CHECK(begins == 1);
		RAVEN_CHECK(world.GetStats().Bodies == 2 && world.GetStats().Rejected == 0);
		// Contacts reach fixed-step systems only.
		world.SetFixedPhase(false);
		RAVEN_CHECK(world.GetContacts().empty());
		world.SetFixedPhase(true);

		// Rewriting the latest value, as render interpolation's restore does, is not a teleport.
		const auto rest = *registry.TryGet<const ECS::TransformComponent>(crate);
		RAVEN_CHECK(registry.Write(crate, rest));
		world.Step(scene, registry, Step);
		RAVEN_CHECK(std::abs(PositionOf(registry, crate).Z - 0.5f) < 0.02f);

		// A game edit of the transform teleports the body; an edit of the velocity changes it.
		auto moved = rest;
		moved.Position = { 6.0f, 0.0f, 3.0f };
		RAVEN_CHECK(registry.Write(crate, moved));
		auto body = *registry.TryGet<const ECS::RigidBodyComponent>(crate);
		body.LinearVelocity = { 0.0f, 4.0f, 0.0f };
		RAVEN_CHECK(registry.Write(crate, body));
		world.Step(scene, registry, Step);
		const auto after = PositionOf(registry, crate);
		RAVEN_CHECK(std::abs(after.X - 6.0f) < 0.01f && std::abs(after.Y - 4.0f / 60.0f) < 0.01f && after.Z > 2.9f);
		const auto velocity = registry.TryGet<const ECS::RigidBodyComponent>(crate)->LinearVelocity;
		RAVEN_CHECK(std::abs(velocity.Y - 4.0f) < 0.05f && velocity.Z < 0.0f);
		// Leaving the ground ended the contact.
		RAVEN_CHECK(CountContacts(world, ContactEventKind::End) <= 1);

		// Queries: a ray down hits the crate, then the ground; masks and sensors filter.
		const auto hit = world.CastRay({ 6.0f, 4.0f / 60.0f, 10.0f }, { 0.0f, 0.0f, -1.0f }, 20.0f);
		RAVEN_CHECK(hit && hit->Entity == crate && hit->Normal.Z > 0.9f);
		RAVEN_CHECK(!world.CastRay({ 6.0f, 0.0f, 10.0f }, { 0.0f, 0.0f, -1.0f }, 20.0f, 0u));
		const auto floor = world.CastRay({ -5.0f, -5.0f, 10.0f }, { 0.0f, 0.0f, -2.0f }, 20.0f);
		RAVEN_CHECK(floor && floor->Entity == ground && std::abs(floor->Distance - 10.0f) < 0.01f);
		PhysicsShape probe;
		probe.Size = { 1.0f, 1.0f, 1.0f };
		probe.Position = { -5.0f, -5.0f, 0.5f };
		const auto overlaps = world.Overlap(probe);
		RAVEN_CHECK(overlaps.size() == 1 && overlaps[0] == ground);
		const auto swept = world.CastShape(probe, { 1.0f, 0.0f, 0.0f }, 20.0f);
		RAVEN_CHECK(swept && swept->Entity == ground);

		// It lands again and falls asleep: Jolt drops a sleeping body's contacts, but for the game it
		// still touches the ground.
		std::size_t landed = 0, ended = 0;
		for (int step = 0; step < 150; ++step)
		{
			world.Step(scene, registry, Step);
			landed += CountContacts(world, ContactEventKind::Begin);
			ended += CountContacts(world, ContactEventKind::End);
		}
		RAVEN_CHECK(landed == 1 && ended == 0 && world.GetStats().ActiveBodies == 0);
		// Waking it in place keeps the contact; despawning it ends the contact at the next step.
		body = *registry.TryGet<const ECS::RigidBodyComponent>(crate);
		body.LinearVelocity = { 0.0f, 0.0f, 0.5f };
		RAVEN_CHECK(registry.Write(crate, body));
		world.Step(scene, registry, Step);
		world.Step(scene, registry, Step);
		RAVEN_CHECK(world.GetContacts().empty());
		scene.DestroyEntity(crate);
		world.Step(scene, registry, Step);
		RAVEN_CHECK(CountContacts(world, ContactEventKind::End) == 1 && world.GetStats().Bodies == 1);
	}

	void SensorsAndRejection()
	{
		ECS::Registry registry;
		Scene scene(registry);
		PhysicsWorld world(PhysicsLimits{});
		world.SetFixedPhase(true);
		AddBody(scene, registry, "wall", { 0.0f, 0.0f, 0.0f }, Box({ 1.0f, 1.0f, 1.0f }), {});
		// A static sensor still sees static bodies: it is kinematic inside Jolt.
		auto trigger = Box({ 0.5f, 0.5f, 0.5f });
		trigger.Sensor = true;
		AddBody(scene, registry, "trigger", { 1.2f, 0.0f, 0.0f }, trigger, {});
		bool sensed = false;
		for (int step = 0; step < 3; ++step)
		{
			world.Step(scene, registry, Step);
			for (const auto& contact : world.GetContacts())
				sensed |= contact.Kind == ContactEventKind::Begin && contact.Sensor;
		}
		RAVEN_CHECK(sensed);
		// Sensors are not hit by queries.
		RAVEN_CHECK(!world.CastRay({ 1.4f, 0.0f, 5.0f }, { 0.0f, 0.0f, -1.0f }, 4.5f));

		// Invalid values and placements are left out and logged, then join once fixed.
		const auto broken = AddBody(scene, registry, "broken", { 5.0f, 0.0f, 0.0f }, Box({ -1.0f, 1.0f, 1.0f }), {});
		const auto parent = scene.CreateEntity("parent");
		RAVEN_CHECK(registry.Add(parent, ECS::TransformComponent{}));
		const auto child = AddBody(scene, registry, "child", { 0.0f, 0.0f, 3.0f }, Box({ 0.5f, 0.5f, 0.5f }),
			ECS::RigidBodyComponent{});
		scene.SetParent(child, parent);
		world.Step(scene, registry, Step);
		RAVEN_CHECK(world.GetStats().Rejected == 2 && world.GetStats().Bodies == 2);
		RAVEN_CHECK(registry.Write(broken, Box({ 1.0f, 1.0f, 1.0f })));
		auto body = *registry.TryGet<const ECS::RigidBodyComponent>(child);
		body.Motion = ECS::MotionType::Kinematic;
		RAVEN_CHECK(registry.Write(child, body));
		world.Step(scene, registry, Step);
		RAVEN_CHECK(world.GetStats().Rejected == 0 && world.GetStats().Bodies == 4);

		// A new scene rebuilds from the components.
		world.Clear();
		world.Step(scene, registry, Step);
		RAVEN_CHECK(world.GetStats().Bodies == 4);
	}
}

namespace RavenTests
{
	void Movers()
	{
		ECS::Registry registry;
		Scene scene(registry);
		PhysicsWorld world(PhysicsLimits{});
		world.SetFixedPhase(true);
		AddBody(scene, registry, "floor", { 0.0f, 0.0f, -0.5f }, Box({ 20.0f, 20.0f, 0.5f }), {});
		AddBody(scene, registry, "wall", { 6.0f, 0.0f, 1.0f }, Box({ 0.5f, 5.0f, 1.0f }), {});
		auto trigger = Box({ 0.5f, 0.5f, 1.0f });
		trigger.Sensor = true;
		AddBody(scene, registry, "trigger", { 3.0f, 0.0f, 1.0f }, trigger, {});
		const auto hero = scene.CreateEntity("hero");
		ECS::TransformComponent start;
		start.Position = { 0.0f, 0.0f, 2.0f };
		RAVEN_CHECK(registry.Add(hero, start));
		RAVEN_CHECK(registry.Add(hero, ECS::MoverComponent{}));
		const auto mover = [&] { return *registry.TryGet<const ECS::MoverComponent>(hero); };
		const auto steer = [&](Vec3 desired)
		{
			auto value = mover();
			value.DesiredVelocity = desired;
			RAVEN_CHECK(registry.Write(hero, value));
		};
		const auto run = [&](int steps)
		{
			bool sensed = false;
			for (int step = 0; step < steps; ++step)
			{
				world.Step(scene, registry, Step);
				for (const auto& contact : world.GetContacts())
					sensed |= contact.Kind == ContactEventKind::Begin && contact.Sensor;
			}
			return sensed;
		};

		// It falls under gravity and stands on the floor: capsule center at radius + half height.
		run(90);
		RAVEN_CHECK(mover().Ground == ECS::MoverGround::OnGround && mover().GroundNormal.Z > 0.99f);
		RAVEN_CHECK(std::abs(PositionOf(registry, hero).Z - 0.9f) < 0.05f);
		RAVEN_CHECK(world.GetStats().Movers == 1);
		// Queries hit its inner body.
		const auto hit = world.CastRay({ 0.0f, 0.0f, 5.0f }, { 0.0f, 0.0f, -1.0f }, 10.0f);
		RAVEN_CHECK(hit && hit->Entity == hero);

		// Walking at the speed limit passes the sensor (which reports it) and stops at the wall.
		steer({ 10.0f, 0.0f, 0.0f });
		RAVEN_CHECK(run(30));
		RAVEN_CHECK(std::abs(mover().Velocity.X - 6.0f) < 0.1f);
		run(90);
		RAVEN_CHECK(std::abs(PositionOf(registry, hero).X - 5.1f) < 0.1f && mover().Ground == ECS::MoverGround::OnGround);

		// A positive Z jumps from the ground.
		steer({ 0.0f, 0.0f, 5.0f });
		run(10);
		// Beside the wall it is touching something that does not hold it up.
		RAVEN_CHECK(mover().Ground != ECS::MoverGround::OnGround && PositionOf(registry, hero).Z > 1.3f);
		steer({});
		run(90);
		RAVEN_CHECK(mover().Ground == ECS::MoverGround::OnGround);

		// A transform write teleports it.
		auto moved = *registry.TryGet<const ECS::TransformComponent>(hero);
		moved.Position = { -4.0f, 3.0f, 0.9f };
		RAVEN_CHECK(registry.Write(hero, moved));
		run(1);
		RAVEN_CHECK(std::abs(PositionOf(registry, hero).X + 4.0f) < 0.01f && std::abs(PositionOf(registry, hero).Y - 3.0f) < 0.01f);

		// A mover with a collider: both are left out and logged; removing the collider brings the mover back.
		RAVEN_CHECK(registry.Add(hero, Box({ 0.5f, 0.5f, 0.5f })));
		run(1);
		RAVEN_CHECK(world.GetStats().Movers == 0 && world.GetStats().Rejected == 2);
		RAVEN_CHECK(registry.Remove<ECS::ColliderComponent>(hero));
		run(1);
		RAVEN_CHECK(world.GetStats().Movers == 1 && world.GetStats().Rejected == 0);

		// Movers block each other (the engine's character grid): walking head-on, they stop apart.
		const auto rival = scene.CreateEntity("rival");
		ECS::TransformComponent rivalStart;
		rivalStart.Position = { 2.0f, -6.0f, 0.9f };
		RAVEN_CHECK(registry.Add(rival, rivalStart));
		ECS::MoverComponent rivalMover;
		rivalMover.DesiredVelocity = { -3.0f, 0.0f, 0.0f };
		RAVEN_CHECK(registry.Add(rival, rivalMover));
		moved = *registry.TryGet<const ECS::TransformComponent>(hero);
		moved.Position = { -2.0f, -6.0f, 0.9f };
		RAVEN_CHECK(registry.Write(hero, moved));
		steer({ 3.0f, 0.0f, 0.0f });
		run(90);
		const float gap = PositionOf(registry, rival).X - PositionOf(registry, hero).X;
		RAVEN_CHECK(gap > 0.75f && gap < 1.0f);
	}

	// A unit cube's 12 triangles, as the renderer's CPU pick copy of a mesh.
	struct CubeMesh
	{
		std::vector<Vec3> Vertices{ { -0.5f, -0.5f, -0.5f }, { 0.5f, -0.5f, -0.5f }, { 0.5f, 0.5f, -0.5f },
			{ -0.5f, 0.5f, -0.5f }, { -0.5f, -0.5f, 0.5f }, { 0.5f, -0.5f, 0.5f }, { 0.5f, 0.5f, 0.5f },
			{ -0.5f, 0.5f, 0.5f } };
		std::vector<std::uint32_t> Indices{ 0, 2, 1, 0, 3, 2, 4, 5, 6, 4, 6, 7, 0, 1, 5, 0, 5, 4, 1, 2, 6, 1, 6, 5,
			2, 3, 7, 2, 7, 6, 3, 0, 4, 3, 4, 7 };
	};

	void Picking()
	{
		ECS::Registry registry;
		Scene scene(registry);
		PickWorld picking;
		const auto cubeId = AssetId::Generate();
		auto version = std::make_shared<CubeMesh>();
		const PickMeshResolver meshes = [&](AssetId id) -> PickMesh
		{
			if (id != cubeId)
				return {};
			return { version, version->Vertices, version->Indices };
		};
		const auto place = [&](const std::string& name, Vec3 position, Vec3 scale)
		{
			const auto entity = scene.CreateEntity(name);
			ECS::TransformComponent transform;
			transform.Position = position;
			transform.Scale = scale;
			RAVEN_CHECK(registry.Add(entity, transform));
			ECS::StaticMeshComponent mesh;
			mesh.Mesh.Id = cubeId;
			RAVEN_CHECK(registry.Add(entity, mesh));
			return entity;
		};
		const auto near = place("near", { 0.0f, 5.0f, 0.0f }, { 1.0f, 1.0f, 1.0f });
		// Scaled by 4 and mirrored: its near face is at Y = 8.
		const auto far = place("far", { 0.0f, 10.0f, 0.0f }, { 4.0f, -4.0f, 4.0f });
		scene.UpdateWorldTransforms();
		const Ray down{ { 0.0f, 0.0f, 0.0f }, { 0.0f, 2.0f, 0.0f } };
		auto hit = picking.Pick(scene, registry, meshes, down, 100.0f);
		RAVEN_CHECK(hit && hit->Entity == near && std::abs(hit->Distance - 4.5f) < 1e-3f);
		// Both faces count: from inside the cube the ray leaves through its far face.
		hit = picking.Pick(scene, registry, meshes, { { 0.0f, 5.0f, 0.0f }, { 0.0f, 1.0f, 0.0f } }, 100.0f);
		RAVEN_CHECK(hit && hit->Entity == near && std::abs(hit->Distance - 0.5f) < 1e-3f);
		RAVEN_CHECK(!picking.Pick(scene, registry, meshes, down, 4.0f));
		RAVEN_CHECK_THROWS(picking.Pick(scene, registry, meshes, down, -1.0f));

		// A moved transform is picked where it is drawn; a despawned entity is gone.
		auto transform = *registry.TryGet<const ECS::TransformComponent>(near);
		transform.Position.X = 3.0f;
		RAVEN_CHECK(registry.Write(near, transform));
		scene.UpdateWorldTransforms();
		hit = picking.Pick(scene, registry, meshes, down, 100.0f);
		RAVEN_CHECK(hit && hit->Entity == far && std::abs(hit->Distance - 8.0f) < 1e-3f);
		// A reloaded mesh (a new version, twice as large) rebuilds the shared shape.
		version = std::make_shared<CubeMesh>();
		for (auto& vertex : version->Vertices)
			vertex = vertex * 2.0f;
		hit = picking.Pick(scene, registry, meshes, down, 100.0f);
		RAVEN_CHECK(hit && hit->Entity == far && std::abs(hit->Distance - 6.0f) < 1e-3f);
		scene.DestroyEntity(far);
		RAVEN_CHECK(!picking.Pick(scene, registry, meshes, down, 100.0f));
		RAVEN_CHECK(picking.GetStats().Bodies == 1 && picking.GetStats().Shapes == 1);

		// More renderables than the initial capacity: the pick world grows.
		for (int index = 0; index < 1500; ++index)
			place("grid-" + std::to_string(index), { static_cast<float>(index % 50) * 2.0f + 10.0f,
				static_cast<float>(index / 50) * 2.0f, 0.0f }, { 1.0f, 1.0f, 1.0f });
		scene.UpdateWorldTransforms();
		hit = picking.Pick(scene, registry, meshes, { { 10.0f, -5.0f, 0.0f }, { 0.0f, 1.0f, 0.0f } }, 100.0f);
		RAVEN_CHECK(hit && scene.GetAuthoredId(hit->Entity) == "grid-0");
		RAVEN_CHECK(picking.GetStats().Bodies == 1501);
	}
}

namespace RavenTests
{
	// A kinematic body that game code stops moving, but keeps writing with the same transform (a
	// door's mutable view, or interpolation's restore), stays where its component says. The sync that
	// guards it is rewritten as a changed-since scan (AA2-PERF-02).
	void KinematicStopsOnUnchangedWrite()
	{
		ECS::Registry registry;
		Scene scene(registry);
		PhysicsWorld world(PhysicsLimits{});
		world.SetFixedPhase(true);
		ECS::RigidBodyComponent kinematic;
		kinematic.Motion = ECS::MotionType::Kinematic;
		const auto door = AddBody(scene, registry, "door", {}, Box({ 0.5f, 0.5f, 0.5f }), kinematic);
		auto transform = *registry.TryGet<const ECS::TransformComponent>(door);
		for (int step = 0; step < 30; ++step)
		{
			transform.Position.X += 0.1f;
			RAVEN_CHECK(registry.Write(door, transform));
			world.Step(scene, registry, Step);
		}
		for (int step = 0; step < 60; ++step)
		{
			RAVEN_CHECK(registry.Write(door, transform));
			world.Step(scene, registry, Step);
		}
		// At 6 m/s, a drifting body would be 6 m away by now.
		const Vec3 above{ transform.Position.X, 0.0f, 5.0f };
		const auto hit = world.CastRay(above, { 0.0f, 0.0f, -1.0f }, 10.0f);
		RAVEN_CHECK(hit && hit->Entity == door);
	}

	// A collider without a RigidBodyComponent slides like one with the component's default friction,
	// including after that component is removed at run time (planning decision 12).
	void BareColliderFriction()
	{
		const auto slide = [](bool floorBody, bool removeIt)
		{
			ECS::Registry registry;
			Scene scene(registry);
			PhysicsWorld world(PhysicsLimits{});
			world.SetFixedPhase(true);
			std::optional<ECS::RigidBodyComponent> floorRigid;
			if (floorBody)
			{
				floorRigid.emplace();
				floorRigid->Motion = ECS::MotionType::Static;
				// The component's default, or a frictionless floor that is about to lose its component.
				floorRigid->Friction = removeIt ? 0.0f : ECS::RigidBodyComponent{}.Friction;
			}
			const auto floor = AddBody(scene, registry, "floor", { 0.0f, 0.0f, -0.5f }, Box({ 20.0f, 20.0f, 0.5f }), floorRigid);
			world.Step(scene, registry, Step);
			if (removeIt)
			{
				RAVEN_CHECK(registry.Remove<ECS::RigidBodyComponent>(floor));
				world.Step(scene, registry, Step);
			}
			ECS::RigidBodyComponent moving;
			moving.LinearVelocity = { 4.0f, 0.0f, 0.0f };
			const auto crate = AddBody(scene, registry, "crate", { 0.0f, 0.0f, 0.5f }, Box({ 0.5f, 0.5f, 0.5f }), moving);
			for (int step = 0; step < 60; ++step)
				world.Step(scene, registry, Step);
			return PositionOf(registry, crate).X;
		};
		const float bare = slide(false, false);
		RAVEN_CHECK(bare > 0.1f && bare < 4.0f);
		RAVEN_CHECK(std::abs(bare - slide(true, false)) < 1.0e-3f);
		RAVEN_CHECK(std::abs(bare - slide(true, true)) < 1.0e-3f);
	}
}

int main()
{
	return RavenTests::Run("PhysicsTests", []
	{
		RavenTests::FallRestAndEdit();
		RavenTests::SensorsAndRejection();
		RavenTests::Picking();
		RavenTests::Movers();
		RavenTests::KinematicStopsOnUnchangedWrite();
		RavenTests::BareColliderFriction();
	});
}
