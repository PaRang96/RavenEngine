#include "Assets/AssetDependencies.hpp"

#include "Assets/AssetKinds.hpp"
#include "Audio/SoundEvent.hpp"
#include "Raven/Scene/SceneFile.hpp"

#include <cstring>
#include <map>
#include <stdexcept>

namespace Raven
{
	void ForEachAssetRef(const SceneFile& file, const std::function<void(AssetId, AssetKind)>& visit)
	{
		std::map<AssetId, AssetKind> used;
		for (const auto& component : file.Components)
			for (std::size_t field = 0; field < component.Fields.size(); ++field)
			{
				const auto& description = component.Fields[field];
				const auto* kind = description.Kind == ECS::FieldKind::AssetRef ? FindAssetKindByTag(description.Tag) : nullptr;
				if (!kind || !kind->SceneDependency)
					continue;
				// The decoder checked that every section holds its rows of every field (SceneFile.cpp).
				const auto count = std::size_t{ description.Extent[0] } * description.Extent[1] * component.Entities.size();
				const auto* values = component.Values.data() + component.FieldOffset(field);
				for (std::size_t item = 0; item < count; ++item)
				{
					AssetId id;
					std::memcpy(&id, values + item * sizeof(AssetId), sizeof(AssetId));
					if (!id.IsValid())
						continue;
					const auto [entry, added] = used.emplace(id, kind->Kind);
					if (!added && entry->second != kind->Kind)
						throw std::invalid_argument("Asset " + id.ToString() + " is referenced as both " +
							std::string(AssetKindName(entry->second)) + " and " + std::string(kind->DisplayName) +
							" (" + component.Type + "::" + description.Name + ")");
				}
			}
		for (const auto& [id, kind] : used)
			visit(id, kind);
	}

	void ExpandSoundClips(const SoundEvent& event, const std::function<void(AssetId, AssetKind)>& visit)
	{
		for (const auto& clip : event.Clips)
			visit(clip, AssetKind::Audio);
	}
}
