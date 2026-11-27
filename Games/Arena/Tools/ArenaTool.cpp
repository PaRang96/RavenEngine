// Generates the arena's content (docs/GameServicesDesign.md, FD-46 and C9):
//   ArenaTool create-arena <output.ravenscene>
//   ArenaTool create-sounds <content root>   synthesized clips (Audio/*.wav, each with its .ravenasset
//                                            sidecar) and their sound events (Sounds/*.ravensound)

#include "ArenaComponents.hpp"

#include "Raven/Components/CameraComponent.hpp"
#include "Raven/Components/LightingComponent.hpp"
#include "Raven/Components/MaterialComponent.hpp"
#include "Raven/Components/PhysicsComponents.hpp"
#include "Raven/Components/StaticMeshComponent.hpp"
#include "Raven/Components/TransformComponent.hpp"
#include "Raven/ECS/Registry.hpp"
#include "Raven/Scene/Scene.hpp"
#include "Raven/Scene/SceneFile.hpp"

#include <cmath>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <numbers>
#include <random>
#include <vector>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace
{
	using namespace Raven;
	using namespace Arena;

	const AssetId Cube = AssetId::Parse("b56e70df-6cdb-4c9f-bf7b-a4fd5c49381c");
	const AssetId Sphere = AssetId::Parse("97acbc47-2656-47e0-b136-353ee5a0d6de");

	// The arena scene, its sound-event assets and the clips they play. Raven formats carry their ID in
	// their header and clips in a sidecar (docs/RenderingContentDesign.md, B0 contract 9).
	const AssetId ArenaScene = AssetId::Parse("ef01d1ef-83d0-4c29-8358-cee13d7fdd56");
	const AssetId PickupSound = AssetId::Parse("e384a51b-7acc-4ed1-9e84-9365d223015a");
	const AssetId JumpSound = AssetId::Parse("774ab08d-5383-4eb1-ae29-b9ff690d680b");
	const AssetId CaughtSound = AssetId::Parse("9d31aeef-5e74-4d1d-8642-502e3224a5cd");
	const AssetId DroneSound = AssetId::Parse("f45fcf40-fda2-4fad-8b7f-299d6d76694d");
	const AssetId ClickSound = AssetId::Parse("b5fa513f-1762-44de-8fc6-2f1ca0494165");
	const AssetId PauseSound = AssetId::Parse("7204a05c-0310-40fe-8a27-a2947dc563ff");

	constexpr std::uint32_t Rate = 48000;
	using Synth = std::function<float(float time, std::mt19937& noise)>;

	void WriteText(const std::filesystem::path& path, const std::string& text)
	{
		std::filesystem::create_directories(path.parent_path());
		std::ofstream file(path, std::ios::binary);
		file << text;
		if (!file)
			throw std::runtime_error("Cannot write " + path.string());
	}

	// 16-bit mono PCM at 48 kHz, peak-normalized to `peak`, with its asset sidecar.
	void WriteWav(const std::filesystem::path& path, AssetId id, float seconds, const Synth& synth, float peak = 0.6f)
	{
		auto sidecar = path;
		sidecar += ".ravenasset";
		WriteText(sidecar, "raven-asset 1\nid " + id.ToString() + "\nkind audio\n");
		std::mt19937 noise(1234);
		std::vector<float> samples(static_cast<std::size_t>(seconds * Rate));
		float loudest = 1.0e-6f;
		for (std::size_t index = 0; index < samples.size(); ++index)
		{
			samples[index] = synth(static_cast<float>(index) / Rate, noise);
			loudest = std::max(loudest, std::abs(samples[index]));
		}
		std::filesystem::create_directories(path.parent_path());
		std::ofstream file(path, std::ios::binary);
		const auto u32 = [&file](std::uint32_t value) { file.write(reinterpret_cast<const char*>(&value), 4); };
		const auto u16 = [&file](std::uint16_t value) { file.write(reinterpret_cast<const char*>(&value), 2); };
		const auto bytes = static_cast<std::uint32_t>(samples.size() * 2);
		file.write("RIFF", 4); u32(36 + bytes); file.write("WAVEfmt ", 8);
		u32(16); u16(1); u16(1); u32(Rate); u32(Rate * 2); u16(2); u16(16);
		file.write("data", 4); u32(bytes);
		for (const float sample : samples)
			u16(static_cast<std::uint16_t>(static_cast<std::int16_t>(std::lround(sample / loudest * peak * 32767.0f))));
		if (!file)
			throw std::runtime_error("Cannot write " + path.string());
	}

	float Tone(float time, float frequency) { return std::sin(2.0f * std::numbers::pi_v<float> * frequency * time); }
	float Decay(float time, float rate) { return std::exp(-rate * time); }
	// A short attack, so no clip starts with a click.
	float Attack(float time, float seconds = 0.004f) { return std::min(1.0f, time / seconds); }

	void WriteSound(const std::filesystem::path& path, AssetId id, const std::string& body)
	{
		WriteText(path, "raven-sound 2\nid " + id.ToString() + "\n" + body);
	}

	void CreateSounds(const std::filesystem::path& root)
	{
		const auto audio = root / "Audio";
		// Energy cell pickup: a bright two-note chime, in two voicings.
		WriteWav(audio / "Pickup1.wav", AssetId::Parse("8373162a-1604-4d1f-a48b-f198879e3938"), 0.32f, [](float t, std::mt19937&)
		{
			const float note = t < 0.07f ? Tone(t, 988.0f) : Tone(t, 1319.0f) + 0.3f * Tone(t, 2638.0f);
			return note * Attack(t) * Decay(t, 9.0f);
		});
		WriteWav(audio / "Pickup2.wav", AssetId::Parse("d72522b8-e70b-4f05-8a63-5f44d6efb86e"), 0.32f, [](float t, std::mt19937&)
		{
			const float note = t < 0.07f ? Tone(t, 880.0f) : Tone(t, 1175.0f) + 0.3f * Tone(t, 2349.0f);
			return note * Attack(t) * Decay(t, 9.0f);
		});
		// Jump: a rising sweep with a little air.
		WriteWav(audio / "Jump.wav", AssetId::Parse("56a0085a-5af7-4ed6-86df-f3c9695788c1"), 0.2f, [](float t, std::mt19937& noise)
		{
			const float phase = 2.0f * std::numbers::pi_v<float> * (220.0f * t + 1100.0f * t * t);
			std::uniform_real_distribution<float> air(-1.0f, 1.0f);
			return (std::sin(phase) + 0.15f * air(noise)) * Attack(t) * Decay(t, 14.0f);
		});
		// Caught by a drone: a falling buzz over a short noise hit.
		WriteWav(audio / "Caught.wav", AssetId::Parse("5302e01f-80d3-4e9f-8864-b124976f378a"), 0.7f, [](float t, std::mt19937& noise)
		{
			const float phase = 2.0f * std::numbers::pi_v<float> * (440.0f * t - 160.0f * t * t);
			std::uniform_real_distribution<float> hit(-1.0f, 1.0f);
			const float buzz = std::sin(phase) + 0.35f * std::sin(3.0f * phase);
			return (buzz * Decay(t, 3.5f) + 0.6f * hit(noise) * Decay(t, 30.0f)) * Attack(t);
		}, 0.7f);
		// A new drone: two quick blips.
		WriteWav(audio / "Drone.wav", AssetId::Parse("8e34abd2-a7fe-461b-9a97-dff014b389c6"), 0.14f, [](float t, std::mt19937&)
		{
			const float local = t < 0.06f ? t : t - 0.07f;
			const float blip = t < 0.06f ? Tone(t, 660.0f) : t >= 0.07f ? Tone(t, 990.0f) : 0.0f;
			return blip * Attack(local, 0.003f) * Decay(local, 40.0f);
		}, 0.45f);
		// Menu click and the pause chord.
		WriteWav(audio / "Click.wav", AssetId::Parse("3d9a6184-8e70-4416-b966-d542a6434a3f"), 0.05f, [](float t, std::mt19937&) { return Tone(t, 2000.0f) * Attack(t, 0.001f) * Decay(t, 120.0f); }, 0.4f);
		WriteWav(audio / "Pause.wav", AssetId::Parse("1f24282d-6b14-4441-be88-72c561dd1d21"), 0.3f, [](float t, std::mt19937&)
		{
			return (Tone(t, 523.25f) + 0.7f * Tone(t, 783.99f)) * Attack(t, 0.01f) * Decay(t, 8.0f);
		}, 0.45f);

		const auto sounds = root / "Sounds";
		WriteSound(sounds / "Pickup.ravensound", PickupSound,
			"clip 8373162a-1604-4d1f-a48b-f198879e3938\nclip d72522b8-e70b-4f05-8a63-5f44d6efb86e\n"
			"volume 0.8 1.0\npitch 0.97 1.03\nbus Effects\nlimit Pickup 3 stop-oldest\nspatial 3 45\n");
		WriteSound(sounds / "Jump.ravensound", JumpSound,
			"clip 56a0085a-5af7-4ed6-86df-f3c9695788c1\nvolume 0.6 0.7\npitch 0.94 1.06\nbus Effects\n"
			"limit Jump 2 stop-oldest\nspatial 3 40\n");
		WriteSound(sounds / "Caught.ravensound", CaughtSound,
			"clip 5302e01f-80d3-4e9f-8864-b124976f378a\nvolume 0.9 0.9\nbus Effects\npriority 4\n");
		WriteSound(sounds / "Drone.ravensound", DroneSound,
			"clip 8e34abd2-a7fe-461b-9a97-dff014b389c6\nvolume 0.5 0.6\npitch 0.9 1.1\nbus Effects\npriority 0.5\n"
			"limit Drone 4 stop-oldest\nspatial 3 40\n");
		WriteSound(sounds / "Click.ravensound", ClickSound,
			"clip 3d9a6184-8e70-4416-b966-d542a6434a3f\nvolume 0.5 0.5\nbus Interface\nlimit Ui 2 stop-oldest\n");
		WriteSound(sounds / "Pause.ravensound", PauseSound,
			"clip 1f24282d-6b14-4441-be88-72c561dd1d21\nvolume 0.6 0.6\nbus Interface\nlimit Ui 2 stop-oldest\n");
	}

	ECS::TransformComponent Pose(Vec3 position, Vec3 scale = { 1, 1, 1 }, Vec3 rotation = {})
	{
		ECS::TransformComponent transform;
		transform.Position = position;
		transform.Rotation = rotation;
		transform.Scale = scale;
		return transform;
	}

	class ArenaBuilder
	{
	public:
		ArenaBuilder() : m_Scene(m_Registry)
		{
			m_Registry.Register<CraftComponent>();
			m_Registry.Register<DroneComponent>();
			m_Registry.Register<CellComponent>();
			m_Registry.Register<PlatformComponent>();
			m_Registry.Register<ArenaRulesComponent>();
			m_Registry.Register<ArenaRunComponent>();
			m_Registry.Register<FollowCameraComponent>();
		}

		// A lit mesh, with a static box collider unless `collider` is empty, and a rigid body if given.
		ECS::Entity Mesh(const std::string& name, AssetId mesh, const ECS::TransformComponent& pose, Vec4 tint,
			std::optional<ECS::ColliderComponent> collider = ECS::ColliderComponent{},
			std::optional<ECS::RigidBodyComponent> body = {})
		{
			const auto entity = m_Scene.CreateEntity(name);
			m_Registry.Add(entity, pose);
			ECS::StaticMeshComponent model;
			model.Mesh.Id = mesh;
			model.Tint = tint;
			m_Registry.Add(entity, model);
			ECS::MaterialComponent material;
			material.Lit = true;
			m_Registry.Add(entity, material);
			if (collider)
				m_Registry.Add(entity, *collider);
			if (body)
				m_Registry.Add(entity, *body);
			return entity;
		}

		template<typename T>
		void Add(ECS::Entity entity, const T& value)
		{
			m_Registry.Add(entity, value);
		}

		ECS::Entity Create(const std::string& name) { return m_Scene.CreateEntity(name); }
		SceneFile Finish() const { return CaptureScene(m_Scene); }

	private:
		ECS::Registry m_Registry;
		Scene m_Scene;
	};

	SceneFile CreateArena()
	{
		ArenaBuilder arena;
		constexpr float half = 14.0f;
		constexpr Vec4 wallTint{ 0.36f, 0.4f, 0.5f, 1 };
		// One collider under a two-tone grid of floor tiles, so motion reads on screen.
		const auto floor = arena.Create("floor");
		arena.Add(floor, Pose({ 0, 0, -0.25f }, { 2 * half + 1, 2 * half + 1, 0.5f }));
		arena.Add(floor, ECS::ColliderComponent{});
		constexpr int tiles = 6;
		constexpr float tile = (2 * half + 1) / tiles;
		for (int x = 0; x < tiles; ++x)
			for (int y = 0; y < tiles; ++y)
				arena.Mesh("tile-" + std::to_string(x) + "-" + std::to_string(y), Cube,
					Pose({ -half - 0.5f + tile * (x + 0.5f), -half - 0.5f + tile * (y + 0.5f), -0.25f }, { tile, tile, 0.5f }),
					(x + y) % 2 ? Vec4{ 0.46f, 0.52f, 0.6f, 1 } : Vec4{ 0.53f, 0.59f, 0.67f, 1 }, std::nullopt);
		const std::pair<const char*, ECS::TransformComponent> walls[]{
			{ "wall-south", Pose({ 0, -half - 0.75f, 0.6f }, { 2 * half + 2.5f, 0.5f, 1.2f }) },
			{ "wall-north", Pose({ 0, half + 0.75f, 0.6f }, { 2 * half + 2.5f, 0.5f, 1.2f }) },
			{ "wall-west", Pose({ -half - 0.75f, 0, 0.6f }, { 0.5f, 2 * half + 1, 1.2f }) },
			{ "wall-east", Pose({ half + 0.75f, 0, 0.6f }, { 0.5f, 2 * half + 1, 1.2f }) } };
		for (const auto& [name, pose] : walls)
			arena.Mesh(name, Cube, pose, wallTint);
		const Vec3 pillars[]{ { -7, -6, 0 }, { 7, -6, 0 }, { -7, 6, 0 }, { 7, 6, 0 }, { 0, 9, 0 }, { -11, 0, 0 } };
		for (int index = 0; index < 6; ++index)
			arena.Mesh("pillar-" + std::to_string(index), Cube, Pose({ pillars[index].X, pillars[index].Y, 1.25f },
				{ 1.6f, 1.6f, 2.5f }), { 0.42f, 0.46f, 0.58f, 1 });
		// A ramp up to a raised deck on the east side.
		arena.Mesh("deck", Cube, Pose({ 12.75f, 0, 0.55f }, { 2.5f, 6, 1.1f }), wallTint);
		arena.Mesh("ramp", Cube, Pose({ 9.59f, 0, 0.352f }, { 4, 2.5f, 0.3f }, { 0, -0.3f, 0 }), { 0.45f, 0.5f, 0.6f, 1 });
		// A kinematic platform sliding along the south wall: movers ride it.
		ECS::RigidBodyComponent kinematic;
		kinematic.Motion = ECS::MotionType::Kinematic;
		const auto platform = arena.Mesh("platform", Cube, Pose({ -9, -11, 0.15f }, { 3, 3, 0.3f }),
			{ 0.85f, 0.7f, 0.25f, 1 }, ECS::ColliderComponent{}, kinematic);
		PlatformComponent slide;
		slide.From = { -9, -11, 0.15f };
		slide.To = { -2, -11, 0.15f };
		slide.Period = 9.0f;
		arena.Add(platform, slide);
		// Crates the craft can push.
		ECS::RigidBodyComponent crate;
		crate.Mass = 6.0f;
		crate.Friction = 0.6f;
		const Vec3 crates[]{ { 4, -9, 0.5f }, { 5.05f, -9, 0.5f }, { 4.52f, -9, 1.51f }, { -4, 3, 0.5f }, { -4, 4.05f, 0.5f } };
		for (int index = 0; index < 5; ++index)
			arena.Mesh("crate-" + std::to_string(index), Cube, Pose(crates[index]), { 0.85f, 0.5f, 0.16f, 1 },
				ECS::ColliderComponent{}, crate);

		// The craft: a mover on its own layer, steered through the input stream.
		const Vec3 spawn{ 0, -3, 1.0f };
		const auto craft = arena.Mesh("craft", Sphere, Pose(spawn, { 0.9f, 0.9f, 1.6f }), { 1.0f, 0.8f, 0.2f, 1 },
			std::nullopt);
		ECS::MoverComponent mover;
		mover.Radius = 0.45f;
		mover.HalfHeight = 0.35f;
		mover.MaxSpeed = 8.0f;
		mover.Layer = CraftLayer;
		arena.Add(craft, mover);
		CraftComponent pilot;
		pilot.Spawn = spawn;
		arena.Add(craft, pilot);

		const auto camera = arena.Create("main");
		FollowCameraComponent rig;
		arena.Add(camera, Pose(spawn - Vec3{ 0, rig.Distance, -rig.Height }, { 1, 1, 1 }, { rig.Pitch, 0, 0 }));
		ECS::CameraComponent lens;
		lens.FarPlane = 120.0f;
		arena.Add(camera, lens);
		arena.Add(camera, ECS::ActiveCameraComponent{});
		arena.Add(camera, rig);

		ECS::LightingComponent sun;
		sun.Ambient = { 0.4f, 0.42f, 0.48f };
		sun.Direction = Normalize(Vec3{ -0.35f, 0.5f, -1.0f });
		sun.Color = { 0.75f, 0.72f, 0.65f };
		arena.Add(arena.Create("sun"), sun);

		const auto state = arena.Create("$arena");
		ArenaRulesComponent rules;
		rules.HalfSize = half;
		rules.CellMesh.Id = Sphere;
		rules.DroneMesh.Id = Sphere;
		rules.PickupSound.Id = PickupSound;
		rules.JumpSound.Id = JumpSound;
		rules.CaughtSound.Id = CaughtSound;
		rules.DroneSound.Id = DroneSound;
		rules.ClickSound.Id = ClickSound;
		rules.PauseSound.Id = PauseSound;
		arena.Add(state, rules);
		arena.Add(state, ArenaRunComponent{});
		return arena.Finish();
	}
}

int main(int argc, char** argv)
{
	try
	{
		const std::string_view command = argc > 1 ? argv[1] : "";
		if (command == "create-arena" && argc == 3)
		{
			auto scene = CreateArena();
			scene.Id = ArenaScene;
			scene.Save(argv[2]);
			std::cout << "Saved scene " << argv[2] << '\n';
		}
		else if (command == "create-sounds" && argc == 3)
		{
			CreateSounds(argv[2]);
			std::cout << "Saved clips and sound events under " << argv[2] << '\n';
		}
		else
			throw std::invalid_argument("Usage: ArenaTool create-arena <output.ravenscene> | create-sounds <content root>");
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
