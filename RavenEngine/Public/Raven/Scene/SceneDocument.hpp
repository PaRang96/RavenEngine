#pragma once

#include "Raven/Assets/AssetCatalog.hpp"
#include "Raven/Components/CameraComponent.hpp"
#include "Raven/Components/LightingComponent.hpp"
#include "Raven/Components/MaterialComponent.hpp"

#include <cstddef>
#include <filesystem>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace Raven
{
	// Authored values only. Runtime components and native object layouts are not a file format.
	struct SceneDocument
	{
		struct MeshDefinition
		{
			std::string Name;
			AssetRef<MeshAsset> Asset;
			ECS::TransformComponent Transform;
			Vec4 Tint{};
			std::string MaterialName;
		};

		struct CameraDefinition
		{
			ECS::TransformComponent Spawn;
			ECS::CameraComponent Lens;
		};

		struct GroupDefinition
		{
			std::string Name;
			ECS::TransformComponent Transform;
		};

		struct SwitchDefinition
		{
			std::string Entity;
		};
		struct RotatorDefinition
		{
			std::string Entity;
			std::string Switch;
			Vec3 Rotation{};
			float Duration = 1.0f;
		};
		struct DoorDefinition
		{
			std::string Entity;
			std::vector<std::string> RequiredRotators;
			Vec3 OpenOffset{};
			float Duration = 1.0f;
		};
		struct GoalDefinition
		{
			std::string Entity;
			std::string Door;
			float Radius = 1.0f;
		};

		struct CollectionDefinition
		{
			std::vector<std::string> Pickups;
		};

		std::optional<CameraDefinition> Camera;
		std::optional<Vec3> OrbitTarget;
		std::vector<MeshDefinition> Meshes;
		std::vector<GroupDefinition> Groups;
		std::map<std::string, std::string> Parents;
		std::string CameraId;
		std::string LightingId;
		std::string ControlledMesh;
		std::string Beacon;
		float BeaconRadius = 0.0f;
		std::map<std::string, ECS::MaterialComponent> Materials;
		std::optional<ECS::LightingComponent> Lighting;
		AssetRef<AudioAsset> WinSound;
		std::vector<SwitchDefinition> Switches;
		std::vector<RotatorDefinition> Rotators;
		std::vector<DoorDefinition> Doors;
		std::vector<GoalDefinition> Goals;
		std::optional<CollectionDefinition> Collection;

		RAVEN_API void Validate() const;
		RAVEN_API static SceneDocument Decode(std::span<const std::byte> bytes);
		RAVEN_API std::vector<std::byte> Encode() const;
		RAVEN_API void Save(const std::filesystem::path& path, const AssetCatalog& catalog,
							const std::filesystem::path& contentRoot) const;
	};
}
