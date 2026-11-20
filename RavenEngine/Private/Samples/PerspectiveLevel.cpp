#include "PerspectiveLevel.hpp"
#include "Assets/AssetPath.hpp"

#include "Raven/Components/CameraComponent.hpp"
#include "Raven/Components/InteractionComponents.hpp"
#include "Raven/Components/StaticMeshComponent.hpp"
#include "Raven/ECS/Registry.hpp"
#include "Raven/Scene/SceneProject.hpp"
#include "Raven/Renderer/MeshHandle.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <utility>

namespace Raven
{
	PerspectiveLevel::PerspectiveLevel(ECS::Registry& registry, SceneProject& project,
									   const Definition& authored, const AssetCatalog& catalog)
		: Scene(registry, project.GetAssets()), m_Definition(authored)
	{
		authored.Validate();
		InstantiateScene(project, catalog);
		InitializeGameplay();
	}

	void PerspectiveLevel::InstantiateScene(SceneProject& project, const AssetCatalog& catalog)
	{
		const auto& level = m_Definition;
		for (const auto& mesh : level.Meshes)
		{
			const auto& asset = catalog.Get(mesh.Asset.Id);
			if (asset.Kind != AssetKind::Mesh)
				throw std::runtime_error("asset '" + mesh.Asset.Id.ToString() + "' is not a mesh");
		}
		// Validate every referenced file before publishing any new level entities.
		for (const auto& [name, material] : level.Materials)
			if (material.BaseColorTexture.IsValid())
			{
				const auto& texture = catalog.GetTexture(material.BaseColorTexture);
				if (!std::filesystem::is_regular_file(
						ResolveAssetPath(project.GetContentRoot(), texture.SourcePath)))
					throw std::runtime_error("texture file is missing");
			}
		std::vector<MeshHandle> handles;
		handles.reserve(level.Meshes.size());
		for (const auto& mesh : level.Meshes)
		{
			const auto material =
				mesh.MaterialName.empty() ? ECS::MaterialComponent{} : level.Materials.at(mesh.MaterialName);
			handles.push_back(UseAsset(catalog, mesh.Asset.Id, AssetKind::Mesh).GetMesh());
			if (material.BaseColorTexture.IsValid())
				UseAsset(catalog, material.BaseColorTexture.Id, AssetKind::Texture);
		}

		m_WinSound = level.WinSound.Id;
		if (m_WinSound.IsValid())
			UseAsset(catalog, m_WinSound, AssetKind::Audio);
		for (const auto& group : level.Groups)
			if (!m_Registry.Add(CreateEntity(group.Name), group.Transform))
				throw std::runtime_error("could not initialize group");
		for (std::size_t index = 0; index < level.Meshes.size(); ++index)
		{
			const auto& definition = level.Meshes[index];
			const auto entity = CreateEntity(definition.Name);
			const auto material = definition.MaterialName.empty()
									  ? ECS::MaterialComponent{}
									  : level.Materials.at(definition.MaterialName);
			ECS::StaticMeshComponent mesh;
			mesh.Mesh = handles[index];
			mesh.Tint = definition.Tint;
			if (!m_Registry.Add(entity, definition.Transform) || !m_Registry.Add(entity, mesh) ||
				!m_Registry.Add(entity, material))
				throw std::runtime_error("could not initialize mesh '" + definition.Name + "'");
		}
		m_Camera = {CreateEntity(level.CameraId), level.Camera->Spawn, *level.OrbitTarget};
		if (!m_Registry.Add(m_Camera.Entity, level.Camera->Spawn) ||
			!m_Registry.Add(m_Camera.Entity, level.Camera->Lens))
			throw std::runtime_error("could not initialize camera");
		m_Lighting = CreateEntity(level.LightingId);
		const auto lighting = level.Lighting.value_or(ECS::LightingComponent{});
		if (!m_Registry.Add(m_Lighting, lighting))
			throw std::runtime_error("could not initialize lighting");
		for (const auto& [child, parent] : level.Parents)
			SetParent(FindEntity(child), FindEntity(parent));
		SelectCamera(m_Camera.Entity);
	}

