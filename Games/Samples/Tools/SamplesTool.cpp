// Generates the tool-made Samples scenes: Puzzle, Collection and the benchmark grids.
//   SamplesTool create-puzzle <output.ravenscene>
//   SamplesTool create-collection <output.ravenscene>
//   SamplesTool create-benchmark <output.ravenscene> <mesh-count>
//   SamplesTool create-sprite-benchmark <output.ravenscene> <sprite-count>
//   SamplesTool create-physics <output.ravenscene>

#include "SampleSceneBuilder.hpp"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <exception>
#include <iostream>
#include <numbers>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace
{
	using namespace Raven;
	using Samples::SampleSceneBuilder;

	const AssetId Cube = AssetId::Parse("6938c6b9-72ca-4bc1-b0bf-9dc31e158087");
	const AssetId Sphere = AssetId::Parse("79607ed4-7591-4672-b47b-7107b21ac4ac");
	const AssetRef<AudioAsset> Win{ AssetId::Parse("ef451c5a-02db-4d81-89a1-ec2ea98ed719") };

	ECS::TransformComponent Pose(Vec3 position, Vec3 scale = { 1, 1, 1 }, Vec3 rotation = {})
	{
		ECS::TransformComponent transform;
		transform.Position = position;
		transform.Rotation = rotation;
		transform.Scale = scale;
		return transform;
	}

	ECS::MaterialComponent Material(bool lit, AssetId texture = {})
	{
		ECS::MaterialComponent material;
		material.Lit = lit;
		material.BaseColorTexture.Id = texture;
		return material;
	}

	// Which objects rotate, and where they sit in the scene file (docs/RenderingContentDesign.md,
	// measurement 3): every fifth at random file positions (the default), the same fifth first in the
	// file, or every object.
	enum class Animation
	{
		Scattered,
		Contiguous,
		All,
	};

	SceneFile CreateBenchmark(std::uint32_t meshCount, Animation animation = Animation::Scattered)
	{
		std::uint32_t columns;
		switch (meshCount)
		{
		case 100: columns = 10; break;
		case 500: columns = 25; break;
		case 2000: columns = 50; break;
		case 4000: columns = 80; break;
		// Stress scenes (docs/GpuDrivenDesign.md, G6), generated locally and never tracked in Content.
		case 10000: columns = 125; break;
		case 50000: columns = 250; break;
		case 100000: columns = 400; break;
		default: throw std::invalid_argument("Benchmark mesh count must be 100, 500, 2000, 4000, 10000, 50000 or 100000");
		}
		const auto rows = meshCount / columns;
		constexpr float spacing = 1.55f;
		const auto centered = [](std::uint32_t index, std::uint32_t count)
		{
			return (static_cast<float>(index) - .5f * static_cast<float>(count - 1)) * spacing;
		};
		SampleSceneBuilder scene;
		if (animation == Animation::Contiguous)
			scene.UseSequentialIds();
		ECS::TransformComponent spawn = Pose({ 0, -79.0f, 0 });
		ECS::CameraComponent lens;
		if (meshCount > 4000)
		{
			// Keep the whole grid in view: the 4,000-mesh camera frames 80 x 50 meshes at 79 units.
			const float scale = std::max(static_cast<float>(columns) / 80.0f, static_cast<float>(rows) / 50.0f);
			spawn.Position.Y = -79.0f * scale;
			lens.FarPlane = 100.0f * scale;
		}
		const auto paint = Material(true);
		const auto textured = Material(true, AssetId::Parse("7a590cee-ec82-4a27-927f-860e64619c19"));
		for (std::uint32_t row = 0; row < rows; ++row)
			scene.Group("row-" + std::to_string(row), Pose({ 0, 0, centered(row, rows) }));
		const auto animated = [animation](std::uint32_t index)
		{
			return animation == Animation::All || index % 5 == 0;
		};
		// Contiguous creates the animated objects first; sequential IDs keep that order in the file.
		std::vector<std::uint32_t> order(rows * columns);
		for (std::uint32_t index = 0; index < order.size(); ++index)
			order[index] = index;
		if (animation == Animation::Contiguous)
			std::stable_partition(order.begin(), order.end(), animated);
		for (const auto index : order)
		{
			const auto row = index / columns, column = index % columns;
			const auto group = "row-" + std::to_string(row);
			const auto suffix = std::to_string(index);
			const bool animate = animated(index);
			const auto pivot = (animate ? "benchmark-rotor-" : "pivot-") + suffix;
			scene.Group(pivot, Pose({ centered(column, columns), 0, 0 }));
			scene.Parent(pivot, group);
			if (animate)
				scene.Rotator(pivot, {}, { 0, .65f, 0 }, 1);
			const auto mesh = "mesh-" + suffix;
			scene.Mesh(mesh, index % 2 == 0 ? Cube : Sphere, Pose({ .12f, 0, 0 }, { .68f, .68f, .68f }),
				index % 2 == 0 ? Vec4{ .9f, .55f, .18f, 1 } : Vec4{ .22f, .67f, .9f, 1 },
				index % 4 == 0 ? textured : paint);
			scene.Parent(mesh, pivot);
		}
		scene.Camera("main", spawn, lens, {});
		scene.Lighting("sun", {});
		return scene.Finish();
	}

	// The sprite budget scene (docs/RenderingContentDesign.md, B1 and the sprite budget): a 100 x 100
	// wall of quads facing the camera, alternating opaque and blended in a checkerboard so blended
	// sprites overlap opaque ones and each other's rows, with both sample textures. Zero sprites gives
	// the control scene: the same camera and light without the wall.
	SceneFile CreateSpriteBenchmark(std::uint32_t spriteCount)
	{
		if (spriteCount != 0 && spriteCount != 10000)
			throw std::invalid_argument("Sprite benchmark count must be 0 or 10000");
		constexpr std::uint32_t columns = 100;
		constexpr float spacing = .5f;
		const AssetRef<TextureAsset> grid{ AssetId::Parse("7a590cee-ec82-4a27-927f-860e64619c19") };
		const AssetRef<TextureAsset> alternate{ AssetId::Parse("a1552d51-3e07-483c-aabd-366e71785a34") };
		SampleSceneBuilder scene;
		for (std::uint32_t index = 0; index < spriteCount; ++index)
		{
			const auto row = index / columns, column = index % columns;
			const bool blended = (row + column) % 2 == 1;
			ECS::SpriteComponent sprite;
			sprite.Texture = row % 2 == 0 ? grid : alternate;
			sprite.Tint = blended ? Vec4{ .65f, .8f, 1.0f, .55f } : Vec4{ 1, 1, 1, 1 };
			sprite.BlendMode = blended ? ECS::SpriteBlendMode::Alpha : ECS::SpriteBlendMode::Opaque;
			sprite.SortOrder = static_cast<std::int32_t>(column);
			// Blended quads sit slightly in front and overlap their neighbours.
			const float depth = blended ? -.05f : 0.0f;
			scene.Sprite("sprite-" + std::to_string(index),
				Pose({ (static_cast<float>(column) - 49.5f) * spacing, depth, (static_cast<float>(row) - 49.5f) * spacing },
					blended ? Vec3{ .7f, 1, .7f } : Vec3{ .45f, 1, .45f }),
				sprite);
		}
		ECS::CameraComponent lens;
		lens.FarPlane = 200.0f;
		scene.Camera("main", Pose({ 0, -60.0f, 0 }), lens, {});
		scene.Lighting("sun", {});
		return scene.Finish();
	}

	SceneFile CreatePuzzle()
	{
		SampleSceneBuilder scene;
		const auto paint = Material(true);
		const auto signal = Material(false);
		const auto mesh = [&](const std::string& id, Vec3 position, Vec3 scale, Vec4 color, bool sphere = false)
		{
			scene.Mesh(id, sphere ? Sphere : Cube, Pose(position, scale), color,
				id.starts_with("switch") || id == "goal" ? signal : paint);
		};
		scene.Group("rig", Pose({ 0, 10, 0 }));
		scene.Group("assembly-a", Pose({ -2, 9, 0 }));
		scene.Group("assembly-b", Pose({ 2, 9, 0 }, { -1, .8f, 1.2f }));
		for (const std::string tail : { "a", "b" })
		{
			const auto base = "base-" + tail, arm = "arm-" + tail, mount = "mount-" + tail, tip = "tip-" + tail;
			mesh(base, { 0, 0, .7f }, { .3f, .3f, 1.4f }, { .6f, .6f, .8f, 1 });
			mesh(arm, { 0, 0, 1.5f }, { 1.8f, .2f, .2f }, { .9f, .45f, .15f, 1 });
			scene.Group(mount, Pose({ .6f, 0, 0 }));
			mesh(tip, {}, { .25f, 1.5f, 1.5f }, { .2f, .8f, 1, 1 }, true);
			scene.Parent(base, "assembly-" + tail);
			scene.Parent(arm, "assembly-" + tail);
			scene.Parent(mount, arm);
			scene.Parent(tip, mount);
		}
		mesh("switch-a", { -2, 6, .9f }, { .65f, .35f, .65f }, { 1, .4f, .1f, 1 });
		mesh("switch-b", { 2, 6, .9f }, { .65f, .35f, .65f }, { 1, .4f, .1f, 1 });
		mesh("floor", { 0, 10, -.15f }, { 8, 16, .15f }, { .65f, .7f, .8f, 1 });
		mesh("wall-left", { -2.5f, 11, 1.5f }, { 3, .3f, 3 }, { .5f, .55f, .65f, 1 });
		mesh("wall-right", { 2.5f, 11, 1.5f }, { 3, .3f, 3 }, { .5f, .55f, .65f, 1 });
		mesh("gate", { 0, 11, 1.5f }, { 2, .3f, 3 }, { .7f, .25f, .8f, 1 });
		mesh("goal", { 0, 14, .6f }, { 1, 1, 1 }, { .1f, .85f, 1, 1 }, true);
		scene.Camera("main", Pose({ 0, -7.8f, 2.2f }, { 1, 1, 1 }, { -.12f, 0, 0 }), {}, { 0, .5f, .5f });
		scene.Parent("main", "rig");
		scene.Lighting("sun", {});
		scene.WinSound(Win);
		scene.ControlledMesh("arm-a");
		scene.Beacon("goal", 1.25f);
		scene.Switch("switch-a");
		scene.Switch("switch-b");
		scene.Rotator("arm-a", "switch-a", { 0, 0, std::numbers::pi_v<float> / 2 }, 1.2f);
		scene.Rotator("arm-b", "switch-b", { 0, 0, -std::numbers::pi_v<float> / 2 }, 1.2f);
		scene.Door("gate", { "arm-a", "arm-b" }, { 0, 0, 3.6f }, 1.0f);
		scene.Goal("goal", "gate", 1.25f);
		return scene.Finish();
	}

	// Physics bodies on the sample meshes (docs/GameServicesDesign.md, FD-32): a crate pyramid, a ramp
	// and bouncing spheres on a static floor. The cube mesh is 1 m per side and the sphere 1 m across,
	// so the default collider sizes match them at any scale.
	SceneFile CreatePhysics()
	{
		SampleSceneBuilder scene;
		const auto paint = Material(true);
		const auto grid = Material(true, AssetId::Parse("7a590cee-ec82-4a27-927f-860e64619c19"));
		ECS::ColliderComponent box;
		ECS::ColliderComponent ball;
		ball.Shape = ECS::ColliderShape::Sphere;
		ECS::RigidBodyComponent crate;
		crate.Mass = 8.0f;
		crate.Friction = 0.6f;
		ECS::RigidBodyComponent bouncy;
		bouncy.Mass = 2.0f;
		bouncy.Restitution = 0.55f;
		bouncy.Friction = 0.3f;

		scene.Mesh("floor", Cube, Pose({ 0, 10, -.25f }, { 16, 16, .5f }), { .55f, .6f, .68f, 1 }, grid);
		scene.Physics("floor", box);
		// Low walls keep rolling spheres on the floor.
		const std::pair<const char*, ECS::TransformComponent> walls[]{
			{ "wall-near", Pose({ 0, 2.15f, .3f }, { 16, .3f, .6f }) },
			{ "wall-far", Pose({ 0, 17.85f, .3f }, { 16, .3f, .6f }) },
			{ "wall-left", Pose({ -7.85f, 10, .3f }, { .3f, 15.4f, .6f }) },
			{ "wall-right", Pose({ 7.85f, 10, .3f }, { .3f, 15.4f, .6f }) } };
		for (const auto& [name, pose] : walls)
		{
			scene.Mesh(name, Cube, pose, { .4f, .45f, .55f, 1 }, paint);
			scene.Physics(name, box);
		}
		scene.Mesh("ramp", Cube,Pose({ 4.5f, 12, 1.2f }, { 5, 3, .3f }, { 0, -.45f, 0 }), { .45f, .5f, .6f, 1 }, paint);
		scene.Physics("ramp", box);
		constexpr float size = .8f;
		for (int row = 0; row < 5; ++row)
			for (int column = 0; column < 5 - row; ++column)
			{
				const auto name = "crate-" + std::to_string(row) + "-" + std::to_string(column);
				const float x = -3.0f + (static_cast<float>(column) - .5f * static_cast<float>(4 - row)) * (size + .02f);
				scene.Mesh(name, Cube, Pose({ x, 10, size * (static_cast<float>(row) + .5f) + .01f }, { size, size, size }),
					(row + column) % 2 ? Vec4{ .9f, .55f, .18f, 1 } : Vec4{ .8f, .4f, .12f, 1 }, paint);
				scene.Physics(name, box, crate);
			}
		for (int index = 0; index < 6; ++index)
		{
			const auto name = "ball-" + std::to_string(index);
			const float x = 3.0f + static_cast<float>(index % 3) * 1.1f;
			const float y = 11.0f + static_cast<float>(index / 3) * 1.2f;
			scene.Mesh(name, Sphere, Pose({ x, y, 5.0f + static_cast<float>(index) * .9f }, { .7f, .7f, .7f }),
				{ .22f, .67f, .9f, 1 }, paint);
			scene.Physics(name, ball, bouncy);
		}
		// Movers (FD-47): capsules walking circles, over the ramp's foot and through the crates.
		ECS::MoverComponent walker;
		walker.Radius = .35f;
		walker.HalfHeight = .45f;
		const std::tuple<const char*, Vec3, float, float> walkers[]{
			{ "walker-0", { 0, 6, 0 }, 3.0f, 2.5f },
			{ "walker-1", { 3, 12, 0 }, 2.5f, -2.0f },
			{ "walker-2", { -4, 12, 0 }, 2.0f, 3.0f } };
		for (const auto& [name, center, radius, speed] : walkers)
		{
			scene.Mesh(name, Sphere, Pose({ center.X + radius, center.Y, 1.0f }, { .7f, .7f, 1.6f }), { .3f, .85f, .4f, 1 },
				paint);
			Samples::PatrolComponent patrol;
			patrol.Center = center;
			patrol.Radius = radius;
			patrol.Speed = speed;
			scene.Patrol(name, walker, patrol);
		}
		scene.Camera("main", Pose({ 0, -4, 5 }, { 1, 1, 1 }, { -.42f, 0, 0 }), {}, { 0, 10, 1 });
		scene.Lighting("sun", {});
		return scene.Finish();
	}

	SceneFile CreateCollection()
	{
		SampleSceneBuilder scene;
		const auto structure = Material(true);
		const auto marker = Material(false);
		scene.Mesh("floor", Cube, Pose({ 0, 12, -.2f }, { 8, 16, .2f }), { .27f, .31f, .38f, 1 }, structure);
		scene.Mesh("exit-post-left", Cube, Pose({ -1.45f, 17, 1.55f }, { .24f, .24f, 3.1f }), { .2f, .55f, .7f, 1 }, structure);
		scene.Mesh("exit-post-right", Cube, Pose({ 1.45f, 17, 1.55f }, { .24f, .24f, 3.1f }), { .2f, .55f, .7f, 1 }, structure);
		scene.Mesh("exit-header", Cube, Pose({ 0, 17, 3.05f }, { 3.15f, .24f, .24f }), { .2f, .55f, .7f, 1 }, structure);
		constexpr Vec4 gold{ 1, .67f, .12f, 1 };
		constexpr Vec3 pickupSize{ .8f, .8f, .8f };
		scene.Mesh("pickup-left", Sphere, Pose({ -1.6f, 12, 1.6f }, pickupSize), gold, marker);
		scene.Mesh("pickup-center", Sphere, Pose({ 0, 12, .85f }, pickupSize), gold, marker);
		scene.Mesh("pickup-right", Sphere, Pose({ 1.6f, 12, 1.6f }, pickupSize), gold, marker);
		scene.Mesh("exit", Sphere, Pose({ 0, 17, 1.6f }), { .1f, .85f, 1, 1 }, marker);
		scene.Camera("main", Pose({ 0, 6, 1.6f }), {}, { 0, 12, 1 });
		scene.Lighting("sun", {});
		scene.WinSound(Win);
		scene.Goal("exit", {}, 1.35f);
		for (const auto* pickup : { "pickup-left", "pickup-center", "pickup-right" })
			scene.Pickup(pickup);
		return scene.Finish();
	}
}

