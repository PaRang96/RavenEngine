#pragma once

#include "Raven/Core/Api.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <stop_token>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace Raven
{
	// The engine's shared compute workers (docs/JobSystemDesign.md). One pool per host, owned by the
	// Application and passed to the subsystems that use it: Jolt (through JoltJobs), text shaping,
	// layout and glyph generation. Work that blocks (file watching, waiting on the game build, the
	// audio device) keeps its own threads, so it never holds a compute worker.
	//
	// Two priorities:
	// - Frame jobs: work the current frame waits for (physics, layout, shaping). Workers take them first,
	//   and the waiting thread runs queued jobs itself instead of idling.
	// - Background jobs: work nothing waits for this frame (glyph generation, imports). They never
	//   occupy every worker, so a frame job always finds one free.
	class JobSystem
	{
	public:
		using Function = void (*)(void* context, std::uint32_t index) noexcept;
		enum class Priority : std::uint8_t { Frame, Background };

		// The CPU count minus one for the main thread, at least one.
		RAVEN_API static std::uint32_t DefaultWorkerCount() noexcept;

		RAVEN_API explicit JobSystem(std::uint32_t workers = DefaultWorkerCount());
		RAVEN_API ~JobSystem();
		JobSystem(const JobSystem&) = delete;
		JobSystem& operator=(const JobSystem&) = delete;

		std::uint32_t GetWorkerCount() const noexcept { return static_cast<std::uint32_t>(m_Workers.size()); }

		// Queues function(context, index). Completion is the caller's to track.
		RAVEN_API void Submit(Function function, void* context, std::uint32_t index = 0,
			Priority priority = Priority::Frame);
		// Runs one queued frame job on the calling thread; false when there was none.
		RAVEN_API bool RunOne() noexcept;

		// Runs body(index) for every index below count, on the workers and the calling thread, and
		// returns when all have finished. The body must not throw.
		template <typename Body>
		void ParallelFor(std::uint32_t count, const Body& body)
		{
			static_assert(std::is_nothrow_invocable_v<const Body&, std::uint32_t>, "ParallelFor bodies must be noexcept");
			if (count == 0)
				return;
			if (count == 1 || m_Workers.empty())
			{
				for (std::uint32_t index = 0; index < count; ++index)
					body(index);
				return;
			}
			struct Batch
			{
				const Body* Run;
				std::atomic<std::uint32_t> Remaining;
			} batch{ &body, count };
			const Function run = [](void* context, std::uint32_t index) noexcept
			{
				auto& work = *static_cast<Batch*>(context);
				(*work.Run)(index);
				if (work.Remaining.fetch_sub(1, std::memory_order_acq_rel) == 1)
					work.Remaining.notify_all();
			};
			// The calling thread takes index 0 itself; the rest go to the queue.
			SubmitRange(run, &batch, 1, count);
			run(&batch, 0);
			Wait(batch.Remaining);
		}

		// Returns once counter reaches zero, running queued frame jobs meanwhile.
		RAVEN_API void Wait(const std::atomic<std::uint32_t>& counter) noexcept;

	private:
		struct Item
		{
			Function Run = nullptr;
			void* Context = nullptr;
			std::uint32_t Index = 0;
		};

		RAVEN_API void SubmitRange(Function function, void* context, std::uint32_t first, std::uint32_t last);
		void Work(std::stop_token stop);
		bool TakeLocked(Item& item, bool background) noexcept;

		std::mutex m_Mutex;
		std::condition_variable_any m_Wake;
		std::deque<Item> m_Frame;
		std::deque<Item> m_Background;
		std::uint32_t m_BackgroundRunning = 0;
		std::uint32_t m_Sleeping = 0;
		// Frame jobs waiting in the queue, readable without the lock by spinning workers and helpers.
		std::atomic<std::uint32_t> m_FrameQueued{ 0 };
		std::uint32_t m_BackgroundLimit = 1;
		// Declared last: workers stop and join before the queues they read are destroyed.
		std::vector<std::jthread> m_Workers;
	};
}
