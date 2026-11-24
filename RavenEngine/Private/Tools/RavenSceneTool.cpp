// Generic scene file commands (docs/DataModelDesign.md, FD-10). Game-specific generators live in
// the game roots, for example Games/Samples/Tools.

#include "PlayerContent.hpp"

#include "Raven/Core/Files.hpp"
#include "Raven/Math/Transform.hpp"
#include "Raven/Scene/SceneFile.hpp"
#include "Raven/Scene/SceneProject.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{
	using namespace Raven;

	// Generous for 100k-entity scenes; a corrupt length still cannot allocate without bound.
	constexpr std::size_t SceneLimit = std::size_t{ 1 } << 30;

	float Number(std::string_view s)
	{
		float value;
		const auto result = std::from_chars(s.data(), s.data() + s.size(), value);
		if (result.ec != std::errc{} || result.ptr != s.data() + s.size() || !std::isfinite(value))
			throw std::invalid_argument("Expected a finite number");
		return value;
	}

	SceneFile ReadScene(const std::filesystem::path& path)
	{
		return SceneFile::Decode(Files::ReadBinary(path, SceneLimit));
	}

	// Sets one Vec3 field of the labeled entity's TransformComponent.
	void EditTransform(SceneFile& file, std::string_view label, std::string_view field, Vec3 value)
	{
		const auto entity = std::find_if(file.Entities.begin(), file.Entities.end(),
			[&](const SceneFile::Entity& candidate) { return candidate.Label == label; });
		if (entity == file.Entities.end())
			throw std::invalid_argument("No entity is labeled '" + std::string(label) + "'");
		const auto component = std::find_if(file.Components.begin(), file.Components.end(),
			[](const SceneFile::Component& candidate) { return candidate.Type == "Raven::ECS::TransformComponent"; });
		if (component == file.Components.end())
			throw std::invalid_argument("The scene stores no transforms");
		const auto row = std::find(component->Entities.begin(), component->Entities.end(), entity->Id);
		if (row == component->Entities.end())
			throw std::invalid_argument("The entity has no transform");
		for (std::size_t index = 0; index < component->Fields.size(); ++index)
			if (component->Fields[index].Name == field && component->Fields[index].Size == sizeof(Vec3))
			{
				const auto offset = component->FieldOffset(index) +
					static_cast<std::size_t>(row - component->Entities.begin()) * sizeof(Vec3);
				std::memcpy(component->Values.data() + offset, &value, sizeof(Vec3));
				return;
			}
		throw std::invalid_argument("The stored transform has no field " + std::string(field));
	}
}

int main(int argc, char** argv)
{
	try
	{
		if (argc < 4)
			throw std::invalid_argument("Usage: RavenSceneTool <Content-root> "
				"<validate|dump|apply|position|rotation|scale|stage-player> <scene-or-destination> [arguments]");
		const auto root = std::filesystem::absolute(argv[1]);
		const std::string_view command(argv[2]);
		const std::filesystem::path input(argv[3]);
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
			const auto project = SceneProject::Decode(
				Files::ReadBinary(root / "Project.ravenproject", 1024 * 1024), root / "Project.ravenproject");
			StagePlayerContent(project, root, input, scenes, extras);
			std::cout << "Staged player content\n";
			return 0;
		}
		if (command == "validate")
		{
			const auto file = ReadScene(input);
			std::cout << "Valid scene: " << file.Entities.size() << " entities, " << file.Components.size()
				<< " component types, " << file.Unknown.size() << " unknown sections\n";
			return 0;
		}
		if (command == "dump")
		{
			const auto text = ReadScene(input).ToText();
			if (argc == 5)
				Files::WriteAtomic(argv[4], std::as_bytes(std::span(text.data(), text.size())));
			else
				std::cout << text;
			return 0;
		}
		if (command == "apply")
		{
			if (argc != 5)
				throw std::invalid_argument("apply <text> <scene>");
			std::ifstream stream(input, std::ios::binary);
			const std::string text((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
			if (!stream && !stream.eof())
				throw std::runtime_error("Cannot read " + input.string());
			SceneFile::FromText(text).Save(argv[4]);
			std::cout << "Saved scene " << argv[4] << '\n';
			return 0;
		}
		if (command == "position" || command == "rotation" || command == "scale")
		{
			if (argc != 8)
				throw std::invalid_argument("position|rotation|scale <scene> <label> <X> <Y> <Z>");
			auto file = ReadScene(input);
			const std::string field = command == "position" ? "Position" : command == "rotation" ? "Rotation" : "Scale";
			EditTransform(file, argv[4], field, { Number(argv[5]), Number(argv[6]), Number(argv[7]) });
			file.Save(input);
			std::cout << "Saved scene " << input.string() << '\n';
			return 0;
		}
		throw std::invalid_argument("Unknown scene command");
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
