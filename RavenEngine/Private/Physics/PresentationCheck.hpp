#pragma once

#include "Raven/Components/TransformComponent.hpp"
#include "Raven/Core/Log.hpp"
#include "Raven/ECS/Registry.hpp"
#include "Raven/Scene/Scene.hpp"

#include <format>

namespace Raven::Physics
{
	// An entity created in Control or Update is presentation (PresentationComponent): its ID is not
	// seeded, so physics on it makes the simulation depend on frame pacing. Development builds report
	// the first such entity the physics world takes in (review amendment, AA2-GP-03 step 4).
	inline void ReportPresentation([[maybe_unused]] const Scene& scene, [[maybe_unused]] const ECS::Registry& registry,
		[[maybe_unused]] ECS::Entity entity) noexcept
	{
#if defined(RAVEN_DEVELOPMENT)
		static bool reported = false;
		if (reported || !registry.Has<ECS::PresentationComponent>(entity))
			return;
		reported = true;
		try
		{
			const auto name = scene.GetAuthoredId(entity);
			Log(LogLevel::Warning, std::format("'{}' was created in Control or Update but has physics components; "
				"create simulated entities in FixedUpdate, so they get seeded IDs",
				name.empty() ? std::format("entity {:016x}", scene.GetRef(entity).Id) : name));
		}
		catch (...)
		{
		}
#endif
	}
}
