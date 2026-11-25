#include "Physics/PhysicsWorld.hpp"

#include "Raven/Components/PhysicsComponents.hpp"
#include "Raven/Components/TransformComponent.hpp"
#include "Raven/Core/Log.hpp"
#include "Raven/ECS/Registry.hpp"
#include "Raven/Scene/Scene.hpp"
#include "Raven/Scene/SceneProject.hpp"

#include "Core/JobSystem.hpp"
#include "Physics/CharacterMovers.hpp"
#include "Physics/JoltJobs.hpp"
#include "Physics/JoltSupport.hpp"

#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Collision/NarrowPhaseQuery.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/ShapeCast.h>
#include <Jolt/Physics/Constraints/ContactConstraintManager.h>
#include <Jolt/Physics/PhysicsSystem.h>
#ifdef JPH_DEBUG_RENDERER
#include <Jolt/Renderer/DebugRendererSimple.h>
#endif

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <format>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <unordered_set>

namespace Raven
{
	using namespace Physics;

	namespace
	{
		using ECS::Entity;
		using Pool = ECS::Detail::PoolView;

		// Broad phase: static bodies in one tree, moving bodies in the other.
		const JPH::BroadPhaseLayer StaticTree(0);
		const JPH::BroadPhaseLayer MovingTree(1);

		class BroadPhaseLayers final : public JPH::BroadPhaseLayerInterface
		{
		public:
			JPH::uint GetNumBroadPhaseLayers() const override { return 2; }
			JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override
			{
				return (layer & MovingBit) ? MovingTree : StaticTree;
			}
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
			const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override
			{
				return layer == MovingTree ? "Moving" : "Static";
			}
#endif
		};

		// Every game layer collides with every other; static bodies never test against each other.
		class ObjectVsTree final : public JPH::ObjectVsBroadPhaseLayerFilter
		{
		public:
			bool ShouldCollide(JPH::ObjectLayer layer, JPH::BroadPhaseLayer tree) const override
			{
				return (layer & MovingBit) || tree == MovingTree;
			}
		};

		class ObjectPairs final : public JPH::ObjectLayerPairFilter
		{
		public:
			bool ShouldCollide(JPH::ObjectLayer first, JPH::ObjectLayer second) const override
			{
				return (first & MovingBit) || (second & MovingBit);
			}
		};

		class LayerMask final : public JPH::ObjectLayerFilter
		{
		public:
			explicit LayerMask(std::uint32_t mask) noexcept : m_Mask(mask) {}
			bool ShouldCollide(JPH::ObjectLayer layer) const override
			{
				return ((m_Mask >> (layer & GameLayerMask)) & 1u) != 0;
			}

		private:
			std::uint32_t m_Mask;
		};

		class NoSensors final : public JPH::BodyFilter
		{
		public:
			bool ShouldCollideLocked(const JPH::Body& body) const override { return !body.IsSensor(); }
		};

		// The Jolt shape of a collider at a world scale, or the reason it cannot be built.
		JPH::ShapeRefC MakeShape(const ECS::ColliderComponent& collider, Vec3 scale, Vec3 offset, std::string& problem)
		{
			const Vec3 magnitude{ std::abs(scale.X), std::abs(scale.Y), std::abs(scale.Z) };
			if (!IsFinite(collider.Size) || !IsFinite(collider.Offset) || !IsFinite(scale))
			{
				problem = "Size, Offset and scale must be finite";
				return nullptr;
			}
			JPH::ShapeSettings::ShapeResult result;
			JPH::Quat rotation = JPH::Quat::sIdentity();
			switch (collider.Shape)
			{
			case ECS::ColliderShape::Box:
			{
				const JPH::Vec3 half(collider.Size.X * magnitude.X, collider.Size.Y * magnitude.Y,
					collider.Size.Z * magnitude.Z);
				if (!(half.ReduceMin() > 0.0f))
				{
					problem = "a box needs a positive Size on every axis and a non-zero scale";
					return nullptr;
				}
				result = JPH::BoxShapeSettings(half, std::min(JPH::cDefaultConvexRadius, 0.5f * half.ReduceMin())).Create();
				break;
			}
			case ECS::ColliderShape::Sphere:
			{
				if (!NearlyEqual(magnitude.X, magnitude.Y) || !NearlyEqual(magnitude.X, magnitude.Z))
				{
					problem = "a sphere needs a uniform scale";
					return nullptr;
				}
				const float radius = collider.Size.X * magnitude.X;
				if (!(radius > 0.0f))
				{
					problem = "a sphere needs a positive Size.X (radius)";
					return nullptr;
				}
				result = JPH::SphereShapeSettings(radius).Create();
				break;
			}
			case ECS::ColliderShape::Capsule:
			{
				if (!NearlyEqual(magnitude.X, magnitude.Y))
				{
					problem = "a capsule needs equal X and Y scale";
					return nullptr;
				}
				const float radius = collider.Size.X * magnitude.X;
				const float halfLength = collider.Size.Z * magnitude.Z;
				if (!(radius > 0.0f) || !(halfLength >= 0.0f))
				{
					problem = "a capsule needs a positive Size.X (radius) and a Size.Z (half length) of zero or more";
					return nullptr;
				}
				if (halfLength > 0.0f)
				{
					// Jolt's capsule lies along Y; Raven's along local Z.
					result = JPH::CapsuleShapeSettings(halfLength, radius).Create();
					rotation = JPH::Quat::sRotation(JPH::Vec3::sAxisX(), 0.5f * JPH::JPH_PI);
				}
				else
					result = JPH::SphereShapeSettings(radius).Create();
				break;
			}
			default:
				problem = "Shape is not Box, Sphere or Capsule";
				return nullptr;
			}
			if (result.HasError())
			{
				problem = ToText(result.GetError());
				return nullptr;
			}
			JPH::ShapeRefC shape = result.Get();
			if (offset.X != 0.0f || offset.Y != 0.0f || offset.Z != 0.0f || !rotation.IsClose(JPH::Quat::sIdentity()))
			{
				result = JPH::RotatedTranslatedShapeSettings(ToJolt(offset), rotation, shape).Create();
				if (result.HasError())
				{
					problem = ToText(result.GetError());
					return nullptr;
				}
				shape = result.Get();
			}
			return shape;
		}

		// Raw contact callbacks from Jolt's worker threads, ordered after the step.
		struct RawContact
		{
			ContactEventKind Kind = ContactEventKind::Begin;
			JPH::BodyID First;
			JPH::BodyID Second;
			std::uint64_t FirstId = 0;
			std::uint64_t SecondId = 0;
			bool Sensor = false;
			Vec3 Point{};
			Vec3 Normal{};
		};

