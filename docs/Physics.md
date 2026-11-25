# Physics

Raven simulates collision and rigid bodies with [Jolt Physics](../ThirdParty/JoltPhysics/README.md), inside the engine. The decision, its rules and its measurements are in [GameServicesDesign.md, FD-32](GameServicesDesign.md#fd-32--collision-and-physics). This page describes what a game uses, and how the engine keeps Jolt in step with the components.

## What a game uses

**Components** (`Raven/Components/PhysicsComponents.hpp`, engine components registered with every scene):

| Component | Fields | Notes |
| --- | --- | --- |
| `ColliderComponent` | `Shape` (Box, Sphere, Capsule), `Size`, `Offset`, `Sensor`, `Layer` | Without a rigid body, the collider is static. Box `Size` is the half extent per axis, sphere `Size.X` is the radius, and a capsule lies along local Z with radius `Size.X` and half length `Size.Z`. `Layer` is below 16. |
| `RigidBodyComponent` | `Motion` (Static, Kinematic, Dynamic), `Mass`, `Friction`, `Restitution`, `LinearVelocity`, `AngularVelocity`, `Continuous` | Dynamic bodies are moved by the simulation. Kinematic bodies are moved by game code through their transform and push dynamic bodies. `Continuous` turns on swept collision for fast bodies. |

**Rules:**
- **Scale:** sizes and offsets are multiplied by the entity's world scale. A sphere needs a uniform scale, and a capsule equal X and Y scale. The sample cube mesh is 1 m per side and the sphere 1 m across, so the default sizes match them at any scale.
- **Placement:**
  - A dynamic body must be a scene root.
  - Static and kinematic colliders may be children. They follow their cached world transform, which is one frame behind within the fixed steps.
  - The world is Z-up, with gravity (0, 0, −9.81).
- **Invalid values:** an invalid collider (a non-finite value, a size or mass of zero or less, a bad layer, an unsupported scale, a dynamic child) is logged once with the entity name and the reason. The entity is left out until its components change to valid values.
- **Edits:** the engine writes dynamic bodies' `Position`, `Rotation` and both velocities after every fixed step. Game code may write them too:
  - a changed transform teleports the body;
  - a changed velocity sets it;
  - a changed size, scale, offset or mass rebuilds only that body's shape;
  - a changed motion type or sensor flag rebuilds the body.
- **Interpolation:** dynamic and kinematic bodies are drawn with render interpolation without the `InterpolateComponent` tag ([FD-34](GameServicesDesign.md#fd-34--smaller-services)).

**Queries and contacts** (`Raven/Physics/PhysicsQueries.hpp`, `SystemContext::Physics`):
- **Queries:**
  - `CastRay` and `CastShape` return the closest hit: entity, point, normal and distance.
  - `Overlap` returns the entities whose colliders overlap a shape, ordered by persistent ID.
  - Each takes a layer mask. Sensors are never hit.
  - Queries see the world after the latest fixed step; edits made since then reach it at the next step.
- **Contact events:**
  - `GetContacts()` returns, during a fixed step, the contacts that began or ended in the previous physics step. The list is ordered by entity pair, and A is the entity with the smaller persistent ID.
  - Begin events carry the point, the normal from A toward B, and whether a sensor took part.
  - A body falling asleep does not end its contacts.
  - A despawned entity's contacts end at the next step.
  - The list is empty in the Control and Update phases.

Physics changes made in fixed-step systems take effect in the same step: each fixed step runs the game's systems, applies their deferred commands, then syncs, simulates and writes back.

## Developer keys

These are actions of the `Engine.Developer` context ([InputProcessing.md](InputProcessing.md#input-mapping-contexts-bindings-and-key-listening-m6c-step-c1-2026-11-25)):

| Key | Action |
| --- | --- |
| F10 | Collider view: shapes in wireframe (static grey, kinematic green, dynamic yellow, sleeping red) and the contact manifolds of the latest step. Lines are depth-tested against the scene. |
| F9 | While paused with F8: run one fixed step (game systems, then physics). |
| Shift+F8 | Restart play: reload the scene from its file and resume. |

The frame trace (`RAVEN_FRAME_TRACE`) records `physics_ns` (the frame's physics steps), `physics_bodies`, `physics_active` and `physics_contacts`.

## Project limits

Jolt's capacities are fixed when the host starts. The project file sets them with `physics-limits <bodies> <body pairs> <contact constraints>`, each from 16 to 4194304. The defaults are 65536, 65536 and 10240 ([SceneWorkflow.md](SceneWorkflow.md#project-data)). Reaching the body limit is logged once, and further colliders are left out. Running out of body pairs or contact constraints during a step is logged as a warning, because Jolt then ignores some contacts.

## How the engine keeps Jolt in step

`PhysicsWorld` (`Private/Physics/`) is host-owned, outlives game module replacement, and is the only code that includes Jolt. It follows the render world's pattern:
- **Lifecycle:** it subscribes to the lifecycle journal of the collider, rigid body and transform components to learn which entities gained or lost a body. It reads change ticks to find edited ones.
- **Batched adds:** new bodies are created in persistent-ID order and added in batches (one for static bodies, one for moving ones), so two runs build the same broad phase.
- **Its own write-back:** after writing back, it closes the change tick, so its own writes are never read as game edits; the render world still sees them. An edit is a teleport only when the value differs from what physics last applied or wrote. Rewriting the same value, as render interpolation's restore does, changes nothing.
- **Sensors:** a static sensor is a kinematic body inside Jolt that never sleeps, so it still reports static and sleeping bodies.
- **Contacts:**
  - They are recorded from Jolt's worker threads, then sorted by persistent entity pair after the step.
  - Jolt reports a contact as removed when a body falls asleep. Such pairs stay touching until both bodies are awake and their bounds have separated.
- **Failures:** Jolt's asserts (Debug) and trace output go to the engine log. A Jolt library built with different settings than RavenCore is caught at startup.
- **Threads:** the job system uses up to eight worker threads (hardware threads minus one).
- **New scene:** a new scene (F3, F4, Shift+F8 or a scene reload) rebuilds the world from the components.

## Movers

`MoverComponent` (`Raven/Components/PhysicsComponents.hpp`) moves an entity as an upright capsule (FD-47). The capsule is centered on the entity: `Radius`, plus `HalfHeight` for half the length of its straight part along Z.

**What each side writes:**
- **The game, in fixed steps:** `DesiredVelocity`, `MaxSpeed`, `GravityScale`, the capsule, `MaxSlope` and `Layer`.
- **The engine, after each step:** the entity's position, `Velocity`, `Ground` (OnGround, OnSteepGround, NotSupported, InAir) and `GroundNormal`. Rotation stays with the game.

**Steering:**
- **With gravity:**
  - X and Y steer, limited to `MaxSpeed`.
  - A positive `DesiredVelocity.Z` jumps when the mover stands on walkable ground.
  - On walkable ground the mover follows the ground's velocity, so it rides moving platforms.
- **Without gravity** (`GravityScale` 0): all three axes steer.
- **What Jolt handles:** sliding along walls, walking up slopes up to `MaxSlope`, stepping up 0.4 m, sticking to the floor when walking down, and pushing dynamic bodies.
- **Game edits:** writing the transform teleports the mover; writing `Velocity` sets its momentum (a knockback, for example).

**Rules:**
- **Placement:** a mover must be a scene root and cannot also have a `ColliderComponent` or `RigidBodyComponent`. Such an entity is logged and left out until fixed.
- **Interpolation:** movers are drawn with render interpolation without the tag.

**Backend:**
- **Characters:** each mover is a Jolt virtual character, updated in persistent-ID order right before the simulation step.
- **Inner body:** an inner kinematic body on `Layer` lets queries, sensors and contact events see it, and the collider view draws it in green.
- **Movers blocking movers:** a grid of nearby characters handles this. Jolt's own checker compares every pair, which made 500 movers cost 22 ms a step.

The crowd backend for hundreds to thousands of units comes later, with navigation (FD-47).

## Picking

Left clicks are picked on Jolt (FD-34) by a `PickWorld` (`Private/Physics/PickWorld.*`), owned by the host next to the physics world. Hits reach game systems as `SystemContext::Picks`.
- **Separate system:** it is a separate Jolt system that is only queried, never simulated, so renderables never use the simulation's body limit or appear in physics queries.
- **One body per renderable:** every entity with a `StaticMeshComponent` and a world transform has a static body.
- **Shared mesh shapes:**
  - Each mesh asset has one exact triangle shape, built from the renderer's CPU copy of the mesh and shared by every entity that draws it.
  - A non-unit or mirrored scale wraps it per entity.
  - A reloaded mesh rebuilds its shape, and a shape no entity uses is released.
- **Matches the frame:**
  - Bodies are brought up to date only when a pick runs, from the world transforms of the frame being drawn, so interpolated entities are picked where they appear.
  - Both triangle faces count.
  - Only meshes the scene already leases can be hit; picking never loads an asset.
- **Capacity:** it starts at 1024 bodies and grows by rebuilding when the scene has more renderables.
- **Log:** the log line for each pick reports its sync and query time.

## Sample

The Samples game's **Physics** scene (`Levels/Physics.ravenscene`, generated by `SamplesTool create-physics`) has a floor with walls, a ramp, a 15-crate pyramid, six bouncing spheres and three walkers. The walkers are movers that the game's fixed-step `Patrol` system steers around circles. Run it with `RavenEngine.exe --scene e28e2f44-752b-4474-9ce4-0677d5177af2`, or press F4 from Collection.

## Not covered yet

- **Constraints and crowds:** joints and constraints are not exposed. The crowd backend for movers comes with navigation (FD-47).
- **Editor picking:** lights and cameras have no pick shapes yet; they come with the first editor that picks them.
- **First pick in a large scene:** it builds every pick body (32 ms at 50k renderables in Release). Later picks only update what changed.
- **Collision layers:** layers do not filter collisions between each other yet; all layers collide, and masks apply to queries only.
- **Other platforms:** macOS and Linux compile the same code but are not verified.
