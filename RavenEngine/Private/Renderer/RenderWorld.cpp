#include "Renderer/RenderWorldState.hpp"

#include "Raven/Components/StaticMeshComponent.hpp"
#include "Raven/Components/TransformComponent.hpp"
#include "Raven/ECS/Registry.hpp"
#include "Raven/Scene/Scene.hpp"
#include "Renderer/Vulkan/VulkanTexture.hpp"

#include <algorithm>
#include <cmath>
#include <span>

namespace Raven
{
	namespace
	{
		using ECS::Entity;
		using Pool = ECS::Detail::PoolView;

		bool IsDrawable(const Mat4& model) noexcept
		{
			for (const auto& column : model.Columns)
				for (const float value : column)
					if (!std::isfinite(value))
						return false;
			return model(3, 0) == 0.0f && model(3, 1) == 0.0f && model(3, 2) == 0.0f && model(3, 3) == 1.0f;
		}

		std::uint32_t IndexOf(Entity entity) noexcept
		{
			return ECS::Detail::EntityAccess::Index(entity);
		}
	}

	// Reads registry and scene internals as Scene does; every borrowed pool view is used only
	// within one Sync, which makes no structural change.
	class RenderWorldSync
	{
	public:
		RenderWorldSync(RenderWorld::State& state, ECS::Registry& registry,
			const RenderWorld::TextureResolver& resolveTexture)
			: m_State(state), m_Registry(registry), m_Resolve(resolveTexture),
			  m_Meshes(registry.GetPool(ECS::ComponentTraits<ECS::StaticMeshComponent>::Info)),
			  m_Worlds(registry.GetPool(ECS::ComponentTraits<ECS::WorldTransformComponent>::Info)),
			  m_Materials(registry.GetPool(ECS::ComponentTraits<ECS::MaterialComponent>::Info)),
			  m_Owners(registry.GetIterationState().Owners)
		{
		}

		void Rebuild()
		{
			auto& state = m_State;
			++state.Generation;
			state.Objects.clear();
			state.Entities.clear();
			std::fill(state.SlotOfIndex.begin(), state.SlotOfIndex.end(), RenderWorld::State::NoSlot);
			state.Dirty.clear();
			state.DirtyFlags.clear();
			state.Materials.clear();
			state.Groups.clear();
			state.GroupCounts.clear();
			for (std::size_t dense = 0; dense < m_Meshes.Count; ++dense)
			{
				const auto entity = m_Meshes.Entities[dense];
				if (Owned(entity) && m_Worlds.Find(entity) != Pool::NoIndex)
					Add(entity);
			}
		}

		// Brings one entity's slot in line with its current components.
		void Reconcile(Entity entity)
		{
			auto& state = m_State;
			const auto index = IndexOf(entity);
			auto slot = index < state.SlotOfIndex.size() ? state.SlotOfIndex[index] : RenderWorld::State::NoSlot;
			if (slot != RenderWorld::State::NoSlot && !(state.Entities[slot] == entity))
			{
				// A destroyed entity's slot index was reused; its own removal entry may follow.
				if (!m_Registry.IsAlive(state.Entities[slot]))
					Remove(slot);
				slot = RenderWorld::State::NoSlot;
			}
			const bool present = m_Registry.IsAlive(entity) && Owned(entity) &&
				m_Meshes.Find(entity) != Pool::NoIndex && m_Worlds.Find(entity) != Pool::NoIndex;
			if (present && slot == RenderWorld::State::NoSlot)
				Add(entity);
			else if (present)
				Write(slot, false, true);
			else if (slot != RenderWorld::State::NoSlot)
				Remove(slot);
		}