		class ContactRecorder final : public JPH::ContactListener
		{
		public:
			void OnContactAdded(const JPH::Body& first, const JPH::Body& second, const JPH::ContactManifold& manifold,
				JPH::ContactSettings&) override
			{
				RawContact contact;
				contact.First = first.GetID();
				contact.Second = second.GetID();
				contact.FirstId = first.GetUserData();
				contact.SecondId = second.GetUserData();
				contact.Sensor = first.IsSensor() || second.IsSensor();
				contact.Point = FromJolt(manifold.mRelativeContactPointsOn1.empty() ? JPH::Vec3(manifold.mBaseOffset)
					: JPH::Vec3(manifold.GetWorldSpaceContactPointOn1(0)));
				contact.Normal = FromJolt(manifold.mWorldSpaceNormal);
				std::lock_guard lock(m_Mutex);
				m_Contacts.push_back(contact);
			}

			void OnContactRemoved(const JPH::SubShapeIDPair& pair) override
			{
				RawContact contact;
				contact.Kind = ContactEventKind::End;
				contact.First = pair.GetBody1ID();
				contact.Second = pair.GetBody2ID();
				std::lock_guard lock(m_Mutex);
				m_Contacts.push_back(contact);
			}

			// After the step, on the host thread.
			std::vector<RawContact>& Take() noexcept { return m_Contacts; }

		private:
			std::mutex m_Mutex;
			std::vector<RawContact> m_Contacts;
		};

#ifdef JPH_DEBUG_RENDERER
		// Jolt's debug renderer, recording lines. Contact manifolds are drawn from worker threads
		// during the step, so recording locks.
		class LineRecorder final : public JPH::DebugRendererSimple
		{
		public:
			static constexpr std::size_t MaxVertices = 1u << 21;

			void DrawLine(JPH::RVec3Arg from, JPH::RVec3Arg to, JPH::ColorArg color) override
			{
				std::lock_guard lock(m_Mutex);
				auto& lines = *m_Target;
				if (lines.size() + 2 > MaxVertices)
					return;
				lines.push_back({ FromJolt(JPH::Vec3(from)), color.GetUInt32() });
				lines.push_back({ FromJolt(JPH::Vec3(to)), color.GetUInt32() });
			}

			void DrawTriangle(JPH::RVec3Arg first, JPH::RVec3Arg second, JPH::RVec3Arg third, JPH::ColorArg color,
				ECastShadow) override
			{
				DrawLine(first, second, color);
				DrawLine(second, third, color);
				DrawLine(third, first, color);
			}

			void DrawText3D(JPH::RVec3Arg, const std::string_view&, JPH::ColorArg, float) override {}

			void SetTarget(std::vector<DebugLineVertex>& target) noexcept { m_Target = &target; }

		private:
			std::mutex m_Mutex;
			std::vector<DebugLineVertex>* m_Target = nullptr;
		};
#endif

		struct PairHash
		{
			std::size_t operator()(const std::pair<std::uint64_t, std::uint64_t>& pair) const noexcept
			{
				return std::hash<std::uint64_t>{}(pair.first * 0x9E3779B97F4A7C15ull ^ pair.second);
			}
		};

		std::uint32_t IndexOf(Entity entity) noexcept
		{
			return ECS::Detail::EntityAccess::Index(entity);
		}

	}

	struct PhysicsWorld::Impl
	{
		static constexpr std::uint32_t NoSlot = 0xffffffffu;

		// One collider entity the world knows, with or without a body.
		struct Entry
		{
			Entity Target;
			std::uint64_t Id = 0;
			// Invalid while the entity is rejected or the body limit was reached.
			JPH::BodyID Body;
			// The configuration the body was built from.
			ECS::ColliderComponent Collider;
			ECS::RigidBodyComponent RigidBody;
			bool HasRigidBody = false;
			bool Child = false;
			Placement Applied;
			// Roots: the transform last applied to or written from the body.
			ECS::TransformComponent Transform;
			// Children: the world matrix last applied.
			Mat4 World = Mat4::Identity();
			Vec3 Linear{};
			Vec3 Angular{};
			bool Active = false;
			bool KinematicMoving = false;
			// Why the entity is left out, logged once per distinct reason.
			std::string Problem;
		};

		Impl(const PhysicsLimits& limits, JobSystem* workers)
			: Limits(limits), Temporary(16u << 20),
			  OwnWorkers(workers ? nullptr : std::make_unique<JobSystem>(std::min<std::uint32_t>(JobSystem::DefaultWorkerCount(), 8))),
			  Jobs(workers ? *workers : *OwnWorkers, JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers)
		{
			System.Init(limits.Bodies, 0, limits.BodyPairs, limits.ContactConstraints, Trees, ObjectTrees, Pairs);
			// Z-up world (FD-32).
			System.SetGravity(JPH::Vec3(0.0f, 0.0f, -9.81f));
			System.SetContactListener(&Contacts);
			Movers = std::make_unique<CharacterMovers>(System);
		}

		~Impl()
		{
			RemoveAll();
		}

		// Queues End events for every contact of the entity with this persistent ID.
		void EndContacts(std::uint64_t id)
		{
			for (auto pair = ActivePairs.begin(); pair != ActivePairs.end();)
				if (pair->first == id || pair->second == id)
				{
					ContactEvent end;
					end.Kind = ContactEventKind::End;
					end.A = { pair->first };
					end.B = { pair->second };
					PendingEnds.push_back(end);
					pair = ActivePairs.erase(pair);
				}
				else
					++pair;
			std::erase_if(Dormant, [&](const auto& item) { return item.first.first == id || item.first.second == id; });
		}

		CharacterMovers::Links MoverLinks()
		{
			return { BodyIds, [this](std::uint64_t id) { EndContacts(id); } };
		}

		void RemoveAll() noexcept
		{
			{
				auto links = MoverLinks();
				links.EndContacts = nullptr;
				Movers->Clear(links);
			}
			std::vector<JPH::BodyID> bodies;
			for (const auto& entry : Entries)
				if (!entry.Body.IsInvalid())
					bodies.push_back(entry.Body);
			if (!bodies.empty())
			{
				auto& interface = System.GetBodyInterfaceNoLock();
				interface.RemoveBodies(bodies.data(), static_cast<int>(bodies.size()));
				interface.DestroyBodies(bodies.data(), static_cast<int>(bodies.size()));
			}
			Entries.clear();
			SlotOfIndex.clear();
			BodyIds.clear();
			ActivePairs.clear();
			Dormant.clear();
			Contacts.Take().clear();
			if (Registry)
				for (const auto subscription : Subscriptions)
					Registry->UnsubscribeLifecycle(subscription);
			Registry = nullptr;
			Source = nullptr;
			Subscriptions = {};
			Since = 0;
		}

		PhysicsLimits Limits;
		BroadPhaseLayers Trees;
		ObjectVsTree ObjectTrees;
		ObjectPairs Pairs;
		JPH::TempAllocatorImplWithMallocFallback Temporary;
		// The host's shared workers, or a pool of the world's own when none is given (tests, tools).
		std::unique_ptr<JobSystem> OwnWorkers;
		Physics::JoltJobs Jobs;
		JPH::PhysicsSystem System;
		ContactRecorder Contacts;
		// Movers' characters (FD-47); destroyed before the system they live in.
		std::unique_ptr<CharacterMovers> Movers;
#ifdef JPH_DEBUG_RENDERER
		std::unique_ptr<LineRecorder> Lines;
		// Contact manifolds drawn during the latest step.
		std::vector<DebugLineVertex> ContactLines;
#endif

