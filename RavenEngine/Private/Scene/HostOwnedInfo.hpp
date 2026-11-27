#pragma once

#include "Raven/ECS/Component.hpp"

namespace Raven
{
	// The generic form of the compile-time HostOwned check (docs/DataModelDesign.md, FD-07), for
	// type-erased callers such as SceneAccess and SceneCommands: the flag ComponentTraits sets from
	// HostOwned<T> (AA2-ECS-02).
	inline bool IsHostOwned(const ECS::ComponentInfo& info) noexcept
	{
		return (info.Flags & ECS::ComponentIsHostOwned) != 0;
	}
}
