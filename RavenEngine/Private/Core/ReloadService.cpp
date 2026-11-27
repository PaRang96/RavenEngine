#include "Core/ReloadService.hpp"

#include "Core/PathText.hpp"
#include "Core/ReloadTrace.hpp"
#include "Platform/PlatformFiles.hpp"
#include "Raven/Core/Files.hpp"
#include "Raven/Core/Log.hpp"

#include <algorithm>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <format>
#include <map>
#include <mutex>
#include <span>
#include <stdexcept>
#include <thread>
#include <utility>

namespace Raven
{
	namespace
	{
		using Clock = std::chrono::steady_clock;
		constexpr auto PollInterval = std::chrono::milliseconds(40);
		constexpr auto AuditInterval = std::chrono::milliseconds(2000);
		constexpr std::size_t WorkerCount = 2;
		constexpr std::size_t DefaultMaxBytes = 64 * 1024 * 1024;

		std::uint64_t Hash(std::span<const std::byte> bytes) noexcept
		{
			std::uint64_t hash = 14695981039346656037ull;
			for (const std::byte byte : bytes)
			{
				hash ^= static_cast<std::uint8_t>(byte);
				hash *= 1099511628211ull;
			}
			return hash;
		}

		std::vector<std::byte> ReadFile(const ReloadInput& input)
		{
			return input.Read ? input.Read(input.Path) : Files::ReadBinary(input.Path, DefaultMaxBytes);
		}

		// A directory input's content: each matching file's relative path and content digest.
		std::vector<std::byte> ReadDirectory(const ReloadInput& input)
		{
			std::vector<std::filesystem::path> files;
			const auto collect = [&](const auto& entries)
			{
				for (const auto& entry : entries)
					if (entry.is_regular_file() && input.Match(entry.path()))
						files.push_back(entry.path());
			};
			if (input.Subtree)
				collect(std::filesystem::recursive_directory_iterator(input.Path));
			else
				collect(std::filesystem::directory_iterator(input.Path));
			std::sort(files.begin(), files.end());
			std::vector<std::byte> content;
			for (const auto& file : files)
			{
				const auto name = file.lexically_relative(input.Path).generic_string();
				const auto* text = reinterpret_cast<const std::byte*>(name.data());
				content.insert(content.end(), text, text + name.size());
				content.push_back(std::byte{ 0 });
				const auto digest = Hash(ReadFile({ file }));
				const auto* bytes = reinterpret_cast<const std::byte*>(&digest);
				content.insert(content.end(), bytes, bytes + sizeof(digest));
			}
			return content;
		}

		// Inputs shared by several consumers (an include used by many shaders) are read once per pass.
		using ReadCache = std::map<std::pair<std::filesystem::path, ReloadInput::ReadFile>, std::vector<std::byte>>;

		ReloadSnapshot ReadSnapshot(const std::vector<ReloadInput>& inputs, ReadCache* cache)
		{
			ReloadSnapshot snapshot;
			try
			{
				for (const auto& input : inputs)
				{
					if (input.Match)
					{
						snapshot.Contents.push_back(ReadDirectory(input));
						continue;
					}
					if (!cache)
					{
						snapshot.Contents.push_back(ReadFile(input));
						continue;
					}
					const auto key = std::make_pair(input.Path, input.Read);
					auto found = cache->find(key);
					if (found == cache->end())
						found = cache->emplace(key, ReadFile(input)).first;
					snapshot.Contents.push_back(found->second);
				}
			}
			catch (const std::exception& error)
			{
				return { {}, error.what() };
			}
			return snapshot;
		}

		std::vector<std::uint64_t> HashContents(const ReloadSnapshot& snapshot)
		{
			std::vector<std::uint64_t> hashes;
			for (const auto& content : snapshot.Contents)
				hashes.push_back(Hash(content));
			return hashes;
		}

		// Size and write time of every file an input covers. A pass reads contents only when these
		// change, or during the periodic audit, which still catches edits that keep their timestamp.
		struct FileMeta
		{
			std::filesystem::path Path;
			std::uintmax_t Size = 0;
			std::filesystem::file_time_type Time{};
			bool operator==(const FileMeta&) const = default;
		};
		using InputMeta = std::vector<FileMeta>;