		Scene* Source = nullptr;
		ECS::Registry* Registry = nullptr;
		// Lifecycle subscriptions: collider, rigid body, transform, mover.
		std::array<std::uint32_t, 4> Subscriptions{};
		std::uint32_t Since = 0;

		std::vector<Entry> Entries;
		std::vector<std::uint32_t> SlotOfIndex;
		// Persistent entity ID per body, for contact removal callbacks.
		std::unordered_map<std::uint32_t, std::uint64_t> BodyIds;
		std::unordered_set<std::pair<std::uint64_t, std::uint64_t>, PairHash> ActivePairs;
		// Touching pairs whose contact Jolt dropped because a body fell asleep (Jolt reports that as a
		// removal). They stay touching for the game until both are awake and their bounds separate.
		std::unordered_map<std::pair<std::uint64_t, std::uint64_t>, std::pair<JPH::BodyID, JPH::BodyID>, PairHash> Dormant;
		// End events for removed bodies, published with the next step's contacts.
		std::vector<ContactEvent> PendingEnds;
		JPH::EPhysicsUpdateError ReportedErrors = JPH::EPhysicsUpdateError::None;
		bool ReportedLimit = false;

		PhysicsStats Stats;
		// Reused by Overlap.
		mutable std::vector<ECS::Entity> OverlapResult;
	};

	// Reads registry pools as RenderWorldSync does; every borrowed pool view is used within one Step,
	// which makes no structural change.
	class PhysicsWorldSync
	{
	public:
		PhysicsWorldSync(PhysicsWorld::Impl& world, Scene& scene, ECS::Registry& registry, double seconds)
			: m_World(world), m_Scene(scene), m_Registry(registry), m_Seconds(static_cast<float>(seconds)),
			  m_Colliders(registry.GetPool(ECS::ComponentTraits<ECS::ColliderComponent>::Info)),
			  m_Bodies(registry.GetPool(ECS::ComponentTraits<ECS::RigidBodyComponent>::Info)),
			  m_Transforms(registry.GetPool(ECS::ComponentTraits<ECS::TransformComponent>::Info)),
			  m_Worlds(registry.GetPool(ECS::ComponentTraits<ECS::WorldTransformComponent>::Info)),
			  m_Interface(world.System.GetBodyInterfaceNoLock())
		{
		}

		// Every collider of the scene again: after a new scene or a lifecycle backlog overflow.
		CharacterMovers::Pools GetMoverPools() const noexcept
		{
			return { m_Registry.GetPool(ECS::ComponentTraits<ECS::MoverComponent>::Info), m_Transforms, m_Colliders,
				m_Bodies };
		}

		void Rebuild()
		{
			auto& world = m_World;
			while (!world.Entries.empty())
				Remove(static_cast<std::uint32_t>(world.Entries.size() - 1));
			for (std::size_t dense = 0; dense < m_Colliders.Count; ++dense)
				if (m_Scene.Owns(m_Colliders.Entities[dense]))
					Reconcile(m_Colliders.Entities[dense]);
		}

		// Brings one entity's entry in line with whether it has a collider and a transform.
		void Reconcile(Entity entity)
		{
			auto& world = m_World;
			const auto index = IndexOf(entity);
			auto slot = index < world.SlotOfIndex.size() ? world.SlotOfIndex[index] : PhysicsWorld::Impl::NoSlot;
			if (slot != PhysicsWorld::Impl::NoSlot && !(world.Entries[slot].Target == entity))
			{
				// The slot index was reused; the old entity is gone.
				if (!m_Registry.IsAlive(world.Entries[slot].Target))
					Remove(slot);
				slot = PhysicsWorld::Impl::NoSlot;
			}
			const bool present = m_Registry.IsAlive(entity) && m_Scene.Owns(entity) &&
				m_Colliders.Find(entity) != Pool::NoIndex && m_Transforms.Find(entity) != Pool::NoIndex;
			if (present && slot == PhysicsWorld::Impl::NoSlot)
				m_Added.push_back(entity);
			else if (present)
				m_Changed.push_back(slot);
			else if (slot != PhysicsWorld::Impl::NoSlot)
				Remove(slot);
		}

		// Entries whose components changed after `since`, excluding ones already queued.
		void ApplyTicks(std::uint32_t since)
		{
			auto& world = m_World;
			for (std::uint32_t slot = 0; slot < world.Entries.size(); ++slot)
			{
				const auto& entry = world.Entries[slot];
				const auto entity = entry.Target;
				const auto collider = m_Colliders.Find(entity);
				const auto body = m_Bodies.Find(entity);
				const auto transform = m_Transforms.Find(entity);
				const auto placed = m_Worlds.Find(entity);
				const bool changed = (collider != Pool::NoIndex && m_Colliders.Ticks[collider] > since) ||
					(body != Pool::NoIndex && m_Bodies.Ticks[body] > since) ||
					(transform != Pool::NoIndex && m_Transforms.Ticks[transform] > since) ||
					(entry.Child && placed != Pool::NoIndex && m_Worlds.Ticks[placed] > since);
				if (changed)
					m_Changed.push_back(slot);
				else if (entry.KinematicMoving && !entry.Body.IsInvalid())
				{
					// A kinematic body keeps its velocity until told otherwise: stop it once its
					// transform stops changing.
					m_Interface.SetLinearAndAngularVelocity(entry.Body, JPH::Vec3::sZero(), JPH::Vec3::sZero());
					world.Entries[slot].KinematicMoving = false;
				}
			}
		}

		// Applies the queued changes and additions. Additions are created in persistent-ID order and
		// added in batches, so two runs build the same broad phase (FD-48).
		void Commit()
		{
			std::sort(m_Changed.begin(), m_Changed.end());
			m_Changed.erase(std::unique(m_Changed.begin(), m_Changed.end()), m_Changed.end());
			for (const auto slot : m_Changed)
				if (slot < m_World.Entries.size())
					Update(slot);
			std::sort(m_Added.begin(), m_Added.end(), [&](Entity left, Entity right)
			{
				return m_Scene.GetRef(left).Id < m_Scene.GetRef(right).Id;
			});
			m_Added.erase(std::unique(m_Added.begin(), m_Added.end()), m_Added.end());
			for (const auto entity : m_Added)
			{
				auto& world = m_World;
				const auto index = IndexOf(entity);
				if (index >= world.SlotOfIndex.size())
					world.SlotOfIndex.resize(index + 1, PhysicsWorld::Impl::NoSlot);
				if (world.SlotOfIndex[index] != PhysicsWorld::Impl::NoSlot)
					continue;
				world.SlotOfIndex[index] = static_cast<std::uint32_t>(world.Entries.size());
				PhysicsWorld::Impl::Entry entry;
				entry.Target = entity;
				entry.Id = m_Scene.GetRef(entity).Id;
				world.Entries.push_back(std::move(entry));
				Build(static_cast<std::uint32_t>(world.Entries.size() - 1));
			}
			AddPending();
			if (!m_Removed.empty())
			{
				m_Interface.RemoveBodies(m_Removed.data(), static_cast<int>(m_Removed.size()));
				m_Interface.DestroyBodies(m_Removed.data(), static_cast<int>(m_Removed.size()));
				m_Removed.clear();
			}
		}

