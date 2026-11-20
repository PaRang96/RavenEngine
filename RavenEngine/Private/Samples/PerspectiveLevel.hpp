#pragma once

#include "Raven/Components/CameraComponent.hpp"
#include "Raven/Components/MaterialComponent.hpp"
#include "Raven/Components/LightingComponent.hpp"
#include "Raven/Assets/AssetRef.hpp"
#include "Raven/ECS/Entity.hpp"
#include "Raven/Scene/Scene.hpp"
#include "Raven/Scene/SceneDocument.hpp"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <map>
#include <span>
#include <string>
#include <vector>

namespace Raven
{
	namespace ECS { class Registry; }
	class SceneProject;

	class PerspectiveLevel final : public Scene
	{
	public:
		struct Camera
		{
			ECS::Entity Entity;
			ECS::TransformComponent Spawn;
			Vec3 OrbitTarget{};
		};

		using Definition = SceneDocument;
		using MeshDefinition = SceneDocument::MeshDefinition;
		using CameraDefinition = SceneDocument::CameraDefinition;
		using GroupDefinition = SceneDocument::GroupDefinition;

		static Definition Decode(std::span<const std::byte> bytes, const std::filesystem::path&) { return Definition::Decode(bytes); }
		// Saves authored values supplied by the caller, never transient gameplay state.
		static std::vector<std::byte> Encode(const Definition& definition) { return definition.Encode(); }
		static void Save(const Definition& definition, const std::filesystem::path& path, const SceneProject& project);
		const Definition& GetAuthoringDefinition() const noexcept { return m_Definition; }
		PerspectiveLevel(ECS::Registry& registry, SceneProject& project, const Definition& definition, const AssetCatalog& catalog);
		~PerspectiveLevel() override = default;
		const AssetLease* GetWinSound() const { return m_WinSound.IsValid() ? &GetAsset(m_WinSound) : nullptr; }
		PerspectiveLevel(const PerspectiveLevel&) = delete;
		PerspectiveLevel& operator=(const PerspectiveLevel&) = delete;

		const Camera& GetCamera() const noexcept { return m_Camera; }
		ECS::Entity GetControlledMesh() const noexcept { return m_ControlledMesh.Entity; }
		SceneLighting GetLighting() const;
		bool ConsumeCompletion();
		bool SwapMeshSources();
		void AnimateMeshes(float angle, bool animateControlledMesh);

	private:
		void InstantiateScene(SceneProject& project, const AssetCatalog& catalog);
		void InitializeGameplay();

		struct AnimatedMesh
		{
			ECS::Entity Entity;
			Vec3 InitialRotation{};
		};

		Definition m_Definition;
		AssetId m_WinSound;
		Camera m_Camera;
		ECS::Entity m_Lighting;
		AnimatedMesh m_ControlledMesh;
		AnimatedMesh m_Beacon;
		bool m_CompletionNotified = false;
		ECS::Entity m_Attempt;
	};
}
