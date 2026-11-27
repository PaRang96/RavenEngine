#pragma once

#include "SampleComponents.hpp"

#include "Raven/Components/CameraComponent.hpp"
#include "Raven/Components/LightingComponent.hpp"
#include "Raven/Components/MaterialComponent.hpp"
#include "Raven/Components/PhysicsComponents.hpp"
#include "Raven/Components/SpriteComponent.hpp"
#include "Raven/Components/TransformComponent.hpp"
#include "Raven/ECS/Registry.hpp"
#include "Raven/Scene/Scene.hpp"
#include "Raven/Scene/SceneFile.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace Samples
{
	// Authors a 3D sample scene as components (docs/DataModelDesign.md, FD-10). Names are entity
	// labels; references between entities become EntityRef values in Finish.
	class SampleSceneBuilder
	{
	public:
		SampleSceneBuilder();

		// Entities created from now on get sequential IDs, so the scene file keeps their creation
		// order (its rows are sorted by ID) and a loaded scene's pools follow it.
		void UseSequentialIds() noexcept { m_NextId = 1; }

		void Group(const std::string& name, const Raven::ECS::TransformComponent& transform);
		void Mesh(const std::string& name, Raven::AssetId mesh, const Raven::ECS::TransformComponent& transform,
			Raven::Vec4 tint, const Raven::ECS::MaterialComponent& material);
		void Parent(const std::string& child, const std::string& parent);
		// A model's node hierarchy as entities (docs/RenderingContentDesign.md, FD-23): a group named `name`
		// placed by `transform`, and below it one entity per node, "<name>/<node path>", with the node's
		// transform and, for a part, its mesh. A part named in `tints` takes that tint instead of `tint`.
		void Model(const std::string& name, const std::filesystem::path& file, const Raven::ECS::TransformComponent& transform,
			Raven::Vec4 tint, const Raven::ECS::MaterialComponent& material,
			const std::vector<std::pair<std::string, Raven::Vec4>>& tints = {});
		// The active camera, orbiting target in the orbit demo and returning to spawn on reset.
		void Camera(const std::string& name, const Raven::ECS::TransformComponent& spawn,
			const Raven::ECS::CameraComponent& lens, Raven::Vec3 orbitTarget);
		void Lighting(const std::string& name, const Raven::ECS::LightingComponent& lighting);
		void ControlledMesh(const std::string& name);
		// Without authored goals, the beacon becomes a spinning goal of this radius.
		void Beacon(const std::string& name, float radius);
		void WinSound(Raven::AssetRef<Raven::AudioAsset> sound);
		void Switch(const std::string& name);
		void Rotator(const std::string& name, const std::string& switchName, Raven::Vec3 rotation, float duration);
		void Door(const std::string& name, const std::vector<std::string>& rotators, Raven::Vec3 openOffset, float duration);
		void Goal(const std::string& name, const std::string& door, float radius);
		void Pickup(const std::string& name);
		// A collider, and a rigid body unless static, on an existing entity (docs/GameServicesDesign.md, FD-32).
		void Physics(const std::string& name, const Raven::ECS::ColliderComponent& collider,
			std::optional<Raven::ECS::RigidBodyComponent> body = {});
		// A world-space quad (docs/RenderingContentDesign.md, FD-20).
		void Sprite(const std::string& name, const Raven::ECS::TransformComponent& transform,
			const Raven::ECS::SpriteComponent& sprite);
		// A mover walking a circle (FD-47) on an existing entity.
		void Patrol(const std::string& name, const Raven::ECS::MoverComponent& mover, const PatrolComponent& patrol);

		// Adds the derived gameplay state and the transparency demo sprites, and captures the scene.
		Raven::SceneFile Finish();

	private:
		Raven::ECS::Entity Create(const std::string& name);
		Raven::ECS::Entity Find(const std::string& name) const;
		Raven::ECS::EntityRef Ref(const std::string& name) const;

		Raven::ECS::Registry m_Registry;
		Raven::Scene m_Scene;
		std::vector<std::pair<std::string, std::string>> m_Parents;
		std::string m_Beacon;
		float m_BeaconRadius = 0.0f;
		bool m_AuthoredMotion = false;
		bool m_HasGoal = false;
		Raven::AssetRef<Raven::AudioAsset> m_WinSound;
		// Zero: random IDs.
		std::uint64_t m_NextId = 0;
	};
}