		// After the step: dynamic bodies' transforms and velocities into their components.
		void WriteBack()
		{
			auto& world = m_World;
			std::uint32_t active = 0;
			for (auto& entry : world.Entries)
			{
				if (entry.Body.IsInvalid())
					continue;
				const bool isActive = m_Interface.IsActive(entry.Body);
				active += isActive ? 1u : 0u;
				if (!entry.HasRigidBody || entry.RigidBody.Motion != ECS::MotionType::Dynamic || (!isActive && !entry.Active))
				{
					entry.Active = isActive;
					continue;
				}
				entry.Active = isActive;
				JPH::RVec3 position;
				JPH::Quat rotation;
				m_Interface.GetPositionAndRotation(entry.Body, position, rotation);
				auto* transform = m_Registry.TryGet<ECS::TransformComponent>(entry.Target);
				auto* body = m_Registry.TryGet<ECS::RigidBodyComponent>(entry.Target);
				if (!transform || !body)
					continue;
				ECS::TransformComponent next = entry.Transform;
				next.Position = FromJolt(JPH::Vec3(position));
				// The Euler angles closest to the stored ones, so they never jump (FD-32).
				next.Rotation = EulerFromQuat(FromJolt(rotation), entry.Transform.Rotation);
				*transform = next;
				entry.Transform = next;
				entry.Linear = body->LinearVelocity = FromJolt(m_Interface.GetLinearVelocity(entry.Body));
				entry.Angular = body->AngularVelocity = FromJolt(m_Interface.GetAngularVelocity(entry.Body));
				entry.RigidBody.LinearVelocity = entry.Linear;
				entry.RigidBody.AngularVelocity = entry.Angular;
			}
			world.Stats.ActiveBodies = active;
		}

	private:
		struct Desired
		{
			ECS::ColliderComponent Collider;
			ECS::RigidBodyComponent RigidBody;
			bool HasRigidBody = false;
			bool Child = false;
			ECS::TransformComponent Transform;
			Mat4 World = Mat4::Identity();
			Placement Place;
		};

		ECS::MotionType MotionOf(const Desired& desired) const noexcept
		{
			return desired.HasRigidBody ? desired.RigidBody.Motion : ECS::MotionType::Static;
		}

		// The entity's current components and placement, or the reason they cannot become a body.
		bool Evaluate(Entity entity, Desired& desired, std::string& problem) const
		{
			if (m_Colliders.Find(entity) == Pool::NoIndex || m_Transforms.Find(entity) == Pool::NoIndex)
			{
				problem = "it has no ColliderComponent and TransformComponent";
				return false;
			}
			desired.Collider = m_Colliders.At<ECS::ColliderComponent>(m_Colliders.Find(entity));
			desired.Transform = m_Transforms.At<ECS::TransformComponent>(m_Transforms.Find(entity));
			const auto body = m_Bodies.Find(entity);
			desired.HasRigidBody = body != Pool::NoIndex;
			if (desired.HasRigidBody)
				desired.RigidBody = m_Bodies.At<ECS::RigidBodyComponent>(body);
			desired.Child = !m_Scene.GetParent(entity).IsNull();
			const auto motion = MotionOf(desired);
			if (m_Registry.Has<ECS::MoverComponent>(entity))
			{
				problem = "a mover cannot also have a ColliderComponent or RigidBodyComponent";
				return false;
			}
			if (desired.Collider.Layer >= ECS::PhysicsLayerCount)
			{
				problem = std::format("Layer {} is not below {}", desired.Collider.Layer, ECS::PhysicsLayerCount);
				return false;
			}
			if (motion != ECS::MotionType::Static && motion != ECS::MotionType::Kinematic && motion != ECS::MotionType::Dynamic)
			{
				problem = "Motion is not Static, Kinematic or Dynamic";
				return false;
			}
			if (desired.HasRigidBody)
			{
				const auto& rigid = desired.RigidBody;
				if (motion == ECS::MotionType::Dynamic && (!(rigid.Mass > 0.0f) || !std::isfinite(rigid.Mass)))
				{
					problem = "a dynamic body needs a positive, finite Mass";
					return false;
				}
				if (!(rigid.Friction >= 0.0f) || !std::isfinite(rigid.Friction) || !(rigid.Restitution >= 0.0f) ||
					!(rigid.Restitution <= 1.0f))
				{
					problem = "Friction must be zero or more and Restitution from 0 to 1";
					return false;
				}
				if (!IsFinite(rigid.LinearVelocity) || !IsFinite(rigid.AngularVelocity))
				{
					problem = "velocities must be finite";
					return false;
				}
			}
			if (desired.Child && motion == ECS::MotionType::Dynamic)
			{
				problem = "a dynamic body must be a scene root";
				return false;
			}
			if (desired.Child)
			{
				const auto placed = m_Worlds.Find(entity);
				desired.World = placed != Pool::NoIndex ? m_Worlds.At<ECS::WorldTransformComponent>(placed).World
					: m_Scene.GetWorldTransform(entity);
				if (!Decompose(desired.World, desired.Place))
				{
					problem = "its world transform has a zero or non-finite scale";
					return false;
				}
			}
			else
			{
				if (!IsFinite(desired.Transform.Position) || !IsFinite(desired.Transform.Rotation))
				{
					problem = "Position and Rotation must be finite";
					return false;
				}
				desired.Place = { desired.Transform.Position, QuatFromEuler(desired.Transform.Rotation),
					desired.Transform.Scale };
			}
			return true;
		}

		void Report(PhysicsWorld::Impl::Entry& entry, const std::string& problem)
		{
			if (entry.Problem == problem)
				return;
			entry.Problem = problem;
			const auto name = m_Scene.GetAuthoredId(entry.Target);
			Log(LogLevel::Warning, std::format("Collider on '{}' left out of the physics world: {}",
				name.empty() ? std::format("entity {:016x}", entry.Id) : name, problem));
		}