	void PerspectiveLevel::InitializeGameplay()
	{
		const auto& level = m_Definition;
		for (const auto& definition : level.Meshes)
		{
			if (definition.Name == level.ControlledMesh)
				m_ControlledMesh = {FindEntity(definition.Name), definition.Transform.Rotation};
			if (definition.Name == level.Beacon)
				m_Beacon = {FindEntity(definition.Name), definition.Transform.Rotation};
		}
		const auto reference = [](const std::string& name)
		{
			ECS::SceneReference value{};
			std::copy(name.begin(), name.end(), value.begin());
			return value;
		};
		for (const auto& definition : level.Switches)
			m_Registry.Add(FindEntity(definition.Entity), ECS::SwitchComponent{});
		for (const auto& definition : level.Rotators)
		{
			const auto entity = FindEntity(definition.Entity);
			ECS::TransformComponent transform;
			m_Registry.Read(entity, transform);
			ECS::RotatorComponent component;
			component.Switch = reference(definition.Switch);
			component.BaseRotation = transform.Rotation;
			component.Rotation = definition.Rotation;
			component.Duration = definition.Duration;
			m_Registry.Add(entity, component);
		}
		for (const auto& definition : level.Doors)
		{
			const auto entity = FindEntity(definition.Entity);
			ECS::TransformComponent transform;
			m_Registry.Read(entity, transform);
			ECS::DoorComponent component;
			component.ClosedPosition = transform.Position;
			component.OpenOffset = definition.OpenOffset;
			component.Duration = definition.Duration;
			component.RequiredCount = static_cast<std::uint32_t>(definition.RequiredRotators.size());
			for (std::size_t index = 0; index < definition.RequiredRotators.size(); ++index)
				component.RequiredRotators[index] = reference(definition.RequiredRotators[index]);
			m_Registry.Add(entity, component);
		}
		for (const auto& definition : level.Goals)
		{
			ECS::GoalComponent component;
			component.Door = reference(definition.Door);
			component.Radius = definition.Radius;
			m_Registry.Add(FindEntity(definition.Entity), component);
		}
		if (level.Collection)
			for (const auto& id : level.Collection->Pickups)
				if (!m_Registry.Add(FindEntity(id), ECS::PickupComponent{}))
					throw std::runtime_error("could not initialize pickup '" + id + "'");
		if (level.Goals.empty() && !m_Beacon.Entity.IsNull())
		{
			ECS::GoalComponent goal;
			goal.Radius = level.BeaconRadius;
			m_Registry.Add(m_Beacon.Entity, goal);
			if (!m_Registry.Has(m_Beacon.Entity, ECS::RotatorComponent{}))
			{
				ECS::RotatorComponent spin;
				spin.BaseRotation = m_Beacon.InitialRotation;
				spin.Rotation = {0, 0, -.455f};
				m_Registry.Add(m_Beacon.Entity, spin);
			}
		}
		m_Attempt = CreateEntity();
		m_Registry.Add(m_Attempt, ECS::AttemptComponent{});
		const bool drivenRotator = m_Registry.Has(m_ControlledMesh.Entity, ECS::RotatorComponent{});
		SetBindings(
			{.Camera = m_Camera.Entity, .Mesh = drivenRotator ? ECS::Entity{} : m_ControlledMesh.Entity});
	}

	void PerspectiveLevel::Save(const Definition& definition, const std::filesystem::path& path,
								const SceneProject& project)
	{
		definition.Save(path, project.GetCatalog(), project.GetContentRoot());
	}

	SceneLighting PerspectiveLevel::GetLighting() const
	{
		ECS::LightingComponent lighting;
		if (!m_Registry.Read(m_Lighting, lighting))
			return {};
		return lighting.GetLighting();
	}

	bool PerspectiveLevel::ConsumeCompletion()
	{
		ECS::AttemptComponent attempt;
		if (m_CompletionNotified || !m_Registry.Read(m_Attempt, attempt) || !attempt.Completed)
			return false;
		m_CompletionNotified = true;
		return true;
	}

	bool PerspectiveLevel::SwapMeshSources()
	{
		ECS::StaticMeshComponent controlled;
		ECS::StaticMeshComponent beacon;
		if (!m_Registry.Read(m_ControlledMesh.Entity, controlled) ||
			!m_Registry.Read(m_Beacon.Entity, beacon))
			return false;

		// Both components were read on the same registry thread.
		std::swap(controlled.Mesh, beacon.Mesh);
		m_Registry.Write(m_ControlledMesh.Entity, controlled);
		m_Registry.Write(m_Beacon.Entity, beacon);
		return true;
	}

	void PerspectiveLevel::AnimateMeshes(float angle, bool animateControlledMesh)
	{
		// Standalone orbit animates only the legacy visual demonstrations.
		if (!m_Definition.Rotators.empty() || !m_Definition.Doors.empty())
			return;
		ECS::TransformComponent transform;
		if (animateControlledMesh && m_Registry.Read(m_ControlledMesh.Entity, transform))
		{
			transform.Rotation = m_ControlledMesh.InitialRotation + Vec3{angle * 0.37f, 0.0f, angle};
			m_Registry.Write(m_ControlledMesh.Entity, transform);
		}
		if (m_Registry.Read(m_Beacon.Entity, transform))
		{
			transform.Rotation = m_Beacon.InitialRotation + Vec3{0.0f, 0.0f, -angle * 0.65f};
			m_Registry.Write(m_Beacon.Entity, transform);
		}
	}
}
