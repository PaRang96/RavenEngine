#include "LegacyScene.hpp"
#include "Samples/LevelFile.hpp"

#include "Raven/Components/CameraComponent.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace Raven
{
	namespace
	{
		using MeshDefinition = SceneDocument::MeshDefinition;
		using CameraDefinition = SceneDocument::CameraDefinition;
		using LevelDefinition = SceneDocument;
		using MaterialAssignments = std::map<std::string, std::string>;


		MeshDefinition ReadMesh(std::istream& input)
		{
			MeshDefinition mesh;
			std::string asset;
			auto& transform = mesh.Transform;
			auto& tint = mesh.Tint;
			if (!(input >> mesh.Name >> asset) || !LevelFile::ReadVector(input, transform.Position) ||
				!LevelFile::ReadVector(input, transform.Rotation) ||
				!LevelFile::ReadVector(input, transform.Scale) ||
				!(input >> tint.X >> tint.Y >> tint.Z >> tint.W))
				throw std::runtime_error("expected mesh name, mesh UUID, position, rotation, scale and tint");

			if (transform.Scale.X == 0.0f || transform.Scale.Y == 0.0f || transform.Scale.Z == 0.0f)
				throw std::runtime_error("mesh scale must be nonzero on every axis");
			const auto model = transform.GetMatrix();
			for (const auto& column : model.Columns)
				for (const float value : column)
					if (!std::isfinite(value))
						throw std::runtime_error("mesh transform exceeds the supported numeric range");
			for (const float channel : {tint.X, tint.Y, tint.Z, tint.W})
				if (!(channel >= 0.0f && channel <= 1.0f))
					throw std::runtime_error("mesh tint must be in the range [0, 1]");
			mesh.Asset.Id = AssetId::Parse(asset);
			return mesh;
		}

		void ReadRecord(LevelDefinition& level, MaterialAssignments& assignments, const std::string& record,
						std::istream& input, int version)
		{
			if (version >= 3 && record == "group")
			{
				SceneDocument::GroupDefinition group;
				if (!(input >> group.Name) || !LevelFile::ReadVector(input, group.Transform.Position) ||
					!LevelFile::ReadVector(input, group.Transform.Rotation) ||
					!LevelFile::ReadVector(input, group.Transform.Scale))
					throw std::runtime_error("expected group ID and local position, rotation and scale");
				level.Groups.push_back(group);
			}
			else if (version >= 3 && record == "parent")
			{
				std::string child, parent;
				if (!(input >> child >> parent) || !level.Parents.emplace(child, parent).second)
					throw std::runtime_error("expected one parent binding per child");
			}
			else if (record == "camera" || (version >= 3 && record == "camera-node"))
			{
				CameraDefinition camera;
				if (record == "camera-node" && !(input >> level.CameraId))
					throw std::runtime_error("expected camera ID");
				if (level.Camera || !LevelFile::ReadVector(input, camera.Spawn.Position) ||
					!LevelFile::ReadVector(input, camera.Spawn.Rotation) ||
					!(input >> camera.Lens.VerticalFovRadians))
					throw std::runtime_error(
						"expected one camera record with position, rotation and vertical FOV");
				if (record == "camera-node")
				{
					std::string mode;
					if (!LevelFile::ReadVector(input, camera.Spawn.Scale) ||
						!(input >> mode >> camera.Lens.OrthographicHeight >> camera.Lens.NearPlane >>
						  camera.Lens.FarPlane) ||
						(mode != "perspective" && mode != "orthographic"))
						throw std::runtime_error(
							"expected camera scale, projection mode, height and clip planes");
					camera.Lens.ProjectionMode = mode == "perspective"
													 ? ECS::CameraProjectionMode::Perspective
													 : ECS::CameraProjectionMode::Orthographic;
				}
				ECS::GetCameraView(camera.Spawn, camera.Lens, 1.0f);
				level.Camera = camera;
			}
			else if (record == "orbit-target")
			{
				Vec3 target;
				if (level.OrbitTarget || !LevelFile::ReadVector(input, target))
					throw std::runtime_error("expected one orbit-target record with position");
				level.OrbitTarget = target;
			}
			else if (record == "mesh")
			{
				auto mesh = ReadMesh(input);
				if (std::any_of(level.Meshes.begin(), level.Meshes.end(),
								[&mesh](const MeshDefinition& other) { return other.Name == mesh.Name; }))
					throw std::runtime_error("duplicate mesh name '" + mesh.Name + "'");
				level.Meshes.push_back(std::move(mesh));
			}
			else if (record == "material")
			{
				std::string name;
				std::string mode;
				std::string texture;
				ECS::MaterialComponent material;
				auto& color = material.BaseColor;
				if (!(input >> name >> mode >> color.X >> color.Y >> color.Z >> color.W >> texture))
					throw std::runtime_error(
						"expected material name, lit/unlit, base RGBA and texture UUID or '-'");
				if (mode != "lit" && mode != "unlit")
					throw std::runtime_error("material mode must be lit or unlit");
				material.Lit = mode == "lit";
				for (const float channel : {color.X, color.Y, color.Z, color.W})
					if (!(channel >= 0.0f && channel <= 1.0f))
						throw std::runtime_error("material base color must be in [0, 1]");
				if (texture != "-")
				{
					material.BaseColorTexture.Id = AssetId::Parse(texture);
					if (!material.BaseColorTexture.IsValid())
						throw std::runtime_error("texture UUID must be non-nil; use - for no texture");
				}
				if (!level.Materials.emplace(name, material).second)
					throw std::runtime_error("duplicate material '" + name + "'");
			}
			else if (record == "use-material")
			{
				std::string mesh;
				std::string material;
				if (!(input >> mesh >> material) || !assignments.emplace(mesh, material).second)
					throw std::runtime_error("expected one use-material binding per mesh");
			}
			else if (record == "lighting" || (version >= 3 && record == "lighting-node"))
			{
				ECS::LightingComponent lighting;
				if (record == "lighting-node" && !(input >> level.LightingId))
					throw std::runtime_error("expected lighting ID");
				if (level.Lighting || !LevelFile::ReadVector(input, lighting.Ambient) ||
					!LevelFile::ReadVector(input, lighting.Direction) ||
					!LevelFile::ReadVector(input, lighting.Color))
					throw std::runtime_error(
						"expected one lighting record with ambient RGB, direction XYZ and light RGB");
				lighting.GetLighting().Validate();
				level.Lighting = lighting;
			}
			else if (record == "win-sound")
			{
				std::string identity;
				if (level.WinSound.IsValid() || !(input >> identity))
					throw std::runtime_error("expected one win-sound UUID");
				level.WinSound.Id = AssetId::Parse(identity);
				if (!level.WinSound.IsValid())
					throw std::runtime_error("sound UUID must be non-nil");
			}
			else if (record == "controlled")
			{
				if (!level.ControlledMesh.empty() || !(input >> level.ControlledMesh))
					throw std::runtime_error("expected one controlled record with a mesh name");
			}
			else if (record == "beacon")
			{
				if (!level.Beacon.empty() || !(input >> level.Beacon >> level.BeaconRadius) ||
					!std::isfinite(level.BeaconRadius) || level.BeaconRadius <= 0.0f)
					throw std::runtime_error(
						"expected one beacon record with a mesh name and positive finite radius");
			}
			else
			{
				throw std::runtime_error("unknown record '" + record + "'");
			}
		}

		const MeshDefinition& FindMesh(const LevelDefinition& level, const std::string& name,
									   const char* binding)
		{
			if (name.empty())
				throw std::runtime_error(std::string("missing ") + binding + " binding");
			const auto mesh =
				std::find_if(level.Meshes.begin(), level.Meshes.end(),
							 [&name](const MeshDefinition& definition) { return definition.Name == name; });
			if (mesh == level.Meshes.end())
				throw std::runtime_error(std::string(binding) + " refers to unknown mesh '" + name + "'");
			return *mesh;
		}
	}

	SceneDocument ImportLegacyScene(std::span<const std::byte> bytes)
	{
		SceneDocument level;
		MaterialAssignments assignments;
		const auto* text = bytes.empty() ? "" : reinterpret_cast<const char*>(bytes.data());
		std::istringstream input(std::string(text, bytes.size()));
		int version = 0;
		LevelFile::ReadRecords(
			input, [&](const std::string& record, std::istream& line)
			{ ReadRecord(level, assignments, record, line, version); }, 3, &version);
		for (const auto& [mesh, material] : assignments)
		{
			FindMesh(level, mesh, "use-material");
			if (!level.Materials.contains(material))
				throw std::runtime_error("Unknown material reference");
		}
		for (auto& mesh : level.Meshes)
			if (const auto found = assignments.find(mesh.Name); found != assignments.end())
				mesh.MaterialName = found->second;
		const auto exists = [&level](const std::string& name)
		{
			return std::any_of(level.Meshes.begin(), level.Meshes.end(),
							   [&](const auto& m) { return m.Name == name; }) ||
				   std::any_of(level.Groups.begin(), level.Groups.end(),
							   [&](const auto& g) { return g.Name == name; });
		};
		if (level.CameraId.empty())
		{
			level.CameraId = "$camera";
			while (exists(level.CameraId))
				level.CameraId += "_";
		}
		if (level.LightingId.empty())
		{
			level.LightingId = "$lighting";
			while (exists(level.LightingId) || level.LightingId == level.CameraId)
				level.LightingId += "_";
		}
		if (!level.WinSound.IsValid())
			level.WinSound.Id = AssetId::Parse("ef451c5a-02db-4d81-89a1-ec2ea98ed719");
		level.Validate();
		return level;
	}
}
