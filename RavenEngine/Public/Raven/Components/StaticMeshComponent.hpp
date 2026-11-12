#pragma once

#include "Raven/ECS/Component.hpp"
#include "Raven/Math/Transform.hpp"
#include "Raven/Renderer/MeshHandle.hpp"

namespace Raven::ECS
{
	struct StaticMeshComponent : Component
	{
		// Runtime identity only. The renderer owns immutable geometry until shutdown.
		MeshHandle Mesh;
		Vec4 Tint{ 1.0f, 1.0f, 1.0f, 1.0f };

		RAVEN_COMPONENT(Raven::ECS::StaticMeshComponent, Mesh, Tint);
	};
}
