#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace Raven
{
	// A buffer the engine binds, with the layout its C++ code writes (docs/HotReloadDesign.md, H2).
	struct ShaderBufferLayout
	{
		std::uint32_t Set = 0;
		std::uint32_t Binding = 0;
		// Element stride of a structured or byte-address buffer; 0 for a constant buffer.
		std::uint32_t Stride = 0;
		// Member offsets of the element struct (or the constant buffer); empty for scalar elements.
		std::vector<std::uint32_t> Offsets;
	};

	struct ShaderStageVariable
	{
		std::uint32_t Location = 0;
		std::uint32_t Components = 0;
		bool Integer = false;
	};

	// The fixed interface of an engine vertex or compute shader. A reloaded shader may use any
	// subset of Buffers, but every buffer it declares must match, and its stage variables, push
	// constants and workgroup size must equal the pipeline's.
	struct ShaderInterface
	{
		enum class Stage { Vertex, Compute };
		Stage Kind = Stage::Vertex;
		std::vector<ShaderStageVariable> Inputs;
		std::vector<ShaderStageVariable> Outputs;
		std::vector<ShaderBufferLayout> Buffers;
		// Member offsets of the push-constant block; empty when the pipeline has none.
		std::vector<std::uint32_t> PushConstantOffsets;
		std::array<std::uint32_t, 3> LocalSize{};
	};

	// Throws std::invalid_argument naming the first difference.
	void CheckShaderInterface(std::span<const std::uint32_t> words, const ShaderInterface& expected);
}
