// Guards Registry behavior that the M1 storage rewrite must preserve (docs/ECSDesign.md).
// Uses only call forms that stay valid after the rewrite.

#include "Raven/Components/TransformComponent.hpp"
#include "Raven/ECS/Registry.hpp"
#include "Support/Check.hpp"

#include <algorithm>
#include <stdexcept>
#include <vector>

namespace RavenTests
{
	struct Health : Raven::ECS::Component
	{
		float Value = 100.0f;
		RAVEN_COMPONENT(RavenTests::Health, Value);
	};

	struct Marker : Raven::ECS::Component
	{
		RAVEN_COMPONENT(RavenTests::Marker);
	};
}

namespace
{
	using Raven::ECS::Entity;
	using Raven::ECS::Registry;
	using Raven::ECS::TransformComponent;
	using RavenTests::Health;
	using RavenTests::Marker;

	bool Equal(Raven::Vec3 left, Raven::Vec3 right)
	{
		return left.X == right.X && left.Y == right.Y && left.Z == right.Z;
	}

	bool Equal(const TransformComponent& left, const TransformComponent& right)
	{
		return Equal(left.Position, right.Position) && Equal(left.Rotation, right.Rotation) &&
			Equal(left.Scale, right.Scale);
	}

	bool Contains(const std::vector<Entity>& entities, Entity entity)
	{
		return std::find(entities.begin(), entities.end(), entity) != entities.end();
	}

	void EntityLifecycle()
	{
		Registry registry;
		RAVEN_CHECK(Entity{}.IsNull());
		RAVEN_CHECK(!registry.IsAlive(Entity{}));

		const auto first = registry.Create();
		RAVEN_CHECK(!first.IsNull());
		RAVEN_CHECK(registry.IsAlive(first));
		RAVEN_CHECK(registry.Destroy(first));
		RAVEN_CHECK(!registry.IsAlive(first));
		RAVEN_CHECK(!registry.Destroy(first));

		// A recycled slot gets a new generation; the old handle stays stale.
		const auto reused = registry.Create();
		RAVEN_CHECK(registry.IsAlive(reused));
		RAVEN_CHECK(!(reused == first));
		RAVEN_CHECK(!registry.IsAlive(first));

		// Handles from another registry are never alive here.
		Registry other;
		const auto foreign = other.Create();
		RAVEN_CHECK(!registry.IsAlive(foreign));
		RAVEN_CHECK(!registry.Destroy(foreign));
		RAVEN_CHECK(other.IsAlive(foreign));
	}

	void EntitySnapshot()
	{
		Registry registry;
		const auto a = registry.Create();
		const auto b = registry.Create();
		const auto c = registry.Create();
		registry.Destroy(b);
		const auto entities = registry.GetEntities();
		RAVEN_CHECK(entities.size() == 2);
		RAVEN_CHECK(Contains(entities, a));
		RAVEN_CHECK(!Contains(entities, b));
		RAVEN_CHECK(Contains(entities, c));
	}

