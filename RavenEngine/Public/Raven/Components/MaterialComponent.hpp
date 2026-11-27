#pragma once

#include "Raven/ECS/Component.hpp"
#include "Raven/Assets/AssetRef.hpp"
#include "Raven/Math/Transform.hpp"

namespace Raven::ECS
{
	struct MaterialComponent : Component
	{
		Vec4 BaseColor{ 1.0f, 1.0f, 1.0f, 1.0f };
		AssetRef<TextureAsset> BaseColorTexture;
		bool Lit = false;
		// A shared .ravenmaterial file (docs/RenderingContentDesign.md, FD-18). When set, it replaces
		// the inline values above, which stay the material of entities without one.
		AssetRef<MaterialAsset> Material;

		RAVEN_COMPONENT(Raven::ECS::MaterialComponent, BaseColor, BaseColorTexture, Lit, Material);
	};
}
