// The arena game (docs/GameServicesDesign.md, FD-46): steer a craft around the arena, collect
// energy cells and avoid the drones that roam it. A drone touching the craft ends the run; the best
// score is kept in the player's profile. Esc pauses.
//
// This file is the module's entry: the contract, the components, the input mapping and the system
// order. The systems live in one file per object or screen (ArenaSystems.hpp).

#include "ArenaSystems.hpp"
#include "ArenaUi.hpp"
#include "GameContract.hpp"

#include "Raven/Core/Api.hpp"
#include "Raven/Platform/Input.hpp"

// These two C-linkage signatures stay fixed. The host checks the contract before
// registering systems, and keeps them only while this module generation is loaded.
extern "C" RAVEN_GAME_API const char* RavenGetGameContract() noexcept
{
	return Raven::GameBuild::Contract;
}

extern "C" RAVEN_GAME_API void RavenRegisterSystems(Raven::SystemTable& systems)
{
	using namespace Arena;
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

	RegisterPreviews(systems);
}