	void ComponentValues()
	{
		Registry registry;
		const auto entity = registry.Create();

		TransformComponent transform;
		transform.Position = { 1.0f, 2.0f, 3.0f };
		transform.Rotation = { 0.1f, 0.2f, 0.3f };
		transform.Scale = { 2.0f, 2.0f, 2.0f };
		RAVEN_CHECK(!registry.Has(entity, transform));
		RAVEN_CHECK(registry.Add(entity, transform));
		RAVEN_CHECK(registry.Has(entity, TransformComponent{}));
		RAVEN_CHECK(!registry.Add(entity, transform));

		TransformComponent read;
		RAVEN_CHECK(registry.Read(entity, read));
		RAVEN_CHECK(Equal(read, transform));
		read.Position.X = 9.0f;
		RAVEN_CHECK(registry.Write(entity, read));
		TransformComponent written;
		RAVEN_CHECK(registry.Read(entity, written));
		RAVEN_CHECK(written.Position.X == 9.0f);

		// Missing component types fail without side effects.
		Health health;
		health.Value = 42.0f;
		RAVEN_CHECK(!registry.Read(entity, health));
		RAVEN_CHECK(health.Value == 42.0f);
		RAVEN_CHECK(!registry.Write(entity, health));
		RAVEN_CHECK(!registry.Remove(entity, health));

		// Component types are independent.
		RAVEN_CHECK(registry.Add(entity, health));
		Health readHealth;
		RAVEN_CHECK(registry.Read(entity, readHealth));
		RAVEN_CHECK(readHealth.Value == 42.0f);
		RAVEN_CHECK(registry.Read(entity, written));
		RAVEN_CHECK(written.Position.X == 9.0f);

		RAVEN_CHECK(registry.Remove<TransformComponent>(entity));
		RAVEN_CHECK(!registry.Has(entity, TransformComponent{}));
		RAVEN_CHECK(!registry.Remove(entity, TransformComponent{}));
		RAVEN_CHECK(registry.Has(entity, Health{}));
	}

	void TagComponents()
	{
		Registry registry;
		const auto entity = registry.Create();
		RAVEN_CHECK(registry.Add(entity, Marker{}));
		RAVEN_CHECK(registry.Has(entity, Marker{}));
		Marker marker;
		RAVEN_CHECK(registry.Read(entity, marker));
		RAVEN_CHECK(!registry.Add(entity, Marker{}));
		RAVEN_CHECK(registry.Remove<Marker>(entity));
		RAVEN_CHECK(!registry.Has(entity, Marker{}));
	}

	void DestroyErasesComponents()
	{
		Registry registry;
		const auto entity = registry.Create();
		RAVEN_CHECK(registry.Add(entity, TransformComponent{}));
		RAVEN_CHECK(registry.Add(entity, Health{}));
		RAVEN_CHECK(registry.Destroy(entity));

		TransformComponent transform;
		RAVEN_CHECK(!registry.Read(entity, transform));
		RAVEN_CHECK(!registry.Write(entity, transform));
		RAVEN_CHECK(!registry.Add(entity, transform));
		RAVEN_CHECK(!registry.Has(entity, Health{}));

		// A new entity, possibly in the recycled slot, starts without components.
		const auto next = registry.Create();
		RAVEN_CHECK(!registry.Has(next, TransformComponent{}));
		RAVEN_CHECK(!registry.Has(next, Health{}));
	}

