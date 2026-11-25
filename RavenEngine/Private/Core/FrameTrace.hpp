#pragma once

#include "Raven/Core/Api.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace Raven
{
	using TraceClock = std::chrono::steady_clock;

	inline std::int64_t TraceNanoseconds(TraceClock::duration duration)
	{
		return std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();
	}

	struct FrameMetrics
	{
		std::int64_t Start = 0;
		std::int64_t Gameplay = 0;
		std::int64_t SceneGather = 0;
		std::int64_t WorldTransforms = 0;
		std::int64_t RenderPrepare = 0;
		std::int64_t RecordSubmit = 0;
		std::int64_t FenceWait = 0;
		std::int64_t AcquireWait = 0;
		std::int64_t PresentWait = 0;
		std::int64_t Presented = 0;
		std::int64_t GpuFrame = 0; // Timestamp results are delivered by a later frame.
		double Gpu = 0;
		// GPU time of the scene upload and culling passes, from the same frame as Gpu.
		double GpuCull = 0;
		std::size_t VisibleMeshes = 0;
		std::size_t DrawCalls = 0;
		std::uint64_t GameGeneration = 0;
		double DeltaSeconds = 0;
		double BenchmarkPhase = std::numeric_limits<double>::quiet_NaN();
		bool SceneReplaced = false;
		// Physics steps of the frame (docs/GameServicesDesign.md, FD-32): sync, simulation and
		// write-back, then the world's state after the latest step.
		std::int64_t Physics = 0;
		std::uint32_t PhysicsBodies = 0;
		std::uint32_t PhysicsActive = 0;
		std::uint32_t PhysicsContacts = 0;
		// UI layout and draw-list build after Update: every context's EndFrame, text included, and the
		// glyph uploads gathered for the frame (docs/UIDesign.md, section 7; measurement 7).
		std::int64_t Ui = 0;
		std::uint32_t UiQuads = 0;
	};

	RAVEN_API FrameMetrics* GetFrameMetrics() noexcept;

	class FrameSection
	{
	public:
		explicit FrameSection(std::int64_t* total) noexcept : m_Total(total)
		{
			if (m_Total)
				m_Start = TraceClock::now();
		}

		~FrameSection() { End(); }
		FrameSection(const FrameSection&) = delete;
		FrameSection& operator=(const FrameSection&) = delete;

		void End() noexcept
		{
			if (m_Total)
			{
				*m_Total += TraceNanoseconds(TraceClock::now() - m_Start);
				m_Total = nullptr;
			}
		}

	private:
		std::int64_t* m_Total;
		TraceClock::time_point m_Start;
	};
}
