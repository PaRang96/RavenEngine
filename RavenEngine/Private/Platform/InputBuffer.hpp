#pragma once

#include "Raven/Platform/Input.hpp"

namespace Raven
{
	// Owned by one native window and used only on its event thread.
	// Native callbacks record pending input; PollEvents publishes the snapshot.
	class InputBuffer
	{
	public:
		const InputState& GetState() const;
		void Publish();

		void SetKeyDown(Key key, bool down);
		void SetMouseButtonDown(MouseButton button, bool down);
		void SetCursorPosition(std::int32_t x, std::int32_t y);
		void AddScroll(float x, float y);
		void HandleFocusLost();
		void ReleaseAllMouseButtons();
		bool HasMouseButtonDown() const;

	private:
		InputState m_Pending{};
		InputState m_Published{};
	};
}
