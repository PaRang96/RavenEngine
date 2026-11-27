#include "SampleSceneBuilder.hpp"

#include "Raven/Components/SpriteComponent.hpp"
#include "Raven/Components/StaticMeshComponent.hpp"

#include <stdexcept>

namespace Samples
{
	using namespace Raven;

	SampleSceneBuilder::SampleSceneBuilder()
		: m_Scene(m_Registry)
	{
		m_Registry.Register<SwitchComponent>();
		m_Registry.Register<RotatorComponent>();
		m_Registry.Register<DoorComponent>();
		m_Registry.Register<GoalComponent>();
		m_Registry.Register<PickupComponent>();
		m_Registry.Register<AttemptComponent>();
		m_Registry.Register<OrbitCameraComponent>();
		m_Registry.Register<ControlledMeshComponent>();
		m_Registry.Register<BeaconComponent>();
		m_Registry.Register<DemoComponent>();
		m_Registry.Register<OrderToggleComponent>();
		m_Registry.Register<PatrolComponent>();
	}

	ECS::Entity SampleSceneBuilder::Find(const std::string& name) const
	{
		const auto entity = m_Scene.FindEntity(name);
		if (entity.IsNull())
			throw std::invalid_argument("Unknown scene entity '" + name + "'");
		return entity;
	}

	ECS::EntityRef SampleSceneBuilder::Ref(const std::string& name) const
	{
		return name.empty() ? ECS::EntityRef{} : m_Scene.GetRef(Find(name));
	}

	void SampleSceneBuilder::Group(const std::string& name, const ECS::TransformComponent& transform)
	{
		m_Registry.Add(m_Scene.CreateEntity(name), transform);
	}

	void SampleSceneBuilder::Sprite(const std::string& name, const ECS::TransformComponent& transform,
		const ECS::SpriteComponent& sprite)
	{
		const auto entity = m_Scene.CreateEntity(name);
		m_Registry.Add(entity, transform);
		m_Registry.Add(entity, sprite);
	}

	void SampleSceneBuilder::Mesh(const std::string& name, AssetId mesh, const ECS::TransformComponent& transform,
		Vec4 tint, const ECS::MaterialComponent& material)
	{
		const auto entity = m_Scene.CreateEntity(name);
		ECS::StaticMeshComponent component;
		component.Mesh.Id = mesh;
		component.Tint = tint;
		m_Registry.Add(entity, transform);
		m_Registry.Add(entity, component);
		m_Registry.Add(entity, material);
	}

	void SampleSceneBuilder::Parent(const std::string& child, const std::string& parent)
	{
		m_Parents.emplace_back(child, parent);
	}

	void SampleSceneBuilder::Camera(const std::string& name, const ECS::TransformComponent& spawn,
		const ECS::CameraComponent& lens, Vec3 orbitTarget)
	{
		const auto entity = m_Scene.CreateEntity(name);
		OrbitCameraComponent orbit;
		orbit.Target = orbitTarget;
		orbit.HomePosition = spawn.Position;
		orbit.HomeRotation = spawn.Rotation;
		m_Registry.Add(entity, spawn);
		m_Registry.Add(entity, lens);
		m_Registry.Add(entity, ECS::ActiveCameraComponent{});
		m_Registry.Add(entity, orbit);
	}

	void SampleSceneBuilder::Lighting(const std::string& name, const ECS::LightingComponent& lighting)
	{
		m_Registry.Add(m_Scene.CreateEntity(name), lighting);
	}

	void SampleSceneBuilder::ControlledMesh(const std::string& name)
	{
		const auto entity = Find(name);
		ControlledMeshComponent controlled;
		controlled.InitialRotation = m_Registry.TryGet<const ECS::TransformComponent>(entity)->Rotation;
		m_Registry.Add(entity, controlled);
	}

	void SampleSceneBuilder::Beacon(const std::string& name, float radius)
	{
		const auto entity = Find(name);
		BeaconComponent beacon;
		beacon.InitialRotation = m_Registry.TryGet<const ECS::TransformComponent>(entity)->Rotation;
		m_Registry.Add(entity, beacon);
		m_Beacon = name;
		m_BeaconRadius = radius;
	}

	void SampleSceneBuilder::WinSound(AssetRef<AudioAsset> sound)
	{
		m_WinSound = sound;
	}

	void SampleSceneBuilder::Switch(const std::string& name)
	{
		m_Registry.Add(Find(name), SwitchComponent{});
	}

	void SampleSceneBuilder::Rotator(const std::string& name, const std::string& switchName, Vec3 rotation, float duration)
	{
		const auto entity = Find(name);
		RotatorComponent rotor;
		rotor.Switch = Ref(switchName);
		rotor.BaseRotation = m_Registry.TryGet<const ECS::TransformComponent>(entity)->Rotation;
		rotor.Rotation = rotation;
		rotor.Duration = duration;
		m_Registry.Add(entity, rotor);
		m_AuthoredMotion = true;
	}

