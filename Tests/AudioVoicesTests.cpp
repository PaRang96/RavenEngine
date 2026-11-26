// Guards the clip lifetime between the voice manager and the audio thread (docs/GameServicesDesign.md,
// C8): a voice slot owns its clip until the mixer releases the slot, so a looping sound can be
// erased while its fade still plays, and a Stop the full command queue drops is retried until the
// voice ends. Audio streaming (C8, next) rewrites this path. The mixer runs on the test thread, with
// no device.

#include "Audio/AudioMixer.hpp"
#include "Audio/AudioVoices.hpp"
#include "Support/Check.hpp"

#include <memory>
#include <vector>

namespace RavenTests
{
	using namespace Raven;

	std::shared_ptr<PcmClip> MakeLoopClip()
	{
		auto clip = std::make_shared<PcmClip>();
		clip->Frames = MixerSampleRate;
		clip->Samples.assign(std::size_t{ clip->Frames } * MixerChannels, 0.25f);
		return clip;
	}

	// Past the 10 ms stop fade plus one block.
	void RenderPastFade(AudioMixer& mixer)
	{
		std::vector<float> out(std::size_t{ AudioMixer::BlockFrames } * MixerChannels);
		for (int block = 0; block < 4; ++block)
			mixer.Render(out.data(), AudioMixer::BlockFrames);
	}

	SoundParams LoopParams()
	{
		SoundParams params;
		params.Loop = true;
		return params;
	}

	// A loop virtualized and then stopped is erased at once, while its voice still fades.
	void ErasedLoopKeepsItsClip()
	{
		AudioMixer mixer;
		AudioVoices voices(mixer);
		auto clip = MakeLoopClip();
		const std::weak_ptr<const PcmClip> watched = clip;
		const auto sound = voices.Play(std::move(clip), LoopParams());
		RAVEN_CHECK(sound.IsValid());
		RenderPastFade(mixer);
		voices.Update(1.0 / 60.0);
		RAVEN_CHECK(voices.GetCounts().Real == 1);

		// Silent, so it loses its voice and becomes virtual; the voice fades out.
		voices.SetVolume(sound, 0.0f);
		voices.Update(1.0 / 60.0);
		RAVEN_CHECK(voices.GetCounts().Virtual == 1);
		voices.StopAll(0.0f);
		RAVEN_CHECK(voices.GetCounts().Virtual == 0 && voices.GetCounts().Real == 0);
		RAVEN_CHECK(!watched.expired());

		RenderPastFade(mixer);
		voices.Update(1.0 / 60.0);
		RAVEN_CHECK(watched.expired());
	}

	// A Stop dropped by the full command queue is sent once there is room, so the loop ends.
	void DroppedStopIsRetried()
	{
		AudioMixer mixer;
		AudioVoices voices(mixer);
		auto clip = MakeLoopClip();
		const std::weak_ptr<const PcmClip> watched = clip;
		RAVEN_CHECK(voices.Play(std::move(clip), LoopParams()).IsValid());
		RenderPastFade(mixer);
		voices.Update(1.0 / 60.0);
		RAVEN_CHECK(voices.GetCounts().Real == 1);

		AudioMixer::Command filler;
		filler.Type = AudioMixer::CommandType::BusGain;
		filler.GainLeft = 1.0f;
		while (mixer.Send(filler))
		{
		}
		voices.StopAll(0.0f);
		RAVEN_CHECK(voices.GetCounts().Stopping == 1);

		// The audio thread drains the queue; the retry goes out on the next Update.
		RenderPastFade(mixer);
		voices.Update(1.0 / 60.0);
		RenderPastFade(mixer);
		voices.Update(1.0 / 60.0);
		const auto counts = voices.GetCounts();
		RAVEN_CHECK(counts.Real == 0 && counts.Virtual == 0 && counts.Stopping == 0);
		RAVEN_CHECK(watched.expired());
	}
}

int main()
{
	return RavenTests::Run("AudioVoicesTests", []
	{
		RavenTests::ErasedLoopKeepsItsClip();
		RavenTests::DroppedStopIsRetried();
	});
}
