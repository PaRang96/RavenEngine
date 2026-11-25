#include "Audio/AudioVoices.hpp"

#include "Raven/Core/Log.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <random>
#include <utility>

namespace Raven
{
	namespace
	{
		// Steals, virtualization and resumes: 10 ms.
		constexpr std::uint32_t FadeFrames = MixerSampleRate / 100;
		// Parameter changes of a playing sound glide over the same time.
		constexpr std::uint32_t ParamRampFrames = MixerSampleRate / 100;
		// The shortest stop, so even an immediate stop does not click.
		constexpr std::uint32_t DeclickFrames = 64;
		// Quieter than -80 dB is inaudible and never takes a voice.
		constexpr float AudibleFloor = 1.0e-4f;
		// A real sound keeps its voice unless another scores 10% higher, so ranks do not flap.
		constexpr float RealBonus = 1.1f;
		// Dialogue lowers Music and Ambience by 9 dB, in 80 ms, and lets go in 400 ms.
		const float DuckGain = std::pow(10.0f, -9.0f / 20.0f);
		constexpr double DuckAttack = 0.08;
		constexpr double DuckRelease = 0.4;

		float Finite(float value, float fallback) noexcept
		{
			return std::isfinite(value) ? value : fallback;
		}

		SoundParams Sanitize(SoundParams params) noexcept
		{
			if (static_cast<std::size_t>(params.Bus) >= AudioBusCount)
				params.Bus = AudioBus::Effects;
			params.Volume = std::clamp(Finite(params.Volume, 1.0f), 0.0f, 4.0f);
			params.Pitch = std::clamp(Finite(params.Pitch, 1.0f), 0.25f, 4.0f);
			params.Pan = std::clamp(Finite(params.Pan, 0.0f), -1.0f, 1.0f);
			params.Position = { Finite(params.Position.X, 0.0f), Finite(params.Position.Y, 0.0f),
				Finite(params.Position.Z, 0.0f) };
			params.MinDistance = std::clamp(Finite(params.MinDistance, 1.0f), 0.01f, 1.0e6f);
			params.MaxDistance = std::clamp(Finite(params.MaxDistance, 50.0f), params.MinDistance * 1.001f, 1.0e7f);
			params.Priority = std::clamp(Finite(params.Priority, 1.0f), 0.0f, 1.0e6f);
			params.FadeIn = std::clamp(Finite(params.FadeIn, 0.0f), 0.0f, 60.0f);
			return params;
		}

		std::uint32_t SecondsToFrames(float seconds) noexcept
		{
			const float clamped = std::clamp(Finite(seconds, 0.0f), 0.0f, 60.0f);
			return static_cast<std::uint32_t>(std::lround(clamped * static_cast<float>(MixerSampleRate)));
		}

		void Report(LogLevel level, const char* message) noexcept
		{
			try
			{
				Log(level, message);
			}
			catch (...)
			{
			}
		}
	}

	AudioVoices::AudioVoices(AudioMixer& mixer)
		: m_Mixer(mixer)
	{
		m_Sounds.reserve(MaxSounds + AudioMixer::MaxVoices);
		m_BusVolume.fill(1.0f);
		m_SentBusGain.fill(1.0f);
		// Handles start at a random value, so one kept in a saved component never names a sound of
		// a later run.
		std::random_device random;
		m_NextSerial = (std::uint64_t{ random() } << 32) ^ random();
	}

