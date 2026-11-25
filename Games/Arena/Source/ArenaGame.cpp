// The arena game (docs/GameServicesDesign.md, FD-46): steer a craft around the arena, collect
// energy cells and avoid the drones that roam it. A drone touching the craft ends the run; the best
// score is kept in the player's profile. Esc pauses. `--swarm` fills the arena with drones and
// turns off the game over, for the character-backend measurement (measurement 3).
//
// Layers (FD-47): controllers in Control turn the mapped actions into CraftCommand records; fixed
// steps read them, run the drones and the rules, and write the movers; the camera rig in Update
// follows the craft.

#include "ArenaComponents.hpp"
#include "ArenaUi.hpp"
#include "GameContract.hpp"

#include "Raven/Components/CameraComponent.hpp"
#include "Raven/Components/MaterialComponent.hpp"
#include "Raven/Components/PhysicsComponents.hpp"
#include "Raven/Components/StaticMeshComponent.hpp"
#include "Raven/Components/TransformComponent.hpp"
#include "Raven/Core/Api.hpp"
#include "Raven/Core/FrameTime.hpp"
#include "Raven/Core/GameSystems.hpp"
#include "Raven/Core/Log.hpp"
#include "Raven/Core/UserData.hpp"
#include "Raven/Math/SimulationMath.hpp"
#include "Raven/Platform/Input.hpp"
#include "Raven/Scene/SceneCommands.hpp"

#include <algorithm>
#include <cmath>
#include <exception>
#include <format>
#include <utility>
#include <vector>

namespace
{
	using namespace Raven;
	using namespace Arena;
	namespace Math = Raven::SimulationMath;

	constexpr std::string_view PlayContext = "Arena.Play";
	constexpr std::string_view PauseContext = "Arena.Pause";
	constexpr ActionId MoveX{ "MoveX" };
	constexpr ActionId MoveY{ "MoveY" };
	constexpr ActionId Jump{ "Jump" };
	constexpr ActionId Turn{ "Turn" };
	constexpr ActionId Pause{ "Pause" };
	constexpr ActionId Resume{ "Resume" };
	constexpr std::string_view ProfileFile = "Profile";

	float StepSeconds(const SystemContext& context)
	{
		return static_cast<float>(context.Time.DeltaSeconds);
	}

	// Frame phases: the first drawable frame after a pause carries a zero delta; long stalls are capped.
	float FrameSeconds(const SystemContext& context)
	{
		return static_cast<float>(std::clamp(context.Time.DeltaSeconds, 0.0, 0.1));
	}

	Vec3 Flat(Vec3 value)
	{
		return { value.X, value.Y, 0.0f };
	}

	float FlatLength(Vec3 value)
	{
		return Math::Sqrt(value.X * value.X + value.Y * value.Y);
	}

	// The camera's forward direction on the floor for a yaw (rotation about Z; zero looks along +Y).
	Vec3 Forward(float yaw)
	{
		return { -std::sin(yaw), std::cos(yaw), 0.0f };
	}

	template<typename T>
	ECS::Entity First(const SceneAccess& scene)
	{
		ECS::Entity found;
		scene.View<const T>().Each([&found](ECS::Entity entity, const T&)
		{
			found = entity;
			return false;
		});
		return found;
	}

	// Entities with T in persistent-ID order, so fixed steps that draw random numbers or spawn visit
	// them in the same order in every run (FD-48).
	template<typename T>
	std::vector<ECS::Entity> InIdOrder(const SceneAccess& scene)
	{
		std::vector<std::pair<std::uint64_t, ECS::Entity>> found;
		scene.View<const T>().Each([&](ECS::Entity entity, const T&) { found.emplace_back(scene.GetRef(entity).Id, entity); });
		std::sort(found.begin(), found.end(), [](const auto& left, const auto& right) { return left.first < right.first; });
		std::vector<ECS::Entity> entities;
		entities.reserve(found.size());
		for (const auto& [id, entity] : found)
			entities.push_back(entity);
		return entities;
	}

