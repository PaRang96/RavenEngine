#include "Raven/Scene/SceneDocument.hpp"
#include "Raven/Core/Files.hpp"
#include "Assets/AssetPath.hpp"
#include "SceneBinary.hpp"

#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>

namespace Raven
{
	namespace
	{
		bool Token(const std::string& s)
		{
			return !s.empty() && s.size() <= 128 &&
				   std::all_of(s.begin(), s.end(),
							   [](unsigned char c) { return c > 0x20 && c != 0x7f && c != '#' && c != '"'; });
		}
		void Require(bool valid, const char* reason)
		{
			if (!valid)
				throw std::invalid_argument(reason);
		}
		bool Finite(Vec3 v)
		{
			return std::isfinite(v.X) && std::isfinite(v.Y) && std::isfinite(v.Z);
		}
		bool Color(Vec4 v)
		{
			return v.X >= 0 && v.X <= 1 && v.Y >= 0 && v.Y <= 1 && v.Z >= 0 && v.Z <= 1 && v.W >= 0 &&
				   v.W <= 1;
		}
		constexpr std::string_view Magic = "RVN3DSCN";
		constexpr std::uint32_t BaseVersion = 1;
		constexpr std::uint32_t CollectionVersion = 2;
	}

	void SceneDocument::Validate() const
	{
		Require(Camera && OrbitTarget, "Scene needs a camera and orbit target");
		Require(Meshes.size() <= 4096 && Groups.size() <= 4096 && Materials.size() <= 4096 &&
					Parents.size() <= 8193 && Switches.size() <= 4096 && Rotators.size() <= 4096 &&
					Doors.size() <= 4096 && Goals.size() <= 4096,
				"Scene count limit exceeded");
		const auto offset = Camera->Spawn.Position - *OrbitTarget;
		const float distance = std::hypot(offset.X, offset.Y, offset.Z);
		Require(Finite(*OrbitTarget) && std::isfinite(distance) && distance > 0,
				"Invalid camera orbit target");
		ECS::GetCameraView(Camera->Spawn, Camera->Lens, 1.0f);
		Require(std::isfinite(Camera->Lens.VerticalFovRadians) &&
					std::isfinite(Camera->Lens.OrthographicHeight),
				"Invalid camera settings");
		Lighting.value_or(ECS::LightingComponent{}).GetLighting().Validate();
		std::map<std::string, const ECS::TransformComponent*> nodes;
		const auto add = [&](const std::string& id, const ECS::TransformComponent& t)
		{
			Require(Token(id) && nodes.emplace(id, &t).second, "Invalid or duplicate authored entity ID");
			Require(Finite(t.Position) && Finite(t.Rotation) && Finite(t.Scale) && t.Scale.X != 0 &&
						t.Scale.Y != 0 && t.Scale.Z != 0,
					"Invalid local transform");
		};
		std::set<std::string> meshes;
		for (const auto& g : Groups)
			add(g.Name, g.Transform);
		for (const auto& m : Meshes)
		{
			add(m.Name, m.Transform);
			meshes.insert(m.Name);
			Require(m.Asset.IsValid() && Color(m.Tint), "Invalid mesh reference or tint");
			Require(m.MaterialName.empty() || Materials.contains(m.MaterialName),
					"Unknown material assignment");
		}
		add(CameraId, Camera->Spawn);
		Require(Token(LightingId) && !nodes.contains(LightingId), "Invalid or duplicate lighting ID");
		for (const auto& [name, m] : Materials)
			Require(Token(name) && Color(m.BaseColor), "Invalid material");
		for (const auto& [child, parent] : Parents)
			Require(nodes.contains(child) && nodes.contains(parent), "Unknown hierarchy reference");
		std::map<std::string, Mat4> worlds;
		std::map<std::string, std::size_t> depths;
		for (const auto& [name, t] : nodes)
		{
			std::vector<std::string> chain;
			std::set<std::string> visited;
			auto ancestor = name;
			while (!worlds.contains(ancestor))
			{
				Require(visited.insert(ancestor).second, "Scene hierarchy contains a cycle");
				Require(chain.size() < 256, "Scene hierarchy exceeds 256 nodes of depth");
				chain.push_back(ancestor);
				const auto parent = Parents.find(ancestor);
				if (parent == Parents.end())
					break;
				ancestor = parent->second;
			}
			auto world = worlds.contains(ancestor) ? worlds.at(ancestor) : Mat4::Identity();
			auto depth = depths.contains(ancestor) ? depths.at(ancestor) : 0;
			for (auto i = chain.rbegin(); i != chain.rend(); ++i)
			{
				Require(++depth <= 256, "Scene hierarchy exceeds 256 nodes of depth");
				world = world * nodes.at(*i)->GetMatrix();
				for (const auto& column : world.Columns)
					for (float v : column)
						Require(std::isfinite(v), "World transform exceeds numeric range");
				worlds.emplace(*i, world);
				depths.emplace(*i, depth);
			}
		}
		for (const auto& m : Meshes)
			if (!m.MaterialName.empty() && Materials.at(m.MaterialName).Lit)
				NormalMatrix(worlds.at(m.Name));
		if (!ControlledMesh.empty() || !Beacon.empty())
			Require(meshes.contains(ControlledMesh) && meshes.contains(Beacon) && ControlledMesh != Beacon &&
						std::isfinite(BeaconRadius) && BeaconRadius > 0,
					"Invalid legacy controlled/beacon binding");
		else
			Require(BeaconRadius == 0, "Unbound beacon must have zero radius");
		std::set<std::string> switches, rotators, doors, goals;
		for (const auto& s : Switches)
			Require(meshes.contains(s.Entity) && switches.insert(s.Entity).second,
					"Invalid or duplicate switch entity");
		for (const auto& r : Rotators)
		{
			Require(nodes.contains(r.Entity) && r.Entity != CameraId && rotators.insert(r.Entity).second,
					"Invalid or duplicate rotator entity");
			Require(r.Switch.empty() || switches.contains(r.Switch), "Unknown rotator switch");
			Require(Finite(r.Rotation) && std::abs(r.Rotation.X) <= 628 && std::abs(r.Rotation.Y) <= 628 &&
						std::abs(r.Rotation.Z) <= 628 && std::isfinite(r.Duration) && r.Duration > 0,
					"Invalid rotator range or duration");
		}
		for (const auto& d : Doors)
		{
			Require(nodes.contains(d.Entity) && d.Entity != CameraId && doors.insert(d.Entity).second &&
						!rotators.contains(d.Entity),
					"Invalid or conflicting door entity");
			Require(!d.RequiredRotators.empty() && d.RequiredRotators.size() <= 4 && Finite(d.OpenOffset) &&
						Finite(nodes.at(d.Entity)->Position + d.OpenOffset) && std::isfinite(d.Duration) &&
						d.Duration > 0,
					"Invalid door settings");
			std::set<std::string> required;
			for (const auto& id : d.RequiredRotators)
			{
				Require(rotators.contains(id) && required.insert(id).second,
						"Unknown or duplicate required rotator");
				const auto r = std::find_if(Rotators.begin(), Rotators.end(),
											[&](const auto& value) { return value.Entity == id; });
				Require(!r->Switch.empty(), "Door requires a switch-driven rotator");
			}
		}
		for (const auto& g : Goals)
			Require(meshes.contains(g.Entity) && goals.insert(g.Entity).second &&
						(g.Door.empty() || doors.contains(g.Door)) && std::isfinite(g.Radius) && g.Radius > 0,
					"Invalid goal binding or radius");
		if (Collection)
		{
			Require(!Collection->Pickups.empty() && Collection->Pickups.size() <= 4096,
					"Invalid collection pickup count");
			Require(Goals.size() == 1 && Goals.front().Door.empty() && Beacon.empty() && Switches.empty(),
					"Collection requires one unbound exit goal and no switch or beacon objective");
			std::set<std::string> pickups;
			for (const auto& id : Collection->Pickups)
				Require(meshes.contains(id) && !goals.contains(id) && pickups.insert(id).second,
						"Invalid or duplicate pickup entity");
		}
	}

