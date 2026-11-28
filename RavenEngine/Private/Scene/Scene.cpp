#include "Raven/Scene/Scene.hpp"

#include "Raven/Components/CameraComponent.hpp"
#include "Raven/Components/LightingComponent.hpp"
#include "Raven/Components/MaterialComponent.hpp"
#include "Raven/Components/PhysicsComponents.hpp"
#include "Raven/Components/SpriteComponent.hpp"
#include "Raven/Components/StaticMeshComponent.hpp"
#include "Raven/Components/TransformComponent.hpp"
#include "Raven/Core/Log.hpp"
#include "Core/JobSystem.hpp"
#include "Platform/PlatformRandom.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <new>
#include <span>
#include <unordered_map>
#include <utility>
#include <stdexcept>

namespace Raven
{
	namespace
	{
		// Room for one more element, growing geometrically: reserving exactly size + 1 would
		// reallocate on every push and make loading a scene quadratic.
		template<typename Vector>
		void ReserveOne(Vector& values)
		{
			if (values.size() == values.capacity())
				values.reserve(values.empty() ? 16 : values.size() * 2);
		}

		// Engine field tables are known before any entity uses them, so a file can name them
		// (docs/DataModelDesign.md, FD-08). Game components register through the module table.
		void RegisterEngineComponents(ECS::Registry& registry)
		{
			const bool registered = registry.Register<ECS::TransformComponent>() &&
				registry.Register<ECS::WorldTransformComponent>() && registry.Register<ECS::ParentComponent>() &&
				registry.Register<ECS::CameraComponent>() && registry.Register<ECS::ActiveCameraComponent>() &&
				registry.Register<ECS::LightingComponent>() && registry.Register<ECS::MaterialComponent>() &&
				registry.Register<ECS::SpriteComponent>() && registry.Register<ECS::StaticMeshComponent>() &&
				registry.Register<ECS::InterpolateComponent>() && registry.Register<ECS::PresentationComponent>() &&
				registry.Register<ECS::ColliderComponent>() &&
				registry.Register<ECS::RigidBodyComponent>() &&
				registry.Register<ECS::MoverComponent>();
			if (!registered)
				throw std::logic_error("An engine component conflicts with a registered layout");
		}

		bool IsFiniteTransform(const Mat4& matrix)
		{
			for (const auto& column : matrix.Columns)
				for (const float value : column)
					if (!std::isfinite(value))
						return false;
			return true;
		}

		bool HasCollapsedAxis(const Mat4& matrix)
		{
			for (std::size_t axis = 0; axis < 3; ++axis)
				if (matrix(0, axis) == 0 && matrix(1, axis) == 0 && matrix(2, axis) == 0)
					return true;
			return false;
		}

		void SineCosine(float radians, float& sine, float& cosine)
		{
			// Zero angles are exact: sin 0 = 0 and cos 0 = 1.
			if (radians == 0)
			{
				sine = 0;
				cosine = 1;
				return;
			}
			sine = std::sin(radians);
			cosine = std::cos(radians);
		}

		Mat4 AppendLocalTransform(const Mat4& parent, const ECS::TransformComponent& local)
		{
			if (local.Rotation.X != 0 || local.Rotation.Y != 0 || local.Rotation.Z != 0)
			{
				// parent * T * Rz * Ry * Rx * S in closed form for an affine parent, matching
				// TransformComponent::GetMatrix without its four full matrix products.
				float sx, cx, sy, cy, sz, cz;
				SineCosine(local.Rotation.X, sx, cx);
				SineCosine(local.Rotation.Y, sy, cy);
				SineCosine(local.Rotation.Z, sz, cz);
				const float rotation[3][3]
				{
					{ cz * cy, cz * sy * sx - sz * cx, cz * sy * cx + sz * sx },
					{ sz * cy, sz * sy * sx + cz * cx, sz * sy * cx - cz * sx },
					{ -sy, cy * sx, cy * cx },
				};
				const float scale[3]{ local.Scale.X, local.Scale.Y, local.Scale.Z };
				Mat4 world = Mat4::Identity();
				for (std::size_t row = 0; row < 3; ++row)
				{
					for (std::size_t column = 0; column < 3; ++column)
						world(row, column) = (parent(row, 0) * rotation[0][column] + parent(row, 1) * rotation[1][column] +
							parent(row, 2) * rotation[2][column]) * scale[column];
					world(row, 3) = parent(row, 0) * local.Position.X + parent(row, 1) * local.Position.Y +
						parent(row, 2) * local.Position.Z + parent(row, 3);
				}
				return world;
			}

			// Diagonal local scale and translation update the parent's affine columns directly.
			Mat4 world = parent;
			for (std::size_t row = 0; row < 3; ++row)
			{
				world(row, 0) = parent(row, 0) * local.Scale.X;
				world(row, 1) = parent(row, 1) * local.Scale.Y;
				world(row, 2) = parent(row, 2) * local.Scale.Z;
				world(row, 3) = parent(row, 0) * local.Position.X + parent(row, 1) * local.Position.Y +
					parent(row, 2) * local.Position.Z + parent(row, 3);
			}
			return world;
		}
	}