	// The profile store, or null in hosts without a user-data folder (tools and smoke tests).
	UserDataStore* Profiles(SystemContext& context)
	{
		try
		{
			return &context.Services.GetUserData();
		}
		catch (const std::exception&)
		{
			return nullptr;
		}
	}

	bool Swarm(const SystemContext& context)
	{
		return context.Services.HasOption("swarm");
	}

	Vec3 RandomPoint(SystemContext& context, const ArenaRulesComponent& rules, float margin, float z)
	{
		const float extent = std::max(rules.HalfSize - margin, 1.0f);
		const float x = context.Random.Range(-extent, extent);
		const float y = context.Random.Range(-extent, extent);
		return { x, y, z };
	}

	// A random floor point clear of the world and at least `distance` from `avoid`; the last try
	// when none is found.
	Vec3 FreePoint(SystemContext& context, const ArenaRulesComponent& rules, Vec3 avoid, float distance, float z,
		float radius)
	{
		Vec3 point{};
		for (int attempt = 0; attempt < 16; ++attempt)
		{
			point = RandomPoint(context, rules, 1.5f, z);
			if (FlatLength(point - avoid) < distance)
				continue;
			PhysicsShape probe;
			probe.Size = { radius, radius, radius };
			probe.Position = point;
			if (context.Physics.Overlap(probe, 1u << WorldLayer).empty())
				break;
		}
		return point;
	}

	ECS::MaterialComponent Paint(bool lit)
	{
		ECS::MaterialComponent material;
		material.Lit = lit;
		return material;
	}

	void SpawnDrone(SystemContext& context, const ArenaRulesComponent& rules, Vec3 craft)
	{
		auto& commands = context.Commands;
		const auto drone = commands.Create();
		ECS::TransformComponent transform;
		transform.Position = FreePoint(context, rules, craft, 8.0f, 0.65f, 0.7f);
		context.Services.PlaySound(rules.DroneSound, { transform.Position });
		transform.Scale = { 0.8f, 0.8f, 1.2f };
		commands.Add(drone, transform);
		ECS::StaticMeshComponent mesh;
		mesh.Mesh = rules.DroneMesh;
		mesh.Tint = { 0.92f, 0.22f, 0.2f, 1.0f };
		commands.Add(drone, mesh);
		commands.Add(drone, Paint(true));
		ECS::MoverComponent mover;
		mover.Radius = 0.4f;
		mover.HalfHeight = 0.2f;
		mover.MaxSpeed = 6.0f;
		mover.Layer = DroneLayer;
		commands.Add(drone, mover);
		DroneComponent ai;
		ai.Speed = context.Random.Range(3.4f, 4.8f);
		ai.Waypoint = transform.Position;
		commands.Add(drone, ai);
	}

	void SpawnCell(SystemContext& context, const ArenaRulesComponent& rules, Vec3 craft)
	{
		auto& commands = context.Commands;
		const auto cell = commands.Create();
		ECS::TransformComponent transform;
		transform.Position = FreePoint(context, rules, craft, 3.0f, 0.9f, 0.5f);
		transform.Scale = { 0.6f, 0.6f, 0.6f };
		commands.Add(cell, transform);
		ECS::StaticMeshComponent mesh;
		mesh.Mesh = rules.CellMesh;
		mesh.Tint = { 0.25f, 0.95f, 1.0f, 1.0f };
		commands.Add(cell, mesh);
		commands.Add(cell, Paint(false));
		ECS::ColliderComponent sensor;
		sensor.Shape = ECS::ColliderShape::Sphere;
		sensor.Sensor = true;
		sensor.Layer = CellLayer;
		commands.Add(cell, sensor);
		commands.Add(cell, CellComponent{});
	}

	// --- Control: once per frame, before the fixed steps -------------------------------------

