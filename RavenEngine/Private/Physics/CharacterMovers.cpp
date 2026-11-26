#include "Physics/CharacterMovers.hpp"
#include "Physics/PresentationCheck.hpp"

#include "Raven/Components/TransformComponent.hpp"
#include "Raven/Core/Log.hpp"
#include "Raven/Scene/Scene.hpp"

#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Geometry/RayAABox.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/CollisionDispatch.h>
#include <Jolt/Physics/Collision/ShapeCast.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/PhysicsSystem.h>

#include <algorithm>
#include <format>
#include <limits>
#include <numbers>

namespace Raven::Physics
{
	namespace
	{
		using ECS::Entity;
		using Pool = ECS::Detail::PoolView;
		constexpr std::uint32_t NoSlot = 0xffffffffu;
		constexpr float Gravity = 9.81f;

		std::uint32_t IndexOf(Entity entity) noexcept
		{
			return ECS::Detail::EntityAccess::Index(entity);
		}

		// A capsule along Z centered on the entity, or a sphere when it has no straight part.
		JPH::ShapeRefC MakeCapsule(float radius, float halfHeight)
		{
			if (halfHeight <= 0.0f)
				return new JPH::SphereShape(radius);
			// Jolt's capsule lies along Y; Raven's along Z.
			return JPH::RotatedTranslatedShapeSettings(JPH::Vec3::sZero(),
				JPH::Quat::sRotation(JPH::Vec3::sAxisX(), 0.5f * JPH::JPH_PI), new JPH::CapsuleShape(halfHeight, radius))
				.Create().Get();
		}

		ECS::MoverGround GroundOf(JPH::CharacterBase::EGroundState state) noexcept
		{
			switch (state)
			{
			case JPH::CharacterBase::EGroundState::OnGround: return ECS::MoverGround::OnGround;
			case JPH::CharacterBase::EGroundState::OnSteepGround: return ECS::MoverGround::OnSteepGround;
			case JPH::CharacterBase::EGroundState::NotSupported: return ECS::MoverGround::NotSupported;
			default: return ECS::MoverGround::InAir;
			}
		}

		Vec3 Clamp(Vec3 value, float maximum) noexcept
		{
			const float length = std::sqrt(Dot(value, value));
			return length > maximum && length > 0.0f ? value * (maximum / length) : value;
		}

		// The fields the engine writes; differences in the others are game edits.
		bool SameSettings(const ECS::MoverComponent& left, const ECS::MoverComponent& right) noexcept
		{
			return left.Radius == right.Radius && left.HalfHeight == right.HalfHeight && left.MaxSlope == right.MaxSlope &&
				left.Layer == right.Layer && left.Backend == right.Backend;
		}
	}

	// Character-vs-character collision (FD-47). Jolt's simple checker compares every pair of characters,
	// which grew to 22 ms a step at 500 movers; this one does the same work for the characters in nearby
	// cells of an XY grid built once per step. Stored bounds carry a margin for movement during the
	// step, and every candidate is tested at its current transform, as Jolt's checker does. Cells are
	// visited in a fixed order and characters in insertion order, so results are deterministic.
	class CharacterGrid final : public JPH::CharacterVsCharacterCollision
	{
	public:
		// Movement within one step that stored bounds tolerate.
		static constexpr float Margin = 1.0f;

		void Add(JPH::CharacterVirtual* character) { m_Characters.push_back(character); }
		void Remove(const JPH::CharacterVirtual* character)
		{
			std::erase(m_Characters, character);
		}