namespace
{
	// The IDs generated scenes carry in their header (docs/RenderingContentDesign.md, B0 contract 9).
	// Stress and sprite scenes are generated into benchmark snapshots only; their IDs match
	// scripts/Benchmark-Windows3D.py.
	AssetId BenchmarkId(std::uint32_t count)
	{
		switch (count)
		{
		case 100: return AssetId::Parse("5f708ee7-8927-4b46-9434-20a5392eb2c4");
		case 500: return AssetId::Parse("675da478-1d28-4440-8d75-4bccd9942c31");
		case 2000: return AssetId::Parse("84d2e14b-30ba-4158-b3e4-dcfad32b35e0");
		case 4000: return AssetId::Parse("b61438cc-321d-41da-b9ec-1d17b34d7357");
		case 10000: return AssetId::Parse("3c5e1f0a-7d2b-4e61-9a84-1b0c6f2d9e31");
		case 50000: return AssetId::Parse("8a41d7c2-5b9e-4f03-b6d1-2e7f90c4a358");
		default: return AssetId::Parse("d29f6b14-0c83-4a7e-8f52-6b1e3a9d07c4");
		}
	}

	void SaveScene(SceneFile file, AssetId id, const char* path)
	{
		file.Id = id;
		file.Save(path);
	}
}

