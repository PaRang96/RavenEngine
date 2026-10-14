#pragma once

#include "Raven/Math/Transform.hpp"

namespace Raven
{
	class VulkanTexturedMesh;
	class VulkanTexture;

	// Resources belong to the submitting context; CPU draws are borrowed during recording.
	struct VulkanTexturedDraw
	{
		const VulkanTexturedMesh& Mesh;
		const VulkanTexture& Texture;
		Mat4 Model = Mat4::Identity();
		Vec4 Tint{ 1.0f, 1.0f, 1.0f, 1.0f };
	};
}
