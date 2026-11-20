#pragma once

#include "Raven/Math/Transform.hpp"
#include "Raven/Renderer/MeshHandle.hpp"

namespace Raven
{
	class VulkanMesh;
	class VulkanTexture;

	// The context owns the mesh. CPU submissions are borrowed only while recording a frame.
	struct VulkanDraw
	{
		const VulkanMesh& Mesh;
		Mat4 Model = Mat4::Identity();
		Vec4 Tint{ 1.0f, 1.0f, 1.0f, 1.0f };
		const VulkanTexture* Texture = nullptr;
		bool Lit = false;
	};
}
