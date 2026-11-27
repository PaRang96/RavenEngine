#include "Renderer/RenderWorldState.hpp"

#include "Renderer/DrawChecks.hpp"
#include "Renderer/MaterialFile.hpp"
#include "Raven/Core/Log.hpp"
#include "Raven/Components/StaticMeshComponent.hpp"
#include "Raven/Components/TransformComponent.hpp"
#include "Raven/ECS/Registry.hpp"
#include "Raven/Scene/Scene.hpp"
#include "Renderer/Vulkan/VulkanTexture.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <span>

namespace Raven
{
	namespace
	{
		using ECS::Entity;
		using Pool = ECS::Detail::PoolView;

		std::uint32_t IndexOf(Entity entity) noexcept
		{
			return ECS::Detail::EntityAccess::Index(entity);
		}

		// Unique across every render world in the process, so a renderer that compares the world's
		// address and generation never mistakes a new world at a reused address for one it uploaded.
		std::uint64_t NextGeneration() noexcept
		{
			static std::atomic<std::uint64_t> next{ 0 };
			return next.fetch_add(1, std::memory_order_relaxed) + 1;
		}
	}

	// Reads registry and scene internals as Scene does; every borrowed pool view is used only
	// within one Sync, which makes no structural change.
	class RenderWorldSync
	{
	public:
		RenderWorldSync(RenderWorld::State& state, ECS::Registry& registry,
			const RenderWorld::AssetResolver& resolveAsset, const RenderWorld::MeshResolver& resolveMesh,
			const RenderWorld::ShaderResolver& resolveShader)
			: m_State(state), m_Registry(registry), m_Resolve(resolveAsset), m_ResolveMesh(resolveMesh),
			  m_ResolveShader(resolveShader),
			  m_Meshes(registry.GetPool(ECS::ComponentTraits<ECS::StaticMeshComponent>::Info)),
			  m_Worlds(registry.GetPool(ECS::ComponentTraits<ECS::WorldTransformComponent>::Info)),
			  m_Materials(registry.GetPool(ECS::ComponentTraits<ECS::MaterialComponent>::Info)),
			  m_Owners(registry.GetIterationState().Owners)
		{
		}

		void Rebuild()
		{
			auto& state = m_State;
			state.Generation = NextGeneration();
			state.Objects.clear();
			state.Entities.clear();
			std::fill(state.SlotOfIndex.begin(), state.SlotOfIndex.end(), RenderWorld::State::NoSlot);
			state.Dirty.clear();
			state.DirtyFlags.clear();
			state.Materials.clear();
			state.MaterialEntries.clear();
			state.FreeMaterials.clear();
			state.MaterialOfAsset.clear();
			state.MaterialOfInline.clear();
			state.DirtyMaterials.clear();
			state.MaterialDirtyFlags.clear();
			state.ReportedValues.clear();
			state.Values.clear();
			++state.ValuesRevision;
			state.WastedValues = 0;
			state.ReportedMaterials.clear();
			state.Groups.clear();
			state.FreeGroups.clear();
			state.GroupOfKey.clear();
			state.MeshRefs.clear();
			++state.LayoutRevision;
			++state.CountRevision;
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
				Write(slot, false, true, true);
			else if (slot != RenderWorld::State::NoSlot)
				Remove(slot);
		}

