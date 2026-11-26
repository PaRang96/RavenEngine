#pragma once

#include "Raven/Components/TransformComponent.hpp"
#include "Raven/ECS/Component.hpp"

namespace Raven
{
	// The generic form of the compile-time HostOwned check (docs/DataModelDesign.md, FD-07), for
	// type-erased callers such as SceneAccess and SceneCommands. A ComponentInfo flag replaces this
	// list in the contract window at the start of B2 (AA2-ECS-02 steps 1-2).
	inline bool IsHostOwned(const ECS::ComponentInfo& info) noexcept
	{
		return info.Id == ECS::ComponentTraits<ECS::WorldTransformComponent>::Info.Id ||
			info.Id == ECS::ComponentTraits<ECS::ParentComponent>::Info.Id ||
			info.Id == ECS::ComponentTraits<ECS::PresentationComponent>::Info.Id;
	}
}
