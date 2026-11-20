#pragma once

#include "Raven/Math/Transform.hpp"

#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <string>

namespace Raven::LevelFile
{
	inline bool ReadVector(std::istream& input, Vec3& value)
	{
		return (input >> value.X >> value.Y >> value.Z) &&
			std::isfinite(value.X) && std::isfinite(value.Y) && std::isfinite(value.Z);
	}

	template<class ReadRecord>
	void ReadRecords(std::istream& file, ReadRecord readRecord, int maxVersion = 1, int* fileVersion = nullptr)
	{
		std::string line;
		std::size_t lineNumber = 0;
		bool hasVersion = false;
		while (std::getline(file, line))
		{
			++lineNumber;
			const auto comment = line.find('#');
			if (comment != std::string::npos)
				line.erase(comment);
			std::istringstream input(line);
			input.imbue(std::locale::classic());
			std::string record;
			if (!(input >> record))
				continue;

			try
			{
				if (hasVersion)
					readRecord(record, input);
				else
				{
					int version = 0;
					if (record != "raven-level" || !(input >> version) || version < 1 || version > maxVersion)
						throw std::runtime_error("unsupported raven-level header or version");
					hasVersion = true;
					if (fileVersion) *fileVersion = version;
				}
				std::string extra;
				if (input >> extra)
					throw std::runtime_error("unexpected field '" + extra + "'");
			}
			catch (const std::runtime_error& error)
			{
				throw std::runtime_error("line " + std::to_string(lineNumber) + ": " + error.what());
			}
			catch (const std::invalid_argument& error)
			{
				throw std::runtime_error("line " + std::to_string(lineNumber) + ": " + error.what());
			}
		}
		if (!file.eof() || file.bad())
			throw std::runtime_error("could not read level");
		if (!hasVersion)
			throw std::runtime_error("missing 'raven-level 1'");
	}
	template<class ReadRecord>
	void ReadRecords(const std::filesystem::path& path, ReadRecord readRecord)
	{
		std::ifstream file(path);
		if (!file)
			throw std::runtime_error("could not open level");
		ReadRecords(file, readRecord);
	}

}