		// Rebuilds the records of material files that reloaded since their record was built. An
		// edit that switches lighting rewrites the objects using it, whose normal matrices follow it.
		void RefreshMaterials()
		{
			auto& state = m_State;
			for (std::uint32_t slot = 0; slot < state.MaterialEntries.size(); ++slot)
			{
				const auto& entry = state.MaterialEntries[slot];
				const bool shaderReloaded = entry.ShaderLease.GetId().IsValid() &&
					entry.ShaderLease.GetRevision() != entry.ShaderRevision;
				if (!entry.Live || !entry.Asset.IsValid() || (entry.Lease.GetRevision() == entry.Revision && !shaderReloaded))
					continue;
				const auto wasLit = state.Materials[slot].Flags & GpuMaterialLit;
				const auto wasKey = entry.ShaderKey;
				BuildMaterial(slot);
				// Lighting decides the normal matrix, and the shader decides the draw group.
				const bool relit = (state.Materials[slot].Flags & GpuMaterialLit) != wasLit;
				const bool regroup = state.MaterialEntries[slot].ShaderKey != wasKey || shaderReloaded;
				if (relit || regroup)
					for (std::uint32_t object = 0; object < state.Objects.size(); ++object)
						if (state.Objects[object].Material == slot)
							Write(object, false, false, false, regroup);
			}
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
				const bool meshChanged = m_Meshes.Ticks[dense] > since;
				if (meshChanged || materialChanged || (world != Pool::NoIndex && m_Worlds.Ticks[world] > since))
					Write(slot, false, materialChanged, meshChanged);
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
			state.MeshRefs.emplace_back();
			const auto index = IndexOf(entity);
			if (index >= state.SlotOfIndex.size())
				state.SlotOfIndex.resize(index + 1, RenderWorld::State::NoSlot);
			state.SlotOfIndex[index] = slot;
			Write(slot, true, true, true);
		}

		void Remove(std::uint32_t slot)
		{
			auto& state = m_State;
			ReleaseGroup(state.Objects[slot].Group);
			ReleaseMaterial(state.Objects[slot].Material);
			state.SlotOfIndex[IndexOf(state.Entities[slot])] = RenderWorld::State::NoSlot;
			const auto last = static_cast<std::uint32_t>(state.Objects.size() - 1);
			if (slot != last)
			{
				state.Objects[slot] = state.Objects[last];
				state.Entities[slot] = state.Entities[last];
				state.MeshRefs[slot] = state.MeshRefs[last];
				state.SlotOfIndex[IndexOf(state.Entities[slot])] = slot;
				state.MarkDirty(slot);
			}
			state.Objects.pop_back();
			state.Entities.pop_back();
			state.MeshRefs.pop_back();
			if (state.DirtyFlags.size() > state.Objects.size())
				state.DirtyFlags.resize(state.Objects.size());
		}

		// The group of a key, adding one (from the free list first) for a new key.
		std::uint32_t FindGroup(const DrawGroupKey& key, float offsetBound = 0.0f)
		{
			auto& state = m_State;
			if (const auto found = state.GroupOfKey.find(key); found != state.GroupOfKey.end())
			{
				// A reloaded shader can change its bound; the mesh table carries it to culling.
				if (state.Groups[found->second].OffsetBound != offsetBound)
				{
					state.Groups[found->second].OffsetBound = offsetBound;
					++state.LayoutRevision;
				}
				return found->second;
			}
			std::uint32_t group;
			if (!state.FreeGroups.empty())
				group = state.FreeGroups.back();
			else
			{
				group = static_cast<std::uint32_t>(state.Groups.size());
				state.Groups.emplace_back();
			}
			state.GroupOfKey.emplace(key, group);
			if (!state.FreeGroups.empty() && state.FreeGroups.back() == group)
				state.FreeGroups.pop_back();
			state.Groups[group] = { key, 0, offsetBound, true };
			++state.LayoutRevision;
			return group;
		}

		// One object fewer in the group; the last one retires it.
		void ReleaseGroup(std::uint32_t group) noexcept
		{
			auto& state = m_State;
			auto& entry = state.Groups[group];
			++state.CountRevision;
			if (--entry.Count != 0)
				return;
			state.GroupOfKey.erase(entry.Key);
			try
			{
				state.FreeGroups.push_back(group);
			}
			catch (...)
			{
				// The slot is not reused; it stays retired.
			}
			entry = {};
			++state.LayoutRevision;
		}

		// A free table slot, its entry live and empty.
		std::uint32_t NewMaterial()
		{
			auto& state = m_State;
			std::uint32_t slot;
			if (!state.FreeMaterials.empty())
			{
				slot = state.FreeMaterials.back();
				state.FreeMaterials.pop_back();
			}
			else
			{
				slot = static_cast<std::uint32_t>(state.Materials.size());
				state.Materials.emplace_back();
				state.MaterialEntries.emplace_back();
			}
			state.MaterialEntries[slot] = {};
			state.MaterialEntries[slot].Live = true;
			return slot;
		}

