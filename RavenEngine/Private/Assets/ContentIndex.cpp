#include "Assets/ContentIndex.hpp"

#include "Assets/AssetKinds.hpp"
#include "Assets/FbxMesh.hpp"
#include "Raven/Assets/Model.hpp"
#include "Audio/SoundEvent.hpp"
#include "Renderer/MaterialFile.hpp"
#include "Core/PathText.hpp"
#include "Raven/Core/Files.hpp"
#include "Raven/Core/Log.hpp"
#include "Raven/Scene/SceneFile.hpp"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <locale>
#include <map>
#include <sstream>
#include <stdexcept>

namespace Raven
{
	namespace
	{
		// Sidecars are a few lines; a larger file is not one.
		constexpr std::size_t MaxSidecarBytes = 64 * 1024;
		// Enough for a scene file's ID section and for any sound event.
		constexpr std::size_t MaxHeaderBytes = 64;
		constexpr std::size_t MaxSoundEventBytes = 1024 * 1024;
		constexpr std::size_t MaxMaterialBytes = 1024 * 1024;

		std::string Relative(const std::filesystem::path& path, const std::filesystem::path& root)
		{
			return PathToUtf8(path.lexically_relative(root).lexically_normal());
		}

		std::vector<std::byte> ReadPrefix(const std::filesystem::path& path, std::size_t count)
		{
			std::ifstream file(path, std::ios::binary);
			std::vector<std::byte> bytes(count);
			file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(count));
			bytes.resize(static_cast<std::size_t>(file.gcount()));
			return bytes;
		}

		// Files the scan considers: sidecars, Raven formats with an ID header, and importable sources.
		enum class Role { None, Sidecar, OwnFormat, Imported };

		Role RoleOf(const std::filesystem::path& path, AssetKind& kind)
		{
			const auto extension = LowercaseExtension(path);
			if (extension == SidecarExtension)
				return Role::Sidecar;
			for (const auto& info : AssetKinds)
			{
				if (!info.IsFileBacked() ||
					std::find(info.Extensions.begin(), info.Extensions.end(), extension) == info.Extensions.end())
					continue;
				kind = info.Kind;
				return info.IdInHeader ? Role::OwnFormat : Role::Imported;
			}
			return Role::None;
		}

		bool Skipped(const std::filesystem::path& relative)
		{
			// Hidden folders (editor and VCS state) never hold assets.
			for (const auto& part : relative)
			{
				const auto text = PathToUtf8(part);
				if (!text.empty() && text.front() == '.')
					return true;
			}
			return false;
		}