	// The pause menu's view state, on the camera rig. The menu system adds it when first needed; it
	// exists from the next frame.
	ArenaMenuComponent* Menu(SystemContext& context, bool add = false)
	{
		const auto rig = First<FollowCameraComponent>(context.World);
		if (rig.IsNull())
			return nullptr;
		if (auto* menu = context.World.TryGet<ArenaMenuComponent>(rig))
			return menu;
		if (add)
			context.Commands.Add(context.World.GetRef(rig), ArenaMenuComponent{});
		return nullptr;
	}

	// A menu screen as it opens: nothing listening, and its first button about to take focus.
	ArenaMenuComponent Screen(MenuScreen screen)
	{
		ArenaMenuComponent menu;
		menu.Screen = screen;
		return menu;
	}

	void PlayUiSound(SystemContext& context, bool pause)
	{
		if (const auto* rules = context.World.Single<ArenaRulesComponent>())
			context.Services.PlaySound(pause ? rules->PauseSound : rules->ClickSound);
	}

	void SetPaused(SystemContext& context, bool paused)
	{
		PlayUiSound(context, true);
		context.Services.SetSimulationPaused(paused);
		context.Actions.SetContextActive(PauseContext, paused);
		if (auto* menu = Menu(context))
			*menu = ArenaMenuComponent{};
	}

	// Esc pauses: the pause context consumes every gameplay input, and the simulation stops. On the
	// key-binding screen, Esc goes back to the menu instead.
	void PauseSystem(SystemContext& context)
	{
		auto& actions = context.Actions;
		if (!context.Services.IsSimulationPaused() && actions.WasPressed(Pause))
		{
			SetPaused(context, true);
			Log(LogLevel::Info, "Arena paused; Esc resumes");
		}
		else if (context.Services.IsSimulationPaused() && actions.WasPressed(Resume))
		{
			auto* menu = Menu(context);
			if (menu && menu->Screen == MenuScreen::Bindings)
				*menu = Screen(MenuScreen::Main);
			else
				SetPaused(context, false);
		}
	}

	// The key-binding screen's rows: each action direction with its keys, from the live bindings.
	struct BindingTarget
	{
		std::string_view Action;
		float Sign = 1.0f;
		const char* Label = "";
	};
	constexpr BindingTarget BindingTargets[]{
		{ "MoveY", 1.0f, "Forward" }, { "MoveY", -1.0f, "Back" }, { "MoveX", -1.0f, "Left" },
		{ "MoveX", 1.0f, "Right" }, { "Jump", 1.0f, "Jump" }, { "Turn", 1.0f, "Turn left" },
		{ "Turn", -1.0f, "Turn right" }, { "Pause", 1.0f, "Pause" } };

	// The binding indices of one row, in order.
	std::vector<std::size_t> RowSlots(InputActions& actions, const BindingTarget& target)
	{
		std::vector<std::size_t> slots;
		const auto bindings = actions.GetBindings(PlayContext, target.Action);
		for (std::size_t index = 0; index < bindings.size(); ++index)
			if ((bindings[index].Scale < 0.0f) == (target.Sign < 0.0f))
				slots.push_back(index);
		return slots;
	}

	std::vector<BindingRow> BindingRows(InputActions& actions)
	{
		std::vector<BindingRow> rows;
		for (const auto& target : BindingTargets)
		{
			BindingRow row{ target.Label, {} };
			const auto bindings = actions.GetBindings(PlayContext, target.Action);
			for (const auto slot : RowSlots(actions, target))
				row.Keys.push_back(actions.GetDisplayName({ bindings[slot].Code, bindings[slot].Modifiers }));
			rows.push_back(std::move(row));
		}
		return rows;
	}