	SoundHandle AudioVoices::Play(std::shared_ptr<const PcmClip> clip, const SoundParams& params)
	{
		Collect();
		if (!clip || clip->Frames == 0)
			return {};
		Sound sound;
		sound.Clip = std::move(clip);
		sound.Params = Sanitize(params);
		Measure(sound);

		const auto& limit = sound.Params.Concurrency;
		if (limit.Group != 0 && limit.MaxCount > 0)
		{
			std::size_t members = 0;
			Sound* victim = nullptr;
			for (auto& other : m_Sounds)
			{
				if (other.Mode == State::Stopping || other.Params.Concurrency.Group != limit.Group)
					continue;
				++members;
				const bool better = !victim ||
					(limit.Rule == SoundLimitRule::StopQuietest ? other.Audible < victim->Audible :
					limit.Rule == SoundLimitRule::StopLowestPriority ? other.Params.Priority < victim->Params.Priority :
					other.Order < victim->Order);
				if (better)
					victim = &other;
			}
			if (members >= limit.MaxCount)
			{
				if (limit.Rule == SoundLimitRule::RefuseNew ||
					(limit.Rule == SoundLimitRule::StopQuietest && victim->Audible > sound.Audible) ||
					(limit.Rule == SoundLimitRule::StopLowestPriority && victim->Params.Priority > sound.Params.Priority))
					return {};
				Release(*victim, FadeFrames, false);
			}
		}

		const auto playing = std::count_if(m_Sounds.begin(), m_Sounds.end(),
			[](const Sound& other) { return other.Mode != State::Stopping; });
		if (static_cast<std::size_t>(playing) >= MaxSounds)
		{
			if (!std::exchange(m_ReportedFull, true))
				Report(LogLevel::Warning, "Too many sounds are playing; new ones are refused until some end");
			EraseStopped();
			return {};
		}
		m_ReportedFull = false;

		if (++m_NextSerial == 0)
			++m_NextSerial;
		sound.Serial = m_NextSerial;
		sound.Order = m_NextOrder++;
		EraseStopped();
		m_Sounds.push_back(std::move(sound));
		Sound& added = m_Sounds.back();
		// Start at once when a voice is free; otherwise the next Update ranks it against the others.
		if (added.Score > AudibleFloor && CountReal() < AudioMixer::MaxVoices)
			StartVoice(added, SecondsToFrames(added.Params.FadeIn));
		return { added.Serial };
	}

	void AudioVoices::Stop(SoundHandle handle, float fadeSeconds) noexcept
	{
		if (Sound* sound = Find(handle))
		{
			Release(*sound, SecondsToFrames(fadeSeconds), false);
			EraseStopped();
		}
	}

	void AudioVoices::StopAll(float fadeSeconds) noexcept
	{
		const auto fade = SecondsToFrames(fadeSeconds);
		for (auto& sound : m_Sounds)
			if (sound.Mode != State::Stopping)
				Release(sound, fade, false);
		EraseStopped();
	}

	bool AudioVoices::IsPlaying(SoundHandle handle) const noexcept
	{
		return Find(handle) != nullptr;
	}

	void AudioVoices::SetVolume(SoundHandle handle, float volume) noexcept
	{
		if (Sound* sound = Find(handle))
			sound->Params.Volume = std::clamp(Finite(volume, sound->Params.Volume), 0.0f, 4.0f);
	}

	void AudioVoices::SetPitch(SoundHandle handle, float pitch) noexcept
	{
		if (Sound* sound = Find(handle))
			sound->Params.Pitch = std::clamp(Finite(pitch, sound->Params.Pitch), 0.25f, 4.0f);
	}

	void AudioVoices::SetPosition(SoundHandle handle, Vec3 position) noexcept
	{
		if (Sound* sound = Find(handle); sound && std::isfinite(position.X) && std::isfinite(position.Y) &&
			std::isfinite(position.Z))
			sound->Params.Position = position;
	}

	void AudioVoices::SetBusVolume(AudioBus bus, float volume) noexcept
	{
		if (static_cast<std::size_t>(bus) < AudioBusCount)
			m_BusVolume[static_cast<std::size_t>(bus)] = std::clamp(Finite(volume, 1.0f), 0.0f, 4.0f);
	}

	float AudioVoices::GetBusVolume(AudioBus bus) const noexcept
	{
		return static_cast<std::size_t>(bus) < AudioBusCount ? m_BusVolume[static_cast<std::size_t>(bus)] : 0.0f;
	}

