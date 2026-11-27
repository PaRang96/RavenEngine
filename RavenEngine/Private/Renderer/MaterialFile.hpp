#pragma once

#include "Raven/Assets/AssetRecord.hpp"
#include "Raven/Core/Api.hpp"
#include "Raven/Math/Transform.hpp"
#include "Renderer/Shaders/ShaderLibrary.hpp"
#include "Renderer/Vulkan/ShaderInterface.hpp"

#include <array>
#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace Raven
{
	// A material asset (docs/RenderingContentDesign.md, FD-18): what designers set about a surface,
	// shared by every object that references it and reloaded live. Text, one setting per line:
	//
	//   raven-material 1
	//   id 2f6b0c1e-...                 the material's own asset ID
	//   base-color 0.9 0.55 0.18 1      a linear multiplier: red, green, blue, alpha, each 0 to 64
	//   texture base-color 7a590cee-... the base-color texture of a material without a shader
	//   lit                             shaded by the scene's light; without it the surface is unlit
	//   shader 4c2e...                  a material shader asset (FD-19)
	//   texture Noise 1d0e...           a texture the shader declares, by its name
	//   value EdgeColor 1 0.4 0.1 1     a float4 value the shader declares, by its name
	//
	// Unknown, repeated or non-finite settings are errors, so a typo never silently draws a default
	// (docs/DataModelDesign.md, version policy); the shader's names are checked when it draws.
	struct MaterialDefinition
	{
		static constexpr std::size_t TextureSlots = 4;

		AssetId Id;
		Vec4 BaseColor{ 1.0f, 1.0f, 1.0f, 1.0f };
		// Slot 0 is the base color of materials without a shader.
		std::array<AssetId, TextureSlots> Textures{};
		bool Lit = false;
		// A material shader (FD-19), and its parameters by the names the shader declares; the renderer
		// checks the names against the compiled shader.
		AssetId Shader;
		std::vector<std::pair<std::string, AssetId>> NamedTextures;
		std::vector<std::pair<std::string, Vec4>> Values;
	};

	// A compiled material shader: its SPIR-V, checked against the lit fragment interface, and the
	// parameters it declares (B0 contract 7).
	struct MaterialShader
	{
		std::vector<std::uint32_t> Words;
		std::vector<MaterialParameter> Parameters;
		// With a vertex offset (B0 contract 2): its vertex program and the bound it declares, in metres.
		std::vector<std::uint32_t> VertexWords;
		float OffsetBound = 0.0f;
	};

	// Whether a material shader's source declares a vertex offset, so it also builds a vertex program.
	RAVEN_API bool DeclaresVertexOffset(std::span<const std::byte> source);
	// The vertex program of a material shader with an offset: the engine's lit vertex program after the
	// shader's own source. Development hosts only; scratch receives the generated source.
	RAVEN_API CompiledHlsl CompileOffsetVertex(const std::filesystem::path& source, const std::filesystem::path& scratch);

	// Exported for the content tools, which stage a material's textures with it.
	RAVEN_API MaterialDefinition ParseMaterial(std::span<const std::byte> bytes, const std::filesystem::path& path);
}
