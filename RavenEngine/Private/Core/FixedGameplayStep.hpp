#pragma once

#include "Raven/Core/FrameTime.hpp"
#include "Raven/Platform/Input.hpp"

#include <algorithm>

namespace Raven
{
	class FixedGameplayStep
	{
	public:
		static constexpr double StepSeconds = 1.0 / 60.0;
		static constexpr int MaxSteps = 4;

		// The fixed gameplay path consumes keyboard input. Pointer input stays frame-based.
		void BeginFrame(const InputState& input, double deltaSeconds)
		{
			for (std::size_t index = 0; index < InputState::KeyCount; ++index)
			{
				auto& pending = m_Input.Keys[index];
				const auto& state = input.Keys[index];
				pending.Down = state.Down;
				pending.Pressed |= state.Pressed;
				pending.Released |= state.Released;
			}
			m_Input.KeyEvents.insert(m_Input.KeyEvents.end(), input.KeyEvents.begin(), input.KeyEvents.end());
			m_Accumulated = std::min(m_Accumulated + deltaSeconds, StepSeconds * MaxSteps);
		}

		bool HasStep() const noexcept { return m_Accumulated + 1.0e-10 >= StepSeconds; }
		const InputState& GetInput() const noexcept { return m_Input; }
		FrameTime GetStepTime() const noexcept { return { m_Elapsed + StepSeconds, StepSeconds }; }

		void ConsumeStep()
		{
			m_Accumulated = std::max(0.0, m_Accumulated - StepSeconds);
			m_Elapsed += StepSeconds;
			m_Input.ClearTransientState();
		}

		void Reset()
		{
			m_Accumulated = 0.0;
			m_Input = {};
		}

	private:
		InputState m_Input;
		double m_Accumulated = 0.0;
		double m_Elapsed = 0.0;
	};
}