	Scene::Scene(ECS::Registry& registry, AssetStore& store)
		: Scene(registry)
	{
		m_Store = &store;
	}

	Scene::Scene(ECS::Registry& registry)
		: m_Registry(registry), m_SceneId(registry.AllocateSceneId())
	{
		RegisterEngineComponents(registry);
		// OS draws seed the scene's ID sequences, so two scenes or contributors never share one.
		std::array<std::uint8_t, sizeof(m_IdState) * 2> seed{};
		Platform::FillRandomBytes(seed);
		std::memcpy(&m_IdState, seed.data(), sizeof(m_IdState));
		std::memcpy(&m_PresentationIdState, seed.data() + sizeof(m_IdState), sizeof(m_PresentationIdState));
	}

	Scene::~Scene()
	{
		for (const auto entity : m_Entities)
			m_Registry.Destroy(entity);
	}

	std::uint64_t Scene::NextId()
	{
		// SplitMix64 over a random start: distinct within the sequence, and never zero.
		for (;;)
		{
			auto& state = m_SimulationIds ? m_IdState : m_PresentationIdState;
			std::uint64_t value = (state += 0x9E3779B97F4A7C15ull);
			value = (value ^ (value >> 30)) * 0xBF58476D1CE4E5B9ull;
			value = (value ^ (value >> 27)) * 0x94D049BB133111EBull;
			value ^= value >> 31;
			if (value != 0 && !m_Ids.contains(value))
				return value;
		}
	}

	ECS::Entity Scene::CreateEntity(const std::string& name)
	{
		if (!name.empty() && m_Names.contains(name))
			throw std::invalid_argument("Duplicate scene entity name: " + name);
		return Adopt(m_Registry.Create(), name, NextId());
	}

	ECS::Entity Scene::CreateEntity(const std::string& name, ECS::EntityRef id)
	{
		if (id.IsNull() || m_Ids.contains(id.Id))
			throw std::invalid_argument("Scene entity ID is null or already in use");
		if (!name.empty() && m_Names.contains(name))
			throw std::invalid_argument("Duplicate scene entity name: " + name);
		return Adopt(m_Registry.Create(), name, id.Id);
	}

	ECS::Entity Scene::Adopt(ECS::Entity entity, const std::string& name, std::uint64_t id)
	{
		const auto index = ECS::Detail::EntityAccess::Index(entity);
		try
		{
			if (index >= m_Nodes.size())
			{
				m_Nodes.resize(index + 1);
				m_World.resize(index + 1);
			}
			// The registry reused the slot of an entity destroyed outside this scene. Its
			// children were already drawn as detached roots; now they become roots.
			if (!m_Nodes[index].Entity.IsNull())
			{
				for (auto child = m_Nodes[index].FirstChild; child != NoNode;)
				{
					const auto next = m_Nodes[child].NextSibling;
					m_Registry.Remove<ECS::ParentComponent>(m_Nodes[child].Entity);
					Unlink(child);
					SetSubtreeDepth(child, 0);
					m_World[child].Flags |= WorldForced;
					child = next;
				}
				Release(index);
			}
			if (m_Depths.empty())
				m_Depths.emplace_back();
			ReserveOne(m_Entities);
			ReserveOne(m_Depths[0]);
			m_Ids.emplace(id, index);
			if (!name.empty())
			{
				m_Names.emplace(name, entity);
				m_Labels[index] = name;
			}
		}
		catch (...)
		{
			m_Ids.erase(id);
			if (const auto found = m_Names.find(name); !name.empty() && found != m_Names.end() && found->second == entity)
				m_Names.erase(found);
			m_Labels.erase(index);
			m_Registry.Destroy(entity);
			throw;
		}
		m_Registry.SetOwner(entity, m_SceneId);
		auto& node = m_Nodes[index];
		node = Node{};
		node.Entity = entity;
		node.Id = id;
		node.EntityPosition = static_cast<std::uint32_t>(m_Entities.size());
		node.DepthPosition = static_cast<std::uint32_t>(m_Depths[0].size());
		m_Entities.push_back(entity);
		m_Depths[0].push_back(index);
		m_World[index] = { entity, NoNode, WorldForced };
		return entity;
	}