		// Fills a slot's record from its file or inline values, leasing the textures it samples.
		// Reports a material whose shader parameters cannot be used, once per material revision.
		void ReportMaterial(const MaterialEntry& entry, const std::string& problem)
		{
			auto& reported = m_State.ReportedMaterials;
			const std::pair key{ entry.Asset, entry.Revision };
			if (std::find(reported.begin(), reported.end(), key) != reported.end())
				return;
			reported.push_back(key);
			Log(LogLevel::Warning, "Material " + entry.Asset.ToString() + " draws with the engine's lit shader: " + problem);
		}

		// Puts a material's parameter block in State::Values, aligned to its own size so shaders index
		// it as one struct; any change uploads the values again (B0 contract 7).
		void PlaceValues(MaterialEntry& entry, const std::vector<Vec4>& block)
		{
			auto& state = m_State;
			const auto count = static_cast<std::uint32_t>(block.size());
			if (entry.ValueCount != count)
			{
				state.WastedValues += entry.ValueCount;
				entry.ValueOffset = 0;
				entry.ValueCount = count;
				if (count != 0)
				{
					const auto offset = (state.Values.size() + count - 1) / count * count;
					state.Values.resize(offset + count);
					entry.ValueOffset = static_cast<std::uint32_t>(offset);
				}
			}
			std::copy(block.begin(), block.end(), state.Values.begin() + entry.ValueOffset);
			++state.ValuesRevision;
			// Blocks left behind by moved materials are reclaimed once they outweigh the live ones.
			if (state.WastedValues > 1024 && state.WastedValues * 2 > state.Values.size())
				CompactValues();
		}

		void CompactValues()
		{
			auto& state = m_State;
			std::vector<Vec4> packed;
			for (std::uint32_t slot = 0; slot < state.MaterialEntries.size(); ++slot)
			{
				auto& entry = state.MaterialEntries[slot];
				if (!entry.Live || entry.ValueCount == 0)
					continue;
				const auto offset = (packed.size() + entry.ValueCount - 1) / entry.ValueCount * entry.ValueCount;
				packed.resize(offset + entry.ValueCount);
				std::copy_n(state.Values.begin() + entry.ValueOffset, entry.ValueCount, packed.begin() + offset);
				entry.ValueOffset = static_cast<std::uint32_t>(offset);
				state.Materials[slot].ValueOffset = entry.ValueOffset;
				state.MarkMaterialDirty(slot);
			}
			state.Values = std::move(packed);
			state.WastedValues = 0;
			++state.ValuesRevision;
		}

		// Packs a shader material's parameters in the shader's order, checking the file's names against
		// the shader's. Returns the problem, or an empty string.
		std::string PackParameters(const MaterialDefinition& definition, const MaterialShader& shader,
			std::vector<Vec4>& block, std::array<AssetId, MaterialDefinition::TextureSlots>& textures,
			std::array<std::uint32_t, MaterialDefinition::TextureSlots>& samplers)
		{
			const auto find = [&](const std::string& name, MaterialParameter::Kind kind) -> const MaterialParameter*
			{
				for (const auto& parameter : shader.Parameters)
					if (parameter.Name == name && (parameter.Type == MaterialParameter::Kind::Value) == (kind == MaterialParameter::Kind::Value))
						return &parameter;
				return nullptr;
			};
			for (const auto& [name, value] : definition.Values)
				if (!find(name, MaterialParameter::Kind::Value))
					return "value " + name + " is not a value of its shader";
			for (const auto& [name, texture] : definition.NamedTextures)
				if (!find(name, MaterialParameter::Kind::ClampTexture))
					return "texture " + name + " is not a texture of its shader";
			block.assign(shader.Parameters.size(), Vec4{});
			textures = {};
			samplers = {};
			std::size_t texture = 0;
			for (std::size_t index = 0; index < shader.Parameters.size(); ++index)
			{
				const auto& parameter = shader.Parameters[index];
				if (parameter.Type == MaterialParameter::Kind::Value)
				{
					for (const auto& [name, value] : definition.Values)
						if (name == parameter.Name)
							block[index] = value;
					continue;
				}
				for (const auto& [name, id] : definition.NamedTextures)
					if (name == parameter.Name)
						textures[texture] = id;
				samplers[texture] = parameter.Type == MaterialParameter::Kind::RepeatTexture ? 1u : 0u;
				++texture;
			}
			return {};
		}