	void ManyEntitiesKeepTheirValues()
	{
		// Interleaved removal and destruction catch dense-storage swap bugs.
		Registry registry;
		std::vector<Entity> entities;
		for (int index = 0; index < 1000; ++index)
		{
			const auto entity = registry.Create();
			entities.push_back(entity);
			Health health;
			health.Value = static_cast<float>(index);
			registry.Add(entity, health);
			if (index % 2 == 0)
			{
				TransformComponent transform;
				transform.Position.X = static_cast<float>(index);
				registry.Add(entity, transform);
			}
		}
		for (int index = 0; index < 1000; ++index)
		{
			if (index % 3 == 0)
				registry.Destroy(entities[index]);
			else if (index % 7 == 0)
				registry.Remove<Health>(entities[index]);
		}
		// Reuse freed slots before checking the survivors.
		for (int index = 0; index < 100; ++index)
			registry.Add(registry.Create(), Health{});

		int mismatches = 0;
		for (int index = 0; index < 1000; ++index)
		{
			const auto entity = entities[index];
			const bool alive = index % 3 != 0;
			if (registry.IsAlive(entity) != alive)
				++mismatches;
			if (!alive)
				continue;

			Health health;
			const bool hasHealth = registry.Read(entity, health);
			if (hasHealth != (index % 7 != 0) || (hasHealth && health.Value != static_cast<float>(index)))
				++mismatches;

			TransformComponent transform;
			const bool hasTransform = registry.Read(entity, transform);
			if (hasTransform != (index % 2 == 0) ||
				(hasTransform && transform.Position.X != static_cast<float>(index)))
				++mismatches;
		}
		RAVEN_CHECK(mismatches == 0);
		RAVEN_CHECK(registry.GetEntities().size() == 1000 - 334 + 100);
	}
	void Views()
	{
		Registry registry;
		std::vector<Entity> both;
		for (int index = 0; index < 100; ++index)
		{
			const auto entity = registry.Create();
			Health health;
			health.Value = static_cast<float>(index);
			registry.Add(entity, health);
			if (index % 3 == 0)
			{
				TransformComponent transform;
				transform.Position.X = static_cast<float>(index);
				registry.Add(entity, transform);
				both.push_back(entity);
			}
		}
		// Remove one to put dense storage out of creation order.
		registry.Destroy(both[1]);
		both.erase(both.begin() + 1);

		// Each visits exactly the entities with every component, and mutable access writes through.
		std::size_t visited = 0;
		int mismatches = 0;
		registry.View<TransformComponent, const Health>().Each(
			[&](Entity entity, TransformComponent& transform, const Health& health)
			{
				++visited;
				if (!Contains(both, entity) || transform.Position.X != health.Value)
					++mismatches;
				transform.Position.Y = health.Value * 2.0f;
			});
		RAVEN_CHECK(visited == both.size());
		RAVEN_CHECK(mismatches == 0);
		for (const auto entity : both)
		{
			TransformComponent transform;
			RAVEN_CHECK(registry.Read(entity, transform) && transform.Position.Y == transform.Position.X * 2.0f);
		}

		// Returning false stops early.
		int seen = 0;
		registry.View<const Health>().Each([&](Entity, const Health&) { return ++seen < 5; });
		RAVEN_CHECK(seen == 5);

		// Structural changes are rejected while iterating and allowed afterwards.
		bool addRejected = false;
		bool removeRejected = false;
		bool destroyRejected = false;
		bool createRejected = false;
		registry.View<const Health>().Each([&](Entity entity, const Health&)
		{
			RAVEN_CHECK(registry.IsIterating());
			addRejected = !registry.Add(entity, Marker{});
			removeRejected = !registry.Remove<Health>(entity);
			destroyRejected = !registry.Destroy(entity);
			try
			{
				registry.Create();
			}
			catch (const std::logic_error&)
			{
				createRejected = true;
			}
			return false;
		});
		RAVEN_CHECK(addRejected && removeRejected && destroyRejected && createRejected);
		RAVEN_CHECK(!registry.IsIterating());
		RAVEN_CHECK(registry.Add(both.front(), Marker{}));

		// Borrowed single-entity access.
		auto* health = registry.TryGet<Health>(both.front());
		RAVEN_CHECK(health && health->Value == 0.0f);
		if (health)
			health->Value = 7.0f;
		Health read;
		RAVEN_CHECK(registry.Read(both.front(), read) && read.Value == 7.0f);
		RAVEN_CHECK(registry.TryGet<Marker>(both.back()) == nullptr);
		RAVEN_CHECK(registry.Count<TransformComponent>() == both.size());
		RAVEN_CHECK(registry.Single<Marker>() != nullptr);

		Registry empty;
		std::size_t emptyVisits = 0;
		empty.View<Health>().Each([&](Entity, Health&) { ++emptyVisits; });
		RAVEN_CHECK(emptyVisits == 0 && empty.Single<Health>() == nullptr && empty.Count<Health>() == 0);
	}
}

int main()
{
	return RavenTests::Run("RegistryTests", []
	{
		EntityLifecycle();
		EntitySnapshot();
		ComponentValues();
		TagComponents();
		DestroyErasesComponents();
		ManyEntitiesKeepTheirValues();
		Views();
	});
}