	ECS::Entity Scene::FindEntity(const std::string& name) const
	{
		const auto found = m_Names.find(name);
		return found != m_Names.end() && m_Registry.IsAlive(found->second) ? found->second : ECS::Entity{};
	}

	std::size_t Scene::EntityHash::operator()(ECS::Entity entity) const noexcept
	{
		// Live slots are unique within a registry. Equality also checks generation and registry.
		return ECS::Detail::EntityAccess::Index(entity);
	}

	const Scene::Node* Scene::FindNode(ECS::Entity entity) const noexcept
	{
		const auto index = ECS::Detail::EntityAccess::Index(entity);
		return !entity.IsNull() && index < m_Nodes.size() && m_Nodes[index].Entity == entity ? &m_Nodes[index] : nullptr;
	}

	bool Scene::OwnsLiveEntity(ECS::Entity entity) const noexcept
	{
		return FindNode(entity) && m_Registry.IsAlive(entity);
	}

	bool Scene::Owns(ECS::Entity entity) const noexcept
	{
		return OwnsLiveEntity(entity);
	}

	void Scene::RequireOwned(ECS::Entity entity) const
	{
		if (!OwnsLiveEntity(entity))
			throw std::invalid_argument("Hierarchy requires a live entity owned by this scene");
	}

	std::string Scene::GetAuthoredId(ECS::Entity entity) const
	{
		RequireOwned(entity);
		const auto found = m_Labels.find(ECS::Detail::EntityAccess::Index(entity));
		return found == m_Labels.end() ? std::string{} : found->second;
	}

	ECS::Entity Scene::Resolve(ECS::EntityRef id) const noexcept
	{
		const auto found = m_Ids.find(id.Id);
		if (id.IsNull() || found == m_Ids.end())
			return {};
		const auto entity = m_Nodes[found->second].Entity;
		return m_Registry.IsAlive(entity) ? entity : ECS::Entity{};
	}

	ECS::EntityRef Scene::NewRef()
	{
		return { NextId() };
	}

	void Scene::SeedIds(std::uint64_t seed) noexcept
	{
		m_IdState = seed;
	}

	std::uint64_t Scene::GetIdState() const noexcept
	{
		return m_IdState;
	}

	void Scene::UseSimulationIds(bool simulation) noexcept
	{
		m_SimulationIds = simulation;
	}

	bool Scene::IsUsingSimulationIds() const noexcept
	{
		return m_SimulationIds;
	}

	ECS::EntityRef Scene::GetRef(ECS::Entity entity) const noexcept
	{
		const auto* node = FindNode(entity);
		return node ? ECS::EntityRef{ node->Id } : ECS::EntityRef{};
	}

	ECS::Entity Scene::GetParent(ECS::Entity entity) const
	{
		const auto* node = FindNode(entity);
		if (!node || !m_Registry.IsAlive(entity))
			throw std::invalid_argument("Hierarchy requires a live entity owned by this scene");
		return node->Parent;
	}

	void Scene::Link(std::uint32_t child, std::uint32_t parent) noexcept
	{
		auto& node = m_Nodes[child];
		auto& owner = m_Nodes[parent];
		node.Parent = owner.Entity;
		m_World[child].Parent = parent;
		node.PreviousSibling = NoNode;
		node.NextSibling = owner.FirstChild;
		if (owner.FirstChild != NoNode)
			m_Nodes[owner.FirstChild].PreviousSibling = child;
		owner.FirstChild = child;
	}