		// Fills a slot's record from its file or inline values, leasing the textures it samples and, for a
		// shader material, its shader with a packed parameter block.
		void BuildMaterial(std::uint32_t slot)
		{
			auto& state = m_State;
			auto& entry = state.MaterialEntries[slot];
			GpuMaterialData record;
			std::array<AssetId, MaterialDefinition::TextureSlots> textures{};
			std::array<std::uint32_t, MaterialDefinition::TextureSlots> samplers{};
			std::vector<Vec4> block;
			entry.ShaderLease = {};
			entry.ShaderRevision = 0;
			entry.ShaderKey = 0;
			if (entry.Asset.IsValid())
			{
				const auto& definition = *entry.Lease.GetMaterial();
				record.BaseColor = definition.BaseColor;
				textures = definition.Textures;
				record.Flags = definition.Lit ? GpuMaterialLit : 0u;
				entry.Revision = entry.Lease.GetRevision();
				if (definition.Shader.IsValid())
				{
					const auto* lease = m_Resolve ? m_Resolve(definition.Shader, AssetKind::Shader) : nullptr;
					const auto* shader = lease ? lease->GetMaterialShader() : nullptr;
					auto shaderTextures = textures;
					std::string problem = shader ? PackParameters(definition, *shader, block, shaderTextures, samplers)
						: "its shader " + definition.Shader.ToString() + " cannot be used";
					if (problem.empty())
					{
						entry.ShaderLease = *lease;
						entry.ShaderRevision = lease->GetRevision();
						entry.ShaderKey = m_ResolveShader ? m_ResolveShader(*lease) : 0u;
						textures = shaderTextures;
					}
					else
					{
						block.clear();
						samplers = {};
						ReportMaterial(entry, problem);
					}
				}
			}
			else
			{
				record.BaseColor = entry.InlineColor;
				textures[0] = entry.Inline.Texture;
				record.Flags = entry.Inline.Lit ? GpuMaterialLit : 0u;
			}
			for (std::size_t index = 0; index < textures.size(); ++index)
			{
				entry.Textures[index] = {};
				// Bindless element 0 is the context's white texture (G4).
				if (const auto* lease = textures[index].IsValid() && m_Resolve ? m_Resolve(textures[index], AssetKind::Texture) : nullptr)
				{
					entry.Textures[index] = *lease;
					record.Textures[index] = lease->GetTexture().GetBindlessIndex();
				}
				record.Samplers[index] = samplers[index];
			}
			// A texture parameter's first word is its bindless element.
			if (const auto* shader = entry.ShaderLease.GetMaterialShader())
			{
				std::size_t texture = 0;
				for (std::size_t index = 0; index < shader->Parameters.size(); ++index)
					if (shader->Parameters[index].Type != MaterialParameter::Kind::Value)
						std::memcpy(&block[index].X, &record.Textures[texture++], sizeof(std::uint32_t));
			}
			PlaceValues(entry, block);
			record.ValueOffset = entry.ValueOffset;
			record.ValueCount = entry.ValueCount;
			state.Materials[slot] = record;
			state.MarkMaterialDirty(slot);
		}

