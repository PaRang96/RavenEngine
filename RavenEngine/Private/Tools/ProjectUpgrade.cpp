#include "ProjectUpgrade.hpp"

#include "Assets/AssetKinds.hpp"
#include "Assets/ContentIndex.hpp"
#include "Core/PathText.hpp"
#include "Raven/Core/Files.hpp"
#include "Raven/Scene/SceneFile.hpp"
#include "Raven/Scene/SceneProject.hpp"

#include <algorithm>
#include <format>
#include <stdexcept>
#include <string_view>

namespace Raven
{
	namespace
	{
		constexpr std::size_t ProjectLimit = 4 * 1024 * 1024;
		constexpr std::size_t SceneLimit = std::size_t{ 1 } << 30;
		constexpr std::size_t SoundLimit = 1024 * 1024;

		std::string Generic(const std::filesystem::path& path)
		{
			const auto text = path.generic_u8string();
			return std::string(reinterpret_cast<const char*>(text.data()), text.size());
		}

		std::string Text(const std::vector<std::byte>& bytes)
		{
			return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
		}

		void Write(const std::filesystem::path& path, const std::string& text)
		{
			Files::WriteAtomic(path, std::as_bytes(std::span(text.data(), text.size())));
		}

		// The same assets, kinds, sources and texture settings, and the same scene order.
		void CheckSame(const SceneProjectDefinition& before, const SceneProjectDefinition& after)
		{
			const auto left = before.Catalog.GetRecords();
			const auto right = after.Catalog.GetRecords();
			if (left.size() != right.size())
				throw std::runtime_error(std::format("The upgraded project has {} assets instead of {}", right.size(), left.size()));
			for (std::size_t index = 0; index < left.size(); ++index)
			{
				const auto& a = left[index];
				const auto& b = right[index];
				if (a.Id != b.Id || a.Kind != b.Kind || a.SourcePath.lexically_normal() != b.SourcePath.lexically_normal())
					throw std::runtime_error("The upgraded project differs at " + a.Id.ToString());
				if (a.Kind == AssetKind::Texture &&
					before.Catalog.GetTexture({ a.Id }).ColorSpace != after.Catalog.GetTexture({ b.Id }).ColorSpace)
					throw std::runtime_error("The upgraded project changed the color space of " + a.Id.ToString());
			}
			if (before.Scenes.size() != after.Scenes.size() ||
				!std::equal(before.Scenes.begin(), before.Scenes.end(), after.Scenes.begin(),
					[](const SceneDescription& l, const SceneDescription& r) { return l.Id == r.Id; }))
				throw std::runtime_error("The upgraded project changed the scene order");
		}
	}

	std::string UpgradeProject(const std::filesystem::path& root)
	{
		const auto path = root / "Project.ravenproject";
		const auto original = Files::ReadBinary(path, ProjectLimit);
		const auto before = SceneProject::Decode(original, path);
		if (Text(original).starts_with("raven-project 3"))
			return "Already raven-project 3";
		std::size_t sidecars = 0, scenes = 0, sounds = 0;
		for (const auto& record : before.Catalog.GetRecords())
		{
			const auto source = root / record.SourcePath;
			const auto* kind = FindAssetKind(record.Kind);
			if (!kind || !kind->IsFileBacked())
				throw std::runtime_error("Cannot upgrade an asset of kind " + std::string(AssetKindName(record.Kind)));
			if (record.Kind == AssetKind::Scene)
			{
				auto file = SceneFile::Decode(Files::ReadBinary(source, SceneLimit));
				if (file.Id.IsValid() && file.Id != record.Id)
					throw std::runtime_error(PathToUtf8(record.SourcePath) + " already carries another ID");
				if (!file.Id.IsValid())
				{
					file.Id = record.Id;
					file.Save(source);
					++scenes;
				}
			}
			else if (record.Kind == AssetKind::Sound)
			{
				auto text = Text(Files::ReadBinary(source, SoundLimit));
				const auto header = text.find("raven-sound 1");
				if (header != std::string::npos)
				{
					text.replace(header, std::string_view("raven-sound 1").size(), "raven-sound 2\nid " + record.Id.ToString());
					Write(source, text);
					++sounds;
				}
			}
			else
			{
				Sidecar sidecar{ record.Id, record.Kind, std::nullopt };
				if (record.Kind == AssetKind::Texture)
					sidecar.ColorSpace = before.Catalog.GetTexture({ record.Id }).ColorSpace;
				const auto sidecarPath = SidecarPath(source);
				if (std::filesystem::exists(sidecarPath))
				{
					const auto existing = ParseSidecar(Files::ReadBinary(sidecarPath, ProjectLimit), sidecarPath);
					if (existing.Id != record.Id)
						throw std::runtime_error(PathToUtf8(sidecarPath) + " already names another ID");
					continue;
				}
				Write(sidecarPath, FormatSidecar(sidecar));
				++sidecars;
			}
		}

		std::string project = "raven-project 3\n\n"
			"# Project settings only. Assets are found in Content/: imported files by their .ravenasset\n"
			"# sidecars, scenes and sound events by the ID in their header (docs/RenderingContentDesign.md).\n";
		if (before.FixedStepRate != SceneProjectDefinition{}.FixedStepRate)
			project += std::format("fixed-step {}\n", before.FixedStepRate);
		if (!(before.Physics == PhysicsLimits{}))
			project += std::format("physics-limits {} {} {}\n", before.Physics.Bodies, before.Physics.BodyPairs,
				before.Physics.ContactConstraints);
		project += "\n# Scene order is the F4 order; the first scene starts.\n";
		for (const auto& scene : before.Scenes)
			project += "scene " + scene.Id.ToString() + "  # " + Generic(scene.SourcePath) + '\n';

		// Check the result before replacing the project: decode it against the written files.
		const auto candidate = std::as_bytes(std::span(project.data(), project.size()));
		const auto after = SceneProject::Decode(candidate, path);
		CheckSame(before, after);
		Write(path, project);
		return std::format("Upgraded to raven-project 3: {} sidecars written, {} scenes and {} sound events given their ID",
			sidecars, scenes, sounds);
	}

	std::string ListProject(const std::filesystem::path& root)
	{
		const auto path = root / "Project.ravenproject";
		const auto project = SceneProject::Decode(Files::ReadBinary(path, ProjectLimit), path);
		std::string out;
		for (const auto& scene : project.Scenes)
			out += "scene " + scene.Id.ToString() + ' ' + Generic(scene.SourcePath) + '\n';
		for (const auto& record : project.Catalog.GetRecords())
			if (record.Kind != AssetKind::Scene)
				out += std::string(AssetKindKeyword(record.Kind)) + ' ' + record.Id.ToString() + ' ' +
					Generic(record.SourcePath) + '\n';
		return out;
	}
}