	void Scene::Unlink(std::uint32_t child) noexcept
	{
		auto& node = m_Nodes[child];
		if (!node.Parent.IsNull())
		{
			// A released parent has already reset its node, so there is no list to fix.
			const auto parent = ECS::Detail::EntityAccess::Index(node.Parent);
			if (parent < m_Nodes.size() && m_Nodes[parent].Entity == node.Parent)
			{
				if (node.PreviousSibling != NoNode)
					m_Nodes[node.PreviousSibling].NextSibling = node.NextSibling;
				else
					m_Nodes[parent].FirstChild = node.NextSibling;
				if (node.NextSibling != NoNode)
					m_Nodes[node.NextSibling].PreviousSibling = node.PreviousSibling;
			}
		}
		node.Parent = {};
		m_World[child].Parent = NoNode;
		node.PreviousSibling = NoNode;
		node.NextSibling = NoNode;
	}

	void Scene::RemoveFromDepth(std::uint32_t index) noexcept
	{
		const auto& node = m_Nodes[index];
		auto& bucket = m_Depths[node.Depth];
		const auto last = bucket.back();
		bucket[node.DepthPosition] = last;
		m_Nodes[last].DepthPosition = node.DepthPosition;
		bucket.pop_back();
	}

	void Scene::SetSubtreeDepth(std::uint32_t root, std::uint32_t depth)
	{
		if (m_Nodes[root].Depth == depth)
			return;
		// Children sit one level below their parent, so walk the subtree with explicit depths. The
		// walk list is kept between calls; spawning leaves never allocates it again.
		auto& pending = m_DepthWalk;
		pending.clear();
		pending.emplace_back(root, depth);
		while (!pending.empty())
		{
			const auto [index, level] = pending.back();
			pending.pop_back();
			if (m_Depths.size() <= level)
				m_Depths.resize(level + 1);
			ReserveOne(m_Depths[level]);
			RemoveFromDepth(index);
			auto& node = m_Nodes[index];
			node.Depth = level;
			node.DepthPosition = static_cast<std::uint32_t>(m_Depths[level].size());
			m_Depths[level].push_back(index);
			for (auto child = node.FirstChild; child != NoNode; child = m_Nodes[child].NextSibling)
				pending.emplace_back(child, level + 1);
		}
	}

	void Scene::Release(std::uint32_t index) noexcept
	{
		auto& node = m_Nodes[index];
		Unlink(index);
		RemoveFromDepth(index);
		const auto last = m_Entities.back();
		m_Entities[node.EntityPosition] = last;
		m_Nodes[ECS::Detail::EntityAccess::Index(last)].EntityPosition = node.EntityPosition;
		m_Entities.pop_back();
		m_Ids.erase(node.Id);
		if (const auto label = m_Labels.find(index); label != m_Labels.end())
		{
			if (const auto name = m_Names.find(label->second); name != m_Names.end() && name->second == node.Entity)
				m_Names.erase(name);
			m_Labels.erase(label);
		}
		m_ReportedDetached.erase(node.Entity);
		node = Node{};
		m_World[index] = {};
	}

	void Scene::SetParent(ECS::Entity entity, ECS::Entity parent)
	{
		RequireOwned(entity);
		if (m_Registry.IsIterating())
			throw std::logic_error("Scene parenting is rejected while a view iterates");
		const auto index = ECS::Detail::EntityAccess::Index(entity);
		std::uint32_t depth = 0;
		ECS::ParentComponent component;
		if (!parent.IsNull())
		{
			RequireOwned(parent);
			// Check the entire prospective ancestry before changing the relationship.
			for (auto ancestor = parent; !ancestor.IsNull();
				ancestor = m_Nodes[ECS::Detail::EntityAccess::Index(ancestor)].Parent)
				if (ancestor == entity)
					throw std::invalid_argument("Scene parenting would create a cycle");
			const auto& owner = m_Nodes[ECS::Detail::EntityAccess::Index(parent)];
			depth = owner.Depth + 1;
			component.Parent = { owner.Id };
		}
		if (m_Nodes[index].Parent == parent)
			return;
		if (parent.IsNull())
			m_Registry.Remove<ECS::ParentComponent>(entity);
		else if (!m_Registry.Write(entity, component) && !m_Registry.Add(entity, component))
			throw std::logic_error("Scene could not record the parent of an entity");
		Unlink(index);
		if (!parent.IsNull())
			Link(index, ECS::Detail::EntityAccess::Index(parent));
		SetSubtreeDepth(index, depth);
		m_World[index].Flags |= WorldForced;
	}