	// The pause menu and the key-binding screen, while the game is paused (Control phase).
	void MenuSystem(SystemContext& context)
	{
		if (!context.Services.IsSimulationPaused())
			return;
		auto* menu = Menu(context, true);
		if (!menu)
			return;
		auto& actions = context.Actions;
		const bool focus = std::exchange(menu->FocusPending, false);
		if (menu->Screen == MenuScreen::Main)
		{
			const auto result = DrawPauseMenu(context.Ui, focus);
			if (result.Choice == MenuChoice::Resume)
				SetPaused(context, false);
			else if (result.Choice == MenuChoice::OpenBindings)
			{
				PlayUiSound(context, false);
				*menu = Screen(MenuScreen::Bindings);
			}
			return;
		}

		// A key captured for the row being rebound replaces its binding, or adds one.
		if (menu->Listening >= 0 && !actions.IsListening())
		{
			if (const auto chord = actions.TakeListened())
			{
				const auto& target = BindingTargets[std::min<std::size_t>(static_cast<std::size_t>(menu->Listening),
					std::size(BindingTargets) - 1)];
				const auto slots = RowSlots(actions, target);
				const auto slot = menu->Slot < slots.size() ? slots[menu->Slot] :
					actions.GetBindings(PlayContext, target.Action).size();
				if (chord->Code != InputCode::FromKey(Key::Escape))
					actions.Rebind(PlayContext, target.Action, slot, { chord->Code, chord->Modifiers, target.Sign });
			}
			menu->Listening = -1;
		}
		const auto result = DrawBindings(context.Ui, BindingRows(actions), menu->Listening, focus);
		if (result.Choice != MenuChoice::None)
			PlayUiSound(context, false);
		if (result.Choice == MenuChoice::Back)
			*menu = Screen(MenuScreen::Main);
		else if (result.Choice == MenuChoice::ResetBindings)
			actions.ResetBindings(PlayContext);
		else if (result.Choice == MenuChoice::Rebind)
		{
			menu->Listening = static_cast<std::int32_t>(result.Row);
			menu->Slot = static_cast<std::uint32_t>(result.Slot);
			actions.BeginListening();
		}
	}

	// Turns the steering keys into a CraftCommand on the floor, relative to the camera.
	void CraftControlSystem(SystemContext& context)
	{
		if (context.Services.IsSimulationPaused())
			return;
		const auto* rig = context.World.TryGet<const FollowCameraComponent>(First<FollowCameraComponent>(context.World));
		const float yaw = rig ? rig->Yaw : 0.0f;
		const Vec3 forward = Forward(yaw);
		const Vec3 right{ forward.Y, -forward.X, 0.0f };
		Vec3 move = right * context.Actions.GetAxis(MoveX) + forward * context.Actions.GetAxis(MoveY);
		const float length = FlatLength(move);
		if (length > 1.0f)
			move = move * (1.0f / length);
		CraftCommand command;
		command.Move = move;
		command.Jump = context.Actions.WasPressed(Jump);
		context.Stream.Emit(command);
	}

	// --- Fixed steps -----------------------------------------------------------------------

	void PlatformSystem(SystemContext& context)
	{
		const float seconds = StepSeconds(context);
		context.World.View<PlatformComponent, ECS::TransformComponent>().Each(
			[seconds](ECS::Entity, PlatformComponent& platform, ECS::TransformComponent& transform)
			{
				const float period = std::max(platform.Period, 0.5f);
				platform.Time = std::fmod(platform.Time + seconds, period);
				const float blend = 0.5f - 0.5f * Math::Cos(platform.Time / period * 6.2831853f);
				transform.Position = platform.From + (platform.To - platform.From) * blend;
			});
	}