		// Creates the entry's body from its components; rejected entries keep no body.
		void Build(std::uint32_t slot)
		{
			auto& world = m_World;
			auto& entry = world.Entries[slot];
			Desired desired;
			std::string problem;
			if (!Evaluate(entry.Target, desired, problem))
			{
				Report(entry, problem);
				return;
			}
			const Vec3 offset{ desired.Collider.Offset.X * desired.Place.Scale.X,
				desired.Collider.Offset.Y * desired.Place.Scale.Y, desired.Collider.Offset.Z * desired.Place.Scale.Z };
			const auto shape = MakeShape(desired.Collider, desired.Place.Scale, offset, problem);
			if (!shape)
			{
				Report(entry, problem);
				return;
			}
			if (world.System.GetNumBodies() >= world.Limits.Bodies)
			{
				if (!world.ReportedLimit)
					Log(LogLevel::Warning, std::format("Physics body limit of {} reached; raise physics-limits in the "
						"project file. Further colliders are left out.", world.Limits.Bodies));
				world.ReportedLimit = true;
				return;
			}
			const auto motion = MotionOf(desired);
			// Sensors are kinematic internally, so they still detect sleeping and static bodies (FD-32).
			const auto internalMotion = desired.Collider.Sensor && motion == ECS::MotionType::Static
				? ECS::MotionType::Kinematic : motion;
			const auto joltMotion = internalMotion == ECS::MotionType::Dynamic ? JPH::EMotionType::Dynamic
				: internalMotion == ECS::MotionType::Kinematic ? JPH::EMotionType::Kinematic : JPH::EMotionType::Static;
			JPH::BodyCreationSettings settings(shape, JPH::RVec3(ToJolt(desired.Place.Position)),
				ToJolt(desired.Place.Rotation), joltMotion,
				MakeObjectLayer(desired.Collider.Layer, internalMotion != ECS::MotionType::Static));
			settings.mUserData = entry.Id;
			settings.mIsSensor = desired.Collider.Sensor;
			if (desired.Collider.Sensor)
			{
				settings.mCollideKinematicVsNonDynamic = true;
				settings.mAllowSleeping = false;
			}
			if (desired.HasRigidBody)
			{
				const auto& rigid = desired.RigidBody;
				settings.mFriction = rigid.Friction;
				settings.mRestitution = rigid.Restitution;
				settings.mMotionQuality = rigid.Continuous ? JPH::EMotionQuality::LinearCast : JPH::EMotionQuality::Discrete;
				if (motion == ECS::MotionType::Dynamic)
				{
					settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
					settings.mMassPropertiesOverride.mMass = rigid.Mass;
					settings.mLinearVelocity = ToJolt(rigid.LinearVelocity);
					settings.mAngularVelocity = ToJolt(rigid.AngularVelocity);
				}
			}
			auto* body = m_Interface.CreateBody(settings);
			if (!body)
			{
				if (!world.ReportedLimit)
					Log(LogLevel::Warning, "Physics body limit reached; further colliders are left out");
				world.ReportedLimit = true;
				return;
			}
			if (!entry.Problem.empty())
				Log(LogLevel::Info, std::format("Collider on '{}' is back in the physics world",
					m_Scene.GetAuthoredId(entry.Target)));
			entry.Problem.clear();
			entry.Body = body->GetID();
			entry.Collider = desired.Collider;
			entry.RigidBody = desired.RigidBody;
			entry.HasRigidBody = desired.HasRigidBody;
			entry.Child = desired.Child;
			entry.Applied = desired.Place;
			entry.Transform = desired.Transform;
			entry.World = desired.World;
			entry.Linear = desired.RigidBody.LinearVelocity;
			entry.Angular = desired.RigidBody.AngularVelocity;
			entry.Active = internalMotion != ECS::MotionType::Static;
			entry.KinematicMoving = false;
			world.BodyIds[entry.Body.GetIndexAndSequenceNumber()] = entry.Id;
			m_Pending.push_back({ entry.Body, internalMotion != ECS::MotionType::Static });
		}

		void AddPending()
		{
			// Static bodies and moving bodies go in two batches: only moving bodies start active.
			std::vector<JPH::BodyID> batch;
			for (const bool moving : { false, true })
			{
				batch.clear();
				for (const auto& pending : m_Pending)
					if (pending.second == moving)
						batch.push_back(pending.first);
				if (batch.empty())
					continue;
				const auto state = m_Interface.AddBodiesPrepare(batch.data(), static_cast<int>(batch.size()));
				m_Interface.AddBodiesFinalize(batch.data(), static_cast<int>(batch.size()), state,
					moving ? JPH::EActivation::Activate : JPH::EActivation::DontActivate);
			}
			m_Pending.clear();
		}

		// Queues the body for removal and ends its contacts.
		void DropBody(PhysicsWorld::Impl::Entry& entry)
		{
			if (entry.Body.IsInvalid())
				return;
			auto& world = m_World;
			world.EndContacts(entry.Id);
			world.BodyIds.erase(entry.Body.GetIndexAndSequenceNumber());
			const auto queued = std::find_if(m_Pending.begin(), m_Pending.end(),
				[&](const auto& pending) { return pending.first == entry.Body; });
			if (queued != m_Pending.end())
			{
				// Created this step but not added yet.
				m_Interface.DestroyBody(entry.Body);
				m_Pending.erase(queued);
			}
			else
				m_Removed.push_back(entry.Body);
			entry.Body = JPH::BodyID();
		}

		void Remove(std::uint32_t slot)
		{
			auto& world = m_World;
			DropBody(world.Entries[slot]);
			world.SlotOfIndex[IndexOf(world.Entries[slot].Target)] = PhysicsWorld::Impl::NoSlot;
			const auto last = static_cast<std::uint32_t>(world.Entries.size() - 1);
			if (slot != last)
			{
				world.Entries[slot] = std::move(world.Entries[last]);
				world.SlotOfIndex[IndexOf(world.Entries[slot].Target)] = slot;
				// A moved entry queued for an update keeps its queue position by slot.
				std::replace(m_Changed.begin(), m_Changed.end(), last, slot);
			}
			world.Entries.pop_back();
		}

