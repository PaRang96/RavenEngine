#pragma once

#include "Raven/ECS/Component.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>

namespace Raven
{
	// The simulation's only door for player input (docs/GameServicesDesign.md, FD-47). Controllers
	// (Control or Update systems) emit records; fixed-step systems read the records of their tick.
	// Mapped actions travel the same way: SystemContext::Actions in a fixed step is the action
	// snapshot of that tick, the stream's default record.
	//
	// Records are values of component types and follow component rules. They name entities only by
	// EntityRef, never by Entity handles, so a record means the same thing in another run or on
	// another machine. A record emitted in a frame reaches that frame's first fixed step, or the next
	// one when the frame has none. Records still waiting when the game module is replaced are
	// dropped (at most one tick of input). AI does not emit records: it acts inside the fixed step.
	// Continuous intent (steering, aim) must therefore be latched in a component by the fixed step,
	// or read from Actions: a frame's later catch-up steps see no record (Arena's CraftInputComponent).
	class InputStream
	{
	public:
		struct Record
		{
			std::uint64_t Type = 0;
			std::uint64_t LayoutHash = 0;
			std::uint8_t Player = 0;
			const std::byte* Data = nullptr;
		};

		template<ECS::ComponentType T>
		void Emit(const T& record, std::uint8_t player = 0)
		{
			const auto& info = ECS::ComponentTraits<T>::Info;
			for (std::size_t field = 0; field < info.FieldCount; ++field)
				if (info.Fields[field].Kind == ECS::FieldKind::Entity)
					throw std::invalid_argument(std::string(info.Name) +
						": input records name entities by EntityRef, not Entity");
			Push(info, &record, player);
		}

		// Calls `visit(const T&, player)` for each record of type T in this tick, in emission order.
		template<ECS::ComponentType T, typename Visit>
		void ForEach(Visit&& visit) const
		{
			const auto& info = ECS::ComponentTraits<T>::Info;
			for (std::size_t index = 0; index < GetRecordCount(); ++index)
			{
				const auto record = GetRecord(index);
				if (record.Type != info.Id || record.LayoutHash != info.LayoutHash)
					continue;
				T value;
				std::memcpy(static_cast<void*>(&value), record.Data, sizeof(T));
				visit(static_cast<const T&>(value), record.Player);
			}
		}

		// The fixed tick being simulated, counted from the start of the scene; the next tick in
		// other phases.
		virtual std::uint64_t GetTick() const noexcept = 0;

	protected:
		~InputStream() = default;
		virtual void Push(const ECS::ComponentInfo& info, const void* record, std::uint8_t player) = 0;
		virtual std::size_t GetRecordCount() const noexcept = 0;
		virtual Record GetRecord(std::size_t index) const noexcept = 0;
	};
}