		InputMeta ReadMeta(const std::vector<ReloadInput>& inputs)
		{
			InputMeta meta;
			std::error_code error;
			const auto add = [&](const std::filesystem::path& path)
			{
				const auto size = std::filesystem::file_size(path, error);
				const auto time = std::filesystem::last_write_time(path, error);
				// A missing or unreadable file still records an entry, so its return is noticed.
				meta.push_back({ path, error ? ~std::uintmax_t{} : size, error ? std::filesystem::file_time_type{} : time });
			};
			for (const auto& input : inputs)
			{
				if (!input.Match)
				{
					add(input.Path);
					continue;
				}
				const auto collect = [&](auto entries)
				{
					for (const auto& entry : entries)
						if (entry.is_regular_file(error) && input.Match(entry.path()))
							add(entry.path());
				};
				try
				{
					if (input.Subtree)
						collect(std::filesystem::recursive_directory_iterator(input.Path, error));
					else
						collect(std::filesystem::directory_iterator(input.Path, error));
					meta.push_back({ input.Path, 0, {} });
				}
				catch (const std::filesystem::filesystem_error&)
				{
					// Unreadable now: the content read reports it.
					meta.push_back({ input.Path, ~std::uintmax_t{}, {} });
				}
			}
			return meta;
		}

		std::vector<std::filesystem::path> InputPaths(const std::vector<ReloadInput>& inputs)
		{
			std::vector<std::filesystem::path> paths;
			for (const auto& input : inputs)
				paths.push_back(input.Path);
			return paths;
		}
	}

	struct ReloadSubscription::State
	{
		ReloadConsumer* Consumer = nullptr;
		ReloadOptions Options;
		mutable std::mutex Mutex;
		std::condition_variable Idle;
		std::vector<ReloadInput> Inputs;
		std::optional<ReloadSnapshot> Observed;
		// File sizes and write times when Observed was read.
		InputMeta ObservedMeta;
		Clock::time_point StableSince{};
		// The first observation of an unsettled change, for reload timing.
		Clock::time_point ChangedAt{};
		bool Handled = false;
		bool Importing = false;
		bool Removed = false;
		bool Paused = false;
		// SetInputs: the next read becomes the baseline instead of a change.
		bool Rebase = false;
		std::string ReportedError;
		ReloadRecord Record;
	};

	namespace
	{
		using State = ReloadSubscription::State;

		class Service
		{
		public:
			Service()
			{
				m_Watcher = std::jthread([this](std::stop_token stop) { Watch(stop); });
				for (std::size_t index = 0; index < WorkerCount; ++index)
					m_Workers.emplace_back([this](std::stop_token stop) { Work(stop); });
			}

			void Add(std::shared_ptr<State> state)
			{
				const std::scoped_lock lock(m_Mutex);
				m_States.push_back(std::move(state));
				++m_Revision;
				m_Wake.request_stop();
			}

			// Removes queued work for the state; returns whether a queued import was dropped.
			bool Remove(const std::shared_ptr<State>& state)
			{
				const std::scoped_lock lock(m_Mutex);
				std::erase(m_States, state);
				const auto dropped = std::erase_if(m_Jobs, [&](const Job& job) { return job.Target == state; });
				++m_Revision;
				m_Wake.request_stop();
				return dropped != 0;
			}

			// The watched directories may differ: rebuild the directory watch.
			void Changed()
			{
				const std::scoped_lock lock(m_Mutex);
				++m_Revision;
				m_Wake.request_stop();
			}

			std::vector<ReloadRecord> Records() const
			{
				std::vector<std::shared_ptr<State>> states;
				{
					const std::scoped_lock lock(m_Mutex);
					states = m_States;
				}
				std::vector<ReloadRecord> records;
				for (const auto& state : states)
				{
					const std::scoped_lock lock(state->Mutex);
					records.push_back(state->Record);
				}
				return records;
			}

		private:
			struct Job
			{
				std::shared_ptr<State> Target;
				ReloadSnapshot Snapshot;
				std::vector<ReloadInput> Inputs;
				Clock::time_point ChangedAt;
			};

