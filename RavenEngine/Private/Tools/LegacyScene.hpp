#pragma once
#include "Raven/Scene/SceneDocument.hpp"
namespace Raven
{
	SceneDocument ImportLegacyScene(std::span<const std::byte> bytes);
}