	void Scene::DestroyEntity(ECS::Entity entity)
	{
		if (!FindNode(entity))
			throw std::invalid_argument("Cannot destroy an entity owned by another scene");
		if (m_Registry.IsIterating())
			throw std::logic_error("Scene entities cannot be destroyed while a view iterates");
		auto& removed = m_RemoveWalk;
		removed.assign(1, ECS::Detail::EntityAccess::Index(entity));
		for (std::size_t position = 0; position < removed.size(); ++position)
			for (auto child = m_Nodes[removed[position]].FirstChild; child != NoNode; child = m_Nodes[child].NextSibling)
				removed.push_back(child);
		// Parents release before their children, so each child finds its parent already gone.
		for (const auto index : removed)
		{
			m_Registry.Destroy(m_Nodes[index].Entity);
			Release(index);
		}
	}

	void Scene::ReportDetachedRoot(ECS::Entity entity) const
	{
		if (!m_ReportedDetached.insert(entity).second)
			return;
		const auto label = m_Labels.find(ECS::Detail::EntityAccess::Index(entity));
		const std::string name = label == m_Labels.end() ? "(unnamed)" : label->second;
		Log(LogLevel::Warning, "Scene entity " + name + " has an ancestor without a TransformComponent; "
			"it is placed as a detached root");
	}

	Mat4 Scene::ComposeTransformUnchecked(ECS::Entity entity, bool rotationOnly) const
	{
		const auto* node = FindNode(entity);
		if (!node || !m_Registry.IsAlive(entity))
			throw std::invalid_argument("Hierarchy requires a live entity owned by this scene");
		ECS::TransformComponent transform;
		if (!m_Registry.Read(entity, transform))
			throw std::invalid_argument("Scene entity has no TransformComponent");
		Mat4 result = Mat4::Identity();
		for (;;)
		{
			const auto local = rotationOnly ? RotationZ(transform.Rotation.Z) * RotationY(transform.Rotation.Y) *
				RotationX(transform.Rotation.X) : transform.GetMatrix();
			result = local * result;
			const auto parent = node->Parent;
			if (parent.IsNull())
				break;
			// A destroyed or transform-less ancestor ends the chain: the part below it is
			// composed as a detached root, as UpdateWorldTransforms does.
			const auto* owner = FindNode(parent);
			if (!owner || !m_Registry.IsAlive(parent) || !m_Registry.Read(parent, transform))
			{
				ReportDetachedRoot(entity);
				break;
			}
			node = owner;
		}
		return result;
	}

	Mat4 Scene::ComposeTransform(ECS::Entity entity, bool rotationOnly) const
	{
		const auto result = ComposeTransformUnchecked(entity, rotationOnly);
		if (!IsFiniteTransform(result))
			throw std::invalid_argument("World transform exceeds the supported numeric range");
		return result;
	}

	Mat4 Scene::GetWorldTransform(ECS::Entity entity) const
	{
		return ComposeTransform(entity, false);
	}

