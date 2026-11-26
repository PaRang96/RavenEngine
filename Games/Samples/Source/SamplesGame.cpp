// The 3D sample game: free flight through the Perspective, Gallery, Assemblies, Puzzle and
// Collection scenes and the benchmark scenes, or the orbit demo with `--orbit`.
//
// This file is the module's entry: the contract, the components, the input mapping and the system
// order. The systems live in one file per object (SampleSystems.hpp).

#include "SampleSystems.hpp"
#include "GameContract.hpp"

#include "Raven/Core/Api.hpp"

// These two C-linkage signatures stay fixed. The host checks the contract before
// registering systems, and keeps them only while this module generation is loaded.
extern "C" RAVEN_GAME_API const char* RavenGetGameContract() noexcept
{
	return Raven::GameBuild::Contract;
}

extern "C" RAVEN_GAME_API void RavenRegisterSystems(Raven::SystemTable& systems)
{
	using namespace Samples;
	systems.Component<SwitchComponent>();
	systems.Component<RotatorComponent>();
	systems.Component<DoorComponent>();
	systems.Component<GoalComponent>();
	systems.Component<PickupComponent>();
	systems.Component<AttemptComponent>();
	systems.Component<OrbitCameraComponent>();
	systems.Component<ControlledMeshComponent>();
	systems.Component<BeaconComponent>();
	systems.Component<DemoComponent>();
	systems.Component<OrderToggleComponent>();
	systems.Component<PatrolComponent>();

	const auto key = [](Key value) { return InputBinding{ InputCode::FromKey(value) }; };
	const InputBinding pause[]{ key(Key::Space) };
	const InputBinding order[]{ key(Key::O), key(Key::F1) };
	const InputBinding swap[]{ key(Key::G) };
	const InputBinding restart[]{ key(Key::R) };
	const InputBinding next[]{ key(Key::Enter) };
	systems.InputContext({ .Name = PlayContext });
	systems.Action({ PlayContext, PauseAction, ActionKind::Button, pause });
	systems.Action({ PlayContext, OrderAction, ActionKind::Button, order });
	systems.Action({ PlayContext, SwapAction, ActionKind::Button, swap });
	systems.Action({ PlayContext, RestartAction, ActionKind::Button, restart });
	systems.Action({ PlayContext, ContinueAction, ActionKind::Button, next });

	systems.Add({ "Patrol", SystemPhase::FixedUpdate, 0, &PatrolSystem });
	systems.Add({ "FreeFly", SystemPhase::Update, 0, &FreeFlySystem });
	systems.Add({ "OrbitCamera", SystemPhase::Update, 0, &OrbitCameraSystem });
	systems.Add({ "MeshRotation", SystemPhase::Update, 10, &MeshRotationSystem });
	systems.Add({ "Interaction", SystemPhase::Update, 20, &InteractionSystem });
	systems.Add({ "Demo", SystemPhase::Update, 30, &DemoSystem });
	systems.Add({ "Trace", SystemPhase::Update, 100, &TraceSystem });
}