	void SampleSceneBuilder::Door(const std::string& name, const std::vector<std::string>& rotators, Vec3 openOffset,
		float duration)
	{
		const auto entity = Find(name);
		if (rotators.empty() || rotators.size() > 4)
			throw std::invalid_argument("A door needs one to four rotators");
		DoorComponent door;
		door.ClosedPosition = m_Registry.TryGet<const ECS::TransformComponent>(entity)->Position;
		door.OpenOffset = openOffset;
		door.Duration = duration;
		door.RequiredCount = static_cast<std::uint32_t>(rotators.size());
		for (std::size_t index = 0; index < rotators.size(); ++index)
			door.RequiredRotators[index] = Ref(rotators[index]);
		m_Registry.Add(entity, door);
		m_AuthoredMotion = true;
	}

	void SampleSceneBuilder::Goal(const std::string& name, const std::string& door, float radius)
	{
		GoalComponent goal;
		goal.Door = Ref(door);
		goal.Radius = radius;
		m_Registry.Add(Find(name), goal);
		m_HasGoal = true;
	}

	void SampleSceneBuilder::Pickup(const std::string& name)
	{
		m_Registry.Add(Find(name), PickupComponent{});
	}

	void SampleSceneBuilder::Physics(const std::string& name, const ECS::ColliderComponent& collider,
		std::optional<ECS::RigidBodyComponent> body)
	{
		const auto entity = Find(name);
		m_Registry.Add(entity, collider);
		if (body)
			m_Registry.Add(entity, *body);
	}

	void SampleSceneBuilder::Patrol(const std::string& name, const ECS::MoverComponent& mover,
		const PatrolComponent& patrol)
	{
		const auto entity = Find(name);
		m_Registry.Add(entity, mover);
		m_Registry.Add(entity, patrol);
	}

	SceneFile SampleSceneBuilder::Finish()
	{
		for (const auto& [child, parent] : m_Parents)
			m_Scene.SetParent(Find(child), Find(parent));
		// Without authored goals, the beacon is the goal, spinning unless a rotator already drives it.
		if (!m_HasGoal && !m_Beacon.empty())
		{
			const auto beacon = Find(m_Beacon);
			GoalComponent goal;
			goal.Radius = m_BeaconRadius;
			m_Registry.Add(beacon, goal);
			if (!m_Registry.Has<RotatorComponent>(beacon))
			{
				RotatorComponent spin;
				spin.BaseRotation = m_Registry.TryGet<const BeaconComponent>(beacon)->InitialRotation;
				spin.Rotation = { 0, 0, -.455f };
				m_Registry.Add(beacon, spin);
			}
		}
		const auto state = m_Scene.CreateEntity("$attempt");
		m_Registry.Add(state, AttemptComponent{});
		DemoComponent demo;
		demo.WinSound = m_WinSound;
		demo.OrbitAnimation = !m_AuthoredMotion;
		m_Registry.Add(state, demo);

		// The transparency demo: two opaque and two overlapping transparent quads.
		const AssetRef<TextureAsset> grid{ AssetId::Parse("7a590cee-ec82-4a27-927f-860e64619c19") };
		const AssetRef<TextureAsset> alternate{ AssetId::Parse("a1552d51-3e07-483c-aabd-366e71785a34") };
		const auto quad = [&](const char* name, AssetRef<TextureAsset> texture, Vec3 position, Vec3 scale, Vec4 tint,
			ECS::SpriteBlendMode blend)
		{
			const auto entity = m_Scene.CreateEntity(name);
			ECS::TransformComponent transform;
			transform.Position = position;
			transform.Scale = scale;
			ECS::SpriteComponent sprite;
			sprite.Texture = texture;
			sprite.Tint = tint;
			sprite.BlendMode = blend;
			m_Registry.Add(entity, transform);
			m_Registry.Add(entity, sprite);
			return entity;
		};
		quad("$quad-grid", grid, { -2.0f, 0.5f, 0.6f }, { 1.6f, 1.0f, 1.6f }, { 1, 1, 1, 1 }, ECS::SpriteBlendMode::Opaque);
		quad("$quad-alternate", alternate, { 2.0f, 0.5f, 0.6f }, { 1.6f, 1.0f, 1.6f }, { 0.8f, 1.0f, 0.8f, 1.0f },
			ECS::SpriteBlendMode::Opaque);
		quad("$glass-red", grid, { -0.45f, -1.55f, 0.65f }, { 1.8f, 1.0f, 1.8f }, { 1.0f, 0.65f, 0.65f, 0.45f },
			ECS::SpriteBlendMode::Alpha);
		const auto blue = quad("$glass-blue", alternate, { 0.45f, -1.15f, 0.85f }, { 1.8f, 1.0f, 1.8f },
			{ 0.65f, 0.8f, 1.0f, 0.65f }, ECS::SpriteBlendMode::Alpha);
		m_Registry.Add(blue, OrderToggleComponent{});
		return CaptureScene(m_Scene);
	}
}