			static std::vector<Platform::WatchDirectory> Directories(const std::vector<std::shared_ptr<State>>& states)
			{
				std::map<std::filesystem::path, bool> unique;
				for (const auto& state : states)
				{
					const std::scoped_lock lock(state->Mutex);
					if (state->Paused)
						continue;
					for (const auto& input : state->Inputs)
					{
						const auto directory = input.Match ? input.Path : input.Path.parent_path();
						unique[directory.lexically_normal()] |= input.Match && input.Subtree;
					}
				}
				std::vector<Platform::WatchDirectory> directories;
				for (const auto& [path, subtree] : unique)
					directories.push_back({ path, subtree });
				return directories;
			}

			void Watch(std::stop_token stop)
			{
				std::optional<Platform::DirectoryWatch> watch;
				std::uint64_t watched = ~0ull;
				auto nextAudit = Clock::now();
				while (!stop.stop_requested())
				{
					std::stop_source wake;
					std::vector<std::shared_ptr<State>> states;
					std::uint64_t revision = 0;
					{
						const std::scoped_lock lock(m_Mutex);
						m_Wake = wake;
						states = m_States;
						revision = m_Revision;
					}
					if (revision != watched)
					{
						const auto directories = Directories(states);
						watch.reset();
						watch.emplace(directories);
						watched = revision;
					}

					bool busy = false;
					ReadCache cache;
					const auto now = Clock::now();
					const bool audit = now >= nextAudit;
					if (audit)
						nextAudit = now + AuditInterval;
					std::vector<Job> ready;
					for (const auto& state : states)
					{
						std::vector<ReloadInput> inputs;
						{
							const std::scoped_lock lock(state->Mutex);
							if (state->Removed || state->Importing || state->Paused)
							{
								busy |= state->Importing;
								continue;
							}
							inputs = state->Inputs;
						}
						// Unchanged sizes and write times skip the read, except during the audit.
						auto meta = ReadMeta(inputs);
						{
							const std::scoped_lock lock(state->Mutex);
							if (!audit && state->Observed && state->Handled && !state->Rebase && meta == state->ObservedMeta)
								continue;
						}
						auto current = ReadSnapshot(inputs, &cache);
						const std::scoped_lock lock(state->Mutex);
						if (state->Removed || state->Importing || state->Paused)
							continue;
						state->ObservedMeta = std::move(meta);
						if (state->Rebase)
						{
							state->Rebase = false;
							state->Observed = std::move(current);
							state->Handled = true;
							state->Record.InputHashes = HashContents(*state->Observed);
							state->Record.Inputs = InputPaths(inputs);
						}
						else if (!state->Observed || current != *state->Observed)
						{
							if (state->Handled || !state->Observed)
								state->ChangedAt = now;
							state->Observed = std::move(current);
							state->StableSince = now;
							state->Handled = false;
							state->ReportedError.clear();
							state->Consumer->Changed();
							busy = true;
						}
						else if (!state->Handled)
						{
							busy = true;
							if (now - state->StableSince >= state->Options.Settle)
							{
								state->Handled = true;
								state->Importing = true;
								ready.push_back({ state, *state->Observed, inputs, state->ChangedAt });
							}
						}
					}
					if (!ready.empty())
					{
						{
							const std::scoped_lock lock(m_Mutex);
							for (auto& job : ready)
								m_Jobs.push_back(std::move(job));
						}
						m_JobReady.notify_all();
					}

					const std::stop_callback forward(stop, [&wake] { wake.request_stop(); });
					watch->Wait(wake.get_token(), busy || !watch->IsAvailable() ? PollInterval : AuditInterval);
				}
			}

			void Work(std::stop_token stop)
			{
				while (true)
				{
					Job job;
					{
						std::unique_lock lock(m_Mutex);
						if (!m_JobReady.wait(lock, stop, [&] { return !m_Jobs.empty(); }))
							return;
						job = std::move(m_Jobs.front());
						m_Jobs.pop_front();
					}
					Run(job, stop);
				}
			}

