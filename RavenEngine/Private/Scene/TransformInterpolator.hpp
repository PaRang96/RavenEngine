#pragma once

#include "Raven/Components/TransformComponent.hpp"
#include "Raven/Core/Api.hpp"
#include "Raven/ECS/Entity.hpp"

#include <cstdint>
#include <unordered_map>

namespace Raven
{
	class Scene;

	// Render interpolation between fixed ticks (docs/GameServicesDesign.md, FD-34). For entities
	// tagged InterpolateComponent, and for dynamic and kinematic physics bodies, it keeps the local
	// transform at the start of the latest fixed step and after it. While a frame is drawn, Apply
	// writes the blend of the two into TransformComponent
	// and Restore writes the latest tick back, so game code, saves and the next steps never see a
	// blended value. Only rendering (and picking during it) does.
	class TransformInterpolator final
	{
	public:
		// Before each fixed step: the transforms that step starts from.
		RAVEN_API void BeforeStep(Scene& scene);
		// After the frame's steps: the latest tick.
		RAVEN_API void AfterSteps(Scene& scene);
		// Writes blended transforms for drawing. `alpha` is the fraction of a step since the latest
		// tick. An entity whose transform changed after the steps (for example a teleport in Update)
		// is drawn where it is. True when anything was written.
		RAVEN_API bool Apply(Scene& scene, float alpha);
		// Writes the latest tick back after drawing.
		RAVEN_API void Restore(Scene& scene);
		// Draws every entity at its latest tick until the next step: after a pause or a reset.
		RAVEN_API void Snap() noexcept;
		// A new scene: forget everything.
		void Clear() noexcept { m_Entries.clear(); }
		std::size_t GetCount() const noexcept { return m_Entries.size(); }

	private:
		// Entities tagged InterpolateComponent, and dynamic and kinematic bodies.
		template<typename Function>
		void ForEachMoving(Scene& scene, Function&& function);

		struct Entry
		{
			ECS::TransformComponent Previous;
			ECS::TransformComponent Current;
			bool HasPrevious = false;
			bool HasCurrent = false;
			bool Applied = false;
			// Reached by this frame's BeforeStep or AfterSteps; others are dropped.
			bool Seen = false;
		};
		// Keyed by persistent entity ID.
		std::unordered_map<std::uint64_t, Entry> m_Entries;
	};
}