		// Rewrites objects whose mesh, world transform or material changed after since.
		void ApplyTicks(std::uint32_t since)
		{
			const auto& state = m_State;
			for (std::size_t dense = 0; dense < m_Meshes.Count; ++dense)
			{
				const auto entity = m_Meshes.Entities[dense];
				const auto index = IndexOf(entity);
				if (index >= state.SlotOfIndex.size())
					continue;
				const auto slot = state.SlotOfIndex[index];
				if (slot == RenderWorld::State::NoSlot || !(state.Entities[slot] == entity))
					continue;
				const auto world = m_Worlds.Find(entity);
				const auto material = m_Materials.Find(entity);
				const bool materialChanged = material != Pool::NoIndex && m_Materials.Ticks[material] > since;
				if (m_Meshes.Ticks[dense] > since || materialChanged ||
					(world != Pool::NoIndex && m_Worlds.Ticks[world] > since))
					Write(slot, false, materialChanged);
			}
		}

	private:
		bool Owned(Entity entity) const noexcept
		{
			return m_Owners[IndexOf(entity)] == m_State.SceneId;
		}

		void Add(Entity entity)
		{
			auto& state = m_State;
			const auto slot = static_cast<std::uint32_t>(state.Objects.size());
			state.Objects.emplace_back();
			state.Entities.push_back(entity);
			const auto index = IndexOf(entity);
			if (index >= state.SlotOfIndex.size())
				state.SlotOfIndex.resize(index + 1, RenderWorld::State::NoSlot);
			state.SlotOfIndex[index] = slot;
			Write(slot, true, true);
		}

		void Remove(std::uint32_t slot)
		{
			auto& state = m_State;
			--state.GroupCounts[state.Objects[slot].Group];
			state.SlotOfIndex[IndexOf(state.Entities[slot])] = RenderWorld::State::NoSlot;
			const auto last = static_cast<std::uint32_t>(state.Objects.size() - 1);
			if (slot != last)
			{
				state.Objects[slot] = state.Objects[last];
				state.Entities[slot] = state.Entities[last];
				state.SlotOfIndex[IndexOf(state.Entities[slot])] = slot;
				state.MarkDirty(slot);
			}
			state.Objects.pop_back();
			state.Entities.pop_back();
			if (state.DirtyFlags.size() > state.Objects.size())
				state.DirtyFlags.resize(state.Objects.size());
		}

		std::uint32_t FindGroup(MeshHandle mesh)
		{
			auto& state = m_State;
			for (std::size_t group = 0; group < state.Groups.size(); ++group)
				if (state.Groups[group] == mesh)
					return static_cast<std::uint32_t>(group);
			state.Groups.push_back(mesh);
			state.GroupCounts.push_back(0);
			return static_cast<std::uint32_t>(state.Groups.size() - 1);
		}

		std::uint32_t FindMaterial(Entity entity)
		{
			auto& state = m_State;
			const auto dense = m_Materials.Find(entity);
			const auto material = dense == Pool::NoIndex ? ECS::MaterialComponent{}
				: m_Materials.At<ECS::MaterialComponent>(dense);
			const auto* texture = m_Resolve ? m_Resolve(entity, material) : nullptr;
			// Bindless element 0 is the context's white texture (G4).
			const GpuMaterialData value{ material.BaseColor, texture ? texture->GetBindlessIndex() : 0u,
				material.Lit ? 1u : 0u };
			for (std::size_t index = 0; index < state.Materials.size(); ++index)
			{
				const auto& known = state.Materials[index];
				if (known.Texture == value.Texture && known.Lit == value.Lit && known.BaseColor.X == value.BaseColor.X &&
					known.BaseColor.Y == value.BaseColor.Y && known.BaseColor.Z == value.BaseColor.Z &&
					known.BaseColor.W == value.BaseColor.W)
					return static_cast<std::uint32_t>(index);
			}
			state.Materials.push_back(value);
			return static_cast<std::uint32_t>(state.Materials.size() - 1);
		}