			void Run(Job& job, std::stop_token stop)
			{
				auto& state = *job.Target;
				{
					const std::scoped_lock lock(state.Mutex);
					if (state.Removed)
					{
						state.Importing = false;
						state.Idle.notify_all();
						return;
					}
				}
				// The consumer stays valid: its subscription waits for Importing to clear.
				const auto started = Clock::now();
				std::string error = job.Snapshot.Error;
				std::string output;
				bool imported = false;
				if (error.empty())
				{
					try
					{
						output = state.Consumer->Import(job.Snapshot, stop);
						imported = true;
					}
					catch (const std::exception& exception)
					{
						error = exception.what();
					}
				}
				const auto finished = Clock::now();
				// A save can supersede the snapshot while the import runs.
				const bool current = ReadSnapshot(job.Inputs, nullptr) == job.Snapshot;
				// An import that replaced the inputs (a shader's includes) takes their contents now,
				// right after confirming the import is current, so a later save is never mistaken
				// for one already imported.
				std::optional<ReloadSnapshot> rebased;
				if (current)
				{
					std::vector<ReloadInput> inputs;
					{
						const std::scoped_lock lock(state.Mutex);
						if (state.Rebase)
							inputs = state.Inputs;
					}
					if (!inputs.empty())
						rebased = ReadSnapshot(inputs, nullptr);
				}

				const std::scoped_lock lock(state.Mutex);
				auto& record = state.Record;
				if (stop.stop_requested() || state.Removed)
				{
					if (imported)
						state.Consumer->Discard();
				}
				else if (!current)
				{
					if (imported)
						state.Consumer->Discard();
					// The next pass reads the replaced inputs and imports them as a change.
					state.Rebase = false;
					state.Observed.reset();
					++record.Superseded;
					if (imported)
						Log(LogLevel::Info, "Reload superseded: " + state.Options.Name +
							"; a newer save arrived during the import");
				}
				else if (imported)
				{
					// The trigger: inputs whose content differs from the previous import.
					const auto hashes = HashContents(job.Snapshot);
					record.Trigger.clear();
					// A baseline (contents the consumer loaded itself) counts as the previous import.
					if (!record.InputHashes.empty())
						for (std::size_t index = 0; index < job.Inputs.size() && index < hashes.size(); ++index)
						{
							const auto previous = std::find(record.Inputs.begin(), record.Inputs.end(), job.Inputs[index].Path);
							const auto slot = static_cast<std::size_t>(previous - record.Inputs.begin());
							if (previous == record.Inputs.end() || slot >= record.InputHashes.size() ||
								record.InputHashes[slot] != hashes[index])
								record.Trigger.push_back(job.Inputs[index].Path);
						}
					++record.Imports;
					record.LastImport = finished - started;
					record.ImportedAt = std::chrono::system_clock::now();
					record.Output = std::move(output);
					record.InputHashes = hashes;
					record.Inputs = InputPaths(job.Inputs);
					if (rebased && state.Rebase)
					{
						state.Rebase = false;
						state.Observed = std::move(*rebased);
						record.InputHashes = HashContents(*state.Observed);
						record.Inputs = InputPaths(state.Inputs);
					}
					record.LastError.clear();
					state.ReportedError.clear();
					state.Consumer->Accept(record);
					if (ReloadTrace::Enabled())
					{
						Log(LogLevel::Info, "Reload trace " + state.Options.Name + ": " +
							ReloadTrace::Span("settle", job.ChangedAt, started) + ", " +
							ReloadTrace::Span("import", started, finished) + " ms");
					}
				}
				else
				{
					++record.Failures;
					record.LastError = error;
					if (error != state.ReportedError)
						Log(LogLevel::Warning, "Cannot reload " + state.Options.Name + ": " + error);
					state.ReportedError = error;
					// A failed import can still have replaced its inputs (a shader's includes).
					if (rebased && state.Rebase)
					{
						state.Rebase = false;
						state.Observed = std::move(*rebased);
					}
					if (state.Options.RetryFailures)
					{
						state.Handled = false;
						state.StableSince = Clock::now();
					}
				}
				state.Importing = false;
				state.Idle.notify_all();
			}

			mutable std::mutex m_Mutex;
			std::vector<std::shared_ptr<State>> m_States;
			std::uint64_t m_Revision = 0;
			std::stop_source m_Wake;
			std::deque<Job> m_Jobs;
			std::condition_variable_any m_JobReady;
			// Declared last, so the threads stop before the state they use is destroyed.
			std::jthread m_Watcher;
			std::vector<std::jthread> m_Workers;
		};

		std::mutex ServiceMutex;
		std::weak_ptr<Service> SharedService;

