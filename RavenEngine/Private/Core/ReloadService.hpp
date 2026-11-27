#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <vector>

// One process-wide service watches every reloadable input (docs/HotReloadDesign.md, H1).
// A single thread waits for directory notifications and settles changes; two workers run
// imports. The service exists only while something subscribes, so --no-watch and player
// builds start no thread.
namespace Raven
{
	struct ReloadInput
	{
		using ReadFile = std::vector<std::byte>(*)(const std::filesystem::path&);

		std::filesystem::path Path;
		// File inputs: how to read the file. Null reads up to 64 MiB.
		ReadFile Read = nullptr;
		// Directory inputs: which files below Path belong to the input. The input's content is
		// each matching file's relative path and content digest, so additions and removals count.
		std::function<bool(const std::filesystem::path&)> Match;
		bool Subtree = false;
	};

	// The settled content of each input, in input order. A read failure fills Error instead.
	struct ReloadSnapshot
	{
		std::vector<std::vector<std::byte>> Contents;
		std::string Error;
		bool operator==(const ReloadSnapshot&) const = default;
	};

	// What the service knows about one consumer: the dependency record (H1, Q4 A+). Its key,
	// the input hashes plus ImporterVersion, is what a future derived-data cache would store.
	struct ReloadRecord
	{
		std::string Name;
		// Bumped by an importer whenever its output changes for the same input (ImporterVersions.hpp).
		std::uint32_t ImporterVersion = 0;
		std::vector<std::filesystem::path> Inputs;
		// FNV-1a of each input's content at the last successful import.
		std::vector<std::uint64_t> InputHashes;
		// The inputs whose content differed from the import before; empty for the first import.
		std::vector<std::filesystem::path> Trigger;
		// What the last import produced, as the consumer describes it.
		std::string Output;
		std::chrono::system_clock::time_point ImportedAt{};
		std::uint64_t Imports = 0;
		std::uint64_t Failures = 0;
		std::uint64_t Superseded = 0;
		std::chrono::duration<double, std::milli> LastImport{};
		// Time the consumer spent publishing the last import, reported through Published.
		std::chrono::duration<double, std::milli> LastPublish{};
		std::string LastError;
	};

	class ReloadConsumer
	{
	public:
		// Runs on a reload worker once every input has been unchanged for the settle time. Returns
		// a short description of the output for the record; throw to keep the last-good result,
		// and the service reports the error.
		virtual std::string Import(const ReloadSnapshot& snapshot, std::stop_token stop) = 0;
		// After a successful Import: Accept when the inputs still match the snapshot, otherwise
		// Discard and wait for the newer contents to settle. The record already describes this import.
		virtual void Accept(const ReloadRecord& record) = 0;
		virtual void Discard() noexcept = 0;
		// The inputs changed since the last settled snapshot; a ready result is now stale.
		virtual void Changed() noexcept {}

	protected:
		~ReloadConsumer() = default;
	};

	struct ReloadOptions
	{
		// Shown in logs and the record, for example "PNG D:\Content\Textures\Grid.png".
		std::string Name;
		std::uint32_t ImporterVersion = 1;
		// Contents already loaded by the consumer; they count as imported.
		std::optional<ReloadSnapshot> Baseline;
		// The baseline's input hashes (HashContent), when the consumer computed them off the main thread;
		// otherwise Subscribe hashes the baseline itself.
		std::vector<std::uint64_t> BaselineHashes;
		// Retry a failed import after another settle time, for inputs that may become valid
		// without changing (a referenced file that appears later).
		bool RetryFailures = false;
		// Start paused, for a consumer that calls its own subscription from Import: it stores the
		// subscription first, and SetPaused(false) publishes it to the reload workers.
		bool StartPaused = false;
		std::chrono::milliseconds Settle{ 100 };
	};

	class ReloadSubscription
	{
	public:
		~ReloadSubscription();
		ReloadSubscription(const ReloadSubscription&) = delete;
		ReloadSubscription& operator=(const ReloadSubscription&) = delete;

		// Replaces the inputs (for example, a shader's includes after a compile). The current
		// contents become the new baseline, so the change itself does not import again.
		void SetInputs(std::vector<ReloadInput> inputs);
		// A paused subscription's inputs are not read, for example while a linker writes them.
		// Resuming compares the inputs with the last observation as usual.
		void SetPaused(bool paused);
		ReloadRecord GetRecord() const;
		// The last observed contents were imported (or failed) and nothing is importing now.
		bool IsSettled() const;
		// Called by the consumer once it published an import. Records the publish time and returns
		// the H7 summary to append to the consumer's own log line: trigger, import and publish times.
		std::string Published(std::chrono::duration<double, std::milli> publish);

		// Opaque to consumers; defined by the service.
		struct State;

	private:
		friend class ReloadService;
		ReloadSubscription(std::shared_ptr<void> service, std::shared_ptr<State> state);
		std::shared_ptr<void> m_Service;
		std::shared_ptr<State> m_State;
	};

	class ReloadService
	{
	public:
		// The consumer must outlive the subscription. Destroying the subscription waits for an
		// import in progress and guarantees no further consumer calls.
		static std::unique_ptr<ReloadSubscription> Subscribe(std::vector<ReloadInput> inputs,
			ReloadConsumer& consumer, ReloadOptions options);
		static std::vector<ReloadRecord> GetRecords();
		// The hash dependency records key inputs by, for consumers that hash a baseline on a worker.
		static std::uint64_t HashContent(std::span<const std::byte> bytes) noexcept;
		// Threads owned by the service right now: zero, or the watcher plus the workers.
		static std::size_t GetThreadCount();
	};
}
