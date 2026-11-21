#include "Raven/Scene/Scene.hpp"

#include "Raven/Components/CameraComponent.hpp"

#include <algorithm>
#include <stdexcept>

namespace Raven
{
	namespace
	{
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

		Mat4 AppendLocalTransform(const Mat4& parent, const ECS::TransformComponent& local)
		{
			if (local.Rotation.X != 0 || local.Rotation.Y != 0 || local.Rotation.Z != 0)
				return parent * local.GetMatrix();

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
		: m_Registry(registry), m_Store(store), m_SceneId(registry.AllocateSceneId())
	{
	}

	Scene::~Scene()
	{
		for (const auto entity : m_Entities)
			m_Registry.Destroy(entity);
	}

	ECS::Entity Scene::CreateEntity(const std::string& name)
	{
		if (!name.empty() && m_Names.contains(name))
			throw std::invalid_argument("Duplicate scene entity name: " + name);
		if (m_Entities.size() == m_Entities.capacity())
			m_Entities.reserve(m_Entities.empty() ? 1 : m_Entities.size() * 2);
		const auto entity = m_Registry.Create();
		try
		{
			m_Parents.emplace(entity, ECS::Entity{});
			m_Registry.SetOwner(entity, m_SceneId);
			if (!name.empty())
				m_Names.emplace(name, entity);
			m_Entities.push_back(entity);
		}
		catch (...)
		{
			m_Parents.erase(entity);
			m_Registry.Destroy(entity);
			throw;
		}
		return entity;
	}

	ECS::Entity Scene::FindEntity(const std::string& name) const
	{
		const auto found = m_Names.find(name);
		return found != m_Names.end() && m_Registry.IsAlive(found->second) ? found->second : ECS::Entity{};
	}

	std::size_t Scene::EntityHash::operator()(ECS::Entity entity) const noexcept
	{
		// Live slots are unique within a scene's registry. Equality also checks generation and registry.
		return entity.m_Index;
	}

	bool Scene::OwnsLiveEntity(ECS::Entity entity) const noexcept
	{
		return m_Registry.IsAlive(entity) && m_Parents.contains(entity);
	}

	void Scene::RequireOwned(ECS::Entity entity) const
	{
		if (!OwnsLiveEntity(entity))
			throw std::invalid_argument("Hierarchy requires a live entity owned by this scene");
	}

	std::string Scene::GetAuthoredId(ECS::Entity entity) const
	{
		RequireOwned(entity);
		for (const auto& [name, owned] : m_Names)
			if (owned == entity)
				return name;
		return {};
	}

	ECS::Entity Scene::GetParent(ECS::Entity entity) const
	{
		const auto found = m_Parents.find(entity);
		if (found == m_Parents.end() || !m_Registry.IsAlive(entity))
			throw std::invalid_argument("Hierarchy requires a live entity owned by this scene");
		return found->second;
	}

	void Scene::SetParent(ECS::Entity entity, ECS::Entity parent)
	{
		RequireOwned(entity);
		if (!parent.IsNull())
		{
			// Check the entire prospective ancestry before changing the relationship.
			for (auto ancestor = parent; !ancestor.IsNull(); ancestor = GetParent(ancestor))
				if (ancestor == entity)
					throw std::invalid_argument("Scene parenting would create a cycle");
		}
		m_Parents.at(entity) = parent;
	}

	void Scene::DestroyEntity(ECS::Entity entity)
	{
		if (!m_Parents.contains(entity))
			throw std::invalid_argument("Cannot destroy an entity owned by another scene");
		if (m_Registry.IsIterating())
			throw std::logic_error("Scene entities cannot be destroyed while a view iterates");
		std::vector<ECS::Entity> removed{ entity };
		for (std::size_t index = 0; index < removed.size(); ++index)
			for (const auto& [child, parent] : m_Parents)
				if (parent == removed[index])
					removed.push_back(child);
		const auto contains = [&removed](ECS::Entity value)
		{
			return std::find(removed.begin(), removed.end(), value) != removed.end();
		};
		for (const auto value : removed)
		{
			m_Registry.Destroy(value);
			m_Parents.erase(value);
		}
		std::erase_if(m_Entities, contains);
		std::erase_if(m_Names, [&contains](const auto& entry) { return contains(entry.second); });
		if (contains(m_SelectedCamera)) m_SelectedCamera = {};
		if (contains(m_Bindings.Camera)) m_Bindings.Camera = {};
		if (contains(m_Bindings.Player)) m_Bindings.Player = {};
		if (contains(m_Bindings.Mesh)) m_Bindings.Mesh = {};
	}

	Mat4 Scene::ComposeTransform(ECS::Entity entity, bool rotationOnly) const
	{
		Mat4 result = Mat4::Identity();
		auto ancestor = entity;
		do
		{
			const auto parent = GetParent(ancestor);
			ECS::TransformComponent transform;
			if (!m_Registry.Read(ancestor, transform))
				throw std::invalid_argument("Scene transform ancestor has no TransformComponent");
			const auto local = rotationOnly ? RotationZ(transform.Rotation.Z) * RotationY(transform.Rotation.Y) *
				RotationX(transform.Rotation.X) : transform.GetMatrix();
			result = local * result;
			ancestor = parent;
		} while (!ancestor.IsNull());
		if (!IsFiniteTransform(result))
			throw std::invalid_argument("World transform exceeds the supported numeric range");
		return result;
	}

	Mat4 Scene::GetWorldTransform(ECS::Entity entity) const
	{
		return ComposeTransform(entity, false);
	}

	Scene::WorldTransformPass::WorldTransformPass(const Scene& scene, WorldTransformStorage& storage)
		: m_Scene(scene), m_Storage(storage)
	{
		m_Storage.m_Parents.clear();
	}

	Mat4 Scene::WorldTransformPass::ResolveParent(ECS::Entity parent)
	{
		auto& cache = m_Storage.m_Parents;
		auto& ancestors = m_Storage.m_Ancestors;
		ancestors.clear();
		std::optional<Mat4> world;
		for (auto current = parent; !current.IsNull(); current = m_Scene.GetParent(current))
		{
			const auto slot = EntityHash{}(current);
			if (slot < cache.size() && cache[slot].Entity == current)
			{
				world = cache[slot].World;
				break;
			}
			ancestors.push_back(current);
		}
		ECS::TransformComponent local;
		for (auto it = ancestors.rbegin(); it != ancestors.rend(); ++it)
		{
			if (!m_Scene.m_Registry.Read(*it, local))
				throw std::invalid_argument("Scene transform ancestor has no TransformComponent");
			world = AppendLocalTransform(world.value_or(Mat4::Identity()), local);
			const auto slot = EntityHash{}(*it);
			if (slot >= cache.size())
				cache.resize(slot + 1);
			cache[slot] = { *it, *world };
		}
		return *world;
	}

	Mat4 Scene::WorldTransformPass::Get(ECS::Entity entity, const ECS::TransformComponent& local)
	{
		const auto parent = m_Scene.GetParent(entity);
		const auto world = AppendLocalTransform(parent.IsNull() ? Mat4::Identity() : ResolveParent(parent), local);
		// A parent's intermediate scale can overflow or collapse before the child's scale is applied.
		return IsFiniteTransform(world) && !HasCollapsedAxis(world) ? world : m_Scene.GetWorldTransform(entity);
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
		return m_Assets.emplace(id, m_Store.Acquire(catalog, id, kind)).first->second;
	}

	const AssetLease& Scene::GetAsset(AssetId id) const { return m_Assets.at(id); }

	void Scene::SelectCamera(ECS::Entity camera)
	{
		if (!m_Parents.contains(camera))
			throw std::invalid_argument("Camera selection must refer to an owned scene entity");
		m_SelectedCamera = camera;
	}

	void Scene::SetBindings(GameBindings bindings)
	{
		for (const auto entity : { bindings.Camera, bindings.Player, bindings.Mesh })
			if (!entity.IsNull() && !OwnsLiveEntity(entity))
				throw std::invalid_argument("Gameplay binding must refer to an owned scene entity");
		bindings.World = GetAccess();
		bindings.Interactions = {};
		m_Bindings = bindings;
	}
}