	// Starts runs, adds drones over time and restarts after a game over.
	void RunSystem(SystemContext& context)
	{
		const auto& scene = context.World;
		auto* run = scene.Single<ArenaRunComponent>();
		const auto* rules = scene.Single<ArenaRulesComponent>();
		const auto craft = First<CraftComponent>(scene);
		if (!run || !rules || craft.IsNull())
			return;
		const float seconds = StepSeconds(context);
		const Vec3 craftPosition = scene.TryGet<const ECS::TransformComponent>(craft)->Position;
		switch (run->State)
		{
		case RunState::Starting:
		{
			for (const auto entity : InIdOrder<DroneComponent>(scene))
				context.Commands.Destroy(scene.GetRef(entity));
			for (const auto entity : InIdOrder<CellComponent>(scene))
				context.Commands.Destroy(scene.GetRef(entity));
			const auto& spawn = scene.TryGet<const CraftComponent>(craft)->Spawn;
			auto transform = *scene.TryGet<const ECS::TransformComponent>(craft);
			transform.Position = spawn;
			scene.Write(craft, transform);
			auto mover = *scene.TryGet<const ECS::MoverComponent>(craft);
			mover.Velocity = {};
			mover.DesiredVelocity = {};
			scene.Write(craft, mover);
			const auto* profiles = Profiles(context);
			const auto profile = profiles ? profiles->Get<ArenaProfile>(ProfileFile) : ArenaProfile{};
			const bool swarm = Swarm(context);
			*run = ArenaRunComponent{};
			run->State = RunState::Playing;
			run->Best = profile.BestScore;
			run->NextDrone = rules->DroneInterval;
			run->Drones = swarm ? rules->SwarmDrones : rules->StartDrones;
			for (std::uint32_t index = 0; index < run->Drones; ++index)
				SpawnDrone(context, *rules, spawn);
			for (std::uint32_t index = 0; index < rules->Cells; ++index)
				SpawnCell(context, *rules, spawn);
			Log(LogLevel::Info, swarm ? std::format("Arena swarm: {} drones, no game over", run->Drones) :
				std::format("Arena run started (best {}): WASD or arrows steer, Space jumps, Q/E turn, Esc pauses",
					run->Best));
			break;
		}
		case RunState::Playing:
			run->Time += seconds;
			if (!Swarm(context) && run->Time >= run->NextDrone && run->Drones < rules->MaxDrones)
			{
				SpawnDrone(context, *rules, craftPosition);
				++run->Drones;
				run->NextDrone += rules->DroneInterval;
			}
			break;
		case RunState::Over:
			run->Timer -= seconds;
			if (run->Timer <= 0.0f)
				run->State = RunState::Starting;
			break;
		}
	}

	// Steers the craft from this tick's records: the latest direction, and a jump if any record had one.
	void CraftSystem(SystemContext& context)
	{
		const auto& scene = context.World;
		const auto* run = scene.Single<ArenaRunComponent>();
		Vec3 move{};
		bool jump = false;
		context.Stream.ForEach<CraftCommand>([&](const CraftCommand& command, std::uint8_t)
		{
			move = Flat(command.Move);
			jump = jump || command.Jump;
		});
		if (!run || run->State != RunState::Playing)
		{
			move = {};
			jump = false;
		}
		const auto* rules = scene.Single<ArenaRulesComponent>();
		scene.View<const CraftComponent, ECS::MoverComponent, const ECS::TransformComponent>().Each(
			[&](ECS::Entity, const CraftComponent& craft, ECS::MoverComponent& mover, const ECS::TransformComponent& transform)
			{
				mover.DesiredVelocity = move * craft.Speed;
				if (jump)
				{
					mover.DesiredVelocity.Z = craft.JumpSpeed;
					if (rules)
						context.Services.PlaySound(rules->JumpSound, { transform.Position });
				}
			});
	}

	// Drones chase the craft within range and otherwise wander between random waypoints.
	void DroneSystem(SystemContext& context)
	{
		const auto& scene = context.World;
		const auto* rules = scene.Single<ArenaRulesComponent>();
		const auto* run = scene.Single<ArenaRunComponent>();
		const auto craft = First<CraftComponent>(scene);
		if (!rules || !run || craft.IsNull())
			return;
		const float seconds = StepSeconds(context);
		const Vec3 target = scene.TryGet<const ECS::TransformComponent>(craft)->Position;
		// A short grace at the start of a run, before drones notice the craft.
		const bool hunting = run->State == RunState::Playing && run->Time > 2.0f;
		for (const auto entity : InIdOrder<DroneComponent>(scene))
		{
			auto* drone = scene.TryGet<DroneComponent>(entity);
			auto* mover = scene.TryGet<ECS::MoverComponent>(entity);
			const auto* transform = scene.TryGet<const ECS::TransformComponent>(entity);
			if (!drone || !mover || !transform)
				continue;
			const Vec3 position = transform->Position;
			Vec3 goal = drone->Waypoint;
			if (hunting && FlatLength(target - position) < drone->ChaseRange)
				goal = target;
			else
			{
				drone->Retarget -= seconds;
				if (drone->Retarget <= 0.0f || FlatLength(drone->Waypoint - position) < 1.0f)
				{
					drone->Waypoint = RandomPoint(context, *rules, 1.5f, 0.0f);
					drone->Retarget = context.Random.Range(3.0f, 6.0f);
				}
				goal = drone->Waypoint;
			}
			const Vec3 toward = Flat(goal - position);
			const float distance = FlatLength(toward);
			mover->DesiredVelocity = distance > 0.05f ? toward * (drone->Speed / distance) : Vec3{};
		}
	}

