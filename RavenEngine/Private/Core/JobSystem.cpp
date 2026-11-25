#include "Core/JobSystem.hpp"

#include <algorithm>

#if defined(_M_X64) || defined(__x86_64__)
#include <immintrin.h>
#endif

namespace Raven
{
	namespace
	{
		// About 10-20 microseconds on the laptop's cores.
		constexpr int SpinLimit = 4000;

		void Pause() noexcept
		{
#if defined(_M_X64) || defined(__x86_64__)
			_mm_pause();
#else
			std::this_thread::yield();
#endif
		}
	}

	std::uint32_t JobSystem::DefaultWorkerCount() noexcept
	{
		const auto hardware = std::thread::hardware_concurrency();
		return std::max<std::uint32_t>(hardware > 1 ? hardware - 1 : 1, 1);
	}

	JobSystem::JobSystem(std::uint32_t workers)
	{
		workers = std::max<std::uint32_t>(workers, 1);
		// Background jobs leave at least one worker for frame jobs.
		m_BackgroundLimit = std::max<std::uint32_t>(workers - 1, 1);
		m_Workers.reserve(workers);
		for (std::uint32_t index = 0; index < workers; ++index)
			m_Workers.emplace_back([this](std::stop_token stop) { Work(stop); });
	}

	JobSystem::~JobSystem()
	{
		for (auto& worker : m_Workers)
			worker.request_stop();
		m_Wake.notify_all();
		m_Workers.clear();
	}

	void JobSystem::Submit(Function function, void* context, std::uint32_t index, Priority priority)
	{
		std::uint32_t sleeping = 0;
		{
			std::lock_guard lock(m_Mutex);
			(priority == Priority::Frame ? m_Frame : m_Background).push_back({ function, context, index });
			if (priority == Priority::Frame)
				m_FrameQueued.fetch_add(1, std::memory_order_release);
			sleeping = m_Sleeping;
		}
		// Spinning workers see the job without a wake-up; only sleeping ones need one.
		if (sleeping > 0)
			m_Wake.notify_one();
	}

	void JobSystem::SubmitRange(Function function, void* context, std::uint32_t first, std::uint32_t last)
	{
		std::uint32_t sleeping = 0;
		{
			std::lock_guard lock(m_Mutex);
			for (std::uint32_t index = first; index < last; ++index)
				m_Frame.push_back({ function, context, index });
			m_FrameQueued.fetch_add(last - first, std::memory_order_release);
			sleeping = m_Sleeping;
		}
		if (sleeping == 0)
			return;
		if (last - first == 1)
			m_Wake.notify_one();
		else
			m_Wake.notify_all();
	}

	bool JobSystem::TakeLocked(Item& item, bool background) noexcept
	{
		if (!m_Frame.empty())
		{
			item = m_Frame.front();
			m_Frame.pop_front();
			m_FrameQueued.fetch_sub(1, std::memory_order_relaxed);
			return true;
		}
		if (background && !m_Background.empty() && m_BackgroundRunning < m_BackgroundLimit)
		{
			item = m_Background.front();
			m_Background.pop_front();
			++m_BackgroundRunning;
			return true;
		}
		return false;
	}

	bool JobSystem::RunOne() noexcept
	{
		Item item;
		{
			if (m_FrameQueued.load(std::memory_order_acquire) == 0)
				return false;
			std::lock_guard lock(m_Mutex);
			if (!TakeLocked(item, false))
				return false;
		}
		item.Run(item.Context, item.Index);
		return true;
	}

	void JobSystem::Wait(const std::atomic<std::uint32_t>& counter) noexcept
	{
		for (;;)
		{
			const auto remaining = counter.load(std::memory_order_acquire);
			if (remaining == 0)
				return;
			// Help with queued frame jobs; when there are none, the rest are running on workers.
			if (!RunOne())
				counter.wait(remaining, std::memory_order_acquire);
		}
	}

	void JobSystem::Work(std::stop_token stop)
	{
		for (;;)
		{
			Item item;
			bool background = false;
			// Physics and layout submit many short jobs in bursts. Spinning briefly before sleeping
			// catches the next one without an operating-system wake-up (about 20 us on Windows).
			for (int spin = 0; spin < SpinLimit && m_FrameQueued.load(std::memory_order_acquire) == 0 &&
				!stop.stop_requested(); ++spin)
				Pause();
			{
				std::unique_lock lock(m_Mutex);
				++m_Sleeping;
				m_Wake.wait(lock, stop, [&]
				{
					return !m_Frame.empty() || (!m_Background.empty() && m_BackgroundRunning < m_BackgroundLimit);
				});
				--m_Sleeping;
				background = m_Frame.empty();
				// Owners wait for their own jobs before they are destroyed, so a stopping pool has none
				// that anyone still needs.
				if (!TakeLocked(item, true))
				{
					if (stop.stop_requested())
						return;
					continue;
				}
			}
			item.Run(item.Context, item.Index);
			if (background)
			{
				{
					std::lock_guard lock(m_Mutex);
					--m_BackgroundRunning;
				}
				// A background slot opened; a waiting worker may take the next one.
				m_Wake.notify_one();
			}
		}
	}
}
