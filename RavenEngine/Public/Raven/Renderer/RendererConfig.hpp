#pragma once

namespace Raven
{
	// How finished frames reach the display. Fifo waits for vertical blank and is always
	// available. Mailbox and Immediate do not limit the frame rate; an unsupported request
	// falls back to Fifo with a log line.
	enum class PresentMode
	{
		Fifo, Mailbox, Immediate,
	};

	// Which device type the GPU selection ranks first. Other usable devices remain fallbacks.
	enum class GpuPreference
	{
		Discrete, Integrated,
	};

	struct RendererConfig
	{
		PresentMode Present = PresentMode::Fifo;
		GpuPreference Gpu = GpuPreference::Discrete;
	};
}
