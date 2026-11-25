#pragma once

// Jolt's jobs on the engine's shared workers (docs/JobSystemDesign.md). Jolt stays unmodified: this
// implements its public JobSystemWithBarrier interface, the extension point Jolt documents for
// engines with their own job systems.
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
