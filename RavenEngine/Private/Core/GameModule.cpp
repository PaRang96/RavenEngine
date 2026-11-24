#include "Raven/Core/GameModule.hpp"
#include "Raven/Core/ApplicationClient.hpp"
#include "Raven/Core/Log.hpp"
#include "Platform/GameModulePlatform.hpp"
#include "Platform/BuildProcess.hpp"
#include "Core/ImporterVersions.hpp"
#include "Core/ReloadService.hpp"
#include "Core/ReloadTrace.hpp"
#include "GameBuildConfig.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <fstream>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace Raven
{
	namespace
	{
		using Clock = std::chrono::steady_clock;
		std::atomic<std::uint64_t> NextCopy{ 0 };
		constexpr std::uint64_t HashOffset = 14695981039346656037ull;
		constexpr std::uint64_t HashPrime = 1099511628211ull;

		void AppendHashByte(std::uint64_t& hash, unsigned char byte)
		{
			hash ^= byte;
			hash *= HashPrime;
		}

		void HashFile(const std::filesystem::path& path, std::uint64_t& hash)
		{
			std::ifstream stream(path, std::ios::binary);
			if (!stream)
				throw std::runtime_error("Cannot read watched game file");
			char buffer[4096];
			while (stream.read(buffer, sizeof(buffer)) || stream.gcount() > 0)
			{
				const auto byteCount = stream.gcount();
				for (std::streamsize index = 0; index < byteCount; ++index)
					AppendHashByte(hash, static_cast<unsigned char>(buffer[index]));
			}
			if (!stream.eof())
				throw std::runtime_error("Cannot finish reading watched game file");
		}

		struct FileStamp
		{
			std::filesystem::file_time_type Time{};
			std::uintmax_t Size = 0;
			std::uint64_t Digest = 0;
			bool operator==(const FileStamp&) const = default;
		};

		FileStamp Stamp(const std::filesystem::path& path)
		{
			FileStamp stamp;
			stamp.Time = std::filesystem::last_write_time(path);
			stamp.Size = std::filesystem::file_size(path);
			stamp.Digest = HashOffset;
			HashFile(path, stamp.Digest);
			if (stamp.Time != std::filesystem::last_write_time(path) || stamp.Size != std::filesystem::file_size(path))
				throw std::runtime_error("Game DLL changed during observation; retry reload");
			return stamp;
		}

		bool IsSourceFile(const std::filesystem::path& path)
		{
			const auto extension = path.extension();
			return extension == ".hpp" || extension == ".cpp" || extension == ".cmake" || extension == ".in" ||
				path.filename() == "CMakeLists.txt";
		}

		// The same files SourceSignature hashes, as reload-service inputs.
		std::vector<ReloadInput> SourceInputs()
		{
			const std::filesystem::path root(GameBuild::Root);
			const auto named = [](std::initializer_list<const char*> names)
			{
				std::vector<std::string> list(names.begin(), names.end());
				return [list](const std::filesystem::path& path)
				{
					return std::find(list.begin(), list.end(), path.filename().string()) != list.end();
				};
			};
			std::vector<ReloadInput> inputs;
			inputs.push_back({ root, nullptr, named({ "CMakeLists.txt", "CMakePresets.json" }) });
			inputs.push_back({ root / "RavenEngine", nullptr, named({ "CMakeLists.txt" }) });
			for (const auto* name : { "RavenEngine/Public", "RavenEngine/Private/Game", "cmake" })
				inputs.push_back({ root / name, nullptr, IsSourceFile, true });
			return inputs;
		}

		// Wakes the game worker once the reload service settles a change; the worker rereads.
		class WakeConsumer final : public ReloadConsumer
		{
		public:
			explicit WakeConsumer(std::function<void()> wake) : m_Wake(std::move(wake)) {}
			std::string Import(const ReloadSnapshot&, std::stop_token) override { return "change notice"; }
			void Accept(const ReloadRecord&) override { m_Wake(); }
			void Discard() noexcept override {}

		private:
			std::function<void()> m_Wake;
		};

		// Content changes drive builds; timestamps alone are not a compatibility check.
		std::uint64_t SourceSignature()
		{
			const std::filesystem::path root(GameBuild::Root);
			std::vector<std::filesystem::path> files;
			for (const auto& directory : { root / "RavenEngine/Public", root / "RavenEngine/Private/Game", root / "cmake" })
			{
				for (const auto& entry : std::filesystem::recursive_directory_iterator(directory))
				{
					if (entry.is_regular_file() && IsSourceFile(entry.path()))
						files.push_back(entry.path());
				}
			}
			for (const auto* name : { "CMakeLists.txt", "CMakePresets.json", "RavenEngine/CMakeLists.txt" })
				files.push_back(root / name);
			std::sort(files.begin(), files.end());
			std::uint64_t hash = HashOffset;
			for (const auto& path : files)
			{
				for (unsigned char byte : path.generic_string())
					AppendHashByte(hash, byte);
				AppendHashByte(hash, 0);
				HashFile(path, hash);
				AppendHashByte(hash, 0);
			}
			return hash;
		}

		// One registered system of the loaded generation; Run points into its module.
		struct LoadedSystem
		{
			std::string Name;
			SystemPhase Phase = SystemPhase::Update;
			std::int32_t Order = 0;
			SystemFunction Run = nullptr;
			// Set when the system throws; a new generation (rebuild or F5) starts it again (H4).
			bool Suspended = false;
		};

		const char* PhaseName(SystemPhase phase)
		{
			return phase == SystemPhase::FixedUpdate ? "FixedUpdate" : "Update";
		}

		// One log line per system added, removed, or moved to another phase or order (H4).
		void LogSystemChanges(const std::vector<LoadedSystem>& previous, const std::vector<LoadedSystem>& next)
		{
			const auto find = [](const std::vector<LoadedSystem>& systems, const std::string& name)
			{
				return std::find_if(systems.begin(), systems.end(), [&](const LoadedSystem& system)
				{
					return system.Name == name;
				});
			};
			const auto describe = [](const LoadedSystem& system)
			{
				return std::string(PhaseName(system.Phase)) + " order " + std::to_string(system.Order);
			};
			for (const auto& system : next)
			{
				const auto old = find(previous, system.Name);
				if (old == previous.end())
					Log(LogLevel::Info, "Game system added: '" + system.Name + "' in " + describe(system));
				else if (old->Phase != system.Phase || old->Order != system.Order)
					Log(LogLevel::Info, "Game system moved: '" + system.Name + "' from " + describe(*old) +
						" to " + describe(system));
			}
			for (const auto& system : previous)
				if (find(next, system.Name) == next.end())
					Log(LogLevel::Info, "Game system removed: '" + system.Name + "'");
		}

		// Points of one automatic reload, for the summary line and RAVEN_RELOAD_TRACE (H5).
		struct BuildTrace
		{
			ReloadTrace::Time Observed{};
			ReloadTrace::Time BuildStarted{};
			ReloadTrace::Time BuildFinished{};
			ReloadTrace::Time Offered{};
		};

		struct LoadedGame
		{
			void* Library = nullptr;
			std::filesystem::path Copy;
			// Sorted by phase, then order. Cleared before the library unloads.
			std::vector<LoadedSystem> Systems;
			~LoadedGame()
			{
				Systems.clear();
				Platform::UnloadGameLibrary(Library);
				std::error_code ignored;
				if (!Copy.empty())
				{
					std::filesystem::remove(Copy, ignored);
					std::filesystem::remove(Copy.parent_path(), ignored); // Only if empty.
				}
			}
		};
	}

	struct GameModule::Impl
	{
		struct WatchEvent
		{
			std::optional<FileStamp> File;
			std::string Error;
			std::uint64_t Revision = 0;
			BuildTrace Trace{};
		};

		std::filesystem::path Path;
		std::unique_ptr<LoadedGame> Active;
		// Keyed by system name, so timing accumulates across compatible reloads.
		std::map<std::string, GameSystemStats, std::less<>> Stats;
		std::uint64_t Generation = 0;
		std::string Error;
		bool AutoBuild = false;
		FileStamp AttemptedFile{};
		std::atomic<bool> Building{ false };
		std::mutex EventMutex;
		std::optional<WatchEvent> Pending;
		std::atomic<bool> HasEvent{ false };
		std::atomic<std::uint64_t> SourceRevision{ 0 };
		std::jthread Worker; // Joins before the state it uses is destroyed.

		void Report(const std::string& error)
		{
			if (Error != error)
				Log(LogLevel::Warning, error);
			Error = error;
		}

		void Publish(WatchEvent event)
		{
			const std::scoped_lock lock(EventMutex);
			Pending = std::move(event);
			HasEvent.store(true, std::memory_order_release);
		}

		void DiscardCandidate()
		{
			const std::scoped_lock lock(EventMutex);
			if (Pending && Pending->File)
			{
				Pending.reset();
				HasEvent.store(false, std::memory_order_release);
			}
		}

		std::optional<WatchEvent> TakeEvent()
		{
			if (!HasEvent.load(std::memory_order_acquire))
				return {};
			const std::scoped_lock lock(EventMutex);
			HasEvent.store(false, std::memory_order_release);
			return std::exchange(Pending, std::nullopt);
		}

		bool Activate(std::optional<FileStamp> expected = {}, std::uint64_t revision = 0, const BuildTrace& trace = {});
		void Watch(std::stop_token stop, FileStamp initial);
		GameUpdateResult RunPhase(SystemPhase phase, GameInstance& game, const InputState& input,
			const FrameTime& frame, GameBindings bindings);
	};

	void SystemTable::Add(const SystemDesc& system)
	{
		m_Systems.push_back(system);
	}

	namespace
	{
		// Copies names out of the module and rejects an unusable table before activation.
		std::vector<LoadedSystem> LoadSystems(const SystemTable& table)
		{
			std::vector<LoadedSystem> systems;
			std::set<std::string, std::less<>> names;
			for (const auto& system : table.GetSystems())
			{
				if (!system.Name || !*system.Name || !system.Run)
					throw std::runtime_error("Game DLL registered a system without a name or function");
				if (system.Phase != SystemPhase::FixedUpdate && system.Phase != SystemPhase::Update)
					throw std::runtime_error(std::string("Game system has an unknown phase: ") + system.Name);
				if (!names.emplace(system.Name).second)
					throw std::runtime_error(std::string("Game DLL registered a system twice: ") + system.Name);
				systems.push_back({ system.Name, system.Phase, system.Order, system.Run });
			}
			std::stable_sort(systems.begin(), systems.end(), [](const LoadedSystem& left, const LoadedSystem& right)
			{
				return left.Phase != right.Phase ? left.Phase < right.Phase : left.Order < right.Order;
			});
			return systems;
		}
	}

	GameUpdateResult GameModule::Impl::RunPhase(SystemPhase phase, GameInstance& game, const InputState& input,
		const FrameTime& frame, GameBindings bindings)
	{
		GameUpdateResult result;
		SystemContext context{ game, input, frame, bindings, result };
		for (auto& system : Active->Systems)
		{
			if (system.Phase != phase || system.Suspended)
				continue;
			const auto start = Clock::now();
			std::string failure;
			try
			{
				system.Run(context);
			}
			catch (const std::exception& error)
			{
				failure = error.what();
			}
			catch (...)
			{
				failure = "unknown exception";
			}
			const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count();
			auto stats = Stats.find(system.Name);
			if (stats == Stats.end())
				stats = Stats.emplace(system.Name, GameSystemStats{ system.Name, system.Phase }).first;
			++stats->second.Calls;
			stats->second.Nanoseconds += static_cast<std::uint64_t>(elapsed);
			stats->second.Phase = system.Phase;
			if (!failure.empty())
			{
				// One line, then skip it: calling it again would repeat a partial update every frame.
				// What the failing call already wrote stays in the world.
				system.Suspended = true;
				stats->second.Suspended = true;
				Log(LogLevel::Error, "Game system '" + system.Name + "' failed: " + failure +
					". Suspended until the next module reload.");
			}
		}
		return result;
	}

	bool GameModule::Impl::Activate(std::optional<FileStamp> expected, std::uint64_t revision, const BuildTrace& trace)
	{
		if (expected && (*expected == AttemptedFile || SourceRevision.load() != revision))
			return false;
		try
		{
			const auto started = ReloadTrace::Now();
			const auto before = Stamp(Path);
			if (expected && before != *expected)
				return false;
			auto candidate = std::make_unique<LoadedGame>();
			// The build output is never loaded, so the linker can replace it at any time.
			// create_directory reserves a unique directory across hosts and processes.
			std::filesystem::path directory;
			do
			{
				directory = Path.parent_path() / (".raven-game-" + std::to_string(++NextCopy));
			} while (!std::filesystem::create_directory(directory));
			candidate->Copy = directory / Path.filename();
			const auto stamped = ReloadTrace::Now();
			std::filesystem::copy_file(Path, candidate->Copy);
			if (!(Stamp(Path) == before) || Stamp(candidate->Copy).Digest != before.Digest)
				throw std::runtime_error("Game DLL changed during copy; retry reload");
			const auto copied = ReloadTrace::Now();
			candidate->Library = Platform::LoadGameLibrary(candidate->Copy);
			const auto loaded = ReloadTrace::Now();
			const auto contract = reinterpret_cast<GetGameContractFunction>(
				Platform::FindGameEntryPoint(candidate->Library, GameContractEntryPoint));
			const char* value = contract();
			if (!value || std::string_view(value) != GameBuild::Contract)
				throw std::runtime_error("Game contract changed: rebuild and restart the host; existing game remains active");
			const auto registerSystems = reinterpret_cast<RegisterSystemsFunction>(
				Platform::FindGameEntryPoint(candidate->Library, RegisterSystemsEntryPoint));
			SystemTable table;
			// Translate a module exception while the candidate is still loaded: unwinding past
			// `candidate` unloads it, and the exception's what() and destructor live in its image.
			try
			{
				registerSystems(table);
			}
			catch (const std::exception& error)
			{
				throw std::runtime_error(std::string("Game system registration failed: ") + error.what());
			}
			catch (...)
			{
				throw std::runtime_error("Game system registration threw a non-standard exception");
			}
			candidate->Systems = LoadSystems(table);
			if (candidate->Systems.empty())
				throw std::runtime_error("Game DLL registered no systems");
			if (expected && SourceRevision.load() != revision)
				return false;
			if (Active)
				LogSystemChanges(Active->Systems, candidate->Systems);
			// A new generation starts every system again, including ones suspended before.
			for (auto& [name, stats] : Stats)
				stats.Suspended = false;
			Active.swap(candidate);
			AttemptedFile = before;
			++Generation;
			Error.clear();
			const auto activated = ReloadTrace::Now();
			std::string message = "Game module activated, generation " + std::to_string(Generation);
			if (const auto total = ReloadTrace::Milliseconds(trace.Observed, activated); total >= 0)
				message += ", " + std::to_string(total) + " ms after the change was seen";
			// The H7 summary's publish time: stamping, copying, loading and registering.
			message += ", activation " + std::to_string(ReloadTrace::Milliseconds(started, activated)) + " ms";
			Log(LogLevel::Info, message);
			if (ReloadTrace::Enabled())
			{
				Log(LogLevel::Info, "Reload trace game: " +
					ReloadTrace::Span("wait", trace.Observed, trace.BuildStarted) + ", " +
					ReloadTrace::Span("build", trace.BuildStarted, trace.BuildFinished) + ", " +
					ReloadTrace::Span("offer", trace.BuildFinished, trace.Offered) + ", " +
					ReloadTrace::Span("handoff", trace.Offered, started) + ", " +
					ReloadTrace::Span("stamp", started, stamped) + ", " +
					ReloadTrace::Span("copy", stamped, copied) + ", " +
					ReloadTrace::Span("load", copied, loaded) + ", " +
					ReloadTrace::Span("register", loaded, activated) + " ms");
			}
			return true;
		}
		catch (const std::exception& error)
		{
			Report(error.what());
			return false;
		}
		catch (...)
		{
			Report("Game module activation failed");
			return false;
		}
	}

	void GameModule::Impl::Watch(std::stop_token stop, FileStamp initial)
	{
		using namespace std::chrono_literals;
		using BuildProcess = std::unique_ptr<Platform::BuildProcess, decltype(&Platform::DestroyBuildProcess)>;
		BuildProcess build{ nullptr, &Platform::DestroyBuildProcess };
		const auto directory = std::filesystem::path(GameBuild::BuildDirectory);
		auto publishedFile = initial;
		std::uint64_t publishedRevision = 0;
		std::optional<std::uint64_t> observedSource;
		std::uint64_t builtSource = 0;
		BuildTrace trace;
		bool buildQueued = AutoBuild;
		bool buildFailed = false;
		std::string lastError;
		const auto report = [&](const std::string& error)
		{
			if (lastError != error)
			{
				lastError = error;
				Publish({ {}, error });
			}
		};
		const auto observeSource = [&](std::uint64_t signature)
		{
			if (observedSource != signature)
			{
				// The first observation is startup, not an edit.
				trace = {};
				if (observedSource)
					trace.Observed = ReloadTrace::Now();
				observedSource = signature;
				++SourceRevision;
				buildQueued = true;
				DiscardCandidate();
			}
		};
		const auto offer = [&](FileStamp file, bool successfulBuild = false)
		{
			const auto revision = SourceRevision.load();
			if (publishedFile != file || publishedRevision != revision)
			{
				publishedFile = file;
				publishedRevision = revision;
				lastError.clear();
				trace.Offered = ReloadTrace::Now();
				Publish({ file, {}, revision, trace });
				trace = {};
			}
			else if (successfulBuild && !lastError.empty())
			{
				lastError.clear();
				Publish({});
			}
		};

		// The shared reload service settles source and DLL changes (docs/HotReloadDesign.md, H1).
		// This worker only starts and polls the build process.
		std::mutex wakeMutex;
		std::condition_variable_any wakeup;
		bool sourcesSettled = false;
		bool outputSettled = false;
		const auto wake = [&](bool& flag)
		{
			{
				const std::scoped_lock lock(wakeMutex);
				flag = true;
			}
			wakeup.notify_all();
		};
		WakeConsumer sources([&] { wake(sourcesSettled); });
		WakeConsumer output([&] { wake(outputSettled); });
		// Declared after their consumers, so they are destroyed first.
		std::unique_ptr<ReloadSubscription> sourceWatch;
		ReloadOptions sourceOptions{ "game sources", ImporterVersion::GameSources };
		if (AutoBuild)
			sourceWatch = ReloadService::Subscribe(SourceInputs(), sources, sourceOptions);
		auto outputWatch = ReloadService::Subscribe({ ReloadInput{ Path } }, output,
			{ "game DLL", ImporterVersion::GameSources });
		bool checkSources = AutoBuild;
		bool checkOutput = false;

		while (!stop.stop_requested())
		{
			try
			{
				int exitCode = 0;
				if (build && Platform::PollBuildProcess(*build, exitCode))
				{
					build.reset();
					Building.store(false);
					outputWatch->SetPaused(false);
					trace.BuildFinished = ReloadTrace::Now();
					if (exitCode != 0)
					{
						buildFailed = true;
						report("Game build failed; keeping active code. See " + (directory / "AutoBuild.log").string());
					}
					// Recheck the build's inputs directly; the service may not have settled an edit yet.
					const bool superseded = buildQueued;
					buildQueued = true;
					const auto signature = SourceSignature();
					observeSource(signature);
					if (!superseded && signature == builtSource)
					{
						if (exitCode == 0)
						{
							offer(Stamp(Path), true);
							buildFailed = false;
						}
						buildQueued = false;
					}
				}
				if (checkSources)
				{
					checkSources = false;
					observeSource(SourceSignature());
				}
				// The service already waited for the sources to settle, so the build starts at once.
				if (!build && buildQueued && observedSource)
				{
					build.reset(Platform::StartBuildProcess(GameBuild::Cmake,
						{ L"-P", (directory / "BuildGame.cmake").wstring() }, directory / "AutoBuild.log"));
					// The linker needs the DLL unopened, so the service must not read it meanwhile.
					// Linking starts long after this, past any read already in progress.
					outputWatch->SetPaused(true);
					Building.store(true);
					trace.BuildStarted = ReloadTrace::Now();
					builtSource = *observedSource;
					buildQueued = false;
					DiscardCandidate();
					Log(LogLevel::Info, "Building game module in background");
				}
				if (checkOutput)
				{
					checkOutput = false;
					if (!build && !buildQueued && !buildFailed)
						offer(Stamp(Path));
				}
			}
			catch (const std::exception& error)
			{
				report(error.what());
			}
			std::unique_lock lock(wakeMutex);
			const auto settled = [&] { return sourcesSettled || outputSettled; };
			if (build)
				wakeup.wait_for(lock, stop, 40ms, settled);
			else
				wakeup.wait(lock, stop, settled);
			checkSources |= std::exchange(sourcesSettled, false);
			checkOutput |= std::exchange(outputSettled, false);
		}
	}

	GameModule::GameModule(const std::filesystem::path& path, bool autoBuild)
		: m_Impl(std::make_unique<Impl>())
	{
		if (!GameBuild::Enabled)
			throw std::runtime_error("Game modules are disabled or unsupported in this build");
		m_Impl->Path = std::filesystem::absolute(path);
		m_Impl->AutoBuild = autoBuild;
		if (autoBuild && !std::filesystem::equivalent(m_Impl->Path,
			std::filesystem::path(GameBuild::BuildDirectory).parent_path() / GameBuild::ModuleFilename))
			throw std::invalid_argument("Automatic builds require this build's configured RavenGame.dll output");
		if (!Reload())
			throw std::runtime_error(m_Impl->Error);
	}

	GameModule::~GameModule() = default;

	bool GameModule::Reload()
	{
		return m_Impl->Activate();
	}

	void GameModule::Poll()
	{
		auto& state = *m_Impl;
		if (!state.Worker.joinable())
		{
			state.Worker = std::jthread([&state, initial = state.AttemptedFile](std::stop_token stop)
			{
				state.Watch(stop, initial);
			});
		}
		if (auto event = state.TakeEvent())
		{
			if (event->File)
			{
				if (*event->File == state.AttemptedFile && event->Revision == state.SourceRevision.load())
					state.Error.clear();
				else
					state.Activate(event->File, event->Revision, event->Trace);
			}
			else if (event->Error.empty())
				state.Error.clear();
			else
				state.Report(event->Error);
		}
	}

	GameUpdateResult GameModule::FixedUpdate(GameInstance& game, const InputState& input, const FrameTime& frame,
		GameBindings bindings)
	{
		return m_Impl->RunPhase(SystemPhase::FixedUpdate, game, input, frame, bindings);
	}

	GameUpdateResult GameModule::Update(GameInstance& game, const InputState& input, const FrameTime& frame,
		GameBindings bindings)
	{
		return m_Impl->RunPhase(SystemPhase::Update, game, input, frame, bindings);
	}

	std::vector<GameSystemStats> GameModule::GetSystemStats() const
	{
		std::vector<GameSystemStats> stats;
		for (const auto& [name, value] : m_Impl->Stats)
			stats.push_back(value);
		return stats;
	}

	std::uint64_t GameModule::GetGeneration() const noexcept { return m_Impl->Generation; }
	bool GameModule::IsBuilding() const noexcept { return m_Impl->Building.load(); }
	std::string GameModule::GetLastError() const { return m_Impl->Error; }
}