	void AudioVoices::SetListener(Vec3 position, Vec3 right) noexcept
	{
		const float length = std::sqrt(Dot(right, right));
		if (!std::isfinite(position.X) || !std::isfinite(position.Y) || !std::isfinite(position.Z) ||
			!std::isfinite(length) || length < 1.0e-6f)
			return;
		m_HasListener = true;
		m_ListenerPosition = position;
		m_ListenerRight = right * (1.0f / length);
	}

	void AudioVoices::ClearListener() noexcept
	{
		m_HasListener = false;
	}

	void AudioVoices::Update(double seconds) noexcept
	{
		Collect();
		seconds = std::clamp(std::isfinite(seconds) ? seconds : 0.0, 0.0, 1.0);
		for (auto& sound : m_Sounds)
		{
			const double frames = sound.Clip->Frames;
			if (sound.Mode == State::Real)
				sound.Position = m_Mixer.GetPosition(sound.Slot);
			else if (sound.Mode == State::Virtual)
			{
				sound.Position += seconds * MixerSampleRate * sound.Params.Pitch;
				if (sound.Params.Loop)
					sound.Position = std::fmod(sound.Position, frames);
				else if (sound.Position >= frames)
					sound.Mode = State::Stopping;
			}
		}
		EraseStopped();

		UpdateBuses(seconds);
		for (auto& sound : m_Sounds)
			Measure(sound);

		// Rank every live sound; the best MaxVoices that are audible get a voice.
		std::array<Sound*, MaxSounds + AudioMixer::MaxVoices> ranked{};
		std::size_t count = 0;
		for (auto& sound : m_Sounds)
			if (sound.Mode != State::Stopping)
				ranked[count++] = &sound;
		const auto rank = [](const Sound* sound) { return sound->Score * (sound->Mode == State::Real ? RealBonus : 1.0f); };
		std::sort(ranked.begin(), ranked.begin() + count, [&rank](const Sound* left, const Sound* right)
		{
			const float a = rank(left);
			const float b = rank(right);
			return a != b ? a > b : left->Order < right->Order;
		});
		for (std::size_t index = 0; index < count; ++index)
		{
			Sound& sound = *ranked[index];
			const bool wanted = index < AudioMixer::MaxVoices && sound.Score > AudibleFloor;
			if (!wanted && sound.Mode == State::Real)
				Release(sound, FadeFrames, true);
		}
		for (std::size_t index = 0; index < count && index < AudioMixer::MaxVoices; ++index)
		{
			Sound& sound = *ranked[index];
			if (sound.Score <= AudibleFloor)
				break;
			if (sound.Mode == State::Virtual)
			{
				// A freed voice returns only after its fade, so a resume may wait a frame.
				StartVoice(sound, sound.Started ? FadeFrames : SecondsToFrames(sound.Params.FadeIn));
				continue;
			}
			if (std::abs(sound.Left - sound.SentLeft) > 1.0e-4f || std::abs(sound.Right - sound.SentRight) > 1.0e-4f ||
				sound.Rate != sound.SentRate)
			{
				AudioMixer::Command command;
				command.Type = AudioMixer::CommandType::Update;
				command.Slot = sound.Slot;
				command.Serial = sound.Voice;
				command.Rate = sound.Rate;
				command.GainLeft = sound.Left;
				command.GainRight = sound.Right;
				command.RampFrames = ParamRampFrames;
				if (Send(command))
				{
					sound.SentLeft = sound.Left;
					sound.SentRight = sound.Right;
					sound.SentRate = sound.Rate;
				}
			}
		}
		EraseStopped();
	}

	AudioVoices::Counts AudioVoices::GetCounts() const noexcept
	{
		Counts counts;
		for (const auto& sound : m_Sounds)
		{
			if (sound.Mode == State::Real)
				++counts.Real;
			else if (sound.Mode == State::Virtual)
				++counts.Virtual;
			else
				++counts.Stopping;
		}
		return counts;
	}