	void Scene::UpdateWorldTransforms(JobSystem* workers)
	{
		if (m_Registry.IsIterating())
			throw std::logic_error("World transforms cannot be updated while a view iterates");
		// Stays set until the pass completes, so a pass interrupted by an allocation failure is
		// followed by a full recompute.
		const bool full = std::exchange(m_WorldFull, true);
		const auto since = m_WorldTick;
		m_WorldTick = m_Registry.AdvanceChangeTick();
		const auto stamp = m_Registry.GetChangeTick();
		constexpr auto NoIndex = ECS::Detail::PoolView::NoIndex;
		const auto& worldInfo = ECS::ComponentTraits<ECS::WorldTransformComponent>::Info;
		const auto transforms = m_Registry.GetPool(ECS::ComponentTraits<ECS::TransformComponent>::Info);
		auto worlds = m_Registry.GetPool(worldInfo);
		const auto findWorld = [&worlds](ECS::Entity entity)
		{
			const auto dense = worlds.Find(entity);
			return dense != NoIndex && worlds.Entities[dense] == entity ? dense : NoIndex;
		};

		// What a run of one depth leaves to the calling thread: structural changes and logging. A run
		// only reads the pools and writes its own slots, worlds and ticks, so the runs of one depth can
		// go to different workers (docs/RenderingContentDesign.md, FD-27) without changing any result.
		struct FollowUp
		{
			std::vector<std::uint32_t> Dropped;
			std::vector<std::uint32_t> Detached;
			std::vector<std::uint32_t> Recomposed;
			std::vector<std::pair<std::uint32_t, Mat4>> Added;
			bool Failed = false;
		};
		const auto run = [&](std::span<const std::uint32_t> nodes, FollowUp& followUp) noexcept
		{
			try
			{
				for (const auto index : nodes)
				{
					auto& slot = m_World[index];
					const auto entity = slot.Entity;
					const bool forced = (slot.Flags & WorldForced) != 0;
					const bool present = (slot.Flags & WorldPresent) != 0;
					slot.Flags = 0;
					auto local = transforms.Find(entity);
					if (local != NoIndex && !(transforms.Entities[local] == entity))
						local = NoIndex;
					const auto world = findWorld(entity);
					if (local == NoIndex)
					{
						// No local transform, or an entity destroyed outside the scene: drop a world the
						// entity no longer drives. Losing a world marks it changed, so every descendant
						// re-roots in this pass.
						if (world != NoIndex)
							followUp.Dropped.push_back(index);
						if (present)
							slot.Flags = WorldChanged;
						continue;
					}
					const auto parent = slot.Parent;
					const bool parentChanged = parent != NoNode && (m_World[parent].Flags & WorldChanged) != 0;
					slot.Flags = WorldPresent;
					if (!full && !forced && world != NoIndex && !parentChanged && transforms.Ticks[local] <= since)
						continue;

					// A parent without a world (destroyed or transform-less) leaves this entity as a
					// detached root.
					Mat4 parentWorld = Mat4::Identity();
					if (parent != NoNode)
					{
						if (const auto parentDense = findWorld(m_World[parent].Entity); parentDense == NoIndex)
							followUp.Detached.push_back(index);
						else
							parentWorld = worlds.At<ECS::WorldTransformComponent>(parentDense).World;
					}
					const auto value = AppendLocalTransform(parentWorld, transforms.At<ECS::TransformComponent>(local));
					slot.Flags = WorldPresent | WorldChanged;
					// A parent's intermediate scale can overflow or collapse before the child's scale is
					// applied; the calling thread recomposes in the direct order.
					if (!IsFiniteTransform(value) || HasCollapsedAxis(value))
						followUp.Recomposed.push_back(index);
					else if (world == NoIndex)
						followUp.Added.emplace_back(index, value);
					else
					{
						worlds.At<ECS::WorldTransformComponent>(world).World = value;
						worlds.Ticks[world] = stamp;
					}
				}
			}
			catch (...)
			{
				// Only an allocation failure: the pass is abandoned and the next one recomputes everything.
				followUp.Failed = true;
			}
		};
		// Applies a run's structural changes in order, finding positions again after each change.
		const auto apply = [&](const FollowUp& followUp)
		{
			if (followUp.Failed)
				throw std::bad_alloc();
			for (const auto index : followUp.Dropped)
				m_Registry.Remove<ECS::WorldTransformComponent>(m_World[index].Entity);
			for (const auto index : followUp.Detached)
				ReportDetachedRoot(m_World[index].Entity);
			worlds = m_Registry.GetPool(worldInfo);
			const auto write = [&](std::uint32_t index, const Mat4& value)
			{
				const auto entity = m_World[index].Entity;
				if (const auto world = findWorld(entity); world != NoIndex)
				{
					worlds.At<ECS::WorldTransformComponent>(world).World = value;
					worlds.Ticks[world] = stamp;
					return;
				}
				ECS::WorldTransformComponent component;
				component.World = value;
				m_Registry.Add(entity, component);
				worlds = m_Registry.GetPool(worldInfo);
			};
			// Unrepresentable values are kept as-is so descendants can still recompose; consumers skip
			// non-finite worlds.
			for (const auto index : followUp.Recomposed)
				write(index, ComposeTransformUnchecked(m_World[index].Entity, false));
			for (const auto& [index, value] : followUp.Added)
				write(index, value);
		};

		// Depth buckets put every parent before its children, so the runs of one depth never read
		// each other's results.
		constexpr std::size_t RunLength = 1024;
		std::vector<FollowUp> followUps;
		for (const auto& bucket : m_Depths)
		{
			const auto runs = (bucket.size() + RunLength - 1) / RunLength;
			if (!workers || runs <= 1)
			{
				FollowUp followUp;
				run(bucket, followUp);
				apply(followUp);
				continue;
			}
			followUps.clear();
			followUps.resize(runs);
			workers->ParallelFor(static_cast<std::uint32_t>(runs), [&](std::uint32_t part) noexcept
			{
				const auto first = part * RunLength;
				run(std::span(bucket).subspan(first, std::min(RunLength, bucket.size() - first)), followUps[part]);
			});
			for (const auto& followUp : followUps)
				apply(followUp);
		}
		m_WorldFull = false;
	}

