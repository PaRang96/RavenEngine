#pragma once
#include "Raven/Scene/SceneDocument.hpp"
#include <cstdint>
namespace Raven
{
	SceneDocument CreatePuzzleScene();
	SceneDocument CreateCollectionScene();
	SceneDocument CreateBenchmarkScene(std::uint32_t meshCount);
}