		void Build()
		{
			m_Cells.clear();
			float extent = 1.0f;
			std::vector<JPH::AABox> bounds;
			bounds.reserve(m_Characters.size());
			for (const auto* character : m_Characters)
			{
				auto box = character->GetShape()->GetWorldSpaceBounds(character->GetCenterOfMassTransform().ToMat44(),
					JPH::Vec3::sOne());
				box.ExpandBy(JPH::Vec3::sReplicate(Margin + character->GetCharacterPadding()));
				extent = std::max({ extent, box.GetSize().GetX(), box.GetSize().GetY() });
				bounds.push_back(box);
			}
			m_CellSize = extent;
			for (std::uint32_t index = 0; index < m_Characters.size(); ++index)
				Visit(bounds[index], [&](std::uint64_t key) { m_Cells.emplace_back(key, index); });
			std::stable_sort(m_Cells.begin(), m_Cells.end(),
				[](const auto& left, const auto& right) { return left.first < right.first; });
			m_Stamps.assign(m_Characters.size(), 0);
			m_Stamp = 0;
		}

		void CollideCharacter(const JPH::CharacterVirtual* character, JPH::RMat44Arg centerOfMass,
			const JPH::CollideShapeSettings& collideSettings, JPH::RVec3Arg baseOffset,
			JPH::CollideShapeCollector& collector) const noexcept override
		{
			const JPH::Mat44 transform1 = centerOfMass.PostTranslated(-baseOffset).ToMat44();
			const auto* shape1 = character->GetShape();
			JPH::CollideShapeSettings settings = collideSettings;
			const auto bounds1 = shape1->GetWorldSpaceBounds(transform1, JPH::Vec3::sOne());
			auto query = shape1->GetWorldSpaceBounds(centerOfMass.ToMat44(), JPH::Vec3::sOne());
			query.ExpandBy(JPH::Vec3::sReplicate(collideSettings.mMaxSeparationDistance + Margin));
			ForEachNear(query, character, [&](const JPH::CharacterVirtual* other)
			{
				if (collector.ShouldEarlyOut())
					return;
				const JPH::Mat44 transform2 = other->GetCenterOfMassTransform().PostTranslated(-baseOffset).ToMat44();
				settings.mMaxSeparationDistance = collideSettings.mMaxSeparationDistance + other->GetCharacterPadding();
				auto bounds2 = other->GetShape()->GetWorldSpaceBounds(transform2, JPH::Vec3::sOne());
				bounds2.ExpandBy(JPH::Vec3::sReplicate(settings.mMaxSeparationDistance));
				if (!bounds1.Overlaps(bounds2))
					return;
				collector.SetUserData(reinterpret_cast<JPH::uint64>(other));
				JPH::CollisionDispatch::sCollideShapeVsShape(shape1, other->GetShape(), JPH::Vec3::sOne(), JPH::Vec3::sOne(),
					transform1, transform2, JPH::SubShapeIDCreator(), JPH::SubShapeIDCreator(), settings, collector);
			});
			collector.SetUserData(0);
		}

		void CastCharacter(const JPH::CharacterVirtual* character, JPH::RMat44Arg centerOfMass, JPH::Vec3Arg direction,
			const JPH::ShapeCastSettings& castSettings, JPH::RVec3Arg baseOffset, JPH::CastShapeCollector& collector) const noexcept override
		{
			const JPH::Mat44 transform1 = centerOfMass.PostTranslated(-baseOffset).ToMat44();
			const JPH::ShapeCast cast(character->GetShape(), JPH::Vec3::sOne(), transform1, direction);
			const auto origin = cast.mShapeWorldBounds.GetCenter();
			const auto extents = cast.mShapeWorldBounds.GetExtent() + JPH::Vec3::sReplicate(castSettings.mExtraConvexRadius);
			JPH::ShapeCastSettings settings = castSettings;
			// The swept bounds in world space.
			auto query = character->GetShape()->GetWorldSpaceBounds(centerOfMass.ToMat44(), JPH::Vec3::sOne());
			auto end = query;
			end.Translate(direction);
			query.Encapsulate(end);
			query.ExpandBy(JPH::Vec3::sReplicate(castSettings.mExtraConvexRadius + Margin));
			ForEachNear(query, character, [&](const JPH::CharacterVirtual* other)
			{
				if (collector.ShouldEarlyOut())
					return;
				const JPH::Mat44 transform2 = other->GetCenterOfMassTransform().PostTranslated(-baseOffset).ToMat44();
				settings.mExtraConvexRadius = castSettings.mExtraConvexRadius + other->GetCharacterPadding();
				auto bounds2 = other->GetShape()->GetWorldSpaceBounds(transform2, JPH::Vec3::sOne());
				bounds2.ExpandBy(extents + JPH::Vec3::sReplicate(other->GetCharacterPadding()));
				if (!JPH::RayAABoxHits(origin, direction, bounds2.mMin, bounds2.mMax))
					return;
				collector.SetUserData(reinterpret_cast<JPH::uint64>(other));
				JPH::CollisionDispatch::sCastShapeVsShapeWorldSpace(cast, settings, other->GetShape(), JPH::Vec3::sOne(), {},
					transform2, JPH::SubShapeIDCreator(), JPH::SubShapeIDCreator(), collector);
			});
			collector.SetUserData(0);
		}