		// The table slot of an entity's material, taking one use of it (B0 contract 6). A material
		// file is keyed by its ID; without one, or when the file cannot be used, the inline values
		// are the key. Only a new key resolves leases.
		std::uint32_t AcquireMaterial(Entity entity, bool& nonFinite)
		{
			auto& state = m_State;
			const auto dense = m_Materials.Find(entity);
			const auto component = dense == Pool::NoIndex ? ECS::MaterialComponent{}
				: m_Materials.At<ECS::MaterialComponent>(dense);
			std::uint32_t slot = RenderWorld::State::NoSlot;
			if (component.Material.IsValid())
			{
				if (const auto found = state.MaterialOfAsset.find(component.Material.Id); found != state.MaterialOfAsset.end())
					slot = found->second;
				else if (const auto* lease = m_Resolve ? m_Resolve(component.Material.Id, AssetKind::Material) : nullptr;
					lease && lease->GetMaterial())
				{
					slot = NewMaterial();
					auto& entry = state.MaterialEntries[slot];
					entry.Asset = component.Material.Id;
					entry.Lease = *lease;
					state.MaterialOfAsset.emplace(entry.Asset, slot);
					BuildMaterial(slot);
				}
			}
			if (slot == RenderWorld::State::NoSlot)
			{
				// Non-finite inline values never enter the table; the object is hidden instead.
				nonFinite = !IsFinite(component.BaseColor);
				const auto color = nonFinite ? Vec4{ 1.0f, 1.0f, 1.0f, 1.0f } : component.BaseColor;
				InlineMaterialKey key;
				std::memcpy(key.Color.data(), &color, sizeof(key.Color));
				key.Texture = component.BaseColorTexture.Id;
				key.Lit = component.Lit;
				if (const auto found = state.MaterialOfInline.find(key); found != state.MaterialOfInline.end())
					slot = found->second;
				else
				{
					slot = NewMaterial();
					auto& entry = state.MaterialEntries[slot];
					entry.Inline = key;
					entry.InlineColor = color;
					state.MaterialOfInline.emplace(key, slot);
					BuildMaterial(slot);
				}
			}
			++state.MaterialEntries[slot].Users;
			return slot;
		}

		// One object fewer uses the slot; the last one frees it and its leases.
		void ReleaseMaterial(std::uint32_t slot) noexcept
		{
			auto& state = m_State;
			auto& entry = state.MaterialEntries[slot];
			if (--entry.Users != 0)
				return;
			if (entry.Asset.IsValid())
				state.MaterialOfAsset.erase(entry.Asset);
			else
				state.MaterialOfInline.erase(entry.Inline);
			state.WastedValues += entry.ValueCount;
			entry = {};
			try
			{
				state.FreeMaterials.push_back(slot);
			}
			catch (...)
			{
				// The slot is not reused.
			}
		}

		void ReportNonFinite(Entity entity)
		{
			auto& reported = m_State.ReportedValues;
			if (std::find(reported.begin(), reported.end(), entity) != reported.end())
				return;
			reported.push_back(entity);
			Log(LogLevel::Warning, "An object's Tint or material values are not finite numbers; it is hidden until they are");
		}

