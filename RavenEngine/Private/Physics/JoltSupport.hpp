#pragma once

// Shared by the physics and pick worlds, the only code that includes Jolt
// (docs/GameServicesDesign.md, FD-32 and FD-34). Jolt.h comes first among Jolt headers.
#include <Jolt/Jolt.h>

#include <Jolt/Math/Mat44.h>
#include <Jolt/Math/Quat.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>

#include "Raven/Components/PhysicsComponents.hpp"
#include "Raven/Math/Transform.hpp"

#include <algorithm>
#include <cmath>
#include <string>

namespace Raven::Physics
{
	// Jolt's process-wide state: its allocator hooks, type factory and log hooks. Each world holds
	// one reference for its lifetime.
	void AcquireJolt();
	void ReleaseJolt() noexcept;

	// Object layers of the physics world: the low bits are the game's collision layer, one bit marks
	// bodies that move (dynamic, kinematic, sensors and the bodies of movers).
	constexpr JPH::ObjectLayer MovingBit = ECS::PhysicsLayerCount;
	constexpr JPH::ObjectLayer GameLayerMask = ECS::PhysicsLayerCount - 1;
	static_assert((ECS::PhysicsLayerCount & (ECS::PhysicsLayerCount - 1)) == 0);

	inline JPH::ObjectLayer MakeObjectLayer(std::uint8_t layer, bool moving) noexcept
	{
		return static_cast<JPH::ObjectLayer>(layer | (moving ? MovingBit : 0));
	}

	inline JPH::Vec3 ToJolt(Vec3 value) noexcept { return JPH::Vec3(value.X, value.Y, value.Z); }
	inline Vec3 FromJolt(JPH::Vec3Arg value) noexcept { return { value.GetX(), value.GetY(), value.GetZ() }; }
	inline JPH::Quat ToJolt(Quat value) noexcept { return JPH::Quat(value.X, value.Y, value.Z, value.W).Normalized(); }
	inline Quat FromJolt(JPH::QuatArg value) noexcept { return { value.GetX(), value.GetY(), value.GetZ(), value.GetW() }; }

	inline bool IsFinite(Vec3 value) noexcept
	{
		return std::isfinite(value.X) && std::isfinite(value.Y) && std::isfinite(value.Z);
	}

	inline bool SameVector(Vec3 left, Vec3 right) noexcept
	{
		return left.X == right.X && left.Y == right.Y && left.Z == right.Z;
	}

	inline bool NearlyEqual(float left, float right) noexcept
	{
		return std::abs(left - right) <= 1e-4f * std::max(std::abs(left), std::abs(right));
	}

	inline std::string ToText(const JPH::String& text)
	{
		return std::string(text.data(), text.size());
	}

	// A pose and scale in world space.
	struct Placement
	{
		Vec3 Position{};
		Quat Rotation{};
		Vec3 Scale{ 1.0f, 1.0f, 1.0f };
	};

	// Translation, rotation and signed axis scale of an affine matrix; a mirrored matrix gets a
	// negative Z scale. Shear (a rotated child of a non-uniformly scaled parent) has no exact
	// rotation, so the axes are orthogonalized. False for a zero or non-finite axis, and for X and Y
	// axes so nearly parallel that no Y direction remains.
	inline bool Decompose(const Mat4& world, Placement& result) noexcept
	{
		Vec3 axes[3];
		for (std::size_t column = 0; column < 3; ++column)
			axes[column] = { world(0, column), world(1, column), world(2, column) };
		const float lengths[3]{ std::sqrt(Dot(axes[0], axes[0])), std::sqrt(Dot(axes[1], axes[1])),
			std::sqrt(Dot(axes[2], axes[2])) };
		for (const float length : lengths)
			if (!(length > 0.0f) || !std::isfinite(length))
				return false;
		const Vec3 x = axes[0] * (1.0f / lengths[0]);
		// Not Normalize, which throws: a throw in this noexcept function would end the process.
		// The limit is Normalize's own.
		const Vec3 orthogonal = axes[1] - x * Dot(x, axes[1]);
		const float orthogonalSquared = Dot(orthogonal, orthogonal);
		if (!std::isfinite(orthogonalSquared) || orthogonalSquared <= 1.0e-12f)
			return false;
		const Vec3 y = orthogonal * (1.0f / std::sqrt(orthogonalSquared));
		const Vec3 z = Cross(x, y);
		result.Position = { world(0, 3), world(1, 3), world(2, 3) };
		result.Scale = { lengths[0], lengths[1], Dot(z, axes[2]) < 0.0f ? -lengths[2] : lengths[2] };
		const JPH::Mat44 rotation(JPH::Vec4(x.X, x.Y, x.Z, 0.0f), JPH::Vec4(y.X, y.Y, y.Z, 0.0f),
			JPH::Vec4(z.X, z.Y, z.Z, 0.0f), JPH::Vec4(0.0f, 0.0f, 0.0f, 1.0f));
		result.Rotation = FromJolt(rotation.GetQuaternion().Normalized());
		return IsFinite(result.Position) && IsFinite({ result.Rotation.X, result.Rotation.Y, result.Rotation.Z });
	}
}
