#include "SampleScenes.hpp"

#include <numbers>
#include <stdexcept>
#include <utility>

namespace Raven
{
	SceneDocument CreateBenchmarkScene(std::uint32_t meshCount)
	{
		std::uint32_t columns;
		switch (meshCount)
		{
		case 100: columns = 10; break;
		case 500: columns = 25; break;
		case 2000: columns = 50; break;
		case 4000: columns = 80; break;
		default: throw std::invalid_argument("Benchmark mesh count must be 100, 500, 2000, or 4000");
		}
		const auto rows = meshCount / columns;
		constexpr float spacing = 1.55f;
		const auto centered = [spacing](std::uint32_t index, std::uint32_t count)
		{
			return (static_cast<float>(index) - .5f * static_cast<float>(count - 1)) * spacing;
		};

		SceneDocument scene;
		scene.CameraId = "main";
		scene.LightingId = "sun";
		scene.Camera.emplace().Spawn.Position = { 0, -79.0f, 0 };
		scene.OrbitTarget = Vec3{};
		scene.Materials["paint"].Lit = true;
		scene.Materials["textured"].Lit = true;
		scene.Materials.at("textured").BaseColorTexture.Id = AssetId::Parse("7a590cee-ec82-4a27-927f-860e64619c19");
		const auto cube = AssetId::Parse("6938c6b9-72ca-4bc1-b0bf-9dc31e158087");
		const auto sphere = AssetId::Parse("79607ed4-7591-4672-b47b-7107b21ac4ac");
		for (std::uint32_t row = 0; row < rows; ++row)
		{
			SceneDocument::GroupDefinition group;
			group.Name = "row-" + std::to_string(row);
			group.Transform.Position.Z = centered(row, rows);
			scene.Groups.push_back(group);
			for (std::uint32_t column = 0; column < columns; ++column)
			{
				const auto index = row * columns + column;
				const auto suffix = std::to_string(index);
				const bool animate = index % 5 == 0;
				SceneDocument::GroupDefinition pivot;
				pivot.Name = (animate ? "benchmark-rotor-" : "pivot-") + suffix;
				pivot.Transform.Position.X = centered(column, columns);
				scene.Groups.push_back(pivot);
				scene.Parents.emplace(pivot.Name, group.Name);
				if (animate)
					scene.Rotators.push_back({ pivot.Name, {}, { 0, .65f, 0 }, 1 });
				SceneDocument::MeshDefinition mesh;
				mesh.Name = "mesh-" + suffix;
				mesh.Asset.Id = index % 2 == 0 ? cube : sphere;
				mesh.Transform.Position.X = .12f;
				mesh.Transform.Scale = { .68f, .68f, .68f };
				mesh.Tint = index % 2 == 0 ? Vec4{ .9f, .55f, .18f, 1 } : Vec4{ .22f, .67f, .9f, 1 };
				mesh.MaterialName = index % 4 == 0 ? "textured" : "paint";
				scene.Parents.emplace(mesh.Name, pivot.Name);
				scene.Meshes.push_back(std::move(mesh));
			}
		}
		return scene;
	}

