#pragma once

#include "Physics/JoltSupport.hpp"
#include "Raven/ECS/Registry.hpp"

#include <Jolt/Physics/Character/CharacterVirtual.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace JPH
{
	class PhysicsSystem;
	class TempAllocator;
}

namespace Raven
{
	class Scene;
}

namespace Raven::Physics
{
	class CharacterGrid;
	class InnerBodyFilter;

	// The character backend of MoverComponent (docs/GameServicesDesign.md, FD-47): one Jolt virtual
	// character per mover, with an inner kinematic body so sensors, contacts and queries find it.
	// Owned by the physics world, which calls Sync after its own body sync, Update right before the
	// simulation step, and WriteBack after it.
	class CharacterMovers
	{
	public:
		// Pools borrowed for one sync; the physics world reads them through its registry access.
		struct Pools
		{
			ECS::Detail::PoolView Movers;
			ECS::Detail::PoolView Transforms;
			ECS::Detail::PoolView Colliders;
			ECS::Detail::PoolView Bodies;
		};

		// Shared with the physics world: the persistent ID of every body (for contact events), and
		// a callback that ends a removed entity's contacts.
		struct Links
		{
			std::unordered_map<std::uint32_t, std::uint64_t>& BodyIds;
			std::function<void(std::uint64_t id)> EndContacts;
		};

		explicit CharacterMovers(JPH::PhysicsSystem& system) noexcept;
		~CharacterMovers();

		// Removes every character.
		void Clear(Links& links) noexcept;
		// Brings the characters in line with the movers: every mover on a rebuild, otherwise the
		// lifecycle entries plus movers whose component or transform changed after `since`.
		void Sync(Scene& scene, ECS::Registry& registry, const Pools& pools, std::span<const ECS::LifecycleEntry> entries,
			bool rebuild, std::uint32_t since, JPH::TempAllocator& temporary, Links& links);
		// Steers and moves every character by one step, in persistent-ID order.
		void Update(float seconds, JPH::TempAllocator& temporary);
		// Positions, velocities and ground state into the components.
		void WriteBack(ECS::Registry& registry);

		std::uint32_t GetCount() const noexcept;
		std::uint32_t GetRejected() const noexcept;

		CharacterMovers(const CharacterMovers&) = delete;
		CharacterMovers& operator=(const CharacterMovers&) = delete;

	private:
		struct Entry
		{
			ECS::Entity Target;
			std::uint64_t Id = 0;
			// Null while the mover is rejected.
			JPH::Ref<JPH::CharacterVirtual> Character;
			// The component as last applied, with the values last written back.
			ECS::MoverComponent Mover;
			Vec3 Position{};
			std::string Problem;
		};

		bool Evaluate(Scene& scene, const Pools& pools, ECS::Entity entity, ECS::MoverComponent& mover,
			Vec3& position, std::string& problem) const;
		void Report(Scene& scene, Entry& entry, const std::string& problem);
		void Create(Entry& entry, Links& links);
		void Destroy(Entry& entry, Links& links) noexcept;
		void Apply(Scene& scene, const Pools& pools, std::uint32_t slot, JPH::TempAllocator& temporary, Links& links);
		void Remove(std::uint32_t slot, Links& links);

		JPH::PhysicsSystem& m_System;
		// Character-vs-character collision through a grid of nearby characters.
		std::unique_ptr<CharacterGrid> m_Characters;
		// Every character's inner body, by body index: character queries skip them, because
		// characters meet each other only through the grid.
		std::unique_ptr<InnerBodyFilter> m_InnerBodies;
		std::vector<Entry> m_Entries;
		std::vector<std::uint32_t> m_SlotOfIndex;
		// Slots in persistent-ID order, rebuilt when membership changes (FD-48).
		std::vector<std::uint32_t> m_Order;
		bool m_OrderDirty = false;
	};
}
