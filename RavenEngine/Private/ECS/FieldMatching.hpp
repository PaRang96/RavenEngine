#pragma once

#include "Raven/ECS/Registry.hpp"

#include <cstddef>
#include <span>
#include <string_view>
#include <vector>

namespace Raven::ECS
{
	// The one field-matching rule of FD-16 (docs/DataModelDesign.md), used by live migration and by
	// scene and user-data loading. For each target field, the index of the stored field that supplies
	// its value, or -1: the stored field of the same name first, then one named by the target's old
	// names, in their declared order. MakeComponentSchema rejects alias tables this rule could read
	// two ways (an old name that is also a current name, or one old name claimed by two fields), so
	// each stored field supplies at most one target and the declaration order never matters.
	template<typename Stored, typename NameOf>
	std::vector<std::ptrdiff_t> MatchSources(std::span<const Stored> stored, const ComponentSchema& target,
		const NameOf& nameOf)
	{
		const auto find = [&](std::string_view name) -> std::ptrdiff_t
		{
			for (std::size_t index = 0; index < stored.size(); ++index)
				if (std::string_view(nameOf(stored[index])) == name)
					return static_cast<std::ptrdiff_t>(index);
			return -1;
		};
		std::vector<std::ptrdiff_t> sources(target.Fields.size(), -1);
		for (std::size_t index = 0; index < target.Fields.size(); ++index)
		{
			const auto& field = target.Fields[index];
			auto source = find(field.Name);
			for (std::size_t old = 0; source < 0 && old < field.OldNames.size(); ++old)
				source = find(field.OldNames[old]);
			sources[index] = source;
		}
		return sources;
	}

	// Same kind, flags, extents, tag and size, for any two field descriptions (a registered
	// FieldSchema, or a field stored in a file): the bytes of one can be copied into the other.
	template<typename Left, typename Right>
	bool SameFieldType(const Left& left, const Right& right) noexcept
	{
		return left.Kind == right.Kind && left.Flags == right.Flags && left.Extent == right.Extent &&
			left.Tag == right.Tag && left.Size == right.Size;
	}
}