		void WriteSidecar(const std::filesystem::path& path, const Sidecar& sidecar)
		{
			const auto text = FormatSidecar(sidecar);
			Files::WriteAtomic(path, std::as_bytes(std::span(text.data(), text.size())));
		}
	}

	Sidecar ParseSidecar(std::span<const std::byte> bytes, const std::filesystem::path& path)
	{
		Sidecar result;
		std::istringstream file(std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
		std::string line;
		bool header = false;
		std::size_t number = 0;
		while (std::getline(file, line))
		{
			++number;
			if (const auto comment = line.find('#'); comment != std::string::npos)
				line.erase(comment);
			std::istringstream input(line);
			input.imbue(std::locale::classic());
			std::string key, value;
			if (!(input >> key))
				continue;
			const auto fail = [&](const std::string& reason)
			{
				throw std::runtime_error(PathToUtf8(path) + " line " + std::to_string(number) + ": " + reason);
			};
			if (!header)
			{
				int version = 0;
				if (key != "raven-asset" || !(input >> version) || version != 1)
					fail("expected raven-asset 1");
				header = true;
			}
			else if (key == "id")
			{
				if (!(input >> value))
					fail("expected an asset UUID");
				result.Id = AssetId::Parse(value);
			}
			else if (key == "kind")
			{
				const auto* kind = (input >> value) ? FindAssetKindByKeyword(value) : nullptr;
				if (!kind || kind->IdInHeader || !kind->IsFileBacked())
					fail("expected kind texture, mesh or audio");
				result.Kind = kind->Kind;
			}
			else if (key == "color-space")
			{
				if (!(input >> value) || (value != "srgb" && value != "linear"))
					fail("expected color-space srgb or linear");
				result.ColorSpace = value == "srgb" ? TextureColorSpace::SRGB : TextureColorSpace::Linear;
			}
			else if (key == "part")
			{
				SidecarPart part;
				if (!(input >> std::quoted(part.Path) >> value) || part.Path.empty())
					fail("expected part \"<name path>\" <uuid>");
				part.Id = AssetId::Parse(value);
				for (const auto& other : result.Parts)
					if (other.Path == part.Path || other.Id == part.Id)
						fail("repeats a part's name path or ID");
				result.Parts.push_back(std::move(part));
			}
			else
				Log(LogLevel::Warning, PathToUtf8(path) + " line " + std::to_string(number) + ": unknown key '" + key + "' skipped");
		}
		if (!header || !result.Id.IsValid() || result.Kind == AssetKind::Unknown)
			throw std::runtime_error(PathToUtf8(path) + ": a sidecar needs raven-asset 1, an id and a kind");
		if (result.Kind == AssetKind::Texture && !result.ColorSpace)
			result.ColorSpace = TextureColorSpace::SRGB;
		if (!result.Parts.empty() && result.Kind != AssetKind::Mesh)
			throw std::runtime_error(PathToUtf8(path) + ": only a model has parts");
		return result;
	}

	std::string FormatSidecar(const Sidecar& sidecar)
	{
		std::string text = "raven-asset 1\nid " + sidecar.Id.ToString() + "\nkind " +
			std::string(AssetKindKeyword(sidecar.Kind)) + '\n';
		if (sidecar.Kind == AssetKind::Texture)
			text += std::string("color-space ") +
				(sidecar.ColorSpace.value_or(TextureColorSpace::SRGB) == TextureColorSpace::SRGB ? "srgb" : "linear") + '\n';
		for (const auto& part : sidecar.Parts)
		{
			std::ostringstream line;
			line << "part " << std::quoted(part.Path) << ' ' << part.Id.ToString() << '\n';
			text += line.str();
		}
		return text;
	}

	PartTableUpdate UpdatePartTable(const Sidecar& sidecar, const std::vector<std::string>& parts)
	{
		PartTableUpdate result;
		result.Updated = sidecar;
		if (sidecar.Kind != AssetKind::Mesh || (parts.size() < 2 && sidecar.Parts.empty()))
			return result;
		// The file's order, each with its recorded ID or a new one; recorded parts it lacks follow.
		std::vector<SidecarPart> table;
		for (const auto& path : parts)
		{
			const auto known = std::find_if(sidecar.Parts.begin(), sidecar.Parts.end(),
				[&](const SidecarPart& part) { return part.Path == path; });
			if (known != sidecar.Parts.end())
				table.push_back(*known);
			else
			{
				table.push_back({ path, AssetId::Generate() });
				result.Added.push_back(table.back());
			}
		}
		for (const auto& part : sidecar.Parts)
			if (std::find(parts.begin(), parts.end(), part.Path) == parts.end())
			{
				table.push_back(part);
				result.Missing.push_back(part);
			}
		result.Changed = !result.Added.empty() || table.size() != sidecar.Parts.size() ||
			!std::equal(table.begin(), table.end(), sidecar.Parts.begin(),
				[](const SidecarPart& left, const SidecarPart& right) { return left.Path == right.Path && left.Id == right.Id; });
		result.Updated.Parts = std::move(table);
		return result;
	}

	PartTableUpdate ImportModel(const std::filesystem::path& source, std::vector<std::string>& report)
	{
		const auto sidecarPath = SidecarPath(source);
		const auto name = PathToUtf8(source.filename());
		// A model without a sidecar yet gets one, as the content scan would give it.
		std::error_code missing;
		const bool created = !std::filesystem::exists(sidecarPath, missing);
		if (created && !AcceptsSource(AssetKind::Mesh, source))
			throw std::invalid_argument(PathToUtf8(source) + " is not a model file");
		const auto sidecar = created ? Sidecar{ AssetId::Generate(), AssetKind::Mesh, std::nullopt, {} }
			: ParseSidecar(Files::ReadBinary(sidecarPath, MaxSidecarBytes), sidecarPath);
		if (created)
			report.push_back(name + ": new model, ID " + sidecar.Id.ToString());
		auto update = UpdatePartTable(sidecar, sidecar.Kind == AssetKind::Mesh ?
			ListFbxParts(ReadFbxBytes(source), source) : std::vector<std::string>{});
		for (const auto& part : update.Added)
			report.push_back(name + ": new part \"" + part.Path + "\" got ID " + part.Id.ToString());
		for (const auto& part : update.Missing)
			report.push_back(name + ": part \"" + part.Path + "\" (" + part.Id.ToString() + ") is no longer in the file; "
				"if it was renamed, run RavenSceneTool <Content> rename-part <model> \"" + part.Path +
				"\" \"<new name path>\", and references to it keep working");
		update.Changed = update.Changed || created;
		if (update.Changed)
			WriteSidecar(sidecarPath, update.Updated);
		return update;
	}

	std::filesystem::path SidecarPath(const std::filesystem::path& source)
	{
		auto path = source;
		path += SidecarExtension;
		return path;
	}

	ContentIndex ScanContent(const std::filesystem::path& root, bool createSidecars)
	{
		ContentIndex index;
		std::vector<std::filesystem::path> files;
		std::error_code error;
		for (std::filesystem::recursive_directory_iterator it(root, error), end; !error && it != end; it.increment(error))
			if (it->is_regular_file(error) && !Skipped(it->path().lexically_relative(root)))
				files.push_back(it->path());
		if (error)
			throw std::runtime_error("Cannot scan the content root " + PathToUtf8(root) + ": " + error.message());
		std::sort(files.begin(), files.end());

		// Who holds each ID: the file that declared it and its entry in the index.
		struct Owner
		{
			std::filesystem::path File;
			std::size_t Entry = 0;
			bool Sidecar = false;
		};
		std::map<AssetId, Owner> owners;
		const auto claim = [&](ContentAsset asset, const std::filesystem::path& file, bool sidecar) -> bool
		{
			const auto [entry, added] = owners.emplace(asset.Id, Owner{ file, index.Assets.size(), sidecar });
			if (!added)
				return false;
			index.Assets.push_back(std::move(asset));
			return true;
		};
		const auto written = [](const std::filesystem::path& file)
		{
			std::error_code ignored;
			return std::filesystem::last_write_time(file, ignored);
		};
		for (const auto& file : files)
		{
			AssetKind kind = AssetKind::Unknown;
			const auto role = RoleOf(file, kind);
			const auto relative = Relative(file, root);
			try
			{
				if (role == Role::Sidecar)
				{
					auto source = file;
					source.replace_extension();
					if (!std::filesystem::is_regular_file(source, error))
					{
						index.Problems.push_back(relative + " has no source file beside it (an orphaned sidecar); it is skipped");
						continue;
					}
					auto sidecar = ParseSidecar(Files::ReadBinary(file, MaxSidecarBytes), file);
					if (!AcceptsSource(sidecar.Kind, source))
					{
						index.Problems.push_back(relative + " names kind " + std::string(AssetKindKeyword(sidecar.Kind)) +
							", which does not match its source; it is skipped");
						continue;
					}
					const auto sourcePath = source.lexically_relative(root).lexically_normal();
					// Development keeps a model's part table in step with the file, so re-exports keep their
					// IDs and a vanished part is reported, never silently dropped (FD-23).
					if (createSidecars && sidecar.Kind == AssetKind::Mesh)
					{
						std::vector<std::string> report;
						try
						{
							sidecar = ImportModel(source, report).Updated;
						}
						catch (const std::exception& problem)
						{
							index.Problems.push_back(relative + ": its parts could not be read: " + problem.what());
						}
						for (auto& line : report)
							index.Problems.push_back(std::move(line));
					}
					for (const auto& part : sidecar.Parts)
						if (!claim({ part.Id, AssetKind::Mesh, sourcePath, std::nullopt, part.Path }, file, true))
							index.Problems.push_back(relative + ": part \"" + part.Path + "\" repeats the ID " +
								part.Id.ToString() + "; it is skipped");
					ContentAsset asset{ sidecar.Id, sidecar.Kind, sourcePath, sidecar.ColorSpace };
					if (claim(asset, file, true))
						continue;
					// Two sidecars with one ID: a copied file and its copied sidecar. The copy is the
					// sidecar written later, whatever the path order; it gets a new ID. Equal times
					// cannot tell them apart, so nothing is rewritten and the later path is skipped.
					auto& owner = owners[asset.Id];
					const auto original = Relative(owner.File, root);
					const auto ownerTime = owner.Sidecar ? written(owner.File) : std::filesystem::file_time_type::max();
					const auto thisTime = written(file);
					if (!createSidecars || ownerTime == thisTime)
					{
						index.Problems.push_back(relative + " repeats the ID of " + original +
							(createSidecars ? "; neither is newer, so it is skipped: delete the copy's sidecar to give it a new ID"
								: "; it is skipped"));
						continue;
					}
					if (thisTime > ownerTime)
					{
						sidecar.Id = AssetId::Generate();
						WriteSidecar(file, sidecar);
						asset.Id = sidecar.Id;
						claim(asset, file, true);
						index.Problems.push_back(relative + " repeated the ID of " + original + ", which is older; it got the new ID " +
							sidecar.Id.ToString());
						continue;
					}
					// The earlier path is the copy: it takes the new ID, and this file keeps the original.
					auto copy = ParseSidecar(Files::ReadBinary(owner.File, MaxSidecarBytes), owner.File);
					copy.Id = AssetId::Generate();
					WriteSidecar(owner.File, copy);
					auto moved = owner;
					index.Assets[moved.Entry].Id = copy.Id;
					owners.erase(asset.Id);
					owners.emplace(copy.Id, moved);
					claim(asset, file, true);
					index.Problems.push_back(original + " repeated the ID of " + relative + ", which is older; it got the new ID " +
						copy.Id.ToString());
				}
				else if (role == Role::Imported)
				{
					if (std::filesystem::exists(SidecarPath(file), error))
						continue;
					if (!createSidecars)
					{
						index.Problems.push_back(relative + " has no sidecar; it is not part of the content");
						continue;
					}
					Sidecar sidecar{ AssetId::Generate(), kind, kind == AssetKind::Texture
						? std::optional<TextureColorSpace>(TextureColorSpace::SRGB) : std::nullopt };
					WriteSidecar(SidecarPath(file), sidecar);
					claim({ sidecar.Id, kind, file.lexically_relative(root).lexically_normal(), sidecar.ColorSpace }, SidecarPath(file), true);
					Log(LogLevel::Info, "New asset " + relative + ": wrote its sidecar with ID " + sidecar.Id.ToString());
				}
				else if (role == Role::OwnFormat)
				{
					AssetId id;
					if (kind == AssetKind::Scene)
						id = ReadSceneFileId(ReadPrefix(file, MaxHeaderBytes));
					else if (kind == AssetKind::Sound)
						id = ParseSoundEvent(Files::ReadBinary(file, MaxSoundEventBytes), file).Id;
					else if (kind == AssetKind::Material)
						id = ParseMaterial(Files::ReadBinary(file, MaxMaterialBytes), file).Id;
					if (!id.IsValid())
					{
						index.Problems.push_back(relative + " has no asset ID in its header; run RavenSceneTool upgrade-project");
						continue;
					}
					if (!claim({ id, kind, file.lexically_relative(root).lexically_normal(), std::nullopt }, file, false))
						index.Problems.push_back(relative + " repeats an ID already used; it is skipped");
				}
			}
			catch (const std::exception& problem)
			{
				index.Problems.push_back(problem.what());
			}
		}
		std::sort(index.Assets.begin(), index.Assets.end(),
			[](const ContentAsset& left, const ContentAsset& right) { return left.Source < right.Source; });
		return index;
	}

	std::vector<std::byte> ContentFingerprint(const std::filesystem::path& root)
	{
		std::vector<std::string> lines;
		std::error_code error;
		for (std::filesystem::recursive_directory_iterator it(root, error), end; !error && it != end; it.increment(error))
		{
			if (!it->is_regular_file(error) || Skipped(it->path().lexically_relative(root)))
				continue;
			AssetKind kind = AssetKind::Unknown;
			const auto role = RoleOf(it->path(), kind);
			if (role == Role::None)
				continue;
			std::string line = Relative(it->path(), root) + '|' + std::to_string(it->file_size(error)) + '|' +
				std::to_string(it->last_write_time(error).time_since_epoch().count());
			if (role == Role::Sidecar)
			{
				// Sidecars are small: their content, not only their size and time, decides.
				const auto bytes = ReadPrefix(it->path(), MaxSidecarBytes);
				line.append(reinterpret_cast<const char*>(bytes.data()), bytes.size());
			}
			lines.push_back(std::move(line));
		}
		std::sort(lines.begin(), lines.end());
		std::vector<std::byte> bytes;
		for (const auto& line : lines)
		{
			const auto* data = reinterpret_cast<const std::byte*>(line.data());
			bytes.insert(bytes.end(), data, data + line.size());
			bytes.push_back(std::byte{ '\n' });
		}
		return bytes;
	}
}