	SceneDocument CreatePuzzleScene()
	{
		SceneDocument d;
		d.CameraId = "main";
		d.LightingId = "sun";
		d.ControlledMesh = "arm-a";
		d.Beacon = "goal";
		d.BeaconRadius = 1.25f;
		d.Camera.emplace();
		d.Camera->Spawn.Position = {0, -7.8f, 2.2f};
		d.Camera->Spawn.Rotation = {-.12f, 0, 0};
		d.OrbitTarget = Vec3{0, .5f, .5f};
		d.Lighting.emplace();
		d.WinSound.Id = AssetId::Parse("ef451c5a-02db-4d81-89a1-ec2ea98ed719");
		d.Materials.emplace("paint", ECS::MaterialComponent{});
		d.Materials.at("paint").Lit = true;
		d.Materials.emplace("signal", ECS::MaterialComponent{});
		const auto group = [&](const std::string& id, Vec3 position, Vec3 scale)
		{
			SceneDocument::GroupDefinition g;
			g.Name = id;
			g.Transform.Position = position;
			g.Transform.Scale = scale;
			d.Groups.push_back(g);
		};
		const auto mesh =
			[&](const std::string& id, Vec3 position, Vec3 scale, Vec4 color, bool sphere = false)
		{
			SceneDocument::MeshDefinition m;
			m.Name = id;
			m.Asset.Id = AssetId::Parse(sphere ? "79607ed4-7591-4672-b47b-7107b21ac4ac"
											   : "6938c6b9-72ca-4bc1-b0bf-9dc31e158087");
			m.Transform.Position = position;
			m.Transform.Scale = scale;
			m.Tint = color;
			m.MaterialName = id.starts_with("switch") || id == "goal" ? "signal" : "paint";
			d.Meshes.push_back(m);
		};
		group("rig", {0, 10, 0}, {1, 1, 1});
		d.Parents["main"] = "rig";
		group("assembly-a", {-2, 9, 0}, {1, 1, 1});
		group("assembly-b", {2, 9, 0}, {-1, .8f, 1.2f});
		for (const auto* suffix : {"a", "b"})
		{
			const std::string tail = suffix, base = "base-" + tail, arm = "arm-" + tail,
							  mount = "mount-" + tail, tip = "tip-" + tail;
			mesh(base, {0, 0, .7f}, {.3f, .3f, 1.4f}, {.6f, .6f, .8f, 1});
			mesh(arm, {0, 0, 1.5f}, {1.8f, .2f, .2f}, {.9f, .45f, .15f, 1});
			group(mount, {.6f, 0, 0}, {1, 1, 1});
			mesh(tip, {}, {.25f, 1.5f, 1.5f}, {.2f, .8f, 1, 1}, true);
			d.Parents[base] = "assembly-" + tail;
			d.Parents[arm] = "assembly-" + tail;
			d.Parents[mount] = arm;
			d.Parents[tip] = mount;
		}
		mesh("switch-a", {-2, 6, .9f}, {.65f, .35f, .65f}, {1, .4f, .1f, 1});
		mesh("switch-b", {2, 6, .9f}, {.65f, .35f, .65f}, {1, .4f, .1f, 1});
		mesh("floor", {0, 10, -.15f}, {8, 16, .15f}, {.65f, .7f, .8f, 1});
		mesh("wall-left", {-2.5f, 11, 1.5f}, {3, .3f, 3}, {.5f, .55f, .65f, 1});
		mesh("wall-right", {2.5f, 11, 1.5f}, {3, .3f, 3}, {.5f, .55f, .65f, 1});
		mesh("gate", {0, 11, 1.5f}, {2, .3f, 3}, {.7f, .25f, .8f, 1});
		mesh("goal", {0, 14, .6f}, {1, 1, 1}, {.1f, .85f, 1, 1}, true);
		d.Switches = {{"switch-a"}, {"switch-b"}};
		d.Rotators = {{"arm-a", "switch-a", {0, 0, std::numbers::pi_v<float> / 2}, 1.2f},
					  {"arm-b", "switch-b", {0, 0, -std::numbers::pi_v<float> / 2}, 1.2f}};
		d.Doors = {{"gate", {"arm-a", "arm-b"}, {0, 0, 3.6f}, 1.0f}};
		d.Goals = {{"goal", "gate", 1.25f}};
		d.Validate();
		return d;
	}

	SceneDocument CreateCollectionScene()
	{
		SceneDocument scene;
		scene.CameraId = "main";
		scene.LightingId = "sun";
		scene.Camera.emplace().Spawn.Position = {0, 6, 1.6f};
		scene.OrbitTarget = Vec3{0, 12, 1};
		scene.WinSound.Id = AssetId::Parse("ef451c5a-02db-4d81-89a1-ec2ea98ed719");
		scene.Materials["structure"].Lit = true;
		scene.Materials["marker"] = ECS::MaterialComponent{};
		const auto cube = AssetId::Parse("6938c6b9-72ca-4bc1-b0bf-9dc31e158087");
		const auto sphere = AssetId::Parse("79607ed4-7591-4672-b47b-7107b21ac4ac");
		const auto mesh = [&](const char* name, AssetId asset, Vec3 position, Vec3 scale, Vec4 tint,
							  const char* material)
		{
			SceneDocument::MeshDefinition definition;
			definition.Name = name;
			definition.Asset.Id = asset;
			definition.Transform.Position = position;
			definition.Transform.Scale = scale;
			definition.Tint = tint;
			definition.MaterialName = material;
			scene.Meshes.push_back(std::move(definition));
		};

		mesh("floor", cube, {0, 12, -.2f}, {8, 16, .2f}, {.27f, .31f, .38f, 1}, "structure");
		mesh("exit-post-left", cube, {-1.45f, 17, 1.55f}, {.24f, .24f, 3.1f}, {.2f, .55f, .7f, 1}, "structure");
		mesh("exit-post-right", cube, {1.45f, 17, 1.55f}, {.24f, .24f, 3.1f}, {.2f, .55f, .7f, 1}, "structure");
		mesh("exit-header", cube, {0, 17, 3.05f}, {3.15f, .24f, .24f}, {.2f, .55f, .7f, 1}, "structure");
		constexpr Vec4 gold{1, .67f, .12f, 1};
		constexpr Vec3 pickupSize{.8f, .8f, .8f};
		mesh("pickup-left", sphere, {-1.6f, 12, 1.6f}, pickupSize, gold, "marker");
		mesh("pickup-center", sphere, {0, 12, .85f}, pickupSize, gold, "marker");
		mesh("pickup-right", sphere, {1.6f, 12, 1.6f}, pickupSize, gold, "marker");
		mesh("exit", sphere, {0, 17, 1.6f}, {1, 1, 1}, {.1f, .85f, 1, 1}, "marker");
		scene.Goals = {{"exit", "", 1.35f}};
		scene.Collection.emplace().Pickups = {"pickup-left", "pickup-center", "pickup-right"};
		return scene;
	}
}
