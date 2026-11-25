#pragma once

#include "Raven/ECS/Component.hpp"
#include "Raven/Math/Transform.hpp"

#include <cstdint>
#include <string_view>

namespace Raven
{
	// The engine's mix buses (docs/GameServicesDesign.md, C8). Every bus feeds Master. Dialogue
	// ducks Music and Ambience while it plays.
	enum class AudioBus : std::uint8_t
	{
		Master,
		Music,
		Effects,
		Dialogue,
		Ambience,
		Interface,
	};
	inline constexpr std::size_t AudioBusCount = 6;

	// What happens when a concurrency group is full and another sound asks to play.
	enum class SoundLimitRule : std::uint8_t
	{
		StopOldest,
		// The quietest sound as heard (volume, distance and bus); refuses the new one if it is quieter.
		StopQuietest,
		// The lowest Priority; refuses the new one if its priority is lower still.
		StopLowestPriority,
		RefuseNew,
	};

	// A cap shared by every sound that names the same group, such as "Footsteps" or "Rifle".
	struct SoundConcurrency
	{
		// SoundGroup("Name"); zero means no group.
		std::uint64_t Group = 0;
		std::uint16_t MaxCount = 0;
		SoundLimitRule Rule = SoundLimitRule::StopOldest;
	};

	constexpr std::uint64_t SoundGroup(std::string_view name) noexcept
	{
		return ECS::Detail::HashText(name);
	}

	// How one sound plays. Sound-event assets fill the same values from data later (decision A4).
	struct SoundParams
	{
		AudioBus Bus = AudioBus::Effects;
		// Linear gain, 0 to 4.
		float Volume = 1.0f;
		// Playback rate, 0.25 to 4; it changes pitch and length together.
		float Pitch = 1.0f;
		// Left -1 to right 1, for sounds that are not spatial.
		float Pan = 0.0f;
		bool Loop = false;
		// Spatial sounds are attenuated and panned from Position relative to the active camera.
		bool Spatial = false;
		Vec3 Position{};
		// Full volume up to MinDistance; inverse-distance falloff, fading to silence at MaxDistance.
		float MinDistance = 1.0f;
		float MaxDistance = 50.0f;
		// Ranks sounds together with how loud they are heard when more want to play than the mixer
		// has voices. The rest become virtual: loops resume where they would be, one-shots stop.
		float Priority = 1.0f;
		// Seconds.
		float FadeIn = 0.0f;
		SoundConcurrency Concurrency;
	};

	// What a play of a sound-event asset changes from the event's data.
	struct SoundOverrides
	{
		// Used by spatial events; other events ignore it.
		Vec3 Position{};
		// Multiply the event's random volume and pitch.
		float Volume = 1.0f;
		float Pitch = 1.0f;
	};

	// A playing sound. A value that never repeats within a run and is safe to keep in a component:
	// once the sound ends, every call with it does nothing.
	struct SoundHandle
	{
		std::uint64_t Value = 0;

		constexpr bool IsValid() const noexcept { return Value != 0; }
		friend constexpr bool operator==(SoundHandle, SoundHandle) = default;
	};
}

namespace Raven::ECS
{
	template<> struct ComponentValue<SoundHandle> : std::true_type {};
}
