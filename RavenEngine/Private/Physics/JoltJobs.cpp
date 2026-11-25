#include "Physics/JoltJobs.hpp"

#include "Core/JobSystem.hpp"

#include <thread>

namespace Raven::Physics
{
	JoltJobs::JoltJobs(Raven::JobSystem& jobs, JPH::uint maxJobs, JPH::uint maxBarriers)
		: JobSystemWithBarrier(maxBarriers), m_Jobs(jobs)
	{
		m_Pool.Init(maxJobs, maxJobs);
	}

	int JoltJobs::GetMaxConcurrency() const
	{
		// The workers plus the thread that waits on a barrier, which runs jobs too.
		return static_cast<int>(m_Jobs.GetWorkerCount()) + 1;
	}

	JoltJobs::JobHandle JoltJobs::CreateJob(const char* name, JPH::ColorArg color, const JobFunction& function,
		JPH::uint32 dependencies)
	{
		// Jolt sizes the pool (cMaxPhysicsJobs). If it is ever full, wait for running jobs to free a slot,
		// as Jolt's own thread pool does.
		JPH::uint32 index = m_Pool.ConstructObject(name, color, this, function, dependencies);
		while (index == decltype(m_Pool)::cInvalidObjectIndex)
		{
			if (!m_Jobs.RunOne())
				std::this_thread::yield();
			index = m_Pool.ConstructObject(name, color, this, function, dependencies);
		}
		Job* job = &m_Pool.Get(index);
		// The handle keeps a reference until the caller drops it; a queued job holds its own.
		JobHandle handle(job);
		if (dependencies == 0)
			QueueJob(job);
		return handle;
	}

	void JoltJobs::CreateJobs(const char* name, JPH::ColorArg color, const JobFunction& function, JobHandle* jobs,
		JPH::uint32 count, JPH::uint32 dependencies)
	{
		for (JPH::uint32 index = 0; index < count; ++index)
			jobs[index] = CreateJob(name, color, function, dependencies);
	}

	void JoltJobs::QueueJob(Job* job)
	{
		job->AddRef();
		m_Jobs.Submit([](void* context, std::uint32_t) noexcept
		{
			auto* queued = static_cast<Job*>(context);
			// A job may already have run on a thread waiting on its barrier; Execute does nothing then.
			queued->Execute();
			queued->Release();
		}, job);
	}

	void JoltJobs::QueueJobs(Job** jobs, JPH::uint count)
	{
		for (JPH::uint index = 0; index < count; ++index)
			QueueJob(jobs[index]);
	}

	void JoltJobs::FreeJob(Job* job)
	{
		m_Pool.DestructObject(job);
	}
}
