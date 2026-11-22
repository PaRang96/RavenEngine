#include "Renderer/Vulkan/FragmentShaderInterface.hpp"

#include <algorithm>
#include <array>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace Raven
{
	namespace
	{
		// SPIR-V instructions and operands used by the fixed fragment interface.
		enum class Op : std::uint16_t
		{
			Extension = 10, EntryPoint = 15, ExecutionMode = 16, Capability = 17,
			TypeInt = 21, TypeFloat = 22, TypeVector = 23, TypeImage = 25, TypeSampler = 26,
			TypeRuntimeArray = 29, TypePointer = 32, Variable = 59, Decorate = 71, DecorationGroup = 73
		};

		enum class Storage : std::uint32_t
		{
			UniformConstant = 0, Input = 1, Output = 3, Private = 6, Function = 7
		};

		enum class Decoration : std::uint32_t
		{
			RelaxedPrecision = 0, Flat = 14, Location = 30, Binding = 33, DescriptorSet = 34
		};

		struct Decorations
		{
			std::optional<std::uint32_t> Location;
			std::optional<std::uint32_t> Binding;
			std::optional<std::uint32_t> Set;
			bool Flat = false;
			bool Unsupported = false;
		};

		struct Variable
		{
			std::uint32_t Pointer;
			std::uint32_t Id;
			Storage Class;
		};

		[[noreturn]] void Reject(const char* reason)
		{
			throw std::invalid_argument(std::string("Incompatible fragment shader: ") + reason);
		}

		std::span<const std::uint32_t> Type(
			const std::unordered_map<std::uint32_t, std::span<const std::uint32_t>>& types,
			std::uint32_t id, Op op, std::size_t size)
		{
			const auto found = types.find(id);
			if (found == types.end() || found->second.size() != size ||
				static_cast<Op>(found->second[0] & 0xffff) != op)
				Reject("unexpected interface type");
			return found->second;
		}
	}

	void CheckFragmentShaderInterface(std::span<const std::uint32_t> words, FragmentShaderKind kind)
	{
		constexpr std::array<std::uint32_t, 2> texturedWidths{ 2, 4 };
		// Lit location 6 is a flat uint texture index (docs/GpuDrivenDesign.md, G4); width 1 marks it.
		constexpr std::array<std::uint32_t, 7> litWidths{ 3, 2, 3, 4, 3, 3, 1 };
		constexpr std::uint32_t LitTextureLocation = 6;
		const bool lit = kind == FragmentShaderKind::Lit;
		const std::span<const std::uint32_t> widths = lit ? std::span<const std::uint32_t>(litWidths)
			: std::span<const std::uint32_t>(texturedWidths);
		constexpr std::uint32_t ShaderCapability = 1;
		constexpr std::uint32_t FragmentModel = 4;
		constexpr std::uint32_t OriginUpperLeft = 7;
		if (words.size() < 5 || words[0] != 0x07230203 || words[1] < 0x00010000 || words[1] > 0x00010500)
			Reject("expected SPIR-V 1.0 to 1.5");
		// From SPIR-V 1.4 the entry point lists every global it uses, including the texture
		// and sampler; earlier versions list only the stage inputs and outputs.
		const bool listsGlobals = words[1] >= 0x00010400;
		const std::size_t interfaceSize = widths.size() + 1 + (listsGlobals ? 2 : 0);

		std::unordered_map<std::uint32_t, std::span<const std::uint32_t>> types;
		std::unordered_map<std::uint32_t, Decorations> decorations;
		std::vector<Variable> variables;
		std::span<const std::uint32_t> entry;
		std::optional<std::uint32_t> origin;
		for (std::size_t offset = 5; offset < words.size();)
		{
			const std::size_t size = words[offset] >> 16;
			if (size == 0 || size > words.size() - offset)
				Reject("invalid instruction size");
			const auto instruction = words.subspan(offset, size);
			offset += size;
			const auto opcode = static_cast<Op>(instruction[0] & 0xffff);
			switch (opcode)
			{
			case Op::Capability:
				// Bindless lit shaders index a runtime texture array non-uniformly.
				if (size != 2 || (instruction[1] != ShaderCapability && !(lit && (instruction[1] == 5301 ||
					instruction[1] == 5302 || instruction[1] == 5307))))
					Reject("requires capabilities beyond the existing shader");
				break;
			case Op::Extension:
			{
				// "SPV_EXT_descriptor_indexing" as a null-terminated literal in seven words.
				constexpr std::array<std::uint32_t, 7> descriptorIndexing{ 0x5f565053, 0x5f545845, 0x63736564,
					0x74706972, 0x695f726f, 0x7865646e, 0x00676e69 };
				if (lit && size == 8 && std::ranges::equal(instruction.subspan(1), descriptorIndexing))
					break;
				Reject("extensions and decoration groups are outside the reload interface");
			}
			case Op::DecorationGroup:
				Reject("extensions and decoration groups are outside the reload interface");
			case Op::EntryPoint:
				// Null-terminated PSMain occupies two little-endian SPIR-V words.
				if (!entry.empty() || size != 5 + interfaceSize || instruction[1] != FragmentModel ||
					instruction[3] != 0x614d5350 || instruction[4] != 0x00006e69)
					Reject("expected one PSMain fragment entry with the configured stage variables");
				entry = instruction;
				break;
			case Op::ExecutionMode:
				if (origin || size != 3 || instruction[2] != OriginUpperLeft)
					Reject("unexpected fragment execution mode");
				origin = instruction[1];
				break;
			case Op::TypeInt:
			case Op::TypeRuntimeArray:
			case Op::TypeFloat:
			case Op::TypeVector:
			case Op::TypeImage:
			case Op::TypeSampler:
			case Op::TypePointer:
				if (size < 2)
					Reject("invalid type instruction");
				types.emplace(instruction[1], instruction);
				break;
			case Op::Variable:
				if (size < 4)
					Reject("invalid variable instruction");
				variables.push_back({ instruction[1], instruction[2], static_cast<Storage>(instruction[3]) });
				break;
			case Op::Decorate:
			{
				if (size < 3)
					Reject("invalid decoration instruction");
				auto& value = decorations[instruction[1]];
				const auto kind = static_cast<Decoration>(instruction[2]);
				if (kind == Decoration::RelaxedPrecision)
					break;
				if (kind == Decoration::Flat && size == 3)
					value.Flat = true;
				else if (size != 4)
					value.Unsupported = true;
				else if (kind == Decoration::Location)
					value.Location = instruction[3];
				else if (kind == Decoration::Binding)
					value.Binding = instruction[3];
				else if (kind == Decoration::DescriptorSet)
					value.Set = instruction[3];
				else
					value.Unsupported = true;
				break;
			}
			default:
				break;
			}
		}
		if (entry.empty() || origin != entry[2])
			Reject("missing PSMain fragment entry");

		std::array<std::uint32_t, litWidths.size()> inputs{};
		std::array<bool, 2> descriptors{};
		std::array<std::uint32_t, 2> descriptorIds{};
		std::uint32_t output = 0;
		for (const auto& variable : variables)
		{
			if (variable.Class == Storage::Function || variable.Class == Storage::Private)
				continue;
			const auto& decoration = decorations[variable.Id];
			if (decoration.Unsupported)
				Reject("unexpected decoration on a shader interface variable");
			const auto pointer = Type(types, variable.Pointer, Op::TypePointer, 4);
			const auto valueType = pointer[3];
			if (variable.Class == Storage::UniformConstant)
			{
				if (decoration.Set != 0 || !decoration.Binding || *decoration.Binding >= descriptors.size() ||
					descriptors[*decoration.Binding] || decoration.Location || decoration.Flat)
					Reject("expected only texture and sampler at set 0 bindings 0 and 1");
				if (*decoration.Binding == 0)
				{
					// Lit shaders sample one bindless array; textured shaders one texture.
					const auto imageId = lit ? Type(types, valueType, Op::TypeRuntimeArray, 3)[2] : valueType;
					const auto image = Type(types, imageId, Op::TypeImage, 9);
					const auto sample = Type(types, image[2], Op::TypeFloat, 3);
					if (sample[2] != 32 || image[3] != 1 || image[4] == 1 ||
						image[5] != 0 || image[6] != 0 || image[7] != 1 || image[8] != 0)
						Reject("binding 0 requires a sampled float Texture2D");
				}
				else
				{
					Type(types, valueType, Op::TypeSampler, 2);
				}
				descriptors[*decoration.Binding] = true;
				descriptorIds[*decoration.Binding] = variable.Id;
				continue;
			}
			if (decoration.Set || decoration.Binding || !decoration.Location)
				Reject("stage variables require a location");
			if (lit && variable.Class == Storage::Input && *decoration.Location == LitTextureLocation)
			{
				const auto integer = Type(types, valueType, Op::TypeInt, 4);
				if (integer[2] != 32 || integer[3] != 0 || !decoration.Flat || inputs[LitTextureLocation] != 0)
					Reject("location 6 requires one flat uint texture index");
				inputs[LitTextureLocation] = variable.Id;
				continue;
			}
			const auto vector = Type(types, valueType, Op::TypeVector, 4);
			const auto scalar = Type(types, vector[2], Op::TypeFloat, 3);
			if (scalar[2] != 32)
				Reject("stage variables require float32 components");
			const auto location = *decoration.Location;
			if (variable.Class == Storage::Input && location < widths.size() &&
				inputs[location] == 0 && vector[3] == widths[location] && decoration.Flat == (lit && location >= 3))
				inputs[location] = variable.Id;
			else if (variable.Class == Storage::Output && location == 0 && output == 0 &&
				vector[3] == 4 && !decoration.Flat)
				output = variable.Id;
			else
				Reject("stage location, type, or interpolation differs from the pipeline interface");
		}
		if (!descriptors[0] || !descriptors[1] || !output)
			Reject("missing texture, sampler, or stage variable");
		const auto exposed = entry.subspan(5);
		for (const auto id : std::span(inputs).first(widths.size()))
			if (id == 0 || std::ranges::find(exposed, id) == exposed.end())
				Reject("entry point does not expose the expected stage variables");
		if (std::ranges::find(exposed, output) == exposed.end())
			Reject("entry point does not expose its color output");
		if (listsGlobals)
			for (const auto id : descriptorIds)
				if (std::ranges::find(exposed, id) == exposed.end())
					Reject("entry point does not expose its texture and sampler");
	}
}
