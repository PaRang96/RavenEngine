#pragma once

#include "Raven/Renderer/RenderWorld.hpp"
#include "Raven/Renderer/MeshHandle.hpp"
#include "Raven/Math/Transform.hpp"

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace Raven
{
	namespace ECS
	{
		class Registry;
	}

	// GPU record of one object slot (docs/GpuDrivenDesign.md, G2). Shaders/GpuScene.hlsli mirrors it.
	struct GpuObjectData
	{
		// Rows of the 3x4 local-to-world matrix.
		std::array<Vec4, 3> WorldRows;
		// Columns of the normal matrix.
		std::array<Vec4, 3> NormalToWorld;
		Vec4 Tint;
		// Index into the per-mesh draw groups and their mesh table entries.
		std::uint32_t Group = 0;
		std::uint32_t Material = 0;
		// Bit 0: hidden (a non-finite or non-affine transform).
		std::uint32_t Flags = 0;
		std::uint32_t Padding = 0;
	};

	static_assert(std::is_trivially_copyable_v<GpuObjectData>);
	static_assert(sizeof(GpuObjectData) == 128);
	static_assert(offsetof(GpuObjectData, Group) == 112);

	struct GpuMaterialData
	{
		Vec4 BaseColor;
		// Element of the bindless texture array (G4).
		std::uint32_t Texture = 0;
		std::uint32_t Lit = 0;
		std::uint32_t Padding[2]{};
	};

	static_assert(std::is_trivially_copyable_v<GpuMaterialData>);
	static_assert(sizeof(GpuMaterialData) == 32);

	inline constexpr std::uint32_t GpuObjectHidden = 1;

	// The pipeline an object draws with (docs/RenderingContentDesign.md, B0 contract 3): the
	// material shader, blend class, cull mode and vertex offset. Every lit object uses the default
	// until B4's material shaders, but draw groups and command ranges are keyed on it already.
	struct DrawPipelineKey
	{
		std::uint32_t Shader = 0;
		std::uint8_t Blend = 0;
		std::uint8_t Cull = 0;
		std::uint8_t Offset = 0;
		std::uint8_t Padding = 0;

		auto operator<=>(const DrawPipelineKey&) const = default;
	};

	struct DrawGroupKey
	{
		DrawPipelineKey Pipeline;
		MeshHandle Mesh;

		bool operator==(const DrawGroupKey&) const = default;
	};

	struct DrawGroupKeyHash
	{
		std::size_t operator()(const DrawGroupKey& key) const noexcept
		{
			const auto pipeline = std::size_t{ key.Pipeline.Shader } << 24 ^ std::size_t{ key.Pipeline.Blend } << 16 ^
				std::size_t{ key.Pipeline.Cull } << 8 ^ key.Pipeline.Offset;
			return std::hash<MeshHandle>{}(key.Mesh) ^ pipeline * 0x9e3779b97f4a7c15ull;
		}
	};

	// One draw group: the objects of one (pipeline, mesh), drawn by one indirect command.
	struct DrawGroup
	{
		DrawGroupKey Key;
		// Live objects; a retired group has none and sits on the free list.
		std::uint32_t Count = 0;
		bool Live = false;
	};

	struct RenderWorld::State
	{
		static constexpr std::uint32_t NoSlot = 0xffffffffu;

		// The scene this world mirrors. A different scene or registry rebuilds the world.
		const Scene* Source = nullptr;
		ECS::Registry* Registry = nullptr;
		std::uint32_t SceneId = 0;
		// Lifecycle subscriptions: static mesh, world transform, material.
		std::array<std::uint32_t, 3> Subscriptions{};
		// The change tick this world has consumed.
		std::uint32_t Since = 0;
		// Process-wide unique, renewed on every rebuild and clear; a renderer re-uploads
		// everything when the world's address or generation changes.
		std::uint64_t Generation = 0;

		// Dense object slots; removal moves the last slot into the gap.
		std::vector<GpuObjectData> Objects;
		std::vector<ECS::Entity> Entities;
		// Slot per entity index, or NoSlot.
		std::vector<std::uint32_t> SlotOfIndex;

		// Slots changed since a renderer last committed an upload. Entries past the object
		// count are stale and skipped.
		std::vector<std::uint32_t> Dirty;
		std::vector<std::uint8_t> DirtyFlags;

		// Append-only within a generation.
		std::vector<GpuMaterialData> Materials;

		// Draw groups by (pipeline, mesh) (B0 contract 3). A group whose count reaches zero is
		// retired to the free list and reused for the next new key, so the IDs objects hold stay
		// stable.
		std::vector<DrawGroup> Groups;
		std::vector<std::uint32_t> FreeGroups;
		std::unordered_map<DrawGroupKey, std::uint32_t, DrawGroupKeyHash> GroupOfKey;
		// LayoutRevision changes when a group is added, retired or reused, which the GPU mesh table
		// and command order follow; CountRevision also changes with any group's count, which moves
		// the instance regions.
		std::uint64_t LayoutRevision = 0;
		std::uint64_t CountRevision = 0;
		// The mesh reference each slot resolved, parallel to Objects, so a Tint write never
		// re-resolves its mesh.
		std::vector<AssetRef<MeshAsset>> MeshRefs;

		void MarkDirty(std::uint32_t slot)
		{
			if (slot >= DirtyFlags.size())
				DirtyFlags.resize(Objects.size(), 0);
			if (DirtyFlags[slot])
				return;
			Dirty.push_back(slot);
			DirtyFlags[slot] = 1;
		}

		// A renderer calls this once an upload containing every dirty slot was submitted.
		void CommitChanges() noexcept
		{
			for (const auto slot : Dirty)
				if (slot < DirtyFlags.size())
					DirtyFlags[slot] = 0;
			Dirty.clear();
		}
	};
}
