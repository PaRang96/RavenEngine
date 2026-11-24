// Guards Registry behavior that the M1 storage rewrite must preserve (docs/ECSDesign.md).
// Uses only call forms that stay valid after the rewrite.

#include "Raven/Components/TransformComponent.hpp"
#include "Raven/ECS/Registry.hpp"
#include "Support/Check.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <stdexcept>
#include <string>
#include <tuple>
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

	// Health after an edit: the type and its field were renamed, and a field was added.
	namespace Next
	{
		struct Health : Raven::ECS::Component
		{
			float Points = 100.0f;
			float Armor = 5.0f;
			RAVEN_COMPONENT(RavenTests::Next::Health, Points, Armor);
			RAVEN_COMPONENT_ALIASES("RavenTests::Health", "Value=Points");
		};
	}
}

namespace
{
	using Raven::ECS::Entity;
	using Raven::ECS::Registry;
	using Raven::ECS::TransformComponent;
	using RavenTests::Health;
	using RavenTests::Marker;
	namespace Next = RavenTests::Next;

	bool Equal(Raven::Vec3 left, Raven::Vec3 right)
	{
		return left.X == right.X && left.Y == right.Y && left.Z == right.Z;
	}

	bool Equal(const TransformComponent& left, const TransformComponent& right)
	{
		return Equal(left.Position, right.Position) && Equal(left.Rotation, right.Rotation) &&
			Equal(left.Scale, right.Scale);
	}

	// A field type change of equal size must change the layout hash (docs/DataModelDesign.md, FD-08),
	// or a reload would reinterpret retained bytes.
	template<typename... T>
	constexpr std::uint64_t LayoutOf() noexcept
	{
		return Raven::ECS::Detail::ComponentFields<std::tuple<T&...>>::HashLayout(Raven::ECS::Detail::HashOffset);
	}
	static_assert(LayoutOf<float>() != LayoutOf<std::int32_t>());
	static_assert(LayoutOf<std::uint32_t>() != LayoutOf<std::int32_t>());
	static_assert(LayoutOf<std::array<float, 4>>() != LayoutOf<Raven::Vec4>());

	// The field table names, places and types every field, and the registered defaults are
	// the value-initialized component.
	void FieldTables()
	{
		using Raven::ECS::FieldKind;
		const auto& table = Raven::ECS::ComponentTraits<TransformComponent>::FieldTable;
		RAVEN_CHECK(table.size() == 3);
		RAVEN_CHECK(table[0].Name == "Position" && table[1].Name == "Rotation" && table[2].Name == "Scale");
		RAVEN_CHECK(table[1].Offset == offsetof(TransformComponent, Rotation));
		RAVEN_CHECK(table[2].Offset == offsetof(TransformComponent, Scale) && table[2].Kind == FieldKind::Vec3);
		RAVEN_CHECK(Raven::ECS::ComponentTraits<Marker>::FieldTable.empty());

		Registry registry;
		RAVEN_CHECK(!registry.FindSchema(Health::TypeId));
		const auto entity = registry.Create();
		Health wounded;
		wounded.Value = 5.0f;
		RAVEN_CHECK(registry.Add(entity, wounded));
		const auto* health = registry.FindSchema(Health::TypeId);
		RAVEN_CHECK(health && health->Name == "RavenTests::Health" && health->Fields.size() == 1);
		if (!health)
			return;
		if (health->Defaults.size() == sizeof(float))
		{
			float value = 0;
			std::memcpy(&value, health->Defaults.data(), sizeof(value));
			RAVEN_CHECK(value == 100.0f && health->Fields[0].Kind == FieldKind::Float32);
		}
		RAVEN_CHECK(registry.Register<Health>());
		RAVEN_CHECK(registry.Register<TransformComponent>() && registry.GetSchemas().size() == 2);

		// The same ID with another layout is refused, and the registered schema stays.
		auto changed = Raven::ECS::MakeComponentSchema(Raven::ECS::ComponentTraits<Health>::Info,
			health->Defaults);
		changed.LayoutHash ^= 1;
		RAVEN_CHECK(!registry.RegisterSchema(changed));
		RAVEN_CHECK(registry.FindSchema(Health::TypeId)->LayoutHash == Raven::ECS::ComponentTraits<Health>::Info.LayoutHash);
	}