	std::vector<std::byte> SceneDocument::Encode() const
	{
		Validate();
		SceneBinary::Writer w;
		w.Text(CameraId);
		w.Text(LightingId);
		w.Text(ControlledMesh);
		w.Text(Beacon);
		w.Float(BeaconRadius);
		w.Id(WinSound.Id);
		w.Transform(Camera->Spawn);
		w.U32(static_cast<std::uint32_t>(Camera->Lens.ProjectionMode));
		w.Float(Camera->Lens.VerticalFovRadians);
		w.Float(Camera->Lens.OrthographicHeight);
		w.Float(Camera->Lens.NearPlane);
		w.Float(Camera->Lens.FarPlane);
		w.Vector(*OrbitTarget);
		const auto light = Lighting.value_or(ECS::LightingComponent{});
		w.Vector(light.Ambient);
		w.Vector(light.Direction);
		w.Vector(light.Color);
		w.U32(static_cast<std::uint32_t>(Meshes.size()));
		for (const auto& m : Meshes)
		{
			w.Text(m.Name);
			w.Id(m.Asset.Id);
			w.Transform(m.Transform);
			w.Color(m.Tint);
			w.Text(m.MaterialName);
		}
		w.U32(static_cast<std::uint32_t>(Groups.size()));
		for (const auto& g : Groups)
		{
			w.Text(g.Name);
			w.Transform(g.Transform);
		}
		w.U32(static_cast<std::uint32_t>(Parents.size()));
		for (const auto& [a, b] : Parents)
		{
			w.Text(a);
			w.Text(b);
		}
		w.U32(static_cast<std::uint32_t>(Materials.size()));
		for (const auto& [id, m] : Materials)
		{
			w.Text(id);
			w.U8(m.Lit ? 1 : 0);
			w.Color(m.BaseColor);
			w.Id(m.BaseColorTexture.Id);
		}
		w.U32(static_cast<std::uint32_t>(Switches.size()));
		for (const auto& s : Switches)
			w.Text(s.Entity);
		w.U32(static_cast<std::uint32_t>(Rotators.size()));
		for (const auto& r : Rotators)
		{
			w.Text(r.Entity);
			w.Text(r.Switch);
			w.Vector(r.Rotation);
			w.Float(r.Duration);
		}
		w.U32(static_cast<std::uint32_t>(Doors.size()));
		for (const auto& d : Doors)
		{
			w.Text(d.Entity);
			w.U32(static_cast<std::uint32_t>(d.RequiredRotators.size()));
			for (const auto& id : d.RequiredRotators)
				w.Text(id);
			w.Vector(d.OpenOffset);
			w.Float(d.Duration);
		}
		w.U32(static_cast<std::uint32_t>(Goals.size()));
		for (const auto& g : Goals)
		{
			w.Text(g.Entity);
			w.Text(g.Door);
			w.Float(g.Radius);
		}
		if (Collection)
		{
			w.U32(static_cast<std::uint32_t>(Collection->Pickups.size()));
			for (const auto& id : Collection->Pickups)
				w.Text(id);
		}
		Require(w.Bytes.size() <= SceneBinary::Limit - 20, "Binary scene exceeds 4 MiB");
		SceneBinary::Writer file;
		for (unsigned char c : Magic)
			file.U8(c);
		file.U32(Collection ? CollectionVersion : BaseVersion);
		file.U32(static_cast<std::uint32_t>(w.Bytes.size()));
		file.U32(SceneBinary::Checksum(w.Bytes));
		file.Bytes.insert(file.Bytes.end(), w.Bytes.begin(), w.Bytes.end());
		return file.Bytes;
	}