int main(int argc, char** argv)
{
	try
	{
		const std::string_view command = argc > 1 ? argv[1] : "";
		if (command == "create-puzzle" && argc == 3)
			SaveScene(CreatePuzzle(), AssetId::Parse("7e6879b6-0ceb-4327-95a6-354cc4cc0863"), argv[2]);
		else if (command == "create-collection" && argc == 3)
			SaveScene(CreateCollection(), AssetId::Parse("72c6c544-fcf5-44fd-8e87-09e507df67d8"), argv[2]);
		else if (command == "create-physics" && argc == 3)
			SaveScene(CreatePhysics(), AssetId::Parse("e28e2f44-752b-4474-9ce4-0677d5177af2"), argv[2]);
		else if (command == "create-sprite-benchmark" && argc == 4)
		{
			std::uint32_t count = 0;
			const std::string_view value(argv[3]);
			if (std::from_chars(value.data(), value.data() + value.size(), count).ec != std::errc{})
				throw std::invalid_argument("Sprite count must be a number");
			SaveScene(CreateSpriteBenchmark(count), AssetId::Parse(count == 0 ? "c6f2a8d1-4e97-4b3c-8d05-7a1e9f3b6c42"
				: "5b0e9c3a-2f61-4d8e-a7c4-93e1d6f0b215"), argv[2]);
		}
		else if (command == "create-benchmark" && (argc == 4 || argc == 5))
		{
			const std::string_view placement = argc == 5 ? argv[4] : "scattered";
			Animation animation = Animation::Scattered;
			if (placement == "contiguous")
				animation = Animation::Contiguous;
			else if (placement == "all")
				animation = Animation::All;
			else if (placement != "scattered")
				throw std::invalid_argument("Benchmark animation must be scattered, contiguous or all");
			std::uint32_t count = 0;
			const std::string_view value(argv[3]);
			const auto result = std::from_chars(value.data(), value.data() + value.size(), count);
			if (result.ec != std::errc{} || result.ptr != value.data() + value.size())
				throw std::invalid_argument("Expected a benchmark mesh count");
			SaveScene(CreateBenchmark(count, animation), BenchmarkId(count), argv[2]);
		}
		else
			throw std::invalid_argument("Usage: SamplesTool create-puzzle|create-collection|create-physics <output> | "
				"create-benchmark <output> <mesh-count> [scattered|contiguous|all] | create-sprite-benchmark <output> <sprite-count>");
		std::cout << "Saved scene " << argv[2] << '\n';
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
