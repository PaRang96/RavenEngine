#pragma once

#include "Raven/Core/Api.hpp"
#include "Raven/Physics/PhysicsQueries.hpp"
#include "Raven/Renderer/DebugLines.hpp"

#include <cstdint>
#include <memory>
#include <vector>

namespace Raven
{
	class Scene;
	struct PhysicsLimits;
	class JobSystem;

	namespace ECS
	{
		class Registry;
	}

	struct PhysicsStats
	{
		std::uint32_t Bodies = 0;
		std::uint32_t ActiveBodies = 0;
		// Movers with a character (FD-47); their inner bodies are counted in Bodies.
		std::uint32_t Movers = 0;
		// Colliders and movers left out of the world because their values or placement are invalid.
		std::uint32_t Rejected = 0;
		// Contact events published by the latest step.
		std::uint32_t Contacts = 0;
		// Nanoseconds of the latest step: sync, simulation and write-back.
		std::int64_t StepNanoseconds = 0;
	};

	// The host-owned physics world of the active scene (docs/GameServicesDesign.md, FD-32 and FD-47);
	// with the pick world, the only code that includes Jolt. Components are the source of truth: each
	// fixed step brings the Jolt bodies and movers' characters in line with ColliderComponent,
	// RigidBodyComponent, MoverComponent and the transforms that changed since the previous step,
	// moves the characters, simulates, and writes positions and velocities back.
	// Its own write-back is never read as a game edit. Game module replacement never touches it; a
	// new scene rebuilds it from the components in one batch.
	class PhysicsWorld final : public PhysicsQueries
	{
	public:
		// workers: the host's shared job system (docs/JobSystemDesign.md). Without one, the world starts
		// a pool of its own, for tests and tools.
		RAVEN_API explicit PhysicsWorld(const PhysicsLimits& limits, JobSystem* workers = nullptr);
		RAVEN_API ~PhysicsWorld();

		// Forgets the scene and rebuilds Jolt's world from scratch: body IDs, broad-phase trees, contact
		// caches and characters start as in a new world, so every scene start simulates exactly like the
		// first (determinism level 1, docs/GameServicesDesign.md FD-48). The next step adds the scene's
		// bodies again from the components.
		RAVEN_API void Clear();
		// One fixed step of `seconds`, after the game's fixed-step systems and their commands.
		RAVEN_API void Step(Scene& scene, ECS::Registry& registry, double seconds);
		// Contacts are visible only to fixed-step systems.
		void SetFixedPhase(bool fixed) noexcept { m_FixedPhase = fixed; }
		RAVEN_API PhysicsStats GetStats() const noexcept;

		// The collider view (F10): body shapes colored by state (static grey, kinematic green,
		// dynamic yellow, sleeping red), plus the contact manifolds of the latest step while it is on.
		RAVEN_API void SetColliderView(bool enabled) noexcept;
		bool IsColliderViewEnabled() const noexcept { return m_ColliderView; }
		// Appends the collider view's lines; nothing when the view is off or this build has no
		// debug renderer.
		RAVEN_API void DrawColliders(std::vector<DebugLineVertex>& lines) const;

		RAVEN_API std::optional<PhysicsHit> CastRay(Vec3 origin, Vec3 direction, float maxDistance,
			std::uint32_t layers = ECS::AllPhysicsLayers) const override;
		RAVEN_API std::optional<PhysicsHit> CastShape(const PhysicsShape& shape, Vec3 direction, float maxDistance,
			std::uint32_t layers = ECS::AllPhysicsLayers) const override;
		RAVEN_API std::span<const ECS::Entity> Overlap(const PhysicsShape& shape,
			std::uint32_t layers = ECS::AllPhysicsLayers) const override;
		std::span<const ContactEvent> GetContacts() const noexcept override
		{
			return m_FixedPhase ? std::span<const ContactEvent>(m_Contacts) : std::span<const ContactEvent>();
		}

		PhysicsWorld(const PhysicsWorld&) = delete;
		PhysicsWorld& operator=(const PhysicsWorld&) = delete;

	private:
		// Step without its recovery: a throw leaves the mirror half-synced, and Step clears it.
		void StepWorld(Scene& scene, ECS::Registry& registry, double seconds);
		friend class PhysicsWorldSync;
		struct Impl;
		// The workers outlive every rebuilt world: the host's, or a pool of this world's own.
		std::unique_ptr<JobSystem> m_OwnWorkers;
		JobSystem* m_Workers = nullptr;
		std::unique_ptr<Impl> m_Impl;
		std::vector<ContactEvent> m_Contacts;
		bool m_FixedPhase = false;
		bool m_ColliderView = false;
	};
}