		std::shared_ptr<Service> AcquireService()
		{
			const std::scoped_lock lock(ServiceMutex);
			auto service = SharedService.lock();
			if (!service)
			{
				service = std::make_shared<Service>();
				SharedService = service;
			}
			return service;
		}
	}

	ReloadSubscription::ReloadSubscription(std::shared_ptr<void> service, std::shared_ptr<State> state)
		: m_Service(std::move(service)), m_State(std::move(state))
	{
	}

	ReloadSubscription::~ReloadSubscription()
	{
		auto& service = *std::static_pointer_cast<Service>(m_Service);
		const bool dropped = service.Remove(m_State);
		std::unique_lock lock(m_State->Mutex);
		m_State->Removed = true;
		if (dropped)
			m_State->Importing = false;
		m_State->Idle.wait(lock, [&] { return !m_State->Importing; });
	}

	void ReloadSubscription::SetInputs(std::vector<ReloadInput> inputs)
	{
		{
			// The record keeps the inputs of its hashes until the next import or rebase.
			const std::scoped_lock lock(m_State->Mutex);
			m_State->Inputs = std::move(inputs);
			m_State->Rebase = true;
		}
		std::static_pointer_cast<Service>(m_Service)->Changed();
	}

	void ReloadSubscription::SetPaused(bool paused)
	{
		{
			const std::scoped_lock lock(m_State->Mutex);
			m_State->Paused = paused;
		}
		// The directory set changes; the first pass after the rebuild compares metadata, so a
		// change made while the watch was rebuilt is still seen.
		std::static_pointer_cast<Service>(m_Service)->Changed();
	}

	bool ReloadSubscription::IsSettled() const
	{
		const std::scoped_lock lock(m_State->Mutex);
		return m_State->Observed && m_State->Handled && !m_State->Importing;
	}

	std::string ReloadSubscription::Published(std::chrono::duration<double, std::milli> publish)
	{
		const std::scoped_lock lock(m_State->Mutex);
		auto& record = m_State->Record;
		record.LastPublish = publish;
		std::string trigger;
		for (const auto& path : record.Trigger)
			trigger += (trigger.empty() ? "" : ", ") + path.filename().string();
		return std::format("trigger {}; import {:.1f} ms, publish {:.1f} ms", trigger.empty() ? "-" : trigger,
			record.LastImport.count(), publish.count());
	}

	ReloadRecord ReloadSubscription::GetRecord() const
	{
		const std::scoped_lock lock(m_State->Mutex);
		return m_State->Record;
	}

	std::unique_ptr<ReloadSubscription> ReloadService::Subscribe(std::vector<ReloadInput> inputs,
		ReloadConsumer& consumer, ReloadOptions options)
	{
		if (inputs.empty())
			throw std::invalid_argument("A reload subscription needs at least one input");
		auto state = std::make_shared<State>();
		state->Consumer = &consumer;
		state->Record.Name = options.Name;
		state->Record.ImporterVersion = options.ImporterVersion;
		state->Record.Inputs = InputPaths(inputs);
		if (options.Baseline)
		{
			state->Observed = std::move(options.Baseline);
			state->Handled = true;
			// A large file's hash is costly; a consumer may bring it from the worker that decoded it.
			state->Record.InputHashes = options.BaselineHashes.size() == state->Observed->Contents.size()
				? std::move(options.BaselineHashes) : HashContents(*state->Observed);
			options.Baseline.reset();
			options.BaselineHashes.clear();
		}
		state->Inputs = std::move(inputs);
		state->Paused = options.StartPaused;
		state->Options = std::move(options);
		auto service = AcquireService();
		service->Add(state);
		return std::unique_ptr<ReloadSubscription>(new ReloadSubscription(std::move(service), std::move(state)));
	}

	std::uint64_t ReloadService::HashContent(std::span<const std::byte> bytes) noexcept
	{
		return Hash(bytes);
	}

	std::vector<ReloadRecord> ReloadService::GetRecords()
	{
		std::shared_ptr<Service> service;
		{
			const std::scoped_lock lock(ServiceMutex);
			service = SharedService.lock();
		}
		return service ? service->Records() : std::vector<ReloadRecord>{};
	}

	std::size_t ReloadService::GetThreadCount()
	{
		const std::scoped_lock lock(ServiceMutex);
		return SharedService.expired() ? 0 : 1 + WorkerCount;
	}
}