		// Applies component changes to an existing body: edits become teleports, velocity changes or
		// a new shape; a changed motion type or sensor flag rebuilds the body.
		void Update(std::uint32_t slot)
		{
			auto& world = m_World;
			auto& entry = world.Entries[slot];
			Desired desired;
			std::string problem;
			if (!Evaluate(entry.Target, desired, problem))
			{
				DropBody(entry);
				Report(entry, problem);
				return;
			}
			const auto motion = MotionOf(desired);
			const bool rebuild = entry.Body.IsInvalid() || motion != (entry.HasRigidBody ? entry.RigidBody.Motion
				: ECS::MotionType::Static) || desired.Collider.Sensor != entry.Collider.Sensor ||
				desired.Child != entry.Child;
			if (rebuild)
			{
				DropBody(entry);
				Build(slot);
				return;
			}
			const auto& old = entry.Collider;
			const bool scaleChanged = !SameVector(desired.Place.Scale, entry.Applied.Scale);
			const bool shapeChanged = scaleChanged || desired.Collider.Shape != old.Shape ||
				!SameVector(desired.Collider.Size, old.Size) || !SameVector(desired.Collider.Offset, old.Offset);
			const bool massChanged = motion == ECS::MotionType::Dynamic && desired.RigidBody.Mass != entry.RigidBody.Mass;
			if (shapeChanged || massChanged)
			{
				const Vec3 offset{ desired.Collider.Offset.X * desired.Place.Scale.X,
					desired.Collider.Offset.Y * desired.Place.Scale.Y, desired.Collider.Offset.Z * desired.Place.Scale.Z };
				const auto shape = MakeShape(desired.Collider, desired.Place.Scale, offset, problem);
				if (!shape)
				{
					DropBody(entry);
					Report(entry, problem);
					return;
				}
				m_Interface.SetShape(entry.Body, shape, false, JPH::EActivation::Activate);
				if (motion == ECS::MotionType::Dynamic)
				{
					JPH::BodyLockWrite lock(world.System.GetBodyLockInterfaceNoLock(), entry.Body);
					if (lock.Succeeded())
					{
						auto mass = shape->GetMassProperties();
						mass.ScaleToMass(desired.RigidBody.Mass);
						lock.GetBody().GetMotionProperties()->SetMassProperties(JPH::EAllowedDOFs::All, mass);
					}
				}
			}
			if (desired.Collider.Layer != old.Layer)
				m_Interface.SetObjectLayer(entry.Body, MakeObjectLayer(desired.Collider.Layer,
					motion != ECS::MotionType::Static || desired.Collider.Sensor));
			if (desired.HasRigidBody)
			{
				const auto& rigid = desired.RigidBody;
				if (rigid.Friction != entry.RigidBody.Friction)
					m_Interface.SetFriction(entry.Body, rigid.Friction);
				if (rigid.Restitution != entry.RigidBody.Restitution)
					m_Interface.SetRestitution(entry.Body, rigid.Restitution);
				if (rigid.Continuous != entry.RigidBody.Continuous)
					m_Interface.SetMotionQuality(entry.Body, rigid.Continuous ? JPH::EMotionQuality::LinearCast
						: JPH::EMotionQuality::Discrete);
			}

			// Placement: compared with what physics last applied or wrote, so an unchanged value
			// rewritten by render interpolation's restore is not a teleport.
			const bool moved = desired.Child ? std::memcmp(&desired.World, &entry.World, sizeof(Mat4)) != 0
				: !SameVector(desired.Transform.Position, entry.Transform.Position) ||
					!SameVector(desired.Transform.Rotation, entry.Transform.Rotation);
			if (moved)
			{
				const auto position = JPH::RVec3(ToJolt(desired.Place.Position));
				const auto rotation = ToJolt(desired.Place.Rotation);
				if (motion == ECS::MotionType::Kinematic)
				{
					m_Interface.MoveKinematic(entry.Body, position, rotation, m_Seconds);
					entry.KinematicMoving = true;
				}
				else
					m_Interface.SetPositionAndRotation(entry.Body, position, rotation,
						motion == ECS::MotionType::Dynamic ? JPH::EActivation::Activate : JPH::EActivation::DontActivate);
			}
			if (motion == ECS::MotionType::Dynamic && (!SameVector(desired.RigidBody.LinearVelocity, entry.Linear) ||
				!SameVector(desired.RigidBody.AngularVelocity, entry.Angular)))
			{
				m_Interface.SetLinearAndAngularVelocity(entry.Body, ToJolt(desired.RigidBody.LinearVelocity),
					ToJolt(desired.RigidBody.AngularVelocity));
				m_Interface.ActivateBody(entry.Body);
				entry.Linear = desired.RigidBody.LinearVelocity;
				entry.Angular = desired.RigidBody.AngularVelocity;
			}
			entry.Collider = desired.Collider;
			entry.RigidBody = desired.RigidBody;
			entry.RigidBody.LinearVelocity = entry.Linear;
			entry.RigidBody.AngularVelocity = entry.Angular;
			entry.Applied = desired.Place;
			entry.Transform = desired.Transform;
			entry.World = desired.World;
		}

		PhysicsWorld::Impl& m_World;
		Scene& m_Scene;
		ECS::Registry& m_Registry;
		const float m_Seconds;
		const Pool m_Colliders;
		const Pool m_Bodies;
		const Pool m_Transforms;
		const Pool m_Worlds;
		JPH::BodyInterface& m_Interface;
		std::vector<Entity> m_Added;
		std::vector<std::uint32_t> m_Changed;
		// Created bodies waiting for the batched add, with whether they move.
		std::vector<std::pair<JPH::BodyID, bool>> m_Pending;
		std::vector<JPH::BodyID> m_Removed;
	};

	PhysicsWorld::PhysicsWorld(const PhysicsLimits& limits, JobSystem* workers)
	{
		AcquireJolt();
		try
		{
			m_Impl = std::make_unique<Impl>(limits, workers);
		}
		catch (...)
		{
			ReleaseJolt();
			throw;
		}
	}

	PhysicsWorld::~PhysicsWorld()
	{
		m_Impl.reset();
		ReleaseJolt();
	}

	void PhysicsWorld::Clear() noexcept
	{
		m_Impl->RemoveAll();
		m_Impl->PendingEnds.clear();
		m_Impl->Stats = {};
		m_Contacts.clear();
#ifdef JPH_DEBUG_RENDERER
		m_Impl->ContactLines.clear();
#endif
	}

