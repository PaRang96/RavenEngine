#pragma once

#include "Core/ImporterVersions.hpp"
#include "Core/PathText.hpp"
#include "Core/ReloadService.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Raven
{
	// One watched file on the shared reload service (docs/HotReloadDesign.md, H1). A reload
	// worker decodes settled bytes; the consumer takes only the latest decoded result.
	template<typename Data>
	class SettledFileWatch final : private ReloadConsumer
	{
	public:
		using ReadFile = ReloadInput::ReadFile;
		using DecodeFile = Data(*)(std::span<const std::byte>, const std::filesystem::path&);

		// importerVersion comes from ImporterVersions.hpp and keys the dependency record (Q4 A+).
		SettledFileWatch(std::filesystem::path path, std::string_view format, std::uint32_t importerVersion, ReadFile read,
			DecodeFile decode, std::optional<std::vector<std::byte>> loadedBytes = std::nullopt, bool retryFailures = false)
			: m_Path(std::filesystem::absolute(path)), m_Format(format), m_Decode(decode)
		{
			ReloadOptions options;
			options.Name = std::string(format) + " " + PathToUtf8(m_Path);
			options.ImporterVersion = importerVersion;
			options.RetryFailures = retryFailures;
			if (loadedBytes)
				options.Baseline = ReloadSnapshot{ { std::move(*loadedBytes) }, {} };
			m_Subscription = ReloadService::Subscribe({ ReloadInput{ m_Path, read } }, *this, std::move(options));
		}

		~SettledFileWatch()
		{
			// No reload worker may call into this object once destruction begins.
			m_Subscription.reset();
		}

		SettledFileWatch(const SettledFileWatch&) = delete;
		SettledFileWatch& operator=(const SettledFileWatch&) = delete;

		const std::filesystem::path& GetPath() const noexcept { return m_Path; }
		// The dependency record, for reload diagnostics (docs/HotReloadDesign.md, H7).
		ReloadRecord GetRecord() const { return m_Subscription->GetRecord(); }
		// After publishing a taken result: records the publish time and returns the H7 summary.
		std::string Published(std::chrono::duration<double, std::milli> publish) { return m_Subscription->Published(publish); }

		std::optional<Data> Take()
		{
			const std::scoped_lock lock(m_Mutex);
			return std::exchange(m_Ready, std::nullopt);
		}

	private:
		std::string Import(const ReloadSnapshot& snapshot, std::stop_token) override
		{
			m_Pending = m_Decode(snapshot.Contents.front(), m_Path);
			return m_Format + " decoded from " + std::to_string(snapshot.Contents.front().size()) + " bytes";
		}

		void Accept(const ReloadRecord&) override
		{
			const std::scoped_lock lock(m_Mutex);
			m_Ready = std::exchange(m_Pending, std::nullopt);
		}

		void Discard() noexcept override
		{
			m_Pending.reset();
		}

		void Changed() noexcept override
		{
			const std::scoped_lock lock(m_Mutex);
			m_Ready.reset();
		}

		const std::filesystem::path m_Path;
		const std::string m_Format;
		const DecodeFile m_Decode;
		std::mutex m_Mutex;
		std::optional<Data> m_Ready;
		// Written only by the reload worker between Import and Accept or Discard.
		std::optional<Data> m_Pending;
		std::unique_ptr<ReloadSubscription> m_Subscription;
	};
}