namespace Raven
{
	std::string RenamePart(const std::filesystem::path& source, const std::string& from, const std::string& to)
	{
		const auto sidecarPath = SidecarPath(source);
		auto sidecar = ParseSidecar(Files::ReadBinary(sidecarPath, MaxSidecarBytes), sidecarPath);
		const auto parts = ListFbxParts(ReadFbxBytes(source), source);
		if (std::find(parts.begin(), parts.end(), to) == parts.end())
			throw std::invalid_argument(PathToUtf8(source.filename()) + " has no part \"" + to + "\"");
		const auto old = std::find_if(sidecar.Parts.begin(), sidecar.Parts.end(),
			[&](const SidecarPart& part) { return part.Path == from; });
		if (old == sidecar.Parts.end())
			throw std::invalid_argument(PathToUtf8(sidecarPath.filename()) + " records no part \"" + from + "\"");
		const auto id = old->Id;
		std::string dropped;
		std::erase_if(sidecar.Parts, [&](const SidecarPart& part)
		{
			if (part.Path != to)
				return false;
			dropped = part.Id.ToString();
			return true;
		});
		for (auto& part : sidecar.Parts)
			if (part.Id == id)
				part.Path = to;
		WriteSidecar(sidecarPath, sidecar);
		return PathToUtf8(source.filename()) + ": part \"" + to + "\" now has " + id.ToString() + ", the ID of \"" + from + "\"" +
			(dropped.empty() ? "" : "; the ID the importer gave it, " + dropped + ", is dropped");
	}

