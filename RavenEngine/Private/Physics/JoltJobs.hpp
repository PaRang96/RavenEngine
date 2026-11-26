#pragma once

// Jolt's jobs on the engine's shared workers (docs/JobSystemDesign.md). Jolt stays unmodified: this
// implements its public JobSystemWithBarrier interface, the extension point Jolt documents for
// engines with their own job systems.
//
// Jolt is built without exceptions, and these jobs may run inline on the host thread. So every Raven
// path that a Jolt frame can reach (listeners, the debug renderer, trace and assert hooks, the
// character grid, body filters) is noexcept: a failure there stops the process the same way on a
// worker and on the host thread, instead of unwinding through Jolt and leaving its locks held
// (review amendment, AA2-RT-04).
#include <Jolt/Jolt.h>

#include <Jolt/Core/FixedSizeFreeList.h>
#include <Jolt/Core/JobSystemWithBarrier.h>

namespace Raven
{
	class JobSystem;
}

namespace Raven::Physics
{
	class JoltJobs final : public JPH::JobSystemWithBarrier
	{
	public:
		JoltJobs(Raven::JobSystem& jobs, JPH::uint maxJobs, JPH::uint maxBarriers);

		int GetMaxConcurrency() const override;
		JobHandle CreateJob(const char* name, JPH::ColorArg color, const JobFunction& function,
			JPH::uint32 dependencies = 0) override;
		void CreateJobs(const char* name, JPH::ColorArg color, const JobFunction& function, JobHandle* jobs,
			JPH::uint32 count, JPH::uint32 dependencies = 0) override;

	protected:
		void QueueJob(Job* job) override;
		void QueueJobs(Job** jobs, JPH::uint count) override;
		void FreeJob(Job* job) override;

	private:
		Raven::JobSystem& m_Jobs;
		JPH::FixedSizeFreeList<Job> m_Pool;
	};
}
