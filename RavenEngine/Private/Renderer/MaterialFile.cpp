#include "Renderer/MaterialFile.hpp"

#include "Core/PathText.hpp"
#include "Raven/Core/Files.hpp"

#include <cmath>
#include <locale>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace Raven
{
	namespace
	{
		float ReadComponent(std::istringstream& input)
		{
			float value = 0.0f;
			if (!(input >> value) || !std::isfinite(value) || value < 0.0f || value > 64.0f)
				throw std::runtime_error("expected base-color <red> <green> <blue> <alpha>, each 0 to 64");
			return value;
		}

		AssetId ReadId(std::istringstream& input, const char* what)
		{
			std::string text;
			if (!(input >> text))
				throw std::runtime_error(std::string("expected ") + what);
			const auto id = AssetId::Parse(text);
			if (!id.IsValid())
				throw std::runtime_error(std::string("expected ") + what);
			return id;
		}
	}

	MaterialDefinition ParseMaterial(std::span<const std::byte> bytes, const std::filesystem::path& path)
	{
		MaterialDefinition material;
		std::istringstream file(std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
		std::string line;
		std::size_t number = 0;
		bool header = false;
		std::set<std::string> seen;
		while (std::getline(file, line))
		{
			++number;
			if (const auto comment = line.find('#'); comment != std::string::npos)
				line.erase(comment);
			std::istringstream input(line);
			input.imbue(std::locale::classic());
			std::string key;
			if (!(input >> key))
				continue;
			try
			{
				if (!header)
				{
					int version = 0;
					if (key != "raven-material" || !(input >> version) || version != 1)
						throw std::runtime_error("expected raven-material 1");
					header = true;
				}
				else
				{
					std::string slot;
					if (key == "texture" && !(input >> slot))
						throw std::runtime_error("expected texture <slot> <texture asset UUID>");
					// A setting given twice is a mistake in one of the two places; values check their names below.
					if (key != "value" && !seen.insert(slot.empty() ? key : key + " " + slot).second)
						throw std::runtime_error("'" + (slot.empty() ? key : key + " " + slot) + "' is set twice");
					if (key == "id")
						material.Id = ReadId(input, "the material's asset UUID");
					else if (key == "base-color")
					{
						material.BaseColor.X = ReadComponent(input);
						material.BaseColor.Y = ReadComponent(input);
						material.BaseColor.Z = ReadComponent(input);
						material.BaseColor.W = ReadComponent(input);
					}
					else if (key == "texture")
					{
						// base-color is the slot of materials without a shader; other names are the shader's.
						if (slot == "base-color")
							material.Textures[0] = ReadId(input, "a texture asset UUID");
						else
							material.NamedTextures.emplace_back(slot, ReadId(input, "a texture asset UUID"));
					}
					else if (key == "shader")
						material.Shader = ReadId(input, "a shader asset UUID");
					else if (key == "value")
					{
						std::string name;
						if (!(input >> name))
							throw std::runtime_error("expected value <name> <x> <y> <z> <w>");
						if (!seen.insert("value " + name).second)
							throw std::runtime_error("'value " + name + "' is set twice");
						Vec4 value;
						float* parts[]{ &value.X, &value.Y, &value.Z, &value.W };
						for (auto* part : parts)
							if (!(input >> *part) || !std::isfinite(*part))
								throw std::runtime_error("expected value " + name + " <x> <y> <z> <w>, four finite numbers");
						material.Values.emplace_back(name, value);
					}
					else if (key == "lit")
						material.Lit = true;
					else
						throw std::runtime_error("unknown setting '" + key + "'");
				}
				std::string extra;
				if (input >> extra)
					throw std::runtime_error("unexpected '" + extra + "'");
			}
			catch (const std::exception& error)
			{
				throw std::runtime_error(PathToUtf8(path) + " line " + std::to_string(number) + ": " + error.what());
			}
		}
		if (!header)
			throw std::runtime_error(PathToUtf8(path) + ": expected raven-material 1");
		if (!material.Id.IsValid())
			throw std::runtime_error(PathToUtf8(path) + ": a material needs its id line");
		if (!material.Shader.IsValid() && (!material.NamedTextures.empty() || !material.Values.empty()))
			throw std::runtime_error(PathToUtf8(path) + ": named textures and values need a shader line");
		return material;
	}

	bool DeclaresVertexOffset(std::span<const std::byte> source)
	{
		const std::string_view text(reinterpret_cast<const char*>(source.data()), source.size());
		return text.find("RAVEN_OFFSET_BOUND") != std::string_view::npos;
	}

	CompiledHlsl CompileOffsetVertex(const std::filesystem::path& source, const std::filesystem::path& scratch)
	{
		std::filesystem::create_directories(scratch);
		const auto wrapper = scratch / "offset-vertex.hlsl";
		const auto path = std::filesystem::absolute(source).generic_u8string();
		const std::string text = "#include \"" + std::string(reinterpret_cast<const char*>(path.data()), path.size()) +
			"\"\n#include \"Color.vert.hlsl\"\n";
		Files::WriteAtomic(wrapper, std::as_bytes(std::span(text.data(), text.size())));
		return CompileHlsl(wrapper, "vs_6_0", "VSMain", scratch / "compile");
	}
}
