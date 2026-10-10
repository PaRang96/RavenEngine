#pragma once

#include "Raven/Core/FrameTime.hpp"

#include <chrono>

namespace Raven
{
	class FrameClock
	{
	public:
		FrameTime Tick() noexcept
		{
			const auto now = Clock::now();

			FrameTime time;
			time.ElapsedSeconds =
				std::chrono::duration<double>(now - m_Start).count();

			if (!m_ResetDelta)
			{
				time.DeltaSeconds =
					std::chrono::duration<double>(now - m_Previous).count();
			}

			m_Previous = now;
			m_ResetDelta = false;
			return time;
		}

		// The next Tick() returns a zero delta.
		void ResetDelta() noexcept
		{
			m_ResetDelta = true;
		}

	private:
		using Clock = std::chrono::steady_clock;

		Clock::time_point m_Start = Clock::now();
		Clock::time_point m_Previous = m_Start;
		bool m_ResetDelta = true;
	};
}