	CameraView Scene::GetCameraView(ECS::Entity entity, float aspect) const
	{
		ECS::CameraComponent camera;
		if (!m_Registry.Read(entity, camera))
			throw std::invalid_argument("Scene camera is missing its CameraComponent");
		const auto world = GetWorldTransform(entity);
		const auto orientation = ComposeTransform(entity, true);
		const Vec3 eye{ world(0, 3), world(1, 3), world(2, 3) };
		const Vec3 forward{ orientation(0, 1), orientation(1, 1), orientation(2, 1) };
		const Vec3 up{ orientation(0, 2), orientation(1, 2), orientation(2, 2) };
		const auto view = LookAtRH({}, forward, up) * Translation(eye * -1.0f);
		for (const auto& column : view.Columns)
			for (const float value : column)
				if (!std::isfinite(value))
					throw std::invalid_argument("World camera exceeds the supported numeric range");
		return { view, camera.GetProjection(aspect) };
	}

	const AssetLease& Scene::UseAsset(const AssetCatalog& catalog, AssetId id, AssetKind kind)
	{
		const auto found = m_Assets.find(id);
		if (found != m_Assets.end())
		{
			if (found->second.GetKind() != kind)
				throw std::invalid_argument("Incorrect scene asset type");
			return found->second;
		}
		if (!m_Store)
			throw std::invalid_argument("This scene has no asset store");
		return m_Assets.emplace(id, m_Store->Acquire(catalog, id, kind)).first->second;
	}

	const AssetLease* Scene::TryUseAsset(const AssetCatalog& catalog, AssetId id, AssetKind kind)
	{
		const auto found = m_Assets.find(id);
		const char* error = nullptr;
		if (found != m_Assets.end())
		{
			if (found->second.GetKind() == kind)
				return &found->second;
			error = "incorrect asset type";
		}
		// A reference that was skipped stays skipped until the scene reloads (F3, or a save), so a
		// broken file is not decoded again on every call (AA2-RT-03).
		else if (m_ReportedAssets.contains(id))
			return nullptr;
		else
			error = m_Store ? m_Store->FindReferenceError(catalog, id, kind) : "no asset store";
		if (!error)
		{
			// A runtime-only ID loads on the workers; its holders draw a placeholder until it arrives (FD-22).
			try
			{
				if (!m_Store)
					throw std::invalid_argument("This scene has no asset store");
				return &m_Assets.emplace(id, m_Store->Request(catalog, id, kind)).first->second;
			}
			catch (const std::exception& failure)
			{
				m_ReportedAssets.insert(id);
				Log(LogLevel::Warning, "Scene asset could not be loaded and is skipped until the scene reloads: " +
					id.ToString() + ": " + failure.what());
				return nullptr;
			}
		}
		if (m_ReportedAssets.insert(id).second)
			Log(LogLevel::Warning, std::string("Scene asset reference skipped (") + error + "): " + id.ToString());
		return nullptr;
	}

	const AssetLease& Scene::GetAsset(AssetId id) const { return m_Assets.at(id); }

	const AssetLease* Scene::FindAsset(AssetId id) const noexcept
	{
		const auto found = m_Assets.find(id);
		return found == m_Assets.end() ? nullptr : &found->second;
	}
}
