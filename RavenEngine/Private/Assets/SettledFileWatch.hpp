#pragma once

#include "Core/PathText.hpp"
#include "Platform/DirectoryWatch.hpp"
#include "Raven/Core/Log.hpp"

#include <chrono>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace Raven
{
	// A worker owns the snapshots and decoder. The consumer takes only the latest decoded result.
	template<typename Data>
	class SettledFileWatch
	{
	public:
		using ReadFile = std::vector<std::byte>(*)(const std::filesystem::path&);
		using DecodeFile = Data(*)(std::span<const std::byte>, const std::filesystem::path&);

		SettledFileWatch(std::filesystem::path path, std::string_view format, ReadFile read,
			DecodeFile decode, std::optional<std::vector<std::byte>> loadedBytes = std::nullopt, bool retryFailures = false)
			: m_Path(std::filesystem::absolute(path)),
			  m_Worker([this, format = std::string(format), read, decode,
				  loadedBytes = std::move(loadedBytes), retryFailures](std::stop_token stop) mutable
			  {
				  Run(stop, format, read, decode, std::move(loadedBytes), retryFailures);
			  })
		{
		}

		const std::filesystem::path& GetPath() const noexcept { return m_Path; }

		std::optional<Data> Take()
		{
			const std::scoped_lock lock(m_Mutex);
			return std::exchange(m_Ready, std::nullopt);
		}

	private:
		struct Snapshot
		{
			std::vector<std::byte> Bytes;
			std::string Error;
			bool operator==(const Snapshot&) const = default;
		};

		Snapshot ReadSnapshot(ReadFile read) const
		{
			try
			{
				return { read(m_Path), {} };
			}
			catch (const std::exception& error)
			{
				return { {}, error.what() };
			}
		}

		void Run(std::stop_token stop, const std::string& format, ReadFile read, DecodeFile decode,
			std::optional<std::vector<std::byte>> loadedBytes, bool retryFailures)
		{
			using Clock = std::chrono::steady_clock;
			constexpr auto pollInterval = std::chrono::milliseconds(40);
			constexpr auto settleTime = std::chrono::milliseconds(100);
			constexpr auto auditInterval = std::chrono::milliseconds(2000);
			const Platform::WatchDirectory directory{ m_Path.parent_path() };
			Platform::DirectoryWatch watch(std::span(&directory, 1));
			std::optional<Snapshot> observed;
			auto stableSince = Clock::now();
			bool handled = false;
			std::string reportedError;
			if (loadedBytes)
			{
				observed = Snapshot{ std::move(*loadedBytes), {} };
				handled = true;
			}

			while (!stop.stop_requested())
			{
				auto current = ReadSnapshot(read);
				const auto now = Clock::now();
				if (!observed || current != *observed)
				{
					observed = std::move(current);
					stableSince = now;
					handled = false;
					reportedError.clear();
					const std::scoped_lock lock(m_Mutex);
					m_Ready.reset();
				}
				else if (!handled && now - stableSince >= settleTime)
				{
					handled = true;
					std::optional<Data> decoded;
					std::string error = observed->Error;
					if (error.empty())
					{
						try
						{
							decoded = decode(observed->Bytes, m_Path);
						}
						catch (const std::exception& exception)
						{
							error = exception.what();
						}
					}

					if (stop.stop_requested())
						return;
					// A save can supersede the candidate while the decoder is working.
					if (ReadSnapshot(read) != *observed)
						observed.reset();
					else if (decoded)
					{
						const std::scoped_lock lock(m_Mutex);
						m_Ready = std::move(decoded);
					}
					else
					{
						if (error != reportedError)
							Log(LogLevel::Warning, "Cannot reload " + format + " " + PathToUtf8(m_Path) + ": " + error);
						reportedError = error;
						if (retryFailures)
						{
							handled = false;
							stableSince = Clock::now();
						}
					}
				}
				watch.Wait(stop, observed && handled && watch.IsAvailable() ? auditInterval : pollInterval);
			}
		}

		const std::filesystem::path m_Path;
		std::mutex m_Mutex;
		std::optional<Data> m_Ready;
		std::jthread m_Worker;
	};
}