	void PhysicsWorld::Step(Scene& scene, ECS::Registry& registry, double seconds)
	{
		if (!(seconds > 0.0) || !std::isfinite(seconds))
			throw std::invalid_argument("A physics step needs a positive duration");
		const auto start = std::chrono::steady_clock::now();
		auto& world = *m_Impl;
		bool rebuild = false;
		if (world.Source != &scene || world.Registry != &registry)
		{
			Clear();
			world.Source = &scene;
			world.Registry = &registry;
			world.Subscriptions = {
				registry.SubscribeLifecycle<ECS::ColliderComponent>(),
				registry.SubscribeLifecycle<ECS::RigidBodyComponent>(),
				registry.SubscribeLifecycle<ECS::TransformComponent>(),
				registry.SubscribeLifecycle<ECS::MoverComponent>() };
			rebuild = true;
		}
		std::array<ECS::LifecycleBatch, 4> batches{};
		for (std::size_t index = 0; index < batches.size(); ++index)
		{
			batches[index] = registry.ReadLifecycle(world.Subscriptions[index]);
			rebuild = rebuild || batches[index].Resync;
		}
		const auto since = world.Since;
		{
			PhysicsWorldSync sync(world, scene, registry, seconds);
			if (rebuild)
				sync.Rebuild();
			else
			{
				for (const auto& batch : batches)
					for (const auto& entry : batch.Entries)
						sync.Reconcile(entry.Target);
				sync.ApplyTicks(since);
			}
			sync.Commit();
			// Movers after the bodies they stand on (FD-47), then moved right before the step.
			// Every lifecycle change can change a mover: its transform, and a collider or rigid body
			// that conflicts with it.
			std::vector<ECS::LifecycleEntry> moverEntries;
			for (const auto& batch : batches)
				moverEntries.insert(moverEntries.end(), batch.Entries.begin(), batch.Entries.end());
			auto links = world.MoverLinks();
			world.Movers->Sync(scene, registry, sync.GetMoverPools(), moverEntries, rebuild, since, world.Temporary, links);
		}
		world.Movers->Update(static_cast<float>(seconds), world.Temporary);

#ifdef JPH_DEBUG_RENDERER
		world.ContactLines.clear();
		if (world.Lines)
			world.Lines->SetTarget(world.ContactLines);
		JPH::ContactConstraintManager::sDrawContactManifolds = world.Lines != nullptr;
#endif
		// Jolt advises one collision step per 1/60 s.
		const int collisionSteps = std::max(1, static_cast<int>(std::ceil(seconds * 60.0 - 1e-6)));
		const auto errors = world.System.Update(static_cast<float>(seconds), collisionSteps, &world.Temporary,
			&world.Jobs);
#ifdef JPH_DEBUG_RENDERER
		JPH::ContactConstraintManager::sDrawContactManifolds = false;
#endif
		if (errors != JPH::EPhysicsUpdateError::None && errors != world.ReportedErrors)
		{
			std::string kinds;
			if ((errors & JPH::EPhysicsUpdateError::BodyPairCacheFull) != JPH::EPhysicsUpdateError::None)
				kinds += " body pairs";
			if ((errors & JPH::EPhysicsUpdateError::ManifoldCacheFull) != JPH::EPhysicsUpdateError::None)
				kinds += " contact manifolds";
			if ((errors & JPH::EPhysicsUpdateError::ContactConstraintsFull) != JPH::EPhysicsUpdateError::None)
				kinds += " contact constraints";
			Log(LogLevel::Warning, "Physics step ran out of" + kinds + "; some contacts were ignored. Raise "
				"physics-limits in the project file.");
		}
		world.ReportedErrors = errors;

		// Contacts in a fixed order: by entity pair, then kind (FD-48). A pair begins only when it was
		// not touching and ends only when it was; ends of removed bodies were queued during the sync.
		auto& raw = world.Contacts.Take();
		std::vector<ContactEvent> events;
		events.reserve(raw.size());
		auto& bodies = world.System.GetBodyInterfaceNoLock();
		const auto asleep = [&](const JPH::BodyID& body)
		{
			return bodies.GetMotionType(body) != JPH::EMotionType::Static && !bodies.IsActive(body);
		};
		for (const auto& contact : raw)
		{
			ContactEvent event;
			event.Kind = contact.Kind;
			event.Sensor = contact.Sensor;
			std::uint64_t first = contact.FirstId, second = contact.SecondId;
			if (contact.Kind == ContactEventKind::End)
			{
				const auto a = world.BodyIds.find(contact.First.GetIndexAndSequenceNumber());
				const auto b = world.BodyIds.find(contact.Second.GetIndexAndSequenceNumber());
				if (a == world.BodyIds.end() || b == world.BodyIds.end())
					continue;
				first = a->second;
				second = b->second;
				if (asleep(contact.First) || asleep(contact.Second))
				{
					const std::pair key{ std::min(first, second), std::max(first, second) };
					if (world.ActivePairs.contains(key))
						world.Dormant[key] = { contact.First, contact.Second };
					continue;
				}
			}
			event.Point = contact.Point;
			event.Normal = contact.Normal;
			if (second < first)
			{
				std::swap(first, second);
				event.Normal = event.Normal * -1.0f;
			}
			event.A = { first };
			event.B = { second };
			events.push_back(event);
		}
		raw.clear();
		const auto order = [](const ContactEvent& left, const ContactEvent& right)
		{
			if (left.A.Id != right.A.Id)
				return left.A.Id < right.A.Id;
			if (left.B.Id != right.B.Id)
				return left.B.Id < right.B.Id;
			if (left.Kind != right.Kind)
				return left.Kind < right.Kind;
			if (left.Point.X != right.Point.X)
				return left.Point.X < right.Point.X;
			if (left.Point.Y != right.Point.Y)
				return left.Point.Y < right.Point.Y;
			return left.Point.Z < right.Point.Z;
		};
		std::sort(events.begin(), events.end(), order);
		m_Contacts = std::move(world.PendingEnds);
		world.PendingEnds.clear();
		for (const auto& event : events)
		{
			const std::pair key{ event.A.Id, event.B.Id };
			// A woken pair that still touches is added again by Jolt: it was never ended.
			if (event.Kind == ContactEventKind::Begin)
				world.Dormant.erase(key);
			if (event.Kind == ContactEventKind::Begin ? world.ActivePairs.insert(key).second
				: world.ActivePairs.erase(key) > 0)
				m_Contacts.push_back(event);
		}
		// Dormant pairs end once both bodies are awake and their bounds no longer overlap.
		for (auto item = world.Dormant.begin(); item != world.Dormant.end();)
		{
			const auto [first, second] = item->second;
			if (asleep(first) || asleep(second))
			{
				++item;
				continue;
			}
			auto bounds = bodies.GetTransformedShape(first).GetWorldSpaceBounds();
			bounds.ExpandBy(JPH::Vec3::sReplicate(0.05f));
			if (bounds.Overlaps(bodies.GetTransformedShape(second).GetWorldSpaceBounds()))
			{
				++item;
				continue;
			}
			ContactEvent end;
			end.Kind = ContactEventKind::End;
			end.A = { item->first.first };
			end.B = { item->first.second };
			m_Contacts.push_back(end);
			world.ActivePairs.erase(item->first);
			item = world.Dormant.erase(item);
		}
		std::stable_sort(m_Contacts.begin(), m_Contacts.end(), order);

		{
			PhysicsWorldSync sync(world, scene, registry, seconds);
			sync.WriteBack();
		}
		world.Movers->WriteBack(registry);
		// Its own write-back is never read as a game edit; the render world still sees it (FD-32).
		world.Since = registry.AdvanceChangeTick();
		world.Stats.Bodies = world.System.GetNumBodies();
		world.Stats.Rejected = static_cast<std::uint32_t>(std::count_if(world.Entries.begin(), world.Entries.end(),
			[](const auto& entry) { return entry.Body.IsInvalid(); })) + world.Movers->GetRejected();
		world.Stats.Movers = world.Movers->GetCount();
		world.Stats.Contacts = static_cast<std::uint32_t>(m_Contacts.size());
		world.Stats.StepNanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(
			std::chrono::steady_clock::now() - start).count();
	}

	PhysicsStats PhysicsWorld::GetStats() const noexcept
	{
		return m_Impl->Stats;
	}

	void PhysicsWorld::SetColliderView(bool enabled) noexcept
	{
		m_ColliderView = enabled;
#ifdef JPH_DEBUG_RENDERER
		if (!enabled)
			m_Impl->Lines.reset();
		else if (!m_Impl->Lines && !JPH::DebugRenderer::sInstance)
			m_Impl->Lines = std::make_unique<LineRecorder>();
		m_Impl->ContactLines.clear();
#endif
	}