	// The previous step's sensor contacts: the craft collects a cell, and a new one appears.
	void CellSystem(SystemContext& context)
	{
		const auto& scene = context.World;
		auto* run = scene.Single<ArenaRunComponent>();
		const auto* rules = scene.Single<ArenaRulesComponent>();
		const auto craft = First<CraftComponent>(scene);
		if (!run || !rules || craft.IsNull() || run->State != RunState::Playing || run->Time <= 0.0f)
			return;
		const auto craftRef = scene.GetRef(craft);
		const Vec3 craftPosition = scene.TryGet<const ECS::TransformComponent>(craft)->Position;
		for (const auto& contact : context.Physics.GetContacts())
		{
			if (contact.Kind != ContactEventKind::Begin || !contact.Sensor)
				continue;
			const auto other = contact.A == craftRef ? contact.B : contact.B == craftRef ? contact.A : ECS::EntityRef{};
			const auto cell = scene.Resolve(other);
			const auto* value = scene.TryGet<const CellComponent>(cell);
			if (!value)
				continue;
			context.Commands.Destroy(other);
			run->Score += value->Value;
			if (const auto* place = scene.TryGet<const ECS::TransformComponent>(cell))
				context.Services.PlaySound(rules->PickupSound, { place->Position });
			SpawnCell(context, *rules, craftPosition);
			Log(LogLevel::Info, std::format("Cell collected: score {}", run->Score));
		}
	}

	// A drone touching the craft ends the run, and a better score goes into the profile.
	void HitSystem(SystemContext& context)
	{
		const auto& scene = context.World;
		auto* run = scene.Single<ArenaRunComponent>();
		const auto craft = First<CraftComponent>(scene);
		if (!run || craft.IsNull() || run->State != RunState::Playing || run->Time <= 0.0f || Swarm(context))
			return;
		const auto* mover = scene.TryGet<const ECS::MoverComponent>(craft);
		if (!mover)
			return;
		PhysicsShape probe;
		probe.Shape = ECS::ColliderShape::Capsule;
		probe.Size = { mover->Radius + 0.08f, mover->Radius + 0.08f, mover->HalfHeight };
		probe.Position = scene.TryGet<const ECS::TransformComponent>(craft)->Position;
		if (context.Physics.Overlap(probe, 1u << DroneLayer).empty())
			return;
		run->State = RunState::Over;
		run->Timer = 2.5f;
		if (const auto* rules = scene.Single<ArenaRulesComponent>())
			context.Services.PlaySound(rules->CaughtSound);
		auto* profiles = Profiles(context);
		auto profile = profiles ? profiles->Get<ArenaProfile>(ProfileFile) : ArenaProfile{};
		++profile.Runs;
		const bool best = run->Score > profile.BestScore;
		if (best)
			profile.BestScore = run->Best = run->Score;
		if (profiles)
		{
			profiles->Set(ProfileFile, profile);
			profiles->Save(ProfileFile);
		}
		Log(LogLevel::Info, best ? std::format("Caught by a drone: score {}, a new best", run->Score) :
			std::format("Caught by a drone: score {} (best {})", run->Score, profile.BestScore));
	}

