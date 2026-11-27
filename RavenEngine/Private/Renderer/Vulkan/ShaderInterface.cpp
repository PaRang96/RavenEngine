#include "Renderer/Vulkan/ShaderInterface.hpp"

#include <algorithm>
#include <cstring>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace Raven
{
	namespace
	{
		// The SPIR-V instructions and enumerants the interface check reads.
		enum Op : std::uint16_t
		{
			OpEntryPoint = 15, OpExecutionMode = 16, OpTypeInt = 21, OpTypeFloat = 22, OpTypeVector = 23,
			OpTypeImage = 25, OpTypeSampler = 26, OpTypeArray = 28, OpTypeRuntimeArray = 29, OpTypeStruct = 30,
			OpTypePointer = 32, OpConstant = 43, OpVariable = 59, OpDecorate = 71, OpMemberDecorate = 72,
			OpName = 5, OpMemberName = 6
		};
		enum Decoration : std::uint32_t
		{
			ArrayStride = 6, BuiltIn = 11, Location = 30, Binding = 33, DescriptorSet = 34, Offset = 35
		};
		enum StorageClass : std::uint32_t
		{
			UniformConstant = 0, Input = 1, Uniform = 2, Output = 3, PushConstant = 9, StorageBuffer = 12
		};
		constexpr std::uint32_t ExecutionModelVertex = 0;
		constexpr std::uint32_t ExecutionModelFragment = 4;
		constexpr std::uint32_t ExecutionModelCompute = 5;
		constexpr std::uint32_t ExecutionModeLocalSize = 17;

		[[noreturn]] void Reject(const std::string& reason)
		{
			throw std::invalid_argument("Incompatible shader: " + reason);
		}

		struct Module
		{
			std::unordered_map<std::uint32_t, std::span<const std::uint32_t>> Types;
			std::unordered_map<std::uint32_t, std::map<std::uint32_t, std::uint32_t>> Decorations;
			std::unordered_map<std::uint32_t, std::map<std::uint32_t, std::uint32_t>> MemberOffsets;
			// 32-bit integer constants, for array lengths.
			std::unordered_map<std::uint32_t, std::uint32_t> Constants;
			// Debug names, which DXC keeps for buffer element types and their members.
			std::unordered_map<std::uint32_t, std::string> Names;
			std::unordered_map<std::uint32_t, std::map<std::uint32_t, std::string>> MemberNames;
			struct Variable
			{
				std::uint32_t Id;
				std::uint32_t Pointer;
				std::uint32_t Class;
			};
			std::vector<Variable> Variables;
			std::span<const std::uint32_t> Entry;
			std::string EntryName;
			std::optional<std::array<std::uint32_t, 3>> LocalSize;

			std::span<const std::uint32_t> Type(std::uint32_t id) const
			{
				const auto found = Types.find(id);
				if (found == Types.end())
					Reject("unknown type");
				return found->second;
			}

			std::optional<std::uint32_t> Decorated(std::uint32_t id, std::uint32_t decoration) const
			{
				const auto found = Decorations.find(id);
				if (found == Decorations.end())
					return std::nullopt;
				const auto value = found->second.find(decoration);
				return value == found->second.end() ? std::nullopt : std::optional(value->second);
			}

			std::vector<std::uint32_t> Offsets(std::uint32_t structType) const
			{
				const auto type = Type(structType);
				const auto count = type.size() - 2;
				const auto found = MemberOffsets.find(structType);
				std::vector<std::uint32_t> offsets;
				for (std::uint32_t member = 0; member < count; ++member)
				{
					if (found == MemberOffsets.end() || !found->second.contains(member))
						Reject("struct member without an offset");
					offsets.push_back(found->second.at(member));
				}
				return offsets;
			}
		};

		Module Parse(std::span<const std::uint32_t> words)
		{
			if (words.size() < 5 || words[0] != 0x07230203)
				Reject("not SPIR-V");
			Module module;
			for (std::size_t offset = 5; offset < words.size();)
			{
				const std::size_t size = words[offset] >> 16;
				if (size == 0 || size > words.size() - offset)
					Reject("invalid instruction size");
				const auto instruction = words.subspan(offset, size);
				offset += size;
				switch (instruction[0] & 0xffff)
				{
				case OpEntryPoint:
				{
					if (!module.Entry.empty())
						Reject("expected one entry point");
					if (size < 4)
						Reject("invalid entry point");
					module.Entry = instruction;
					const auto* text = reinterpret_cast<const char*>(instruction.data() + 3);
					module.EntryName.assign(text, strnlen(text, (size - 3) * sizeof(std::uint32_t)));
					break;
				}
				case OpExecutionMode:
					if (size == 6 && instruction[2] == ExecutionModeLocalSize)
						module.LocalSize = std::array{ instruction[3], instruction[4], instruction[5] };
					break;
				case OpTypeInt:
				case OpTypeFloat:
				case OpTypeVector:
				case OpTypeImage:
				case OpTypeSampler:
				case OpTypeArray:
				case OpTypeRuntimeArray:
				case OpTypeStruct:
				case OpTypePointer:
					if (size < 2)
						Reject("invalid type");
					module.Types.emplace(instruction[1], instruction);
					break;
				case OpName:
					if (size >= 3)
					{
						const auto* text = reinterpret_cast<const char*>(instruction.data() + 2);
						module.Names[instruction[1]].assign(text, strnlen(text, (size - 2) * sizeof(std::uint32_t)));
					}
					break;
				case OpMemberName:
					if (size >= 4)
					{
						const auto* text = reinterpret_cast<const char*>(instruction.data() + 3);
						module.MemberNames[instruction[1]][instruction[2]].assign(text,
							strnlen(text, (size - 3) * sizeof(std::uint32_t)));
					}
					break;
				case OpConstant:
					if (size == 4)
						module.Constants[instruction[2]] = instruction[3];
					break;
				case OpVariable:
					if (size < 4)
						Reject("invalid variable");
					module.Variables.push_back({ instruction[2], instruction[1], instruction[3] });
					break;
				case OpDecorate:
					if (size >= 3)
						module.Decorations[instruction[1]][instruction[2]] = size >= 4 ? instruction[3] : 1;
					break;
				case OpMemberDecorate:
					if (size >= 5 && instruction[3] == Offset)
						module.MemberOffsets[instruction[1]][instruction[2]] = instruction[4];
					break;
				default:
					break;
				}
			}
			if (module.Entry.empty())
				Reject("missing entry point");
			return module;
		}

		std::string List(const std::vector<std::uint32_t>& values)
		{
			std::string text;
			for (const auto value : values)
				text += (text.empty() ? "" : ", ") + std::to_string(value);
			return "{" + text + "}";
		}

		void CheckStageVariables(const Module& module, std::uint32_t storage,
			const std::vector<ShaderStageVariable>& expected, const char* label, bool subset = false)
		{
			std::vector<ShaderStageVariable> found;
			for (const auto& variable : module.Variables)
			{
				if (variable.Class != storage || module.Decorated(variable.Id, BuiltIn))
					continue;
				const auto location = module.Decorated(variable.Id, Location);
				if (!location)
					Reject(std::string(label) + " without a location");
				auto type = module.Type(module.Type(variable.Pointer)[3]);
				std::uint32_t components = 1;
				if ((type[0] & 0xffff) == OpTypeVector)
				{
					components = type[3];
					type = module.Type(type[2]);
				}
				const auto opcode = type[0] & 0xffff;
				if ((opcode != OpTypeInt && opcode != OpTypeFloat) || type[2] != 32)
					Reject(std::string(label) + " at location " + std::to_string(*location) + " is not a 32-bit scalar or vector");
				found.push_back({ *location, components, opcode == OpTypeInt });
			}
			const auto byLocation = [](const ShaderStageVariable& left, const ShaderStageVariable& right)
			{
				return left.Location < right.Location;
			};
			std::sort(found.begin(), found.end(), byLocation);
			auto wanted = expected;
			std::sort(wanted.begin(), wanted.end(), byLocation);
			// A subset keeps the expected entries the shader declares, so the comparison below names
			// only real differences.
			if (subset)
				std::erase_if(wanted, [&](const ShaderStageVariable& entry)
				{
					return std::none_of(found.begin(), found.end(),
						[&](const ShaderStageVariable& used) { return used.Location == entry.Location; });
				});
			for (std::size_t index = 0; index < std::max(found.size(), wanted.size()); ++index)
			{
				if (index >= found.size())
					Reject(std::string("missing ") + label + " at location " + std::to_string(wanted[index].Location));
				if (index >= wanted.size() || found[index].Location != wanted[index].Location)
					Reject(std::string("unexpected ") + label + " at location " + std::to_string(found[index].Location));
				if (found[index].Components != wanted[index].Components || found[index].Integer != wanted[index].Integer)
					Reject(std::string(label) + " at location " + std::to_string(found[index].Location) + " has " +
						std::to_string(found[index].Components) + (found[index].Integer ? " integer" : " float") +
						" components; the pipeline expects " + std::to_string(wanted[index].Components) +
						(wanted[index].Integer ? " integer" : " float"));
			}
		}

		// A float, 2D, non-arrayed, single-sample image used with a sampler: Texture2D<float4>.
		void CheckSampledImage(const Module& module, std::uint32_t id, const std::string& where)
		{
			const auto image = module.Type(id);
			if ((image[0] & 0xffff) != OpTypeImage || image.size() < 9)
				Reject(where + " must be a Texture2D");
			const auto sample = module.Type(image[2]);
			if ((sample[0] & 0xffff) != OpTypeFloat || sample[2] != 32 || image[3] != 1 || image[4] == 1 ||
				image[5] != 0 || image[6] != 0 || image[7] != 1)
				Reject(where + " must be a sampled float Texture2D");
		}

		void CheckImage(const Module& module, const Module::Variable& variable, const ShaderInterface& expected)
		{
			const auto set = module.Decorated(variable.Id, DescriptorSet);
			const auto binding = module.Decorated(variable.Id, Binding);
			if (!set || !binding)
				Reject("a resource without a set and binding");
			const std::string where = "set " + std::to_string(*set) + " binding " + std::to_string(*binding);
			const auto found = std::find_if(expected.Images.begin(), expected.Images.end(),
				[&](const ShaderImageBinding& image) { return image.Set == *set && image.Binding == *binding; });
			if (found == expected.Images.end())
				Reject("uses a texture or sampler at " + where + ", which the engine does not bind to this stage");
			const auto type = module.Type(module.Type(variable.Pointer)[3]);
			const auto opcode = type[0] & 0xffff;
			using Kind = ShaderImageBinding::Kind;
			switch (found->Type)
			{
			case Kind::Texture:
				CheckSampledImage(module, type[1], where);
				break;
			case Kind::TextureArray:
				if (opcode != OpTypeRuntimeArray)
					Reject(where + " must be a runtime array of Texture2D (the bindless array)");
				CheckSampledImage(module, type[2], where);
				break;
			case Kind::Sampler:
				if (opcode != OpTypeSampler)
					Reject(where + " must be a SamplerState");
				break;
			case Kind::SamplerArray:
			{
				const auto length = opcode == OpTypeArray ? module.Constants.find(type[3]) : module.Constants.end();
				if (opcode != OpTypeArray || (module.Type(type[2])[0] & 0xffff) != OpTypeSampler ||
					length == module.Constants.end() || length->second != found->Count)
					Reject(where + " must be SamplerState[" + std::to_string(found->Count) + "]");
				break;
			}
			}
		}

		bool IsFloat4(const Module& module, std::uint32_t id)
		{
			const auto type = module.Type(id);
			if ((type[0] & 0xffff) != OpTypeVector || type[3] != 4)
				return false;
			const auto component = module.Type(type[2]);
			return (component[0] & 0xffff) == OpTypeFloat && component[2] == 32;
		}

		// The members of a parameter block element (FD-19), or nothing for plain float4 elements.
		std::vector<MaterialParameter> ReadParameterBlock(const Module& module, std::uint32_t element,
			std::optional<std::uint32_t> stride, const std::string& where)
		{
			if (IsFloat4(module, element))
			{
				if (stride != 16u)
					Reject(where + " must hold float4 elements 16 bytes apart");
				return {};
			}
			const auto type = module.Type(element);
			if ((type[0] & 0xffff) != OpTypeStruct)
				Reject(where + " must hold float4 values or one material parameter struct");
			const auto offsets = module.Offsets(element);
			std::vector<MaterialParameter> parameters;
			std::size_t values = 0, textures = 0;
			const auto names = module.MemberNames.find(element);
			for (std::uint32_t member = 0; member < offsets.size(); ++member)
			{
				if (offsets[member] != member * 16)
					Reject(where + " parameter members must each take 16 bytes, in order");
				MaterialParameter parameter;
				if (names != module.MemberNames.end() && names->second.contains(member))
					parameter.Name = names->second.at(member);
				if (parameter.Name.empty())
					Reject(where + " parameter member " + std::to_string(member) + " has no name in the compiled shader");
				const auto memberType = type[2 + member];
				if (IsFloat4(module, memberType))
					++values;
				else
				{
					const auto typeName = module.Names.find(memberType);
					const std::string name = typeName == module.Names.end() ? "" : typeName->second;
					if (name == "RavenClampTexture")
						parameter.Type = MaterialParameter::Kind::ClampTexture;
					else if (name == "RavenRepeatTexture")
						parameter.Type = MaterialParameter::Kind::RepeatTexture;
					else
						Reject(where + " parameter " + parameter.Name + " must be a float4, RavenClampTexture or RavenRepeatTexture");
					++textures;
				}
				parameters.push_back(std::move(parameter));
			}
			if (values > MaxMaterialValues || textures > MaxMaterialTextures)
				Reject(where + " declares " + std::to_string(values) + " values and " + std::to_string(textures) +
					" textures; a material has at most " + std::to_string(MaxMaterialValues) + " and " +
					std::to_string(MaxMaterialTextures));
			if (stride != static_cast<std::uint32_t>(offsets.size() * 16))
				Reject(where + " element stride differs from its parameter count");
			return parameters;
		}

		void CheckBuffer(const Module& module, const Module::Variable& variable, const ShaderInterface& expected)
		{
			const auto set = module.Decorated(variable.Id, DescriptorSet);
			const auto binding = module.Decorated(variable.Id, Binding);
			if (!set || !binding)
				Reject("a resource without a set and binding");
			const auto layout = std::find_if(expected.Buffers.begin(), expected.Buffers.end(),
				[&](const ShaderBufferLayout& buffer) { return buffer.Set == *set && buffer.Binding == *binding; });
			const std::string where = "set " + std::to_string(*set) + " binding " + std::to_string(*binding);
			if (layout == expected.Buffers.end())
				Reject("uses " + where + ", which the engine does not bind to this stage");
			if (variable.Class == UniformConstant)
				Reject("uses a texture or sampler at " + where + "; the engine binds a buffer there");
			const auto block = module.Type(module.Type(variable.Pointer)[3]);
			if ((block[0] & 0xffff) != OpTypeStruct)
				Reject(where + " is not a buffer block");
			if (layout->Stride == 0)
			{
				if (variable.Class != Uniform)
					Reject(where + " must be a constant buffer");
				if (const auto offsets = module.Offsets(block[1]); offsets != layout->Offsets)
					Reject(where + " member offsets " + List(offsets) + " differ from the engine's " + List(layout->Offsets));
				return;
			}
			if (block.size() != 3)
				Reject(where + " must hold one runtime array");
			const auto array = module.Type(block[2]);
			if ((array[0] & 0xffff) != OpTypeRuntimeArray)
				Reject(where + " must hold one runtime array");
			if (layout->ParameterBlock)
			{
				ReadParameterBlock(module, array[2], module.Decorated(block[2], ArrayStride), where);
				return;
			}
			const auto stride = module.Decorated(block[2], ArrayStride);
			if (stride != layout->Stride)
				Reject(where + " element stride " + std::to_string(stride.value_or(0)) + " differs from the engine's " +
					std::to_string(layout->Stride) + " bytes");
			const auto element = module.Type(array[2]);
			const std::vector<std::uint32_t> offsets = (element[0] & 0xffff) == OpTypeStruct ?
				module.Offsets(array[2]) : std::vector<std::uint32_t>{};
			if (offsets != layout->Offsets)
				Reject(where + " member offsets " + List(offsets) + " differ from the engine's " + List(layout->Offsets));
		}
	}

	void CheckShaderInterface(std::span<const std::uint32_t> words, const ShaderInterface& expected)
	{
		const auto module = Parse(words);
		const bool compute = expected.Kind == ShaderInterface::Stage::Compute;
		const bool fragment = expected.Kind == ShaderInterface::Stage::Fragment;
		const auto model = compute ? ExecutionModelCompute : fragment ? ExecutionModelFragment : ExecutionModelVertex;
		if (module.Entry[1] != model)
			Reject(compute ? "expected a compute shader" : fragment ? "expected a fragment shader" : "expected a vertex shader");
		const char* entry = compute ? "CSMain" : fragment ? "PSMain" : "VSMain";
		if (module.EntryName != entry)
			Reject(std::string("expected the entry point ") + entry);
		if (compute)
		{
			if (module.LocalSize != expected.LocalSize)
			{
				const auto size = module.LocalSize.value_or(std::array<std::uint32_t, 3>{});
				Reject("numthreads is (" + std::to_string(size[0]) + ", " + std::to_string(size[1]) + ", " +
					std::to_string(size[2]) + "); the engine dispatches (" + std::to_string(expected.LocalSize[0]) + ", " +
					std::to_string(expected.LocalSize[1]) + ", " + std::to_string(expected.LocalSize[2]) + ")");
			}
		}
		else
		{
			CheckStageVariables(module, Input, expected.Inputs, "input", fragment);
			CheckStageVariables(module, Output, expected.Outputs, "output");
		}
		for (const auto& variable : module.Variables)
		{
			switch (variable.Class)
			{
			case UniformConstant:
				CheckImage(module, variable, expected);
				break;
			case Uniform:
			case StorageBuffer:
				CheckBuffer(module, variable, expected);
				break;
			case PushConstant:
			{
				if (expected.PushConstantOffsets.empty())
					Reject("declares push constants, which this pipeline does not have");
				const auto block = module.Type(module.Type(variable.Pointer)[3]);
				if (const auto offsets = module.Offsets(block[1]); offsets != expected.PushConstantOffsets)
					Reject("push constant offsets " + List(offsets) + " differ from the engine's " +
						List(expected.PushConstantOffsets));
				break;
			}
			default:
				break;
			}
		}
	}

	std::vector<MaterialParameter> ReadMaterialParameters(std::span<const std::uint32_t> words)
	{
		const auto module = Parse(words);
		for (const auto& variable : module.Variables)
		{
			if (variable.Class != StorageBuffer && variable.Class != Uniform)
				continue;
			if (module.Decorated(variable.Id, DescriptorSet) != 2u ||
				module.Decorated(variable.Id, Binding) != RavenSceneParameterBinding)
				continue;
			const auto block = module.Type(module.Type(variable.Pointer)[3]);
			if ((block[0] & 0xffff) != OpTypeStruct || block.size() != 3)
				continue;
			const auto array = module.Type(block[2]);
			if ((array[0] & 0xffff) != OpTypeRuntimeArray)
				continue;
			auto parameters = ReadParameterBlock(module, array[2], module.Decorated(block[2], ArrayStride),
				"the material parameters");
			if (!parameters.empty())
				return parameters;
		}
		return {};
	}
}