	void PhysicsWorld::DrawColliders(std::vector<DebugLineVertex>& lines) const
	{
#ifdef JPH_DEBUG_RENDERER
		auto& world = *m_Impl;
		if (!m_ColliderView || !world.Lines)
			return;
		world.Lines->SetTarget(lines);
		JPH::BodyManager::DrawSettings settings;
		settings.mDrawShapeWireframe = true;
		// Static grey, kinematic green, dynamic yellow, sleeping red.
		settings.mDrawShapeColor = JPH::BodyManager::EShapeColor::SleepColor;
		world.System.DrawBodies(settings, world.Lines.get());
		lines.insert(lines.end(), world.ContactLines.begin(), world.ContactLines.end());
		world.Lines->SetTarget(world.ContactLines);
#else
		(void)lines;
#endif
	}

	namespace
	{
		ECS::Entity ResolveBody(const Scene* scene, const JPH::BodyLockInterface& locks, const JPH::BodyID& body)
		{
			if (!scene)
				return {};
			JPH::BodyLockRead lock(locks, body);
			return lock.Succeeded() ? scene->Resolve(ECS::EntityRef{ lock.GetBody().GetUserData() }) : ECS::Entity{};
		}

		JPH::ShapeRefC MakeQueryShape(const PhysicsShape& shape, std::string& problem)
		{
			ECS::ColliderComponent collider;
			collider.Shape = shape.Shape;
			collider.Size = shape.Size;
			return MakeShape(collider, { 1.0f, 1.0f, 1.0f }, {}, problem);
		}

		JPH::RMat44 QueryTransform(const PhysicsShape& shape)
		{
			return JPH::RMat44::sRotationTranslation(ToJolt(QuatFromEuler(shape.Rotation)),
				JPH::RVec3(ToJolt(shape.Position)));
		}
	}

	std::optional<PhysicsHit> PhysicsWorld::CastRay(Vec3 origin, Vec3 direction, float maxDistance,
		std::uint32_t layers) const
	{
		const float length = std::sqrt(Dot(direction, direction));
		if (!(length > 0.0f) || !(maxDistance > 0.0f) || !IsFinite(origin) || !IsFinite(direction) ||
			!std::isfinite(maxDistance))
			return std::nullopt;
		const auto& world = *m_Impl;
		const JPH::RRayCast ray{ JPH::RVec3(ToJolt(origin)), ToJolt(direction * (maxDistance / length)) };
		JPH::RayCastResult result;
		const LayerMask mask(layers);
		const NoSensors bodies;
		if (!world.System.GetNarrowPhaseQueryNoLock().CastRay(ray, result, {}, mask, bodies))
			return std::nullopt;
		PhysicsHit hit;
		const auto point = ray.GetPointOnRay(result.mFraction);
		hit.Point = FromJolt(JPH::Vec3(point));
		hit.Distance = result.mFraction * maxDistance;
		JPH::BodyLockRead lock(world.System.GetBodyLockInterfaceNoLock(), result.mBodyID);
		if (!lock.Succeeded() || !world.Source)
			return std::nullopt;
		hit.Normal = FromJolt(lock.GetBody().GetWorldSpaceSurfaceNormal(result.mSubShapeID2, point));
		hit.Entity = world.Source->Resolve(ECS::EntityRef{ lock.GetBody().GetUserData() });
		return hit;
	}

	std::optional<PhysicsHit> PhysicsWorld::CastShape(const PhysicsShape& shape, Vec3 direction, float maxDistance,
		std::uint32_t layers) const
	{
		const float length = std::sqrt(Dot(direction, direction));
		std::string problem;
		if (!(length > 0.0f) || !(maxDistance > 0.0f) || !std::isfinite(maxDistance) || !IsFinite(direction) ||
			!IsFinite(shape.Position) || !IsFinite(shape.Rotation))
			return std::nullopt;
		const auto query = MakeQueryShape(shape, problem);
		if (!query)
			return std::nullopt;
		const auto& world = *m_Impl;
		const auto cast = JPH::RShapeCast::sFromWorldTransform(query, JPH::Vec3::sOne(), QueryTransform(shape),
			ToJolt(direction * (maxDistance / length)));
		JPH::ShapeCastSettings settings;
		JPH::ClosestHitCollisionCollector<JPH::CastShapeCollector> collector;
		const LayerMask mask(layers);
		const NoSensors bodies;
		world.System.GetNarrowPhaseQueryNoLock().CastShape(cast, settings, JPH::RVec3::sZero(), collector, {}, mask, bodies);
		if (!collector.HadHit())
			return std::nullopt;
		const auto& result = collector.mHit;
		PhysicsHit hit;
		hit.Point = FromJolt(JPH::Vec3(result.mContactPointOn2));
		const auto axis = result.mPenetrationAxis;
		hit.Normal = axis.LengthSq() > 0.0f ? FromJolt(-axis.Normalized()) : Vec3{};
		hit.Distance = result.mFraction * maxDistance;
		hit.Entity = ResolveBody(world.Source, world.System.GetBodyLockInterfaceNoLock(), result.mBodyID2);
		if (hit.Entity.IsNull())
			return std::nullopt;
		return hit;
	}

	std::span<const ECS::Entity> PhysicsWorld::Overlap(const PhysicsShape& shape, std::uint32_t layers) const
	{
		auto& world = *m_Impl;
		world.OverlapResult.clear();
		std::string problem;
		if (!IsFinite(shape.Position) || !IsFinite(shape.Rotation) || !world.Source)
			return {};
		const auto query = MakeQueryShape(shape, problem);
		if (!query)
			return {};
		JPH::CollideShapeSettings settings;
		JPH::AllHitCollisionCollector<JPH::CollideShapeCollector> collector;
		const LayerMask mask(layers);
		const NoSensors bodies;
		const auto transform = QueryTransform(shape) * JPH::Mat44::sTranslation(query->GetCenterOfMass());
		world.System.GetNarrowPhaseQueryNoLock().CollideShape(query, JPH::Vec3::sOne(), transform, settings,
			JPH::RVec3::sZero(), collector, {}, mask, bodies);
		std::vector<std::uint64_t> ids;
		const auto& locks = world.System.GetBodyLockInterfaceNoLock();
		for (const auto& hit : collector.mHits)
		{
			JPH::BodyLockRead lock(locks, hit.mBodyID2);
			if (lock.Succeeded())
				ids.push_back(lock.GetBody().GetUserData());
		}
		std::sort(ids.begin(), ids.end());
		ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
		for (const auto id : ids)
		{
			const auto entity = world.Source->Resolve(ECS::EntityRef{ id });
			if (!entity.IsNull())
				world.OverlapResult.push_back(entity);
		}
		return world.OverlapResult;
	}
}
