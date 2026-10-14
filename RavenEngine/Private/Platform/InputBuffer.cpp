#include "Platform/InputBuffer.hpp"

namespace Raven
{
	const InputState& InputBuffer::GetState() const
	{
		return m_Published;
	}

	void InputBuffer::Publish()
	{
		m_Published = m_Pending;
		m_Pending.ClearTransientState();
	}

	void InputBuffer::SetKeyDown(Key key, bool down)
	{
		m_Pending.SetKeyDown(key, down);
	}

	void InputBuffer::SetMouseButtonDown(MouseButton button, bool down)
	{
		m_Pending.SetMouseButtonDown(button, down);
	}

	void InputBuffer::SetCursorPosition(std::int32_t x, std::int32_t y)
	{
		m_Pending.SetCursorPosition(x, y);
	}

	void InputBuffer::AddScroll(float x, float y)
	{
		m_Pending.AddScroll(x, y);
	}

	void InputBuffer::HandleFocusLost()
	{
		m_Pending.HandleFocusLost();
	}

	void InputBuffer::ReleaseAllMouseButtons()
	{
		m_Pending.ReleaseAllMouseButtons();
	}

	bool InputBuffer::HasMouseButtonDown() const
	{
		return m_Pending.HasMouseButtonDown();
	}
}
