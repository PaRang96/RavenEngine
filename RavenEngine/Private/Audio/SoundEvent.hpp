#pragma once

#include "Raven/Assets/AssetRecord.hpp"
#include "Raven/Audio/Sound.hpp"
#include "Raven/Core/Api.hpp"

#include <cstddef>
#include <filesystem>
#include <span>
#include <vector>

namespace Raven
{
	// A sound-event asset (docs/GameServicesDesign.md, C8, decision A4): what designers tune about a
	// sound, as data that hot-reloads, while game code only says which event plays and where. Text,
	// one setting per line:
	//
	//   raven-sound 2
	//   id 4c1e9a52-...            the event's own asset ID (version 1 files have none)
	//   clip 0b47e26c-...          one line per variation; each play picks one at random
	//   volume 0.8 1.0             a random linear gain in this range (0 to 4)
	//   pitch 0.95 1.05            a random playback rate in this range (0.25 to 4)
	//   bus Effects                Master, Music, Effects, Dialogue, Ambience or Interface
	//   priority 1
	//   limit Pickup 4 stop-oldest a concurrency group: stop-oldest, stop-quietest, stop-lowest-priority
	//                              or refuse-new
	//   spatial 1 30               heard from its position, full volume to 1 m, silent at 30 m
	//   loop
	//   fade-in 0.2                seconds
	//
	// Unknown settings are errors, so a typo never silently plays a default.
	struct SoundEvent
	{
		// Version 2 files carry their own asset ID (docs/RenderingContentDesign.md, B0 contract 9).
		AssetId Id;
		std::vector<AssetId> Clips;
		float VolumeMin = 1.0f;
		float VolumeMax = 1.0f;
		float PitchMin = 1.0f;
		float PitchMax = 1.0f;
		// Bus, priority, loop, spatial distances, fade and concurrency; volume and pitch come from the
		// ranges at each play.
		SoundParams Defaults;
	};

	// Exported for the content tools, which stage an event's clips with it.
	RAVEN_API SoundEvent ParseSoundEvent(std::span<const std::byte> bytes, const std::filesystem::path& path);
}
