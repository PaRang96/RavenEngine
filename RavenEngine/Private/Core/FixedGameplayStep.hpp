#pragma once

#include "Raven/Core/FrameTime.hpp"
#include "Raven/Input/InputMap.hpp"
#include "Raven/Input/InputStream.hpp"
#include "Raven/Platform/Input.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

namespace Raven
{
	// The fixed gameplay clock and the input stream it delivers (docs/GameServicesDesign.md, FD-34,
	// FD-47). Each fixed step sees one tick: the key and mouse-button input and mapped actions
	// accumulated since the previous step (already filtered by the input contexts), and the stream
	// records emitted before it. BeginStep takes all three into one per-tick record, so a tick is a
	// function of its record alone (review amendment, AA2-GP-02 step 3); replay can store exactly that.
	// Pointer motion and scroll stay frame-based.
	class FixedGameplayStep final : public InputStream
	{
	public:
		// A frame catches up at most this much game time; the rest is dropped and counted, so a long
		// stall (a breakpoint, a load) slows the game briefly instead of spiralling.
		static constexpr double MaxCatchUpSeconds = 4.0 / 60.0;

		explicit FixedGameplayStep(double rate = 60.0)
		{
			SetRate(rate);
		}

		void SetRate(double rate)
		{
			if (!(rate >= 10.0 && rate <= 480.0))
				throw std::invalid_argument("Fixed-step rate must be from 10 to 480 Hz");
			m_StepSeconds = 1.0 / rate;
		}
		double GetStepSeconds() const noexcept { return m_StepSeconds; }

		void BeginFrame(const InputState& input, std::span<const ActionSample> actions, double deltaSeconds)
		{
			for (std::size_t index = 0; index < InputState::KeyCount; ++index)
				Accumulate(m_Input.Keys[index], input.Keys[index]);
			for (std::size_t index = 0; index < InputState::MouseButtonCount; ++index)
				Accumulate(m_Input.MouseButtons[index], input.MouseButtons[index]);
			m_Input.Events.insert(m_Input.Events.end(), input.Events.begin(), input.Events.end());
			m_Input.Focused = input.Focused;

			for (auto& pending : m_Actions)
			{
				pending.Down = false;
				pending.Axis = 0.0f;
			}
			for (const auto& sample : actions)
			{
				const auto pending = std::find_if(m_Actions.begin(), m_Actions.end(),
					[&](const ActionSample& candidate) { return candidate.Id == sample.Id; });
				if (pending == m_Actions.end())
				{
					m_Actions.push_back(sample);
					continue;
				}
				pending->Down = sample.Down;
				pending->Axis = sample.Axis;
				pending->Pressed |= sample.Pressed;
				pending->Released |= sample.Released;
			}

			const double limit = std::max(MaxCatchUpSeconds, m_StepSeconds);
			const double total = m_Accumulated + deltaSeconds;
			if (total > limit)
				m_DroppedSeconds += total - limit;
			m_Accumulated = std::min(total, limit);
		}

		bool HasStep() const noexcept { return m_Accumulated + 1.0e-10 >= m_StepSeconds; }
		// The running tick's input and actions, from BeginStep to ConsumeStep.
		const InputState& GetInput() const noexcept { return m_Running.Input; }
		std::span<const ActionSample> GetActions() const noexcept { return m_Running.Actions; }
		// Derived from the tick alone, so a restarted scene or a restored snapshot sees the same time
		// at the same tick (AA2-GP-03).
		FrameTime GetStepTime() const noexcept
		{
			return { static_cast<double>(m_Tick + 1) * m_StepSeconds, m_StepSeconds };
		}
		// How far the frame is past the latest tick, as a fraction of a step: the render
		// interpolation weight (docs/GameServicesDesign.md, FD-34).
		double GetAlpha() const noexcept { return std::clamp(m_Accumulated / m_StepSeconds, 0.0, 1.0); }

		// Takes the input, actions and records gathered so far into the record of the tick about to run.
		void BeginStep()
		{
			m_Running.Input = m_Input;
			m_Running.Actions = m_Actions;
			m_Running.Records.swap(m_Pending);
			m_Pending.clear();
			m_Input.ClearTransientState();
			for (auto& pending : m_Actions)
				pending.Pressed = pending.Released = false;
		}

		void ConsumeStep()
		{
			m_Accumulated = std::max(0.0, m_Accumulated - m_StepSeconds);
			m_Running = {};
			++m_Tick;
		}

		// Pause, a hidden window or a failed step: waiting input and records are dropped.
		void Reset()
		{
			m_Accumulated = 0.0;
			m_Input = {};
			m_Actions.clear();
			m_Pending.clear();
			m_Running = {};
		}

		// A new scene starts its ticks from zero; a restored snapshot from its own tick.
		void RestartTicks(std::uint64_t tick = 0) noexcept { m_Tick = tick; }

		// A replaced game module may have changed record layouts (FD-47): waiting records go.
		void DropRecords() noexcept
		{
			m_Pending.clear();
			m_Running.Records.clear();
		}

		// Game time dropped since the last call, in seconds.
		double TakeDroppedSeconds() noexcept
		{
			const double dropped = m_DroppedSeconds;
			m_DroppedSeconds = 0.0;
			return dropped;
		}

		std::uint64_t GetTick() const noexcept override { return m_Tick; }

	private:
		struct Stored
		{
			std::uint64_t Type = 0;
			std::uint64_t LayoutHash = 0;
			std::uint8_t Player = 0;
			std::vector<std::byte> Bytes;
		};

		void Push(const ECS::ComponentInfo& info, const void* record, std::uint8_t player) override
		{
			const auto* bytes = static_cast<const std::byte*>(record);
			m_Pending.push_back({ info.Id, info.LayoutHash, player, { bytes, bytes + info.Size } });
		}
		std::size_t GetRecordCount() const noexcept override { return m_Running.Records.size(); }
		Record GetRecord(std::size_t index) const noexcept override
		{
			const auto& stored = m_Running.Records[index];
			return { stored.Type, stored.LayoutHash, stored.Player, stored.Bytes.data() };
		}

		static void Accumulate(ButtonState& pending, const ButtonState& state) noexcept
		{
			pending.Down = state.Down;
			pending.Pressed |= state.Pressed;
			pending.Released |= state.Released;
		}

		// Everything one tick sees.
		struct TickRecord
		{
			InputState Input;
			std::vector<ActionSample> Actions;
			std::vector<Stored> Records;
		};

		// Gathered for the next tick.
		InputState m_Input;
		std::vector<ActionSample> m_Actions;
		std::vector<Stored> m_Pending;
		TickRecord m_Running;
		double m_StepSeconds = 1.0 / 60.0;
		double m_Accumulated = 0.0;
		double m_DroppedSeconds = 0.0;
		std::uint64_t m_Tick = 0;
	};
}