	private:
		static std::uint64_t Key(std::int32_t x, std::int32_t y) noexcept
		{
			return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(x)) << 32) | static_cast<std::uint32_t>(y);
		}

		// Every cell the bounds cover, in a fixed order.
		template<typename Function>
		void Visit(const JPH::AABox& bounds, Function&& function) const
		{
			const auto cell = [&](float value) { return static_cast<std::int32_t>(std::floor(value / m_CellSize)); };
			const auto x0 = cell(bounds.mMin.GetX()), x1 = cell(bounds.mMax.GetX());
			const auto y0 = cell(bounds.mMin.GetY()), y1 = cell(bounds.mMax.GetY());
			for (auto x = x0; x <= x1; ++x)
				for (auto y = y0; y <= y1; ++y)
					function(Key(x, y));
		}

		template<typename Function>
		void ForEachNear(const JPH::AABox& bounds, const JPH::CharacterVirtual* self, Function&& function) const
		{
			if (++m_Stamp == 0)
			{
				std::fill(m_Stamps.begin(), m_Stamps.end(), 0u);
				m_Stamp = 1;
			}
			Visit(bounds, [&](std::uint64_t key)
			{
				auto item = std::lower_bound(m_Cells.begin(), m_Cells.end(), key,
					[](const auto& cell, std::uint64_t value) { return cell.first < value; });
				for (; item != m_Cells.end() && item->first == key; ++item)
				{
					const auto index = item->second;
					if (m_Stamps[index] == m_Stamp || m_Characters[index] == self)
						continue;
					m_Stamps[index] = m_Stamp;
					function(m_Characters[index]);
				}
			});
		}

		std::vector<JPH::CharacterVirtual*> m_Characters;
		std::vector<std::pair<std::uint64_t, std::uint32_t>> m_Cells;
		float m_CellSize = 1.0f;
		mutable std::vector<std::uint32_t> m_Stamps;
		mutable std::uint32_t m_Stamp = 0;
	};

	// Rejects every character's inner body in character queries. Jolt skips only the character's
	// own inner body; another character's would be solved a second time as a static obstacle, after
	// the grid has already handled that pair (docs/Physics.md).
	class InnerBodyFilter final : public JPH::BodyFilter
	{
	public:
		void Add(JPH::BodyID body)
		{
			const auto index = body.GetIndex();
			if (index >= m_Inner.size())
				m_Inner.resize(index + 1, 0);
			m_Inner[index] = 1;
		}

		void Remove(JPH::BodyID body) noexcept
		{
			if (body.GetIndex() < m_Inner.size())
				m_Inner[body.GetIndex()] = 0;
		}

		void Clear() noexcept
		{
			m_Inner.clear();
		}

		bool ShouldCollide(const JPH::BodyID& body) const noexcept override
		{
			const auto index = body.GetIndex();
			return index >= m_Inner.size() || m_Inner[index] == 0;
		}

	private:
		// Indexed by body index; a removed body's index is cleared before Jolt can reuse it.
		std::vector<std::uint8_t> m_Inner;
	};

	CharacterMovers::CharacterMovers(JPH::PhysicsSystem& system) noexcept
		: m_System(system), m_Characters(std::make_unique<CharacterGrid>()),
		m_InnerBodies(std::make_unique<InnerBodyFilter>())
	{
	}

	CharacterMovers::~CharacterMovers()
	{
		// Characters remove their inner bodies from the system, which outlives this object.
		for (auto& entry : m_Entries)
			if (entry.Character)
			{
				m_Characters->Remove(entry.Character);
				entry.Character = nullptr;
			}
	}

	void CharacterMovers::Clear(Links& links) noexcept
	{
		for (auto& entry : m_Entries)
			Destroy(entry, links);
		m_Entries.clear();
		m_SlotOfIndex.clear();
		m_Order.clear();
		m_OrderDirty = false;
		m_InnerBodies->Clear();
	}

	std::uint32_t CharacterMovers::GetCount() const noexcept
	{
		return static_cast<std::uint32_t>(std::count_if(m_Entries.begin(), m_Entries.end(),
			[](const Entry& entry) { return entry.Character != nullptr; }));
	}

	std::uint32_t CharacterMovers::GetRejected() const noexcept
	{
		return static_cast<std::uint32_t>(m_Entries.size()) - GetCount();
	}

	bool CharacterMovers::Evaluate(Scene& scene, const Pools& pools, Entity entity, ECS::MoverComponent& mover,
		Vec3& position, std::string& problem) const
	{
		const auto moverIndex = pools.Movers.Find(entity);
		const auto transformIndex = pools.Transforms.Find(entity);
		if (moverIndex == Pool::NoIndex || transformIndex == Pool::NoIndex)
		{
			problem = "it has no MoverComponent and TransformComponent";
			return false;
		}
		mover = pools.Movers.At<ECS::MoverComponent>(moverIndex);
		position = pools.Transforms.At<ECS::TransformComponent>(transformIndex).Position;
		if (pools.Colliders.Find(entity) != Pool::NoIndex || pools.Bodies.Find(entity) != Pool::NoIndex)
			problem = "a mover cannot also have a ColliderComponent or RigidBodyComponent";
		else if (!scene.GetParent(entity).IsNull())
			problem = "a mover must be a scene root";
		else if (mover.Backend != ECS::MoverBackend::Character)
			problem = "Backend is not Character";
		else if (mover.Layer >= ECS::PhysicsLayerCount)
			problem = std::format("Layer {} is not below {}", mover.Layer, ECS::PhysicsLayerCount);
		else if (!(mover.Radius > 0.0f) || !std::isfinite(mover.Radius) || !(mover.HalfHeight >= 0.0f) ||
			!std::isfinite(mover.HalfHeight))
			problem = "a mover needs a positive, finite Radius and a HalfHeight of zero or more";
		else if (!(mover.MaxSpeed >= 0.0f) || !std::isfinite(mover.MaxSpeed) || !std::isfinite(mover.GravityScale))
			problem = "MaxSpeed must be zero or more and GravityScale finite";
		else if (!(mover.MaxSlope > 0.0f) || !(mover.MaxSlope <= 0.5f * std::numbers::pi_v<float>))
			problem = "MaxSlope must be above 0 and at most pi/2";
		else if (!IsFinite(mover.DesiredVelocity) || !IsFinite(mover.Velocity) || !IsFinite(position))
			problem = "DesiredVelocity, Velocity and Position must be finite";
		return problem.empty();
	}

	void CharacterMovers::Report(Scene& scene, Entry& entry, const std::string& problem)
	{
		if (entry.Problem == problem)
			return;
		entry.Problem = problem;
		const auto name = scene.GetAuthoredId(entry.Target);
		Log(LogLevel::Warning, std::format("Mover on '{}' left out of the physics world: {}",
			name.empty() ? std::format("entity {:016x}", entry.Id) : name, problem));
	}

	void CharacterMovers::Create(Entry& entry, Links& links)
	{
		const auto& mover = entry.Mover;
		JPH::Ref<JPH::CharacterVirtualSettings> settings = new JPH::CharacterVirtualSettings();
		settings->mShape = MakeCapsule(mover.Radius, mover.HalfHeight);
		settings->mInnerBodyShape = settings->mShape;
		settings->mInnerBodyLayer = MakeObjectLayer(mover.Layer, true);
		// Z-up (FD-32); the lower hemisphere of the capsule is what stands on the ground.
		settings->mUp = JPH::Vec3::sAxisZ();
		settings->mSupportingVolume = JPH::Plane(JPH::Vec3::sAxisZ(), mover.HalfHeight);
		settings->mMaxSlopeAngle = mover.MaxSlope;
		entry.Character = new JPH::CharacterVirtual(settings, JPH::RVec3(ToJolt(entry.Position)), JPH::Quat::sIdentity(),
			entry.Id, &m_System);
		entry.Character->SetLinearVelocity(ToJolt(mover.Velocity));
		entry.Character->SetCharacterVsCharacterCollision(m_Characters.get());
		m_Characters->Add(entry.Character);
		if (!entry.Character->GetInnerBodyID().IsInvalid())
		{
			links.BodyIds[entry.Character->GetInnerBodyID().GetIndexAndSequenceNumber()] = entry.Id;
			m_InnerBodies->Add(entry.Character->GetInnerBodyID());
		}
	}

	void CharacterMovers::Destroy(Entry& entry, Links& links) noexcept
	{
		if (!entry.Character)
			return;
		if (links.EndContacts)
			links.EndContacts(entry.Id);
		if (!entry.Character->GetInnerBodyID().IsInvalid())
		{
			links.BodyIds.erase(entry.Character->GetInnerBodyID().GetIndexAndSequenceNumber());
			m_InnerBodies->Remove(entry.Character->GetInnerBodyID());
		}
		m_Characters->Remove(entry.Character);
		entry.Character = nullptr;
	}

	void CharacterMovers::Remove(std::uint32_t slot, Links& links)
	{
		Destroy(m_Entries[slot], links);
		m_SlotOfIndex[IndexOf(m_Entries[slot].Target)] = NoSlot;
		const auto last = static_cast<std::uint32_t>(m_Entries.size() - 1);
		if (slot != last)
		{
			m_Entries[slot] = std::move(m_Entries[last]);
			m_SlotOfIndex[IndexOf(m_Entries[slot].Target)] = slot;
		}
		m_Entries.pop_back();
		m_OrderDirty = true;
	}

	// New values: a changed shape, layer or slope rebuilds the character; a changed position is a
	// teleport; a changed velocity sets its momentum. Desired velocity and speeds apply at Update.
	void CharacterMovers::Apply(Scene& scene, const Pools& pools, std::uint32_t slot, JPH::TempAllocator& temporary,
		Links& links)
	{
		auto& entry = m_Entries[slot];
		ECS::MoverComponent mover;
		Vec3 position;
		std::string problem;
		if (!Evaluate(scene, pools, entry.Target, mover, position, problem))
		{
			Destroy(entry, links);
			Report(scene, entry, problem);
			return;
		}
		if (!entry.Character || !SameSettings(mover, entry.Mover))
		{
			const bool shapeOnly = entry.Character && mover.Layer == entry.Mover.Layer &&
				mover.MaxSlope == entry.Mover.MaxSlope && mover.Backend == entry.Mover.Backend;
			if (shapeOnly)
			{
				const JPH::DefaultBroadPhaseLayerFilter trees(m_System.GetObjectVsBroadPhaseLayerFilter(),
					MakeObjectLayer(mover.Layer, true));
				const JPH::DefaultObjectLayerFilter layers(m_System.GetObjectLayerPairFilter(), MakeObjectLayer(mover.Layer, true));
				const auto shape = MakeCapsule(mover.Radius, mover.HalfHeight);
				entry.Character->SetShape(shape, std::numeric_limits<float>::max(), trees, layers, *m_InnerBodies, {}, temporary);
				entry.Character->SetInnerBodyShape(shape);
				entry.Character->SetSupportingVolume(JPH::Plane(JPH::Vec3::sAxisZ(), mover.HalfHeight));
			}
			else
			{
				Destroy(entry, links);
				entry.Mover = mover;
				entry.Position = position;
				if (m_System.GetNumBodies() >= m_System.GetMaxBodies())
				{
					Report(scene, entry, "the physics body limit is reached; raise physics-limits in the project file");
					return;
				}
				Create(entry, links);
				if (!entry.Problem.empty())
					Log(LogLevel::Info, std::format("Mover on '{}' is back in the physics world",
						scene.GetAuthoredId(entry.Target)));
				entry.Problem.clear();
				m_OrderDirty = true;
				return;
			}
		}
		if (!SameVector(position, entry.Position))
			entry.Character->SetPosition(JPH::RVec3(ToJolt(position)));
		if (!SameVector(mover.Velocity, entry.Mover.Velocity))
			entry.Character->SetLinearVelocity(ToJolt(mover.Velocity));
		entry.Mover = mover;
		entry.Position = position;
	}

	void CharacterMovers::Sync(Scene& scene, ECS::Registry& registry, const Pools& pools,
		std::span<const ECS::LifecycleEntry> entries, bool rebuild, std::uint32_t since, JPH::TempAllocator& temporary,
		Links& links)
	{
		std::vector<Entity> visit;
		if (rebuild)
		{
			while (!m_Entries.empty())
				Remove(static_cast<std::uint32_t>(m_Entries.size() - 1), links);
			for (std::size_t dense = 0; dense < pools.Movers.Count; ++dense)
				visit.push_back(pools.Movers.Entities[dense]);
		}
		else
		{
			for (const auto& entry : entries)
				visit.push_back(entry.Target);
			for (const auto& entry : m_Entries)
			{
				const auto mover = pools.Movers.Find(entry.Target);
				const auto transform = pools.Transforms.Find(entry.Target);
				if ((mover != Pool::NoIndex && pools.Movers.Ticks[mover] > since) ||
					(transform != Pool::NoIndex && pools.Transforms.Ticks[transform] > since))
					visit.push_back(entry.Target);
			}
		}
		// New movers are created in persistent-ID order, so two runs create the same inner bodies.
		std::sort(visit.begin(), visit.end(), [&](Entity left, Entity right)
		{
			return scene.GetRef(left).Id < scene.GetRef(right).Id;
		});
		visit.erase(std::unique(visit.begin(), visit.end()), visit.end());
		for (const auto entity : visit)
		{
			const auto index = IndexOf(entity);
			auto slot = index < m_SlotOfIndex.size() ? m_SlotOfIndex[index] : NoSlot;
			if (slot != NoSlot && !(m_Entries[slot].Target == entity))
			{
				if (!registry.IsAlive(m_Entries[slot].Target))
					Remove(slot, links);
				slot = NoSlot;
			}
			const bool present = registry.IsAlive(entity) && scene.Owns(entity) &&
				pools.Movers.Find(entity) != Pool::NoIndex && pools.Transforms.Find(entity) != Pool::NoIndex;
			if (!present)
			{
				if (slot != NoSlot)
					Remove(slot, links);
				continue;
			}
			if (slot == NoSlot)
			{
				if (index >= m_SlotOfIndex.size())
					m_SlotOfIndex.resize(index + 1, NoSlot);
				slot = m_SlotOfIndex[index] = static_cast<std::uint32_t>(m_Entries.size());
				Entry entry;
				entry.Target = entity;
				entry.Id = scene.GetRef(entity).Id;
				ReportPresentation(scene, registry, entity);
				m_Entries.push_back(std::move(entry));
				m_OrderDirty = true;
			}
			Apply(scene, pools, slot, temporary, links);
		}
	}

	void CharacterMovers::Update(float seconds, JPH::TempAllocator& temporary)
	{
		if (m_OrderDirty)
		{
			m_Order.resize(m_Entries.size());
			for (std::uint32_t slot = 0; slot < m_Order.size(); ++slot)
				m_Order[slot] = slot;
			std::sort(m_Order.begin(), m_Order.end(), [&](std::uint32_t left, std::uint32_t right)
			{
				return m_Entries[left].Id < m_Entries[right].Id;
			});
			m_OrderDirty = false;
		}
		const JPH::Vec3 up = JPH::Vec3::sAxisZ();
		m_Characters->Build();
		for (const auto slot : m_Order)
		{
			auto& entry = m_Entries[slot];
			if (!entry.Character)
				continue;
			auto& character = *entry.Character;
			const auto& mover = entry.Mover;
			const JPH::Vec3 gravity(0.0f, 0.0f, -Gravity * mover.GravityScale);
			character.UpdateGroundVelocity();
			JPH::Vec3 velocity;
			JPH::CharacterVirtual::ExtendedUpdateSettings settings;
			settings.mWalkStairsStepUp = JPH::Vec3(0.0f, 0.0f, 0.4f);
			if (mover.GravityScale == 0.0f)
			{
				// Flying: all three axes steer.
				velocity = ToJolt(Clamp(mover.DesiredVelocity, mover.MaxSpeed));
				settings.mStickToFloorStepDown = JPH::Vec3::sZero();
			}
			else
			{
				// As Jolt advises: on walkable ground, follow the ground and jump from it; in the air,
				// keep the vertical speed; gravity always pulls; X and Y steer.
				const float vertical = character.GetLinearVelocity().Dot(up);
				const JPH::Vec3 ground = character.GetGroundVelocity();
				const bool towardGround = vertical - ground.Dot(up) < 0.1f;
				if (character.GetGroundState() == JPH::CharacterBase::EGroundState::OnGround && towardGround)
				{
					velocity = ground;
					if (mover.DesiredVelocity.Z > 0.0f)
						velocity += up * mover.DesiredVelocity.Z;
				}
				else
					velocity = up * vertical;
				velocity += gravity * seconds;
				const auto steer = Clamp({ mover.DesiredVelocity.X, mover.DesiredVelocity.Y, 0.0f }, mover.MaxSpeed);
				velocity += ToJolt(steer);
				settings.mStickToFloorStepDown = JPH::Vec3(0.0f, 0.0f, -0.5f);
			}
			character.SetLinearVelocity(velocity);
			const auto layer = MakeObjectLayer(mover.Layer, true);
			const JPH::DefaultBroadPhaseLayerFilter trees(m_System.GetObjectVsBroadPhaseLayerFilter(), layer);
			const JPH::DefaultObjectLayerFilter layers(m_System.GetObjectLayerPairFilter(), layer);
			character.ExtendedUpdate(seconds, gravity, settings, trees, layers, *m_InnerBodies, {}, temporary);
		}
	}

	void CharacterMovers::WriteBack(ECS::Registry& registry)
	{
		for (auto& entry : m_Entries)
		{
			if (!entry.Character)
				continue;
			const auto& character = *entry.Character;
			const auto position = FromJolt(JPH::Vec3(character.GetPosition()));
			const auto velocity = FromJolt(character.GetLinearVelocity());
			const auto ground = GroundOf(character.GetGroundState());
			const auto normal = ground == ECS::MoverGround::InAir ? Vec3{} : FromJolt(character.GetGroundNormal());
			if (!SameVector(position, entry.Position))
				if (auto* transform = registry.TryGet<ECS::TransformComponent>(entry.Target))
				{
					transform->Position = position;
					entry.Position = position;
				}
			if (!SameVector(velocity, entry.Mover.Velocity) || ground != entry.Mover.Ground ||
				!SameVector(normal, entry.Mover.GroundNormal))
				if (auto* mover = registry.TryGet<ECS::MoverComponent>(entry.Target))
				{
					mover->Velocity = entry.Mover.Velocity = velocity;
					mover->Ground = entry.Mover.Ground = ground;
					mover->GroundNormal = entry.Mover.GroundNormal = normal;
				}
		}
	}
}
