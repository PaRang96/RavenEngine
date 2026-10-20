#pragma once

#include "Raven/Assets/AssetRef.hpp"
#include "Raven/ECS/Component.hpp"
#include "Raven/Math/Transform.hpp"
#include "Raven/Renderer/MeshHandle.hpp"

#include <cstdint>

namespace Raven::ECS
{
	struct TransformComponent : Component
	{
		Vec3 Position{};
		// Radians; local X, then Y, then Z rotation with column vectors.
		Vec3 Rotation{};
		Vec3 Scale{ 1.0f, 1.0f, 1.0f };

		RAVEN_COMPONENT(Raven::ECS::TransformComponent, Position, Rotation, Scale);

		Mat4 GetMatrix() const
		{
			return Translation(Position) * RotationZ(Rotation.Z) * RotationY(Rotation.Y) *
				RotationX(Rotation.X) * Raven::Scale(Scale);
		}
	};

	struct StaticMeshComponent : Component
	{
		// Runtime identity only. The renderer owns immutable geometry until shutdown.
		MeshHandle Mesh;
		Vec4 Tint{ 1.0f, 1.0f, 1.0f, 1.0f };

		RAVEN_COMPONENT(Raven::ECS::StaticMeshComponent, Mesh, Tint);
	};

	enum class SpriteBlendMode : std::uint8_t
	{
		Opaque,
		Alpha
	};

	struct SpriteComponent : Component
	{
		// Persistent identity only; the asset cache resolves the renderer-owned texture.
		AssetRef<TextureAsset> Texture;
		Vec4 Tint{ 1.0f, 1.0f, 1.0f, 1.0f };
		// Alpha uses straight-alpha blending; opaque sprites use depth testing.
		SpriteBlendMode BlendMode = SpriteBlendMode::Alpha;
		// Alpha sprites sort by layer/order, then camera depth. Higher values draw later.
		std::int32_t SortLayer = 0;
		std::int32_t SortOrder = 0;

		RAVEN_COMPONENT(Raven::ECS::SpriteComponent, Texture, Tint, BlendMode, SortLayer, SortOrder);
	};
}