	// --- Update: once per frame, after the fixed steps -------------------------------------

	// The heads-up display: score, best, time and drones, and the start and game-over banners.
	void HudSystem(SystemContext& context)
	{
		const auto* run = context.World.Single<ArenaRunComponent>();
		if (!run || Swarm(context))
			return;
		HudModel model;
		model.Score = run->Score;
		model.Best = run->Best;
		model.Drones = run->Drones;
		model.Time = run->Time;
		// The pause menu replaces the banners; the HUD is declared after it and would cover it.
		const bool paused = context.Services.IsSimulationPaused();
		model.Starting = !paused && run->State == RunState::Starting;
		model.Over = !paused && run->State == RunState::Over;
		model.NewBest = model.Over && run->Score > 0 && run->Score >= run->Best;
		DrawHud(context.Ui, model);
	}

	// The follow camera: behind and above the craft, turned by Q and E, easing toward its place.
	void CameraSystem(SystemContext& context)
	{
		const auto& scene = context.World;
		const auto camera = First<FollowCameraComponent>(scene);
		const auto craft = First<CraftComponent>(scene);
		auto* rig = scene.TryGet<FollowCameraComponent>(camera);
		const auto* target = scene.TryGet<const ECS::TransformComponent>(craft);
		ECS::TransformComponent transform;
		if (!rig || !target || !scene.Read(camera, transform))
			return;
		const float seconds = FrameSeconds(context);
		rig->Yaw = std::remainder(rig->Yaw + context.Actions.GetAxis(Turn) * rig->TurnSpeed * seconds, 6.2831853f);
		const Vec3 place = target->Position - Forward(rig->Yaw) * rig->Distance + Vec3{ 0.0f, 0.0f, rig->Height };
		const Vec3 offset = place - transform.Position;
		// Far away (a new run, a teleport): jump there instead of sweeping across the arena.
		if (Dot(offset, offset) > 100.0f)
			transform.Position = place;
		else
			transform.Position = transform.Position + offset * (1.0f - std::exp(-rig->Follow * seconds));
		transform.Rotation = { rig->Pitch, 0.0f, rig->Yaw };
		scene.Write(camera, transform);
	}
}

// These two C-linkage signatures stay fixed. The host checks the contract before
// registering systems, and keeps them only while this module generation is loaded.
extern "C" RAVEN_GAME_API const char* RavenGetGameContract() noexcept
{
	return Raven::GameBuild::Contract;
}

