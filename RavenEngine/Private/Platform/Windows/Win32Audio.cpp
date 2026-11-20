#include "Platform/AudioBackend.hpp"

#include <Windows.h>
#include <objbase.h>
#include <xaudio2.h>

#include <format>
#include <stdexcept>

namespace Raven
{
	namespace
	{
		void CheckAudio(HRESULT result, const char* operation)
		{
			if (FAILED(result))
				throw std::runtime_error(std::format("{} failed (0x{:08X})", operation,
					static_cast<unsigned int>(result)));
		}

		class ComApartment
		{
		public:
			ComApartment()
			{
				const HRESULT result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
				if (result != RPC_E_CHANGED_MODE)
					CheckAudio(result, "CoInitializeEx");
				m_Initialized = SUCCEEDED(result);
			}

			~ComApartment()
			{
				if (m_Initialized)
					CoUninitialize();
			}

			ComApartment(const ComApartment&) = delete;
			ComApartment& operator=(const ComApartment&) = delete;

		private:
			bool m_Initialized = false;
		};

		struct ReleaseEngine
		{
			void operator()(IXAudio2* engine) const { engine->Release(); }
		};

		struct DestroyVoice
		{
			void operator()(IXAudio2Voice* voice) const { voice->DestroyVoice(); }
		};

		class Win32Audio final : public AudioBackend
		{
		public:
			Win32Audio()
			{
				IXAudio2* engine = nullptr;
				CheckAudio(XAudio2Create(&engine), "XAudio2Create");
				m_Engine.reset(engine);
				IXAudio2MasteringVoice* output = nullptr;
				CheckAudio(m_Engine->CreateMasteringVoice(&output), "CreateMasteringVoice");
				m_Output.reset(output);
			}

			void Play(const PcmClip& clip) override
			{
				Stop();
				WAVEFORMATEX format{};
				format.wFormatTag = WAVE_FORMAT_PCM;
				format.nChannels = clip.Channels;
				format.nSamplesPerSec = clip.SampleRate;
				format.wBitsPerSample = 16;
				format.nBlockAlign = clip.Channels * 2;
				format.nAvgBytesPerSec = clip.SampleRate * format.nBlockAlign;

				IXAudio2SourceVoice* source = nullptr;
				CheckAudio(m_Engine->CreateSourceVoice(&source, &format), "CreateSourceVoice");
				std::unique_ptr<IXAudio2SourceVoice, DestroyVoice> voice(source);

				XAUDIO2_BUFFER buffer{};
				buffer.Flags = XAUDIO2_END_OF_STREAM;
				buffer.AudioBytes = static_cast<UINT32>(clip.Samples.size());
				buffer.pAudioData = clip.Samples.data();
				CheckAudio(voice->SubmitSourceBuffer(&buffer), "SubmitSourceBuffer");
				CheckAudio(voice->Start(), "Start audio voice");
				m_Voice = std::move(voice);
			}

			void Stop() noexcept override
			{
				m_Voice.reset();
			}

		private:
			ComApartment m_Com;
			std::unique_ptr<IXAudio2, ReleaseEngine> m_Engine;
			std::unique_ptr<IXAudio2MasteringVoice, DestroyVoice> m_Output;
			std::unique_ptr<IXAudio2SourceVoice, DestroyVoice> m_Voice;
		};
	}

	std::unique_ptr<AudioBackend> CreateAudioBackend()
	{
		return std::make_unique<Win32Audio>();
	}
}
