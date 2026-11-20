#include "Raven/Audio/AudioPlayer.hpp"

#include "Core/PathText.hpp"
#include "Raven/Core/Files.hpp"
#include "Audio/AudioClip.hpp"
#include "Assets/ManagedAsset.hpp"
#include "Raven/Assets/AssetStore.hpp"
#include "Platform/AudioBackend.hpp"
#include "Raven/Assets/AssetCatalog.hpp"
#include "Raven/Core/Log.hpp"
#include "Raven/Core/Paths.hpp"

#include <algorithm>
#include <fstream>
#include <map>
#include <set>
#include <span>
#include <stdexcept>
#include <string>

namespace Raven
{

	struct AudioPlayer::Impl
	{
		explicit Impl(const AssetCatalog& catalog)
			: Catalog(catalog)
		{
		}

		const AssetCatalog& Catalog;
		std::map<AssetId, PcmClip> Clips;
		std::set<AssetId> ReportedFailures;
		std::shared_ptr<const PcmClip> CurrentClip;
		// The backend releases the voice before the PCM cache is destroyed.
		std::unique_ptr<AudioBackend> Backend;
	};

	AudioPlayer::AudioPlayer(const AssetCatalog& catalog)
		: m_Impl(std::make_unique<Impl>(catalog))
	{
	}

	AudioPlayer::~AudioPlayer() = default;

	void AudioPlayer::Play(AssetRef<AudioAsset> asset)
	{
		try
		{
			auto clip = m_Impl->Clips.find(asset.Id);
			if (clip == m_Impl->Clips.end())
			{
				const auto& record = m_Impl->Catalog.Get(asset.Id);
				if (record.Kind != AssetKind::Audio)
					throw std::invalid_argument("asset does not refer to audio");
				clip = m_Impl->Clips.emplace(asset.Id, DecodeWav(Files::ReadBinary(Paths::GetContentPath(record.SourcePath), 16 * 1024 * 1024), Paths::GetContentPath(record.SourcePath))).first;
			}
			if (!m_Impl->Backend)
				m_Impl->Backend = CreateAudioBackend();
			m_Impl->Backend->Play(clip->second);
			m_Impl->ReportedFailures.erase(asset.Id);
		}
		catch (const std::exception& error)
		{
			if (m_Impl->ReportedFailures.insert(asset.Id).second)
				Log(LogLevel::Warning, "Cannot play audio '" + asset.Id.ToString() + "': " + error.what());
		}
	}

	void AudioPlayer::Play(const AssetLease& asset)
	{
		try
		{
			if (!asset.m_Asset || !asset.m_Asset->Clip)
				throw std::invalid_argument("asset does not refer to a loaded audio clip");
			if (!m_Impl->Backend)
				m_Impl->Backend = CreateAudioBackend();
			m_Impl->Backend->Stop();
			m_Impl->CurrentClip = asset.m_Asset->Clip;
			m_Impl->Backend->Play(*m_Impl->CurrentClip);
		}
		catch (const std::exception& error)
		{
			Log(LogLevel::Warning, "Cannot play scene audio: " + std::string(error.what()));
		}
	}

	void AudioPlayer::Stop() noexcept
	{
		if (m_Impl->Backend)
			m_Impl->Backend->Stop();
		m_Impl->CurrentClip.reset();
	}
}