		// Only an added object or a changed mesh component resolves its mesh reference.
		void Write(std::uint32_t slot, bool added, bool materialChanged, bool meshChanged, bool regroup = false)
		{
			auto& state = m_State;
			const auto entity = state.Entities[slot];
			const auto& mesh = m_Meshes.At<ECS::StaticMeshComponent>(m_Meshes.Find(entity));
			const auto& world = m_Worlds.At<ECS::WorldTransformComponent>(m_Worlds.Find(entity)).World;
			auto& object = state.Objects[slot];
			// Resolve everything that can fail before the group counts change.
			bool nonFinite = !added && !materialChanged && (object.Flags & GpuObjectNonFinite) != 0;
			const auto material = added || materialChanged ? AcquireMaterial(entity, nonFinite) : object.Material;
			auto group = object.Group;
			if (added || materialChanged || meshChanged || regroup)
			{
				// A write that kept the mesh reference (a Tint edit) reuses the resolved handle.
				auto& reference = state.MeshRefs[slot];
				const bool resolve = added || !(reference == mesh.Mesh);
				const auto handle = resolve ? (m_ResolveMesh ? m_ResolveMesh(entity, mesh.Mesh) : MeshHandle{})
					: state.Groups[object.Group].Key.Mesh;
				// The material's shader picks the pipeline (B0 contract 7).
				const auto& entry = state.MaterialEntries[material];
				const auto* shader = entry.ShaderLease.GetMaterialShader();
				const float bound = shader ? shader->OffsetBound : 0.0f;
				DrawPipelineKey pipeline;
				pipeline.Shader = entry.ShaderKey;
				pipeline.Offset = bound > 0.0f ? 1 : 0;
				group = FindGroup({ pipeline, handle }, bound);
				reference = mesh.Mesh;
			}
			if (added)
			{
				++state.Groups[group].Count;
				++state.CountRevision;
			}
			else if (group != object.Group)
			{
				++state.Groups[group].Count;
				ReleaseGroup(object.Group);
			}
			if (!added && materialChanged)
				ReleaseMaterial(object.Material);
			object.Group = group;
			object.Material = material;
			object.Tint = mesh.Tint;
			nonFinite = nonFinite || !IsFinite(mesh.Tint);
			if (nonFinite)
				ReportNonFinite(entity);
			object.Flags = IsDrawable(world) ? 0u : GpuObjectHidden;
			for (std::size_t row = 0; row < 3; ++row)
				object.WorldRows[row] = { world(row, 0), world(row, 1), world(row, 2), world(row, 3) };
			// Only drawable lit objects pay for the normal matrix. A collapsed or out-of-range
			// transform rasterizes nothing, so hiding it is exact.
			Mat4 normal = Mat4::Identity();
			if (object.Flags == 0 && (state.Materials[material].Flags & GpuMaterialLit) != 0 && !TryNormalMatrix(world, normal))
			{
				object.Flags = GpuObjectHidden;
				normal = Mat4::Identity();
			}
			for (std::size_t column = 0; column < 3; ++column)
				object.NormalToWorld[column] = { normal(0, column), normal(1, column), normal(2, column), 0.0f };
			if (nonFinite)
				object.Flags |= GpuObjectHidden | GpuObjectNonFinite;
			state.MarkDirty(slot);
		}

		RenderWorld::State& m_State;
		ECS::Registry& m_Registry;
		const RenderWorld::AssetResolver& m_Resolve;
		const RenderWorld::ShaderResolver& m_ResolveShader;
		const RenderWorld::MeshResolver& m_ResolveMesh;
		const Pool m_Meshes;
		const Pool m_Worlds;
		const Pool m_Materials;
		const std::uint32_t* m_Owners;
	};

	RenderWorld::RenderWorld()
		: m_State(std::make_unique<State>())
	{
		m_State->Generation = NextGeneration();
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
		state = State{};
		state.Generation = NextGeneration();
	}

	std::size_t RenderWorld::GetObjectCount() const noexcept
	{
		return m_State->Objects.size();
	}

	void RenderWorld::Sync(Scene& scene, const AssetResolver& resolveAsset, const MeshResolver& resolveMesh,
		const ShaderResolver& resolveShader)
	{
		try
		{
			SyncUnchecked(scene, resolveAsset, resolveMesh, resolveShader);
		}
		catch (...)
		{
			// A partly applied sync leaves slots, groups or counts inconsistent. Forgetting the
			// scene makes the next Sync rebuild from the components (docs/DataModelDesign.md, FD-06).
			Clear();
			throw;
		}
	}

	void RenderWorld::SyncUnchecked(Scene& scene, const AssetResolver& resolveAsset, const MeshResolver& resolveMesh,
		const ShaderResolver& resolveShader)
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

		RenderWorldSync sync(state, registry, resolveAsset, resolveMesh, resolveShader);
		if (rebuild)
		{
			sync.Rebuild();
			return;
		}
		for (const auto& batch : batches)
			for (const auto& entry : batch.Entries)
				sync.Reconcile(entry.Target);
		sync.ApplyTicks(since);
		sync.RefreshMaterials();
	}
}
