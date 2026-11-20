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

		std::array<Vec4, 3> NormalTransform(const Mat4& model, bool lit)
		{
			const Mat4 normal = lit ? NormalMatrix(model) : Mat4::Identity();
			std::array<Vec4, 3> result{};
			for (std::size_t index = 0; index < result.size(); ++index)
				result[index] = { normal(0, index), normal(1, index), normal(2, index), 0.0f };
			return result;
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
			const auto bounds = TransformBounds(draw.Mesh.GetData()->LocalBounds, draw.Model);
			const auto transform = PrepareTransform(viewProjection, draw.Model, draw.Tint);
			const auto normal = NormalTransform(draw.Model, draw.Lit);
			if (frustum.Intersects(bounds))
			{
				queue.Color.push_back(&draw);
				queue.MeshInstances.push_back({ transform, normal, draw.Tint,
					{ lighting.Ambient.X, lighting.Ambient.Y, lighting.Ambient.Z, draw.Lit ? 1.0f : 0.0f },
					{ direction.X, direction.Y, direction.Z, 0.0f },
					{ lighting.Color.X, lighting.Color.Y, lighting.Color.Z, 0.0f } });
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