	AudioVoices::Sound* AudioVoices::Find(SoundHandle handle) noexcept
	{
		if (!handle.IsValid())
			return nullptr;
		for (auto& sound : m_Sounds)
			if (sound.Serial == handle.Value)
				return sound.Mode == State::Stopping ? nullptr : &sound;
		return nullptr;
	}

	const AudioVoices::Sound* AudioVoices::Find(SoundHandle handle) const noexcept
	{
		return const_cast<AudioVoices*>(this)->Find(handle);
	}

	float AudioVoices::BusGain(AudioBus bus) const noexcept
	{
		const auto index = static_cast<std::size_t>(bus);
		float gain = m_BusVolume[index];
		if (bus != AudioBus::Master)
			gain *= m_BusVolume[0];
		if (bus == AudioBus::Music || bus == AudioBus::Ambience)
			gain *= m_Duck;
		return gain;
	}

	void AudioVoices::Measure(Sound& sound) const noexcept
	{
		const auto& params = sound.Params;
		float attenuation = 1.0f;
		float pan = params.Spatial ? 0.0f : params.Pan;
		if (params.Spatial && m_HasListener)
		{
			const Vec3 offset = params.Position - m_ListenerPosition;
			const float distance = std::sqrt(Dot(offset, offset));
			if (distance >= params.MaxDistance)
				attenuation = 0.0f;
			else
			{
				if (distance > params.MinDistance)
					attenuation = params.MinDistance / distance;
				// The last fifth before MaxDistance fades to silence, so sounds never cut off.
				const float fadeStart = params.MaxDistance * 0.8f;
				if (distance > fadeStart)
					attenuation *= (params.MaxDistance - distance) / (params.MaxDistance - fadeStart);
			}
			if (distance > 1.0e-4f)
				pan = std::clamp(Dot(offset, m_ListenerRight) / distance, -1.0f, 1.0f);
		}
		// Constant-power panning, scaled so a centered sound keeps full level on both sides: the near
		// side stays at exactly 1, so centered playback is sample-exact.
		const float angle = (pan + 1.0f) * std::numbers::pi_v<float> * 0.25f;
		const auto side = [](float value) { return value < 1.0e-6f ? 0.0f : std::min(1.0f, value); };
		const float volume = params.Volume * attenuation;
		sound.Left = (pan <= 0.0f ? 1.0f : side(std::numbers::sqrt2_v<float> * std::cos(angle))) * volume;
		sound.Right = (pan >= 0.0f ? 1.0f : side(std::numbers::sqrt2_v<float> * std::sin(angle))) * volume;
		sound.Rate = params.Pitch;
		sound.Audible = volume * BusGain(params.Bus);
		sound.Score = sound.Audible * params.Priority;
	}

	bool AudioVoices::StartVoice(Sound& sound, std::uint32_t rampFrames) noexcept
	{
		const auto slot = FreeSlot();
		if (!slot)
			return false;
		const auto& clip = *sound.Clip;
		const double position = sound.Params.Loop ? std::fmod(sound.Position, static_cast<double>(clip.Frames)) : sound.Position;
		if (!sound.Params.Loop && position >= clip.Frames)
			return false;

		AudioMixer::Command command;
		command.Type = AudioMixer::CommandType::Start;
		command.Bus = static_cast<std::uint8_t>(sound.Params.Bus);
		command.Loop = sound.Params.Loop;
		command.Slot = *slot;
		command.Serial = m_NextVoice;
		command.Samples = clip.Samples.data();
		command.Frames = clip.Frames;
		command.StartFrame = static_cast<std::uint32_t>(position);
		command.Rate = sound.Rate;
		command.GainLeft = sound.Left;
		command.GainRight = sound.Right;
		command.RampFrames = rampFrames;
		if (!Send(command))
			return false;
		m_Slots[*slot] = m_NextVoice;
		sound.Slot = *slot;
		sound.Voice = m_NextVoice++;
		sound.Mode = State::Real;
		sound.Started = true;
		sound.SentLeft = sound.Left;
		sound.SentRight = sound.Right;
		sound.SentRate = sound.Rate;
		return true;
	}

