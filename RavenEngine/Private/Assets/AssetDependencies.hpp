#pragma once

#include "Raven/Assets/AssetRecord.hpp"
#include "Raven/Core/Api.hpp"

#include <functional>

namespace Raven
{
	struct SceneFile;
	struct SoundEvent;
	struct MaterialDefinition;

	// Every load dependency a scene file names (CQ-DATA-01): each valid ID in an AssetRef field whose
	// kind is a scene dependency (AssetKinds.hpp), once per ID, in ID order. An ID referenced under
	// two different kinds is a data error, rejected with std::invalid_argument before any callback,
	// so the editor and a staged player agree on what a scene needs.
	RAVEN_API void ForEachAssetRef(const SceneFile& file, const std::function<void(AssetId, AssetKind)>& visit);

	// A sound event plays its clips, so they are its dependencies.
	RAVEN_API void ExpandSoundClips(const SoundEvent& event, const std::function<void(AssetId, AssetKind)>& visit);

	// A material samples its textures, so they are its dependencies (docs/RenderingContentDesign.md, FD-18).
	RAVEN_API void ExpandMaterialTextures(const MaterialDefinition& material,
		const std::function<void(AssetId, AssetKind)>& visit);
}