	SceneDocument SceneDocument::Decode(std::span<const std::byte> bytes)
	{
		Require(bytes.size() <= SceneBinary::Limit, "Binary scene exceeds 4 MiB");
		SceneBinary::Reader header{bytes};
		for (unsigned char c : Magic)
			Require(header.U8() == c,
					"Invalid binary scene signature; import legacy text with RavenSceneTool");
		const auto version = header.U32();
		Require(version == BaseVersion || version == CollectionVersion, "Unsupported binary scene version");
		const auto size = header.U32(), checksum = header.U32();
		Require(size == header.Bytes.size() && checksum == SceneBinary::Checksum(header.Bytes),
				"Binary scene size/checksum mismatch");
		SceneBinary::Reader r{header.Bytes};
		SceneDocument d;
		d.CameraId = r.Text();
		d.LightingId = r.Text();
		d.ControlledMesh = r.Text();
		d.Beacon = r.Text();
		d.BeaconRadius = r.Float();
		d.WinSound.Id = r.Id();
		CameraDefinition camera;
		camera.Spawn = r.Transform();
		const auto projection = r.U32();
		Require(projection <= 1, "Unknown scene projection");
		camera.Lens.ProjectionMode = static_cast<ECS::CameraProjectionMode>(projection);
		camera.Lens.VerticalFovRadians = r.Float();
		camera.Lens.OrthographicHeight = r.Float();
		camera.Lens.NearPlane = r.Float();
		camera.Lens.FarPlane = r.Float();
		d.Camera = camera;
		d.OrbitTarget = r.Vector();
		ECS::LightingComponent light;
		light.Ambient = r.Vector();
		light.Direction = r.Vector();
		light.Color = r.Vector();
		d.Lighting = light;
		for (auto n = r.Count(); n--;)
		{
			MeshDefinition m;
			m.Name = r.Text();
			m.Asset.Id = r.Id();
			m.Transform = r.Transform();
			m.Tint = r.Color();
			m.MaterialName = r.Text();
			d.Meshes.push_back(std::move(m));
		}
		for (auto n = r.Count(); n--;)
		{
			GroupDefinition g;
			g.Name = r.Text();
			g.Transform = r.Transform();
			d.Groups.push_back(std::move(g));
		}
		for (auto n = r.Count(8193); n--;)
		{
			auto child = r.Text();
			auto parent = r.Text();
			Require(d.Parents.emplace(child, parent).second, "Duplicate binary parent record");
		}
		for (auto n = r.Count(); n--;)
		{
			auto id = r.Text();
			ECS::MaterialComponent m;
			const auto lit = r.U8();
			Require(lit <= 1, "Invalid material mode");
			m.Lit = lit != 0;
			m.BaseColor = r.Color();
			m.BaseColorTexture.Id = r.Id();
			Require(d.Materials.emplace(id, m).second, "Duplicate binary material");
		}
		for (auto n = r.Count(); n--;)
			d.Switches.push_back({r.Text()});
		for (auto n = r.Count(); n--;)
		{
			RotatorDefinition v;
			v.Entity = r.Text();
			v.Switch = r.Text();
			v.Rotation = r.Vector();
			v.Duration = r.Float();
			d.Rotators.push_back(std::move(v));
		}
		for (auto n = r.Count(); n--;)
		{
			DoorDefinition v;
			v.Entity = r.Text();
			for (auto k = r.Count(4); k--;)
				v.RequiredRotators.push_back(r.Text());
			v.OpenOffset = r.Vector();
			v.Duration = r.Float();
			d.Doors.push_back(std::move(v));
		}
		for (auto n = r.Count(); n--;)
		{
			GoalDefinition v;
			v.Entity = r.Text();
			v.Door = r.Text();
			v.Radius = r.Float();
			d.Goals.push_back(std::move(v));
		}
		if (version == CollectionVersion)
		{
			auto& collection = d.Collection.emplace();
			for (auto n = r.Count(); n--;)
				collection.Pickups.push_back(r.Text());
		}
		Require(r.Bytes.empty(), "Unexpected trailing binary scene data");
		d.Validate();
		return d;
	}

	void SceneDocument::Save(const std::filesystem::path& path, const AssetCatalog& catalog,
							 const std::filesystem::path& root) const
	{
		const auto bytes = Encode();
		const auto check = [&](AssetId id, AssetKind kind)
		{
			const auto& record = catalog.Get(id);
			Require(record.Kind == kind &&
						std::filesystem::is_regular_file(ResolveAssetPath(root, record.SourcePath)),
					"Scene asset has wrong type or missing source");
		};
		for (const auto& m : Meshes)
			check(m.Asset.Id, AssetKind::Mesh);
		for (const auto& [id, m] : Materials)
			if (m.BaseColorTexture.IsValid())
				check(m.BaseColorTexture.Id, AssetKind::Texture);
		if (WinSound.IsValid())
			check(WinSound.Id, AssetKind::Audio);
		Files::WriteAtomic(path, bytes);
	}
}
