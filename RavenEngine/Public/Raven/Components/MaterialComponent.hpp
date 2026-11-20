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

		RAVEN_COMPONENT(Raven::ECS::MaterialComponent, BaseColor, BaseColorTexture, Lit);
	};
}