	// Field migration (docs/HotReloadDesign.md, H6; docs/DataModelDesign.md, FD-16): values move by
	// field name or old name, new fields take defaults, and a type change is refused.
	void Migration()
	{
		Registry registry;
		const auto first = registry.Create();
		const auto second = registry.Create();
		Health value;
		value.Value = 5.0f;
		RAVEN_CHECK(registry.Add(first, value));
		value.Value = 7.0f;
		RAVEN_CHECK(registry.Add(second, value));

		std::array<std::byte, sizeof(Next::Health)> defaults;
		RAVEN_CHECK(Raven::ECS::Detail::CaptureDefaults<Next::Health>(defaults.data()));
		const auto next = Raven::ECS::MakeComponentSchema(Raven::ECS::ComponentTraits<Next::Health>::Info, defaults);
		RAVEN_CHECK(next.OldNames.size() == 1 && next.FindField("Value") == &next.Fields[0]);

		// A field that changed type is refused, and nothing changes.
		auto retyped = next;
		retyped.Fields[0].Kind = Raven::ECS::FieldKind::Int32;
		RAVEN_CHECK(!registry.CheckMigration(std::span(&retyped, 1)).empty());
		RAVEN_CHECK_THROWS(registry.Migrate(std::span(&retyped, 1)));
		RAVEN_CHECK(registry.Read(first, value) && value.Value == 5.0f && registry.FindSchema(Health::TypeId));

		RAVEN_CHECK(registry.CheckMigration(std::span(&next, 1)).empty());
		const auto reports = registry.Migrate(std::span(&next, 1));
		RAVEN_CHECK(reports.size() == 1 && reports[0].Elements == 2);
		RAVEN_CHECK(reports.size() == 1 && reports[0].Added == std::vector<std::string>{ "Armor" } &&
			reports[0].Renamed.size() == 1 && reports[0].Removed.empty());
		RAVEN_CHECK(!registry.FindSchema(Health::TypeId) && registry.FindSchema(Next::Health::TypeId));
		Next::Health migrated;
		RAVEN_CHECK(registry.Read(first, migrated) && migrated.Points == 5.0f && migrated.Armor == 5.0f);
		RAVEN_CHECK(registry.Read(second, migrated) && migrated.Points == 7.0f);
		RAVEN_CHECK(!registry.Has<Health>(first));
		// The migrated layout is now the registered one.
		RAVEN_CHECK(registry.CheckMigration(std::span(&next, 1)).empty() && registry.Migrate(std::span(&next, 1)).empty());
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
	// The lifecycle journal (docs/GpuDrivenDesign.md, G1) is a new Registry contract M3 depends on.
	void LifecycleJournal()
	{
		using Raven::ECS::LifecycleEvent;
		Registry registry;
		const auto before = registry.Create();
		RAVEN_CHECK(registry.Add(before, Health{}));

		const auto first = registry.SubscribeLifecycle<Health>();
		const auto second = registry.SubscribeLifecycle<Health>(2);
		RAVEN_CHECK(first != 0 && second != 0 && first != second);
		auto batch = registry.ReadLifecycle(first);
		RAVEN_CHECK(batch.Resync && batch.Entries.empty());
		RAVEN_CHECK(registry.ReadLifecycle(second).Resync);

		const auto added = registry.Create();
		RAVEN_CHECK(registry.Add(added, Health{}) && registry.Add(added, Marker{}));
		RAVEN_CHECK(registry.Remove<Health>(before));
		RAVEN_CHECK(registry.Destroy(added));
		batch = registry.ReadLifecycle(first);
		RAVEN_CHECK(!batch.Resync && batch.Entries.size() == 3);
		if (batch.Entries.size() == 3)
		{
			RAVEN_CHECK(batch.Entries[0].Target == added && batch.Entries[0].Event == LifecycleEvent::Added);
			RAVEN_CHECK(batch.Entries[1].Target == before && batch.Entries[1].Event == LifecycleEvent::Removed);
			RAVEN_CHECK(batch.Entries[2].Target == added && batch.Entries[2].Event == LifecycleEvent::Removed);
		}
		RAVEN_CHECK(registry.ReadLifecycle(first).Entries.empty());

		// The second subscriber allows two unread entries, so the third forces a resync.
		batch = registry.ReadLifecycle(second);
		RAVEN_CHECK(batch.Resync && batch.Entries.empty());
		const auto later = registry.Create();
		RAVEN_CHECK(registry.Add(later, Health{}));
		batch = registry.ReadLifecycle(second);
		RAVEN_CHECK(!batch.Resync && batch.Entries.size() == 1 && batch.Entries.front().Target == later);
		RAVEN_CHECK(registry.ReadLifecycle(first).Entries.size() == 1);

		// Unsubscribed types record nothing, and unknown subscriptions read empty.
		registry.UnsubscribeLifecycle(first);
		registry.UnsubscribeLifecycle(second);
		RAVEN_CHECK(registry.Remove<Health>(later));
		RAVEN_CHECK(registry.ReadLifecycle(first).Entries.empty() && !registry.ReadLifecycle(first).Resync);
		const auto marker = registry.SubscribeLifecycle<Marker>();
		RAVEN_CHECK(registry.Add(later, Marker{}));
		RAVEN_CHECK(registry.ReadLifecycle(marker).Entries.size() == 1);
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
		LifecycleJournal();
		FieldTables();
		Migration();
	});
}