		void Write(std::uint32_t slot, bool added, bool materialChanged)
		{
			auto& state = m_State;
			const auto entity = state.Entities[slot];
			const auto& mesh = m_Meshes.At<ECS::StaticMeshComponent>(m_Meshes.Find(entity));
			const auto& world = m_Worlds.At<ECS::WorldTransformComponent>(m_Worlds.Find(entity)).World;
			auto& object = state.Objects[slot];
			// Resolve everything that can fail before the group counts change.
			const auto material = added || materialChanged ? FindMaterial(entity) : object.Material;
			const auto group = FindGroup(mesh.Mesh);
			if (added)
				++state.GroupCounts[group];
			else if (group != object.Group)
			{
				--state.GroupCounts[object.Group];
				++state.GroupCounts[group];
			}
			object.Group = group;
			object.Material = material;
			object.Tint = mesh.Tint;
			object.Flags = IsDrawable(world) ? 0u : GpuObjectHidden;
			for (std::size_t row = 0; row < 3; ++row)
				object.WorldRows[row] = { world(row, 0), world(row, 1), world(row, 2), world(row, 3) };
			// Only drawable lit objects pay for the normal matrix. A collapsed or out-of-range
			// transform rasterizes nothing, so hiding it is exact.
			Mat4 normal = Mat4::Identity();
			if (object.Flags == 0 && state.Materials[material].Lit && !TryNormalMatrix(world, normal))
			{
				object.Flags = GpuObjectHidden;
				normal = Mat4::Identity();
			}
			for (std::size_t column = 0; column < 3; ++column)
				object.NormalToWorld[column] = { normal(0, column), normal(1, column), normal(2, column), 0.0f };
			state.MarkDirty(slot);
		}

		RenderWorld::State& m_State;
		ECS::Registry& m_Registry;
		const RenderWorld::TextureResolver& m_Resolve;
		const Pool m_Meshes;
		const Pool m_Worlds;
		const Pool m_Materials;
		const std::uint32_t* m_Owners;
	};

	RenderWorld::RenderWorld()
		: m_State(std::make_unique<State>())
	{
	}

	RenderWorld::~RenderWorld()
	{
		Clear();
	}

	void RenderWorld::Clear() noexcept
	{
		auto& state = *m_State;
		if (state.Registry)
			for (const auto subscription : state.Subscriptions)
				state.Registry->UnsubscribeLifecycle(subscription);
		const auto generation = state.Generation + 1;
		state = State{};
		state.Generation = generation;
	}

	std::size_t RenderWorld::GetObjectCount() const noexcept
	{
		return m_State->Objects.size();
	}

	void RenderWorld::Sync(Scene& scene, const TextureResolver& resolveTexture)
	{
		auto& state = *m_State;
		auto& registry = scene.m_Registry;
		bool rebuild = false;
		if (state.Source != &scene || state.Registry != &registry || state.SceneId != scene.m_SceneId)
		{
			Clear();
			state.Source = &scene;
			state.Registry = &registry;
			state.SceneId = scene.m_SceneId;
			state.Subscriptions = {
				registry.SubscribeLifecycle<ECS::StaticMeshComponent>(),
				registry.SubscribeLifecycle<ECS::WorldTransformComponent>(),
				registry.SubscribeLifecycle<ECS::MaterialComponent>() };
			rebuild = true;
		}
		// Batches from different subscriptions stay valid together: each read trims only its own type.
		std::array<ECS::LifecycleBatch, 3> batches{};
		for (std::size_t index = 0; index < batches.size(); ++index)
		{
			batches[index] = registry.ReadLifecycle(state.Subscriptions[index]);
			rebuild = rebuild || batches[index].Resync;
		}
		const auto since = state.Since;
		state.Since = registry.AdvanceChangeTick();

		RenderWorldSync sync(state, registry, resolveTexture);
		if (rebuild)
		{
			sync.Rebuild();
			return;
		}
		for (const auto& batch : batches)
			for (const auto& entry : batch.Entries)
				sync.Reconcile(entry.Target);
		sync.ApplyTicks(since);
	}
}
