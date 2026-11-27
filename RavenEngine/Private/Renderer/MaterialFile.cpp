#include "Renderer/MaterialFile.hpp"

#include "Core/PathText.hpp"

#include <cmath>
#include <locale>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>

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
					// A setting given twice is a mistake in one of the two places.
					if (!seen.insert(slot.empty() ? key : key + " " + slot).second)
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
						// B4's shaders name slots 1 to 3; until then only the base color has one.
						if (slot != "base-color")
							throw std::runtime_error("unknown texture slot '" + slot + "'; the slot is base-color");
						material.Textures[0] = ReadId(input, "a texture asset UUID");
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
		return material;
	}
}