	ModelDescription ReadModel(const std::filesystem::path& source)
	{
		const auto sidecarPath = SidecarPath(source);
		const auto sidecar = ParseSidecar(Files::ReadBinary(sidecarPath, MaxSidecarBytes), sidecarPath);
		if (sidecar.Kind != AssetKind::Mesh)
			throw std::invalid_argument(PathToUtf8(source) + " is not a model");
		// One mesh: the file's own ID, which is that mesh already placed in the model.
		if (sidecar.Parts.empty())
			return { sidecar.Id, { ModelNode{ PathToUtf8(source.stem()), PathToUtf8(source.stem()), -1, {}, {},
				{ 1.0f, 1.0f, 1.0f }, sidecar.Id } } };
		ModelDescription result{ sidecar.Id, {} };
		for (const auto& node : ReadFbxNodes(ReadFbxBytes(source), source))
		{
			ModelNode entry{ node.Name, node.Path, node.Parent, node.Position, EulerFromQuat(node.Rotation), node.Scale, {} };
			if (node.Mesh)
			{
				const auto part = std::find_if(sidecar.Parts.begin(), sidecar.Parts.end(),
					[&](const SidecarPart& recorded) { return recorded.Path == node.Path; });
				if (part == sidecar.Parts.end())
					throw std::runtime_error(PathToUtf8(source) + ": part \"" + node.Path + "\" is not in " +
						PathToUtf8(sidecarPath.filename()) + " yet; run the content scan or RavenSceneTool import-model");
				entry.Part = part->Id;
			}
			result.Nodes.push_back(std::move(entry));
		}
		return result;
	}
}
