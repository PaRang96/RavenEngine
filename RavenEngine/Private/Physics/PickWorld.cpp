#include "Physics/PickWorld.hpp"

#include "Physics/JoltSupport.hpp"
#include "Raven/Components/StaticMeshComponent.hpp"
#include "Raven/Components/TransformComponent.hpp"
#include "Raven/Core/Log.hpp"
#include "Raven/ECS/Registry.hpp"
#include "Raven/Scene/Scene.hpp"

#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/NarrowPhaseQuery.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/ScaledShape.h>
#include <Jolt/Physics/PhysicsSystem.h>

#include <array>
#include <bit>
#include <chrono>
#include <cstring>
#include <format>
#include <map>
#include <stdexcept>
#include <vector>

namespace Raven
{
	using namespace Physics;

	namespace
	{
		using ECS::Entity;
		using Pool = ECS::Detail::PoolView;
		using Clock = std::chrono::steady_clock;

		// One object layer and one broad-phase tree; pick bodies never collide with anything.
		class OneTree final : public JPH::BroadPhaseLayerInterface
		{
		public:
			JPH::uint GetNumBroadPhaseLayers() const override { return 1; }
			JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer) const override { return JPH::BroadPhaseLayer(0); }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
			const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer) const override { return "Pick"; }
#endif
		};

		class NeverAgainstTree final : public JPH::ObjectVsBroadPhaseLayerFilter
		{
		public:
			bool ShouldCollide(JPH::ObjectLayer, JPH::BroadPhaseLayer) const override { return false; }
		};

		class NeverPairs final : public JPH::ObjectLayerPairFilter
		{
		public:
			bool ShouldCollide(JPH::ObjectLayer, JPH::ObjectLayer) const override { return false; }
		};

		std::uint32_t IndexOf(Entity entity) noexcept
		{
			return ECS::Detail::EntityAccess::Index(entity);
		}

		bool IsUnit(Vec3 scale) noexcept
		{
			return NearlyEqual(scale.X, 1.0f) && NearlyEqual(scale.Y, 1.0f) && NearlyEqual(scale.Z, 1.0f);
		}

		std::int64_t Nanoseconds(Clock::duration duration) noexcept
		{
			return std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();
		}
	}

	struct PickWorld::Impl
	{
		static constexpr std::uint32_t NoSlot = 0xffffffffu;
		static constexpr std::uint32_t InitialCapacity = 1024;

		struct Entry
		{
			Entity Target;
			AssetId Mesh;
			// Invalid while the mesh has no usable triangles.
			JPH::BodyID Body;
			Mat4 World = Mat4::Identity();
			Vec3 Scale{ 1.0f, 1.0f, 1.0f };
			// The shared shape the body uses, to notice a rebuilt mesh.
			const JPH::Shape* Shape = nullptr;
		};

		// One exact shape per mesh asset version.
		struct MeshShape
		{
			std::weak_ptr<const void> Version;
			JPH::ShapeRefC Shape;
			std::string Problem;
		};

		Impl()
		{
			Create(InitialCapacity);
		}

		~Impl()
		{
			RemoveAll();
		}

		void Create(std::uint32_t capacity)
		{
			System = std::make_unique<JPH::PhysicsSystem>();
			System->Init(capacity, 0, 1024, 1024, Tree, NoTree, NoPairs);
			Capacity = capacity;
		}

		void RemoveAll() noexcept
		{
			std::vector<JPH::BodyID> bodies;
			for (const auto& entry : Entries)
				if (!entry.Body.IsInvalid())
					bodies.push_back(entry.Body);
			if (!bodies.empty())
			{
				auto& interface = System->GetBodyInterfaceNoLock();
				interface.RemoveBodies(bodies.data(), static_cast<int>(bodies.size()));
				interface.DestroyBodies(bodies.data(), static_cast<int>(bodies.size()));
			}
			Entries.clear();
			SlotOfIndex.clear();
			if (Registry)
				for (const auto subscription : Subscriptions)
					Registry->UnsubscribeLifecycle(subscription);
			Registry = nullptr;
			Source = nullptr;
			Subscriptions = {};
			Since = 0;
		}

		OneTree Tree;
		NeverAgainstTree NoTree;
		NeverPairs NoPairs;
		std::unique_ptr<JPH::PhysicsSystem> System;
		std::uint32_t Capacity = 0;

		Scene* Source = nullptr;
		// The scene mirrored: its address alone could be reused by the next scene.
		std::uint32_t SceneId = 0;
		ECS::Registry* Registry = nullptr;
		// Lifecycle subscriptions: static mesh, world transform.
		std::array<std::uint32_t, 2> Subscriptions{};
		std::uint32_t Since = 0;
		std::vector<Entry> Entries;
		std::vector<std::uint32_t> SlotOfIndex;
		std::map<AssetId, MeshShape> Shapes;
		PickStats Stats;
	};

	// Reads registry pools as RenderWorldSync does; every borrowed pool view is used within one pick,
	// which makes no structural change.
	class PickWorldSync
	{
	public:
		PickWorldSync(PickWorld::Impl& world, Scene& scene, ECS::Registry& registry, const PickMeshResolver& meshes)
			: m_World(world), m_Scene(scene), m_Registry(registry), m_Resolve(meshes),
			  m_Meshes(registry.GetPool(ECS::ComponentTraits<ECS::StaticMeshComponent>::Info)),
			  m_Worlds(registry.GetPool(ECS::ComponentTraits<ECS::WorldTransformComponent>::Info)),
			  m_Interface(world.System->GetBodyInterfaceNoLock())
		{
		}

		std::size_t GetMeshCount() const noexcept { return m_Meshes.Count; }

		void Rebuild()
		{
			for (std::size_t dense = 0; dense < m_Meshes.Count; ++dense)
				if (m_Scene.Owns(m_Meshes.Entities[dense]))
					Reconcile(m_Meshes.Entities[dense]);
		}

		void Reconcile(Entity entity)
		{
			auto& world = m_World;
			const auto index = IndexOf(entity);
			auto slot = index < world.SlotOfIndex.size() ? world.SlotOfIndex[index] : PickWorld::Impl::NoSlot;
			if (slot != PickWorld::Impl::NoSlot && !(world.Entries[slot].Target == entity))
			{
				if (!m_Registry.IsAlive(world.Entries[slot].Target))
					Remove(slot);
				slot = PickWorld::Impl::NoSlot;
			}
			const bool present = m_Registry.IsAlive(entity) && m_Scene.Owns(entity) &&
				m_Meshes.Find(entity) != Pool::NoIndex && m_Worlds.Find(entity) != Pool::NoIndex;
			if (present && slot == PickWorld::Impl::NoSlot)
			{
				if (index >= world.SlotOfIndex.size())
					world.SlotOfIndex.resize(index + 1, PickWorld::Impl::NoSlot);
				world.SlotOfIndex[index] = static_cast<std::uint32_t>(world.Entries.size());
				PickWorld::Impl::Entry entry;
				entry.Target = entity;
				world.Entries.push_back(entry);
				Update(static_cast<std::uint32_t>(world.Entries.size() - 1));
			}
			else if (present)
				Update(slot);
			else if (slot != PickWorld::Impl::NoSlot)
				Remove(slot);
		}

		// Mesh assets whose triangles changed (a reload) get a new shape.
		void RefreshShapes()
		{
			for (auto& [id, mesh] : m_World.Shapes)
			{
				const auto current = m_Resolve ? m_Resolve(id) : PickMesh{};
				if (current.Owner.get() != mesh.Version.lock().get())
				{
					Build(id, mesh, current);
					m_Rebuilt = true;
				}
			}
		}

		// Entries whose mesh or world transform changed after `since`, or whose shape was rebuilt.
		void ApplyTicks(std::uint32_t since)
		{
			auto& world = m_World;
			for (std::uint32_t slot = 0; slot < world.Entries.size(); ++slot)
			{
				const auto& entry = world.Entries[slot];
				const auto mesh = m_Meshes.Find(entry.Target);
				const auto placed = m_Worlds.Find(entry.Target);
				if ((mesh != Pool::NoIndex && m_Meshes.Ticks[mesh] > since) ||
					(placed != Pool::NoIndex && m_Worlds.Ticks[placed] > since) ||
					(m_Rebuilt && entry.Shape != Find(entry.Mesh)))
					Update(slot);
			}
		}

		void Commit()
		{
			if (!m_Added.empty())
			{
				const auto state = m_Interface.AddBodiesPrepare(m_Added.data(), static_cast<int>(m_Added.size()));
				m_Interface.AddBodiesFinalize(m_Added.data(), static_cast<int>(m_Added.size()), state,
					JPH::EActivation::DontActivate);
			}
			if (!m_Removed.empty())
			{
				m_Interface.RemoveBodies(m_Removed.data(), static_cast<int>(m_Removed.size()));
				m_Interface.DestroyBodies(m_Removed.data(), static_cast<int>(m_Removed.size()));
			}
			// A large batch of new static bodies leaves a loose tree; rebuild it once. Moved bodies only
			// widen their tree nodes, which keeps queries correct.
			if (m_Added.size() > 256)
				m_World.System->OptimizeBroadPhase();
			m_World.Stats.ShapeNanoseconds = m_ShapeTime;
		}

	private:
		const JPH::Shape* Find(AssetId id) const
		{
			const auto found = m_World.Shapes.find(id);
			return found == m_World.Shapes.end() ? nullptr : found->second.Shape.GetPtr();
		}

		void Build(AssetId id, PickWorld::Impl::MeshShape& mesh, const PickMesh& source)
		{
			const auto start = Clock::now();
			mesh.Version = source.Owner;
			mesh.Shape = nullptr;
			std::string problem;
			if (!source.Owner || source.Vertices.empty() || source.Indices.size() < 3 || source.Indices.size() % 3 != 0)
				problem = "the scene holds no triangles for it";
			else
			{
				JPH::VertexList vertices;
				vertices.reserve(source.Vertices.size());
				for (const auto& vertex : source.Vertices)
					vertices.push_back(JPH::Float3(vertex.X, vertex.Y, vertex.Z));
				JPH::IndexedTriangleList triangles;
				triangles.reserve(source.Indices.size() / 3);
				for (std::size_t index = 0; index + 2 < source.Indices.size(); index += 3)
				{
					const auto a = source.Indices[index], b = source.Indices[index + 1], c = source.Indices[index + 2];
					if (a >= vertices.size() || b >= vertices.size() || c >= vertices.size())
					{
						problem = "an index is out of range";
						break;
					}
					triangles.push_back(JPH::IndexedTriangle(a, b, c));
				}
				if (problem.empty())
				{
					const auto result = JPH::MeshShapeSettings(std::move(vertices), std::move(triangles)).Create();
					if (result.HasError())
						problem = ToText(result.GetError());
					else
						mesh.Shape = result.Get();
				}
			}
			if (!problem.empty() && mesh.Problem != problem)
				Log(LogLevel::Warning, std::format("Mesh {} cannot be picked: {}", id.ToString(), problem));
			mesh.Problem = problem;
			m_ShapeTime += Nanoseconds(Clock::now() - start);
		}

		JPH::ShapeRefC Shape(AssetId id)
		{
			auto found = m_World.Shapes.find(id);
			if (found == m_World.Shapes.end())
			{
				found = m_World.Shapes.emplace(id, PickWorld::Impl::MeshShape{}).first;
				Build(id, found->second, m_Resolve ? m_Resolve(id) : PickMesh{});
			}
			return found->second.Shape;
		}

		void Drop(PickWorld::Impl::Entry& entry)
		{
			if (entry.Body.IsInvalid())
				return;
			const auto added = std::find(m_Added.begin(), m_Added.end(), entry.Body);
			if (added != m_Added.end())
			{
				m_Interface.DestroyBody(entry.Body);
				m_Added.erase(added);
			}
			else
				m_Removed.push_back(entry.Body);
			entry.Body = JPH::BodyID();
			entry.Shape = nullptr;
		}

		void Remove(std::uint32_t slot)
		{
			auto& world = m_World;
			Drop(world.Entries[slot]);
			world.SlotOfIndex[IndexOf(world.Entries[slot].Target)] = PickWorld::Impl::NoSlot;
			const auto last = static_cast<std::uint32_t>(world.Entries.size() - 1);
			if (slot != last)
			{
				world.Entries[slot] = world.Entries[last];
				world.SlotOfIndex[IndexOf(world.Entries[slot].Target)] = slot;
			}
			world.Entries.pop_back();
		}

		void Update(std::uint32_t slot)
		{
			auto& entry = m_World.Entries[slot];
			const auto& mesh = m_Meshes.At<ECS::StaticMeshComponent>(m_Meshes.Find(entry.Target));
			const auto& world = m_Worlds.At<ECS::WorldTransformComponent>(m_Worlds.Find(entry.Target)).World;
			Placement place;
			const auto base = mesh.Mesh.IsValid() ? Shape(mesh.Mesh.Id) : JPH::ShapeRefC();
			if (!base || !Decompose(world, place))
			{
				// Not pickable until its mesh or transform changes; drawn nothing pickable either.
				Drop(entry);
				entry.Mesh = mesh.Mesh.Id;
				entry.World = world;
				return;
			}
			// A scaled wrapper only when the shape or its scale changed.
			const bool reshape = entry.Body.IsInvalid() || entry.Shape != base.GetPtr() || !SameVector(entry.Scale, place.Scale);
			JPH::ShapeRefC shape = base;
			if (reshape && !IsUnit(place.Scale))
				shape = new JPH::ScaledShape(base, ToJolt(place.Scale));
			const auto position = JPH::RVec3(ToJolt(place.Position));
			const auto rotation = ToJolt(place.Rotation);
			if (entry.Body.IsInvalid())
			{
				JPH::BodyCreationSettings settings(shape, position, rotation, JPH::EMotionType::Static, 0);
				settings.mUserData = m_Scene.GetRef(entry.Target).Id;
				auto* body = m_Interface.CreateBody(settings);
				if (!body)
					throw std::logic_error("The pick world ran out of bodies below its capacity");
				entry.Body = body->GetID();
				m_Added.push_back(entry.Body);
			}
			else
			{
				if (reshape)
					m_Interface.SetShape(entry.Body, shape, false, JPH::EActivation::DontActivate);
				if (std::memcmp(&entry.World, &world, sizeof(Mat4)) != 0)
					m_Interface.SetPositionAndRotation(entry.Body, position, rotation, JPH::EActivation::DontActivate);
			}
			entry.Mesh = mesh.Mesh.Id;
			entry.Shape = base.GetPtr();
			entry.Scale = place.Scale;
			entry.World = world;
		}

		PickWorld::Impl& m_World;
		Scene& m_Scene;
		ECS::Registry& m_Registry;
		const PickMeshResolver& m_Resolve;
		const Pool m_Meshes;
		const Pool m_Worlds;
		JPH::BodyInterface& m_Interface;
		std::vector<JPH::BodyID> m_Added;
		std::vector<JPH::BodyID> m_Removed;
		std::int64_t m_ShapeTime = 0;
		bool m_Rebuilt = false;
	};

	PickWorld::PickWorld()
	{
		AcquireJolt();
		try
		{
			m_Impl = std::make_unique<Impl>();
		}
		catch (...)
		{
			ReleaseJolt();
			throw;
		}
	}

	PickWorld::~PickWorld()
	{
		m_Impl.reset();
		ReleaseJolt();
	}

	void PickWorld::Clear() noexcept
	{
		m_Impl->RemoveAll();
		m_Impl->Shapes.clear();
		m_Impl->Stats = {};
	}

	std::optional<PickHit> PickWorld::Pick(Scene& scene, ECS::Registry& registry, const PickMeshResolver& meshes,
		const Ray& ray, float maxDistance)
	{
		if (!std::isfinite(maxDistance) || maxDistance < 0.0f)
			throw std::invalid_argument("Picking distance must be finite and nonnegative");
		if (!IsFinite(ray.Origin) || !IsFinite(ray.Direction))
			throw std::invalid_argument("Picking ray must be finite");
		const float length = std::sqrt(Dot(ray.Direction, ray.Direction));
		if (!(length > 0.0f) || maxDistance == 0.0f)
			return std::nullopt;

		const auto syncStart = Clock::now();
		auto& world = *m_Impl;
		bool rebuild = false;
		if (world.Source != &scene || world.Registry != &registry || world.SceneId != scene.GetSceneId())
		{
			Clear();
			world.Source = &scene;
			world.SceneId = scene.GetSceneId();
			world.Registry = &registry;
			world.Subscriptions = { registry.SubscribeLifecycle<ECS::StaticMeshComponent>(),
				registry.SubscribeLifecycle<ECS::WorldTransformComponent>() };
			rebuild = true;
		}
		std::array<ECS::LifecycleBatch, 2> batches{};
		for (std::size_t index = 0; index < batches.size(); ++index)
		{
			batches[index] = registry.ReadLifecycle(world.Subscriptions[index]);
			rebuild = rebuild || batches[index].Resync;
		}
		// The capacity grows with the scene: a larger system is built and filled from the components.
		const auto meshCount = registry.Count<ECS::StaticMeshComponent>();
		if (meshCount > world.Capacity)
		{
			const auto subscriptions = world.Subscriptions;
			// Keep the subscriptions: only the bodies are rebuilt.
			world.Registry = nullptr;
			world.RemoveAll();
			world.Source = &scene;
			world.Registry = &registry;
			world.Subscriptions = subscriptions;
			world.Create(std::bit_ceil(static_cast<std::uint32_t>(meshCount + meshCount / 4)));
			rebuild = true;
		}
		const auto since = world.Since;
		world.Since = registry.AdvanceChangeTick();
		// A failure part-way through the sync leaves the mirror out of step with the components (the
		// lifecycle batches are already read), so it is rebuilt from them next time (AA2-RT-04).
		try
		{
			PickWorldSync sync(world, scene, registry, meshes);
			sync.RefreshShapes();
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
		}
		catch (...)
		{
			Clear();
			throw;
		}
		// Shapes no entity uses any more are released.
		std::erase_if(world.Shapes, [&](const auto& item)
		{
			return std::none_of(world.Entries.begin(), world.Entries.end(),
				[&](const auto& entry) { return entry.Mesh == item.first; });
		});
		const auto queryStart = Clock::now();
		world.Stats.SyncNanoseconds = Nanoseconds(queryStart - syncStart);

		const JPH::RRayCast cast{ JPH::RVec3(ToJolt(ray.Origin)), ToJolt(ray.Direction * (maxDistance / length)) };
		JPH::RayCastSettings settings;
		// Both faces count, as for picking before Jolt.
		settings.SetBackFaceMode(JPH::EBackFaceMode::CollideWithBackFaces);
		JPH::ClosestHitCollisionCollector<JPH::CastRayCollector> collector;
		world.System->GetNarrowPhaseQueryNoLock().CastRay(cast, settings, collector);
		std::optional<PickHit> result;
		if (collector.HadHit())
		{
			JPH::BodyLockRead lock(world.System->GetBodyLockInterfaceNoLock(), collector.mHit.mBodyID);
			if (lock.Succeeded())
			{
				const auto entity = scene.Resolve(ECS::EntityRef{ lock.GetBody().GetUserData() });
				if (!entity.IsNull())
					result = PickHit{ entity, collector.mHit.mFraction * maxDistance };
			}
		}
		world.Stats.QueryNanoseconds = Nanoseconds(Clock::now() - queryStart);
		world.Stats.Bodies = world.System->GetNumBodies();
		world.Stats.Shapes = static_cast<std::uint32_t>(world.Shapes.size());
		return result;
	}

	PickStats PickWorld::GetStats() const noexcept
	{
		return m_Impl->Stats;
	}
}
