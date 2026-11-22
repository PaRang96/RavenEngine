#include "Renderer/Vulkan/VulkanDrawQueue.hpp"

#include "Renderer/Visibility.hpp"
#include "Renderer/Vulkan/VulkanMesh.hpp"
#include "Renderer/Vulkan/VulkanTexturedMesh.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace Raven
{
	namespace
	{
		Mat4 PrepareTransform(const Mat4& viewProjection, const Mat4& model, Vec4 tint)
		{
			if (!std::isfinite(tint.X) || !std::isfinite(tint.Y) || !std::isfinite(tint.Z) || !std::isfinite(tint.W))
				throw std::invalid_argument("Draw tint must be finite");
			const Mat4 transform = viewProjection * model;
			for (const auto& column : transform.Columns)
				for (const float value : column)
					if (!std::isfinite(value))
						throw std::invalid_argument("Draw transform exceeds the supported numeric range");
			return transform;
		}

		void CheckFinite(const Mat4& model, Vec4 tint)
		{
			if (!std::isfinite(tint.X) || !std::isfinite(tint.Y) || !std::isfinite(tint.Z) || !std::isfinite(tint.W))
				throw std::invalid_argument("Draw tint must be finite");
			for (const auto& column : model.Columns)
				for (const float value : column)
					if (!std::isfinite(value))
						throw std::invalid_argument("Draw transform exceeds the supported numeric range");
		}

		// Only visible meshes get here, and only lit ones pay for the normal matrix (R4).
		MeshInstanceData PrepareMeshInstance(const VulkanDraw& draw)
		{
			MeshInstanceData instance{};
			for (std::size_t row = 0; row < 3; ++row)
				instance.WorldRows[row] = { draw.Model(row, 0), draw.Model(row, 1), draw.Model(row, 2), draw.Model(row, 3) };
			const Mat4 normal = draw.Lit ? NormalMatrix(draw.Model) : Mat4::Identity();
			for (std::size_t column = 0; column < 3; ++column)
				instance.NormalToWorld[column] = { normal(0, column), normal(1, column), normal(2, column), 0.0f };
			instance.NormalToWorld[0].W = draw.Lit ? 1.0f : 0.0f;
			instance.Tint = draw.Tint;
			return instance;
		}

		// Solves View(3x3) * position = -translation, so any invertible affine view works.
		Vec4 CameraPosition(const Mat4& view)
		{
			const auto m = [&view](std::size_t row, std::size_t column) { return view(row, column); };
			const float determinant =
				m(0, 0) * (m(1, 1) * m(2, 2) - m(1, 2) * m(2, 1)) -
				m(0, 1) * (m(1, 0) * m(2, 2) - m(1, 2) * m(2, 0)) +
				m(0, 2) * (m(1, 0) * m(2, 1) - m(1, 1) * m(2, 0));
			if (!std::isfinite(determinant) || determinant == 0.0f)
				return { 0.0f, 0.0f, 0.0f, 1.0f };
			const float b0 = -m(0, 3), b1 = -m(1, 3), b2 = -m(2, 3);
			const float x = (b0 * (m(1, 1) * m(2, 2) - m(1, 2) * m(2, 1)) -
				m(0, 1) * (b1 * m(2, 2) - m(1, 2) * b2) + m(0, 2) * (b1 * m(2, 1) - m(1, 1) * b2)) / determinant;
			const float y = (m(0, 0) * (b1 * m(2, 2) - m(1, 2) * b2) -
				b0 * (m(1, 0) * m(2, 2) - m(1, 2) * m(2, 0)) + m(0, 2) * (m(1, 0) * b2 - b1 * m(2, 0))) / determinant;
			const float z = (m(0, 0) * (m(1, 1) * b2 - b1 * m(2, 1)) -
				m(0, 1) * (m(1, 0) * b2 - b1 * m(2, 0)) + b0 * (m(1, 0) * m(2, 1) - m(1, 1) * m(2, 0))) / determinant;
			return { x, y, z, 1.0f };
		}

		void AppendTexturedBatches(std::span<const QueuedTexturedDraw> draws,
			std::vector<TexturedInstanceData>& instances, std::vector<TexturedBatch>& batches)
		{
			constexpr std::size_t maxInstances = std::numeric_limits<std::uint32_t>::max();
			if (instances.size() > maxInstances || draws.size() > maxInstances - instances.size())
				throw std::length_error("Textured instance count exceeds the draw range");

			instances.reserve(instances.size() + draws.size());
			batches.reserve(draws.size());
			for (const auto& prepared : draws)
			{
				const auto& draw = *prepared.Draw;
				const auto firstInstance = static_cast<std::uint32_t>(instances.size());
				if (!batches.empty() && batches.back().Mesh == &draw.Mesh &&
					batches.back().Texture == &draw.Texture)
				{
					++batches.back().InstanceCount;
				}
				else
				{
					batches.push_back({ &draw.Mesh, &draw.Texture, firstInstance, 1 });
				}
				instances.push_back({ prepared.ModelViewProjection, draw.Tint });
			}
		}
	}

	void BuildVulkanDrawQueue(VulkanDrawQueue& queue, const Mat4& view, const Mat4& viewProjection,
		std::span<const VulkanDraw> colorDraws, std::span<const VulkanTexturedDraw> opaqueDraws,
		std::span<const VulkanTransparentDraw> transparentDraws, const SceneLighting& lighting)
	{
		lighting.Validate();
		const auto direction = Normalize(lighting.Direction);
		queue.Frame = FrameUniformData{ viewProjection, view, CameraPosition(view),
			{ lighting.Ambient.X, lighting.Ambient.Y, lighting.Ambient.Z, 0.0f },
			{ direction.X, direction.Y, direction.Z, 0.0f },
			{ lighting.Color.X, lighting.Color.Y, lighting.Color.Z, 0.0f } };
		queue.Color.clear();
		queue.MeshInstances.clear();
		queue.OpaqueTexture.clear();
		queue.Transparent.clear();
		queue.TexturedInstances.clear();
		queue.OpaqueBatches.clear();
		queue.TransparentBatches.clear();
		const ViewFrustum frustum(viewProjection);
		queue.Color.reserve(colorDraws.size());
		queue.MeshInstances.reserve(colorDraws.size());
		queue.OpaqueTexture.reserve(opaqueDraws.size());
		queue.Transparent.reserve(transparentDraws.size());
		for (const auto& draw : colorDraws)
		{
			CheckFinite(draw.Model, draw.Tint);
			const auto bounds = TransformBounds(draw.Mesh.GetData()->LocalBounds, draw.Model);
			if (frustum.Intersects(bounds))
			{
				queue.Color.push_back(&draw);
				queue.MeshInstances.push_back(PrepareMeshInstance(draw));
			}
		}
		for (const auto& draw : opaqueDraws)
		{
			const auto bounds = TransformBounds(draw.Mesh.GetLocalBounds(), draw.Model);
			const auto transform = PrepareTransform(viewProjection, draw.Model, draw.Tint);
			if (frustum.Intersects(bounds))
				queue.OpaqueTexture.push_back({ &draw, transform });
		}
		for (std::size_t index = 0; index < transparentDraws.size(); ++index)
		{
			const auto& submission = transparentDraws[index];
			const auto& draw = submission.Draw;
			const auto bounds = TransformBounds(draw.Mesh.GetLocalBounds(), draw.Model);
			const auto transform = PrepareTransform(viewProjection, draw.Model, draw.Tint);
			if (!frustum.Intersects(bounds))
				continue;
			const Vec4 center = view * Vec4{ bounds.Center.X, bounds.Center.Y, bounds.Center.Z, 1.0f };
			if (!std::isfinite(center.Z))
				throw std::invalid_argument("Transparent draw depth must be finite");
			queue.Transparent.push_back({ &draw, transform, -center.Z,
				submission.SortLayer, submission.SortOrder, index });
		}
		std::sort(queue.Transparent.begin(), queue.Transparent.end(), [](const auto& left, const auto& right)
		{
			if (left.SortLayer != right.SortLayer)
				return left.SortLayer < right.SortLayer;
			if (left.SortOrder != right.SortOrder)
				return left.SortOrder < right.SortOrder;
			if (left.ViewDepth != right.ViewDepth)
				return left.ViewDepth > right.ViewDepth;
			return left.SubmissionIndex < right.SubmissionIndex;
		});
		AppendTexturedBatches(queue.OpaqueTexture, queue.TexturedInstances, queue.OpaqueBatches);
		AppendTexturedBatches(queue.Transparent, queue.TexturedInstances, queue.TransparentBatches);
	}
}