	void AudioVoices::Release(Sound& sound, std::uint32_t fadeFrames, bool keep) noexcept
	{
		if (sound.Mode != State::Real)
		{
			sound.Mode = State::Stopping;
			sound.Voice = 0;
			return;
		}
		AudioMixer::Command command;
		command.Type = AudioMixer::CommandType::Stop;
		command.Slot = sound.Slot;
		command.Serial = sound.Voice;
		command.RampFrames = std::max(fadeFrames, DeclickFrames);
		// If the queue is full the voice plays on to its end, and its release still arrives.
		Send(command);
		sound.Position = m_Mixer.GetPosition(sound.Slot) + static_cast<double>(command.RampFrames) * sound.Rate;
		if (keep && sound.Params.Loop)
		{
			// The slot stays taken until the audio thread releases it; the sound itself is virtual now.
			sound.Mode = State::Virtual;
			sound.Voice = 0;
		}
		else
			sound.Mode = State::Stopping;
	}

	bool AudioVoices::Send(const AudioMixer::Command& command) noexcept
	{
		if (m_Mixer.Send(command))
		{
			m_ReportedQueue = false;
			return true;
		}
		if (!std::exchange(m_ReportedQueue, true))
			Report(LogLevel::Warning, "Audio commands are not being consumed; sound changes are dropped until the output recovers");
		return false;
	}

	void AudioVoices::Collect() noexcept
	{
		AudioMixer::Release release;
		while (m_Mixer.TakeRelease(release))
		{
			if (release.Slot < m_Slots.size() && m_Slots[release.Slot] == release.Serial)
				m_Slots[release.Slot] = 0;
			for (auto& sound : m_Sounds)
				if (sound.Voice == release.Serial)
				{
					// A one-shot reached its end, or a stop fade finished.
					sound.Mode = State::Stopping;
					sound.Voice = 0;
				}
		}
		EraseStopped();
	}

	void AudioVoices::EraseStopped() noexcept
	{
		std::erase_if(m_Sounds, [](const Sound& sound) { return sound.Mode == State::Stopping && sound.Voice == 0; });
	}

	void AudioVoices::UpdateBuses(double seconds) noexcept
	{
		const bool dialogue = std::any_of(m_Sounds.begin(), m_Sounds.end(), [](const Sound& sound)
		{
			return sound.Mode != State::Stopping && sound.Params.Bus == AudioBus::Dialogue && sound.Audible > AudibleFloor;
		});
		const float target = dialogue ? DuckGain : 1.0f;
		const double time = target < m_Duck ? DuckAttack : DuckRelease;
		m_Duck += (target - m_Duck) * static_cast<float>(1.0 - std::exp(-seconds / time));

		for (std::size_t index = 0; index < AudioBusCount; ++index)
		{
			// Voices already carry nothing of their bus; the mixer applies each bus once, and Master last.
			float gain = m_BusVolume[index];
			if (index == static_cast<std::size_t>(AudioBus::Music) || index == static_cast<std::size_t>(AudioBus::Ambience))
				gain *= m_Duck;
			if (std::abs(gain - m_SentBusGain[index]) <= 1.0e-5f)
				continue;
			AudioMixer::Command command;
			command.Type = AudioMixer::CommandType::BusGain;
			command.Bus = static_cast<std::uint8_t>(index);
			command.GainLeft = gain;
			if (Send(command))
				m_SentBusGain[index] = gain;
		}
	}

	std::optional<std::uint32_t> AudioVoices::FreeSlot() const noexcept
	{
		for (std::uint32_t slot = 0; slot < m_Slots.size(); ++slot)
			if (m_Slots[slot] == 0)
				return slot;
		return std::nullopt;
	}

	std::size_t AudioVoices::CountReal() const noexcept
	{
		return static_cast<std::size_t>(std::count_if(m_Sounds.begin(), m_Sounds.end(),
			[](const Sound& sound) { return sound.Mode == State::Real; }));
	}
}
