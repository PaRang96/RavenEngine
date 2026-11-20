#include "LegacyScene.hpp"
#include "PlayerContent.hpp"
#include "SampleScenes.hpp"
#include "Raven/Core/Files.hpp"
#include "Raven/Scene/SceneProject.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace
{
	float Number(const char* text)
	{
		const std::string_view s(text);
		float value;
		const auto result = std::from_chars(s.data(), s.data() + s.size(), value);
		if (result.ec != std::errc{} || result.ptr != s.data() + s.size() || !std::isfinite(value))
			throw std::invalid_argument("Expected a finite number");
		return value;
	}
}

int main(int argc, char** argv)
{
	try
	{
		using namespace Raven;
		if (argc < 4)
			throw std::invalid_argument("Usage: RavenSceneTool <Content-root> "
										"<import|create-puzzle|create-collection|create-benchmark|validate|position|rotation|scale|lighting|"
										"rotator|stage-player> <scene-or-destination> [arguments]");
		const auto root = std::filesystem::absolute(argv[1]);
		const std::string_view command(argv[2]);
		const std::filesystem::path input(argv[3]);
		const auto project = SceneProject::Decode(
			Files::ReadBinary(root / "Project.ravenproject", 1024 * 1024), root / "Project.ravenproject");
		if (command == "stage-player")
		{
			if (argc < 6 || (argc - 4) % 2 != 0)
				throw std::invalid_argument("stage-player <destination> --scene <UUID> [--scene <UUID>] [--asset <UUID>]");
			std::vector<AssetId> scenes, extras;
			for (int i = 4; i < argc; i += 2)
			{
				const std::string_view option(argv[i]);
				if (option == "--scene")
					scenes.push_back(AssetId::Parse(argv[i + 1]));
				else if (option == "--asset")
					extras.push_back(AssetId::Parse(argv[i + 1]));
				else
					throw std::invalid_argument("Unknown stage-player option");
			}
			StagePlayerContent(project, root, input, scenes, extras);
			std::cout << "Staged player content\n";
			return 0;
		}
		if (command == "import")
		{
			if (argc != 5)
				throw std::invalid_argument("import <legacy-file> <binary-output>");
			ImportLegacyScene(Files::ReadBinary(input, 4 * 1024 * 1024)).Save(argv[4], project.Catalog, root);
		}
		else if (command == "create-puzzle")
		{
			if (argc != 4)
				throw std::invalid_argument("create-puzzle <binary-output>");
			CreatePuzzleScene().Save(input, project.Catalog, root);
		}
		else if (command == "create-collection")
		{
			if (argc != 4)
				throw std::invalid_argument("create-collection <binary-output>");
			CreateCollectionScene().Save(input, project.Catalog, root);
		}
		else if (command == "create-benchmark")
		{
			std::uint32_t count = 0;
			if (argc != 5)
				throw std::invalid_argument("create-benchmark <binary-output> <mesh-count>");
			const std::string_view value(argv[4]);
			const auto result = std::from_chars(value.data(), value.data() + value.size(), count);
			if (result.ec != std::errc{} || result.ptr != value.data() + value.size())
				throw std::invalid_argument("Expected a benchmark mesh count");
			CreateBenchmarkScene(count).Save(input, project.Catalog, root);
		}
		else
		{
			auto document = SceneDocument::Decode(Files::ReadBinary(input, 4 * 1024 * 1024));
			if (command == "validate")
			{
				if (argc != 4)
					throw std::invalid_argument("validate <scene>");
				std::cout << "Valid binary 3D scene: " << document.Meshes.size() << " meshes, "
						  << document.Groups.size() << " groups\n";
				return 0;
			}
			if (command == "position" || command == "rotation" || command == "scale")
			{
				if (argc != 8)
					throw std::invalid_argument("position|rotation|scale <scene> <node-ID> <X> <Y> <Z>");
				ECS::TransformComponent* transform = nullptr;
				for (auto& m : document.Meshes)
					if (m.Name == argv[4])
						transform = &m.Transform;
				for (auto& g : document.Groups)
					if (g.Name == argv[4])
						transform = &g.Transform;
				if (document.CameraId == argv[4])
					transform = &document.Camera->Spawn;
				if (!transform)
					throw std::invalid_argument("Unknown transform node");
				const Vec3 value{Number(argv[5]), Number(argv[6]), Number(argv[7])};
				if (command == "position")
					transform->Position = value;
				else if (command == "rotation")
					transform->Rotation = value;
				else
					transform->Scale = value;
			}
			else if (command == "lighting")
			{
				if (argc != 8)
					throw std::invalid_argument("lighting <scene> <ambient|direction|color> <X> <Y> <Z>");
				auto& l = document.Lighting.emplace(document.Lighting.value_or(ECS::LightingComponent{}));
				const Vec3 value{Number(argv[5]), Number(argv[6]), Number(argv[7])};
				const std::string_view field(argv[4]);
				if (field == "ambient")
					l.Ambient = value;
				else if (field == "direction")
					l.Direction = value;
				else if (field == "color")
					l.Color = value;
				else
					throw std::invalid_argument("Unknown lighting field");
			}
			else if (command == "rotator")
			{
				if (argc != 9)
					throw std::invalid_argument("rotator <scene> <node-ID> <X> <Y> <Z> <duration>");
				auto found = std::find_if(document.Rotators.begin(), document.Rotators.end(),
										  [&](const auto& r) { return r.Entity == argv[4]; });
				if (found == document.Rotators.end())
					throw std::invalid_argument("Unknown rotator");
				found->Rotation = {Number(argv[5]), Number(argv[6]), Number(argv[7])};
				found->Duration = Number(argv[8]);
			}
			else
				throw std::invalid_argument("Unknown scene command");
			document.Save(input, project.Catalog, root);
		}
		std::cout << "Saved binary 3D scene\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
