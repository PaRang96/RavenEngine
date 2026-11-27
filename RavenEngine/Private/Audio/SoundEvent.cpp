#include "Audio/SoundEvent.hpp"

#include "Core/PathText.hpp"

#include <cmath>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <string>

namespace Raven
{
	namespace
	{
		float ReadFloat(std::istringstream& input, float low, float high, const char* what)
		{
			float value = 0.0f;
			if (!(input >> value) || !std::isfinite(value) || value < low || value > high)
				throw std::runtime_error(std::string("expected ") + what);
			return value;
		}
	}

	SoundEvent ParseSoundEvent(std::span<const std::byte> bytes, const std::filesystem::path& path)
	{
		SoundEvent event;
		std::istringstream file(std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
		std::string line;
		std::size_t number = 0;
		bool header = false;
		int version = 0;
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
					// Version 2 adds the ID; version 1 is still read (FD-10, version policy).
					if (key != "raven-sound" || !(input >> version) || (version != 1 && version != 2))
						throw std::runtime_error("expected raven-sound 1 or 2");
					header = true;
				}
				else if (key == "id" && version == 2)
				{
					std::string id;
					if (!(input >> id))
						throw std::runtime_error("expected the sound event's asset UUID");
					event.Id = AssetId::Parse(id);
				}
				else if (key == "clip")
				{
					std::string id;
					if (!(input >> id))
						throw std::runtime_error("expected an audio asset UUID");
					event.Clips.push_back(AssetId::Parse(id));
				}
				else if (key == "volume")
				{
					event.VolumeMin = ReadFloat(input, 0.0f, 4.0f, "volume minimum and maximum, 0 to 4");
					event.VolumeMax = ReadFloat(input, event.VolumeMin, 4.0f, "a volume maximum at least the minimum");
				}
				else if (key == "pitch")
				{
					event.PitchMin = ReadFloat(input, 0.25f, 4.0f, "pitch minimum and maximum, 0.25 to 4");
					event.PitchMax = ReadFloat(input, event.PitchMin, 4.0f, "a pitch maximum at least the minimum");
				}
				else if (key == "bus")
				{
					std::string bus;
					input >> bus;
					static constexpr const char* names[]{ "Master", "Music", "Effects", "Dialogue", "Ambience", "Interface" };
					std::size_t index = 0;
					while (index < std::size(names) && bus != names[index])
						++index;
					if (index == std::size(names))
						throw std::runtime_error("unknown bus '" + bus + "'");
					event.Defaults.Bus = static_cast<AudioBus>(index);
				}
				else if (key == "priority")
					event.Defaults.Priority = ReadFloat(input, 0.0f, 1.0e6f, "a priority of 0 or more");
				else if (key == "limit")
				{
					std::string group, rule;
					int count = 0;
					if (!(input >> group >> count >> rule) || count < 1 || count > 512)
						throw std::runtime_error("expected limit <group> <1 to 512> <rule>");
					event.Defaults.Concurrency.Group = SoundGroup(group);
					event.Defaults.Concurrency.MaxCount = static_cast<std::uint16_t>(count);
					if (rule == "stop-oldest")
						event.Defaults.Concurrency.Rule = SoundLimitRule::StopOldest;
					else if (rule == "stop-quietest")
						event.Defaults.Concurrency.Rule = SoundLimitRule::StopQuietest;
					else if (rule == "stop-lowest-priority")
						event.Defaults.Concurrency.Rule = SoundLimitRule::StopLowestPriority;
					else if (rule == "refuse-new")
						event.Defaults.Concurrency.Rule = SoundLimitRule::RefuseNew;
					else
						throw std::runtime_error("unknown limit rule '" + rule + "'");
				}
				else if (key == "spatial")
				{
					event.Defaults.Spatial = true;
					event.Defaults.MinDistance = ReadFloat(input, 0.01f, 1.0e6f, "a minimum distance above 0");
					event.Defaults.MaxDistance = ReadFloat(input, event.Defaults.MinDistance, 1.0e7f,
						"a maximum distance beyond the minimum");
				}
				else if (key == "loop")
					event.Defaults.Loop = true;
				else if (key == "fade-in")
					event.Defaults.FadeIn = ReadFloat(input, 0.0f, 60.0f, "fade-in seconds, 0 to 60");
				else
					throw std::runtime_error("unknown setting '" + key + "'");
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
			throw std::runtime_error(PathToUtf8(path) + ": expected raven-sound 1 or 2");
		if (event.Clips.empty())
			throw std::runtime_error(PathToUtf8(path) + ": a sound event needs at least one clip");
		return event;
	}
}
