#pragma once

#include "Raven/Math/Transform.hpp"

namespace Raven
{
	// Derived values for one rendered view.
	struct CameraView
	{
		Mat4 View;
		Mat4 Projection;
	};
}
