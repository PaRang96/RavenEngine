#pragma once

#include "Raven/Assets/AssetRef.hpp"
#include "Raven/ECS/Component.hpp"
#include "Raven/Math/Transform.hpp"

namespace Raven::ECS
{
	struct StaticMeshComponent : Component
	{
		// Persistent mesh identity (docs/DataModelDesign.md, FD-09). The render world resolves it to
		// renderer geometry when the component changes; an unusable reference draws nothing.
		AssetRef<MeshAsset> Mesh;
		Vec4 Tint{ 1.0f, 1.0f, 1.0f, 1.0f };

		RAVEN_COMPONENT(Raven::ECS::StaticMeshComponent, Mesh, Tint);
	};
}