extern "C" RAVEN_GAME_API void RavenRegisterSystems(Raven::SystemTable& systems)
{
	using namespace Raven;
	systems.Component<CraftComponent>();
	systems.Component<CraftCommand>();
	systems.Component<DroneComponent>();
	systems.Component<CellComponent>();
	systems.Component<PlatformComponent>();
	systems.Component<ArenaRulesComponent>();
	systems.Component<ArenaRunComponent>();
	systems.Component<FollowCameraComponent>();
	systems.Component<ArenaProfile>();
	systems.Component<ArenaMenuComponent>();

	const auto key = [](Key value, float scale = 1.0f) { return InputBinding{ InputCode::FromKey(value), Modifier::None, scale }; };
	const InputBinding moveX[]{ key(Key::D), key(Key::A, -1.0f), key(Key::Right), key(Key::Left, -1.0f) };
	const InputBinding moveY[]{ key(Key::W), key(Key::S, -1.0f), key(Key::Up), key(Key::Down, -1.0f) };
	const InputBinding jump[]{ key(Key::Space) };
	const InputBinding turn[]{ key(Key::Q), key(Key::E, -1.0f) };
	const InputBinding pause[]{ key(Key::Escape), key(Key::P) };
	systems.InputContext({ PlayContext, 0 });
	// Blocking: while paused, nothing below it sees input.
	systems.InputContext({ PauseContext, 10, true, false });
	systems.Action({ PlayContext, "MoveX", ActionKind::Axis, moveX });
	systems.Action({ PlayContext, "MoveY", ActionKind::Axis, moveY });
	systems.Action({ PlayContext, "Jump", ActionKind::Button, jump });
	systems.Action({ PlayContext, "Turn", ActionKind::Axis, turn });
	systems.Action({ PlayContext, "Pause", ActionKind::Button, pause });
	systems.Action({ PauseContext, "Resume", ActionKind::Button, pause });

	systems.Add({ "Pause", SystemPhase::Control, 0, &PauseSystem });
	systems.Add({ "Menu", SystemPhase::Control, 5, &MenuSystem });
	systems.Add({ "CraftControl", SystemPhase::Control, 10, &CraftControlSystem });
	systems.Add({ "Platforms", SystemPhase::FixedUpdate, 0, &PlatformSystem });
	systems.Add({ "Run", SystemPhase::FixedUpdate, 10, &RunSystem });
	systems.Add({ "Craft", SystemPhase::FixedUpdate, 20, &CraftSystem });
	systems.Add({ "Drones", SystemPhase::FixedUpdate, 30, &DroneSystem });
	systems.Add({ "Cells", SystemPhase::FixedUpdate, 40, &CellSystem });
	systems.Add({ "Hits", SystemPhase::FixedUpdate, 50, &HitSystem });
	systems.Add({ "Camera", SystemPhase::Update, 0, &CameraSystem });
	systems.Add({ "Hud", SystemPhase::Update, 10, &HudSystem });

	// UI preview entries (F6): each screen alone, with sample data (docs/UIDesign.md, layer 1).
	systems.Preview({ "Arena HUD", [](UI::Ui& ui) { DrawHud(ui, { 42, 57, 6, 83.0f }); } });
	systems.Preview({ "Arena game over", [](UI::Ui& ui) { DrawHud(ui, { 61, 61, 9, 140.0f, false, true, true }); } });
	systems.Preview({ "Pause menu", [](UI::Ui& ui) { DrawPauseMenu(ui, false); } });
	systems.Preview({ "Key bindings", [](UI::Ui& ui)
	{
		const std::vector<BindingRow> rows{ { "Forward", { "W", "Up" } }, { "Back", { "S", "Down" } },
			{ "Left", { "A", "Left" } }, { "Right", { "D", "Right" } }, { "Jump", { "Space" } },
			{ "Turn left", { "Q" } }, { "Turn right", { "E" } }, { "Pause", { "Esc", "P" } } };
		DrawBindings(ui, rows, 2, false);
	} });
	// Every script the engine's fonts cover, for checking shaping, fallback and direction at a glance.
	systems.Preview({ "Text samples", [](UI::Ui& ui)
	{
		UI::Style card;
		card.Absolute = true;
		card.Start = 40.0f;
		card.Top = 40.0f;
		card.Width = 620.0f;
		card.Padding = UI::All(20.0f);
		card.Gap = 10.0f;
		card.Background = UI::Rgba(0x0B1118E0);
		card.CornerRadius = 10.0f;
		ui.Begin("Samples", card);
		UI::Style line;
		line.FontSize = 22.0f;
		ui.Text("Latin", "Raven engine: café, naïve, Œuvre, Ångström", line);
		ui.Text("Greek", "Ελληνικά: γρήγορη καφέ αλεπού", line);
		ui.Text("Cyrillic", "Кириллица: быстрая лиса", line);
		ui.Text("Korean", "한국어: 레이븐 엔진에 오신 것을 환영합니다", line);
		ui.Text("Arabic", "العربية: مرحبا بكم في محرك رافين", line);
		ui.Text("Mixed", "Score 42 نقطة in the arena", line);
		UI::Style wrapped = line;
		wrapped.FontSize = 18.0f;
		ui.Text("Wrapped", "긴 한국어 문장은 단어 사이에서 줄이 바뀌어야 합니다. Long English text wraps between words "
			"as well, and Arabic does too: هذا نص عربي طويل يلتف بين الكلمات.", wrapped);
		ui.End();
	} });
}
