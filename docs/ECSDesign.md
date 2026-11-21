# M1 ECS design note

Status: **implemented; M1 complete** (2026-11-21). Approved by the owner on 2026-11-20, with these revisions:

- D1 keeps an empty, non-virtual `Component` base.
- D6 is the existing loaded-generation rule.
- D8 is deferred.
- Budgets are set from the M0 baseline.

Implementation follows the migration steps below as reviewed snippets. Contracts move into [EngineModules.md](EngineModules.md) as each step lands.

This note covers milestone M1 in [ROADMAP.md](../ROADMAP.md): replace per-entity heap storage with dense typed pools and real systems, while keeping the joint ECS/hot-reload contract.

## Goals and fixed constraints

Goals:

- Systems iterate thousands to 100k entities with cache-friendly, contiguous access.
- Query cost scales with matching entities, not with all entities.
- World transforms are computed once per frame for changed entities, in a form that M3 can upload to the GPU incrementally.
- A host-owned system schedule gives per-system ordering and timing, and later parallelism.

Constraints from existing contracts, which this design keeps:

- **The host owns retained state.** Component values live in RavenCore memory and survive game-DLL replacement. Unloading a module never frees or invalidates them.
- **No module-owned objects in retained state.** That means no virtual-table pointers, callbacks or pointers into a module persist across unload.
- **Compatible reload preserves entities, values and bindings.** Changes to the header/build contract are still rejected until the host is rebuilt and restarted (`cmake/GameContract.cmake` hashes public and game headers). There are no manual layout versions.
- **Failed candidates keep the active code.** Rejection happens before anything in the active world changes.
- **Every target is met on the minimum-spec laptop** (i7-12700H, RTX 3070 Ti Laptop GPU).
- **Small, reviewable steps.** Each migration step keeps the samples working.

## Current implementation and cost

| Area | Today | Cost |
| --- | --- | --- |
| Storage | `std::map<std::string, Pool>`, where each pool is `unordered_map<entityIndex, vector<byte>>` ([Registry.cpp](../RavenEngine/Private/ECS/Registry.cpp)) | One heap block per component per entity, scattered in memory |
| Read/Write | String map lookup, then hash lookup, then a virtual call that copies field by field | Paid for every component access |
| Query | `SceneAccess::Query` calls `Has` for every scene entity and every requested type, and returns a `vector<Entity>` ([SceneAccess.cpp](../RavenEngine/Private/Scene/SceneAccess.cpp)) | O(scene entities × types) plus a later Read for each value |
| Systems | One `Update` entry point in the DLL calls hand-written loops | No ordering, per-system timing or declared access |
| World transforms | Composed on demand by walking ancestors. The draw pass caches parents per frame ([Scene.hpp](../RavenEngine/Public/Raven/Scene/Scene.hpp)) | Recomputed every frame for every mesh, changed or not |

Measured on the minimum-spec laptop with 4,000 meshes and 800 animated pivots, at the 95th percentile ([Benchmark3D.md](Benchmark3D.md)): gameplay ECS about 1.6–1.8 ms, mesh world transforms 0.7–1.0 ms, remaining scene gather 1.2–1.6 ms. Scaling linearly to 50k objects would cost over 40 ms of CPU per frame for these stages alone.

## Decisions

### D1 — Components keep an empty, non-virtual `Component` base (approved)

**Problem.** Components derive from the abstract `ECS::Component`, which is virtual. The Registry stores their fields packed, without padding or the virtual-table pointer, so stored bytes do not match the C++ object layout. A dense pool therefore cannot be exposed as `std::span<T>` or `T&`. Storing the full objects would retain virtual-table pointers, and for game-defined components such as `Velocity` those pointers point into the DLL that gets unloaded.

**Decision.** Every component still derives from `ECS::Component`, but `Component` becomes an empty base with no virtual functions. Because the base is empty, it takes no space in a standard-layout derived struct, so `sizeof(TransformComponent)` is still 36 bytes and dense pools can hold `T` directly. `RAVEN_COMPONENT(Type, Fields...)` stays and generates static, non-virtual information instead of overrides:

```cpp
namespace Raven::ECS
{
	struct Component
	{
	protected:
		Component() = default; // Only derived component types are created.
	};
}

struct TransformComponent : ECS::Component
{
	Vec3 Position{};
	Vec3 Rotation{};
	Vec3 Scale{ 1.0f, 1.0f, 1.0f };
	RAVEN_COMPONENT(Raven::ECS::TransformComponent, Position, Rotation, Scale);
};
// The macro generates: TypeName, a constexpr TypeId, the field list,
// a layout hash, and static_asserts for the field and layout rules.
```

- `ComponentType<T>` keeps requiring `std::derived_from<T, Component>`, so "every component is a Component" stays visible in declarations and enforced by APIs.
- The existing field rules stay as compile-time checks: `ComponentValue` approval, no pointers, standard layout, trivially copyable and destructible. The macro also asserts that `T` itself is standard layout, trivially copyable and free of virtual functions, that the empty base adds no size, and that the listed fields cover every member.
- Pools store `T` in its native layout, including padding. Scene codecs keep writing fields explicitly, so padding never reaches files.
- Type-erased callers (the scene tool, codecs, future editor inspection) use a `ComponentInfo` descriptor (name, id, size, alignment, layout hash and field table) and a `ComponentRef` (info plus bytes) instead of virtual calls on `Component&`.
- `registry.Read(entity, transform)` call sites still compile through templates. The `const Component&` overloads and `SceneAccess::Query(span<const Component*>)` are replaced by template and `ComponentInfo` versions.
- Per-type behavior such as add/remove hooks or editor display is registered per type, the same way systems are (D6), rather than as virtual methods on the data. Virtual methods on component data would put DLL-owned virtual tables back into retained state.

This matches the data-component pattern in Unreal Mass (`struct FTransformFragment : FMassFragment`, an empty base) and Unity DOTS (`struct X : IComponentData`, no virtual table). The earlier safety goals are kept: no virtual table or callback in retained state, and reviewed field types only.

Alternatives considered:

- **A plain struct with no base.** Rejected by the owner because it loses the visible is-a relation.
- **A CRTP base, `Component<T>`.** Noisier declarations, and it gains nothing over the macro.
- **Keep the virtual base and copy each element in and out during views.** No spans, about 2–4× slower iteration than direct references, and an extra copy for GPU uploads.

### D2 — Type identity and layout checks

- `TypeId` is a 64-bit FNV-1a hash of the fully qualified `TypeName`, computed at compile time. The same type has the same ID in the host and in every DLL generation.
- At runtime the Registry maps `TypeId` to a pool index once per pool. Views resolve their pools once per call, not once per entity.
- On first registration the Registry stores the name, so a hash collision between different names is a hard error.
- Each pool records size, alignment and a layout hash of the field names, field sizes and offsets. If a module presents a component whose layout hash differs from the existing pool, it is rejected. This backs up the header contract and also covers edge cases such as macros that change across configurations.

### D3 — Storage: one sparse set per component type

Each pool holds:

- A paged sparse array mapping entity index to dense index (4,096 entries per page, allocated on demand).
- Dense arrays: the entity handles, the `T` values (aligned) and a per-element change tick (`uint32`).
- Removal swaps the last element into the gap and shrinks the arrays. Dense order is not stable.

Why sparse sets rather than archetypes:

- Adding or removing a component is O(1) and never moves other components. Archetypes move the whole entity row on every structural change.
- Each type is one contiguous array, which maps directly to the M3 GPU upload of changed world transforms and instances.
- Reload and diagnostics stay simple: one byte array per type, with a size and layout hash.
- The cost is that iterating several components does random lookups in the non-driving pools. Iteration starts from the smallest pool to limit this. If benchmarks show that the hot two-component paths (Transform + WorldTransform, WorldTransform + StaticMesh) are too slow, add EnTT-style owning groups for those specific pairs later.

**Gate:** M0 micro-benchmarks compare the current Registry, a sparse-set prototype in an ignored `out/` probe, and the budgets below before the production change.

### D4 — Views with borrowed references

```cpp
registry.View<const TransformComponent, RotatorComponent>().Each(
	[&](ECS::Entity entity, const TransformComponent& transform, RotatorComponent& rotor) { ... });

for (WorldTransformComponent& world : registry.Values<WorldTransformComponent>()) { ... } // one pool, span

AttemptComponent* attempt = registry.TryGet<AttemptComponent>(entity); // borrowed, same lifetime rule
```

- **Lifetime:** references, spans and views are valid only during the current synchronous system call or host phase. They must not be stored in components, statics or module state. This is the rule already agreed in `AGENTS.md`.
- **Structural safety:** while a view iterates, `Add`/`Remove`/`Destroy` are rejected and logged, and `Create` throws `std::logic_error`. A Debug abort was considered and rejected, because it would end the live session during hot reload. Systems make structural changes through commands (D5).
- **Change ticks** (implemented in step 6):
  - Each pool stores one tick per dense element.
  - `Add`, `Write`, mutable `TryGet` and the non-const types in a view stamp the Registry's current tick. `Read`, `TryGet<const T>` and const view types do not.
  - A system that writes conditionally should iterate const and call `Write` for the elements it changes.
  - A consumer remembers the tick that `AdvanceChangeTick()` returned and processes elements stamped after it.
  - These ticks drive D7 and the M3 GPU uploads.
- **Copy API:** `Read`/`Write` copies stay for one-off access and for tools. They become a typed lookup plus one `memcpy`.

### D5 — Deferred structural changes (deferred to first need)

Owner decision (2026-11-21): no current gameplay spawns or despawns entities at runtime, so `Commands` waits for the first feature that does. Until then, views reject structural changes (D4). The planned design:


- Systems receive a `Commands` buffer for create, destroy, add and remove.
- `Commands::Create` reserves an entity slot immediately, so later commands in the same buffer can refer to the new entity.
- The host applies commands in recorded order at the end of each phase, after all views are released.
- Host code outside systems (scene loading, SceneSession switching, the tool) keeps direct structural calls. The view iteration guard rejects misuse.
- Destroying an entity that belongs to a scene goes through `Scene` so the hierarchy, names and bindings stay consistent. Scene registers a command hook for this.

### D6 — Systems and schedule

Phases:

| Phase | Owner | Purpose |
| --- | --- | --- |
| `FixedUpdate` | game | Courtyard-style fixed-rate gameplay (existing `FixedGameplayStep`) |
| `Update` | game | Variable-rate gameplay, camera controllers, interaction |
| `PostUpdate` | host | Apply commands, propagate hierarchy (D7), update audio and other services |
| `Extract` | host | Read-only render and picking gather from cached data |

Each system is registered with a name, phase and order. Declared read/write access waits for parallel scheduling. Per-system run time is recorded for the benchmark.

**Implemented in step 5 (2026-11-21):**

- `Raven/Core/GameSystems.hpp` defines `SystemPhase` (`FixedUpdate`, `Update`), `SystemContext`, `SystemDesc` and `SystemTable`.
- The DLL exports `RavenRegisterSystems` in place of `RavenGetGameUpdate`. Activation rejects a table that is empty, has an unnamed system or null function, an unknown phase, or a duplicate name. Systems are stably sorted by phase, then order.
- `Application` runs `FixedUpdate` once per fixed step for fixed-step clients, then `Update` once per drawable frame for every client. `GameModule::Update` keeps its signature for existing callers such as the smoke test.
- The host catches exceptions per system and logs the system name.
- Time and call counts accumulate by system name across reloads, and are written to `<trace>.systems.csv` when a frame trace is enabled.
- Owner decision (2026-11-21): `GameUpdateResult` stays, as `SystemContext::Result`. A FixedUpdate system writes the player displacement there, and Courtyard's `OnGameplayStep` is unchanged.
- `PostUpdate` and `Extract` arrive with the host systems in step 6.

**Registration across reload (approved).** The game DLL exports `RavenRegisterSystems(SystemTable&)` next to the existing contract entry point. The host calls it after a candidate passes validation and keeps the function pointers in a table tagged with that module generation. Before the old module unloads, the host clears its table. If a candidate is rejected, the old table stays active.

This uses the rule the host already follows. `GameModule` keeps the DLL's `Update` pointer for the loaded library and sets it to null before `UnloadGameLibrary` ([GameModule.cpp](../RavenEngine/Private/Core/GameModule.cpp)). Stated explicitly: **the host may hold module function pointers only for the loaded generation, in tables it clears before unload. Retained world state never holds them.** The system table is cleared at that same point.

Alternative: keep one `Update` entry point per phase and have the DLL call its own systems in order. This needs no rule change, but the host loses per-system timing, ordering and any future parallel scheduling. Not recommended.

The existing `GameBindings` (camera, player, mesh, interactions) reach systems through the `SystemContext`. Over time they become components or singletons (D9).

### D7 — Hierarchy and cached world transforms

- Add a host-managed `WorldTransformComponent { Mat4 World; }` to every scene entity with a Transform.
- `Scene` stays the authority for parents. It keeps an array of its entities with parents before children, rebuilt only when the hierarchy changes (create, destroy, reparent).
- The PostUpdate hierarchy system walks that array once. It recomputes `World = ParentWorld * Local` only when the local Transform changed or the parent's World changed this tick, and marks recomputed elements changed.
- `Extract`, picking, culling and later the M3 GPU upload read the cached `World`.
- Gameplay `SceneAccess::GetWorldTransform` keeps live composition. Interaction code writes a Transform and checks distances in the same update, so cached values from the previous PostUpdate would be one step stale there. This difference is documented in the API.
- The draw-time `WorldTransformPass` is removed once Extract uses the cache.

**Implemented in step 6 (2026-11-21):**

- `WorldTransformComponent` lives beside `TransformComponent`. `Scene::UpdateWorldTransforms()` adds it to owned entities that have a Transform, and removes it when the Transform goes away.
- The update reprocesses everything after a hierarchy change. Otherwise it recomputes an entity only when its Transform tick is newer than the last pass or its parent was recomputed in this pass. Recomputed worlds are stamped with the new tick for later consumers.
- Rotated local transforms compose in closed form (`parent · T · Rz · Ry · Rx · S`), and zero-angle axes skip their trigonometry. This halved the 20%-changed update. Unrotated transforms keep the exact scale-and-translate path.
- Extreme scales keep the earlier retry in the direct multiplication order. A world that stays unrepresentable is stored as-is so descendants can still recompose, and draw gathering skips non-finite worlds instead of throwing.
- A live entity whose ancestor has no Transform (for example, after its root was destroyed outside the Scene) still makes the update throw `std::invalid_argument`, matching the earlier draw path.
- Differences from the plan:
  - **Picking** keeps live composition. It runs per click, so its cost doesn't matter, and it already matches the next frame.
  - **No named host phase:** the Perspective sample calls the update right before its draw gather, instead of a named `PostUpdate`/`Extract` host phase. A shared phase can follow when a second consumer needs it.

### D8 — Scene membership in a shared Registry (per-slot scene ID, step 4)

One Registry is shared by every scene. `SceneSession` builds a candidate scene and either publishes or discards it within one `Update` call, before any game code runs ([SceneSession.cpp](../RavenEngine/Private/Scene/SceneSession.cpp)). But the Registry can also hold entities that no scene owns, and the 3D smoke test creates one on purpose to check that scene queries skip it ([Windows3DSmoke.cpp](../scripts/Windows3DSmoke.cpp)). An unfiltered scene view would count it, so the original deferral did not hold.

**Decision (owner, 2026-11-21):**

- Each Registry slot stores a 32-bit owning scene ID, and `Scene::CreateEntity` sets it.
- `SceneAccess` views compare it per visited entity, which is one array read. `SceneAccess::TryGet` uses the same check.
- Host-side `Registry` views stay unfiltered.
- The "no candidate scene during systems" Debug check is not needed, because scoping no longer depends on how many scenes exist.
- One Registry per world remains an option when editor and play sessions are designed.

### D9 — Singletons and threading

- Single-instance state such as `AttemptComponent` or the scene lighting uses `registry.Single<T>()`. This replaces `Query<T>().front()`.
- M1 stays single-threaded. Declared system access is recorded so a later milestone can schedule systems in parallel without changing the API.

## Performance budgets

These are the M1 exit criteria, measured in Release on the minimum-spec laptop. The micro-benchmark rows come from [EcsBenchmark](../Tests/Benchmarks/EcsBenchmark.cpp), and the 4,000-mesh row from [the 3D benchmark](Benchmark3D.md#4000-meshes-at-native-resolution). Both M0 baselines are recorded below.

| Operation | M0 baseline (current Registry) | After step 6 | M1 budget |
| --- | ---: | ---: | ---: |
| Mutable iteration over 100k Transforms | 9.20 ms (read+write copies over a `GetEntities` snapshot) | 0.11 ms (`View<Transform>`) | ≤ 0.3 ms |
| Two-component view (Rotator + Transform) | 5.96 ms for 10k matches among 100k entities (596 ns per match) | 0.09–0.13 ms for 10k matches (9–13 ns per match); 50k matches not yet measured | ≤ 0.5 ms for 50k matches among 100k |
| World transforms with 20% changed | 9.62 ms for 50k meshes, all recomputed every frame | 0.52–0.57 ms for 50k entities | ≤ 1.0 ms |
| World transforms for 50k meshes (gameplay `GetWorldTransform`) | 26.48 ms (530 ns each) | 24 ms (unchanged, live) | Not budgeted; stays live composition (D7) |
| Typed `Read` of one component, random order, 100k entities | 78 ns | 15–17 ns | ≤ 50 ns |
| 4,000-mesh benchmark: gameplay ECS + mesh world transforms + remaining scene gather (sum of each stage's median p95) | 3.91 ms (1.77 + 0.82 + 1.33) | 0.37 ms (0.04 + 0.14 + 0.19) | ≤ 1.96 ms (at least 2× faster) |

The [exit record](#m1-exit-record-2026-11-21) rechecked every row together on master.

### M0 baseline record (2026-11-21)

- **Machine:** Intel Core i7-12700H, NVIDIA GeForce RTX 3070 Ti Laptop GPU (also has Intel Iris Xe), on AC power, Razer Cortex power plan, Windows 11.
- **Build and method:** `x64-release` built with MSVC 19.51. Each value is the median of 9 timed runs after one warm-up run, from a single invocation.
- **Scene layout:** like the 3D benchmark scenes. Row roots own pivots, each pivot owns one mesh, and one pivot in five has a rotator. The ns/op column is per visited entity, per matched rotator, or per mesh.

| Case | Entities | Median ms | ns/op |
| --- | ---: | ---: | ---: |
| Transform read+write, all entities | 10,050 | 0.623 | 61.9 |
| Query<Rotator, Transform> + read/write | 10,050 | 0.501 | 500.6 |
| World transforms, draw pass (per mesh) | 10,050 | 0.681 | 136.3 |
| World transforms, GetWorldTransform (per mesh) | 10,050 | 2.685 | 536.9 |
| Single Read<Transform>, random order | 10,050 | 0.211 | 42.2 |
| Transform read+write, all entities | 50,250 | 3.905 | 77.7 |
| Query<Rotator, Transform> + read/write | 50,250 | 3.277 | 655.4 |
| World transforms, draw pass (per mesh) | 50,250 | 4.072 | 162.9 |
| World transforms, GetWorldTransform (per mesh) | 50,250 | 13.415 | 536.6 |
| Single Read<Transform>, random order | 50,250 | 1.066 | 42.7 |
| Transform read+write, all entities | 100,500 | 9.199 | 91.5 |
| Query<Rotator, Transform> + read/write | 100,500 | 5.963 | 596.3 |
| World transforms, draw pass (per mesh) | 100,500 | 9.622 | 192.4 |
| World transforms, GetWorldTransform (per mesh) | 100,500 | 26.476 | 529.5 |
| Single Read<Transform>, random order | 100,500 | 3.886 | 77.7 |

Rerun with `out\build\x64-release\RavenEngine\EcsBenchmark.exe` after each M1 step and compare against this table.

### M0 3D baseline record (2026-11-21)

- **Command:** `python scripts/Benchmark-Windows3D.py scale m0-baseline --case nvidia-mailbox`, run from the VS developer shell.
- **Workload:** the 4,000-mesh animated level (800 animated pivots, 4,004 draw calls) with the default watchers, on the RTX 3070 Ti Laptop GPU. Mailbox presentation at 2560 × 1440 on the 165 Hz display.
- **Method:** three 4-second captures after a 1.5 s warm-up, about 460 frames each. Same machine and power settings as the micro-benchmark baseline.
- **Result:** median 114.9 FPS (range 114.3–116.7). CPU preparation was under 5 ms in 56–78% of frames.

| Stage | p50 ms | p95 ms (median of runs) |
| --- | ---: | ---: |
| Gameplay ECS | 1.499 | 1.766 |
| Mesh world transforms | 0.644 | 0.818 |
| Remaining scene gather | 1.032 | 1.330 |
| Renderer preparation | 0.661 | 0.824 |
| Recording and submission | 0.837 | 1.033 |
| GPU pass | 0.495 | 0.757 |
| Queue present call | 3.455 | 6.755 |
| CPU preparation (frame work minus waits) | 4.826 | 5.499 |
| Frame interval | 8.271 | 11.683 |

The first three rows are the M1 target. Together they are about two thirds of CPU preparation. Rendering stages (M2/M3) and the present wait make up the rest.

This run is about 6% slower than the [earlier optimized 4,000-mesh record](Benchmark3D.md#before-and-after) (122.3 FPS, CPU preparation p95 4.81 ms). That record predates the current working-tree changes and may have used different power or thermal conditions, so compare M1 results against this baseline rather than the older one.

Local evidence (ignored): `out/benchmark-scale-u2q92fnr`.

### M1 step 6 3D record (2026-11-21)

Same command, case, machine and method as the M0 3D baseline, run on the step 6 tree.

| Stage | M0 p95 ms | Step 6 p95 ms |
| --- | ---: | ---: |
| Gameplay ECS | 1.766 | 0.038 |
| Mesh world transforms (now the cached update) | 0.818 | 0.143 |
| Remaining scene gather | 1.330 | 0.193 |
| Renderer preparation | 0.824 | 0.742 |
| Recording and submission | 1.033 | 0.921 |
| GPU pass | 0.757 | 0.575 |
| CPU preparation | 5.499 | 1.982 |
| Frame interval | 11.683 | 6.677 |

The median rose from 114.9 to 162.8 FPS (162.7–162.8), and CPU preparation stayed under 5 ms in 100% of frames. The 165 Hz display now limits the frame rate. The remaining CPU time is mostly renderer preparation and recording (M2). Per-system timing for the same run: Interaction 22.4 µs, FreeFly 3.0 µs, MeshRotation 0.1 µs per frame. Local evidence (ignored): `out/m1-step6/out/benchmark-scale-7n252cng`.

## M1 exit record (2026-11-21)

Measured on master (`0fe3f0a` plus the new benchmark case) on the minimum-spec laptop, on an idle machine. The micro-benchmark rows are two Release `EcsBenchmark` rounds.

| Budget | Result | Target | Status |
| --- | ---: | ---: | --- |
| Mutable `View<Transform>` over 100k entities | 0.158 / 0.282 ms | ≤ 0.3 ms | Pass (round 2 close) |
| Two-component view, 50k matches among 100k (scene-scoped `View<Spin, Transform>`) | 0.175 / 0.199 ms (3.5–4.0 ns per match) | ≤ 0.5 ms | Pass |
| Same, unfiltered Registry view | 0.159 / 0.166 ms | — | — |
| World update, 50k entities with 20% changed | 0.580 / 0.624 ms | ≤ 1.0 ms | Pass |
| Typed random `Read`, 100k entities | 16.7 / 15.8 ns | ≤ 50 ns | Pass |
| 4,000-mesh gameplay ECS + world + gather, sum of median p95 | 0.381 ms (0.036 + 0.145 + 0.200) | ≤ 1.96 ms | Pass |

- **4,000-mesh run:** RTX 3070 Ti Laptop, mailbox presentation at 2560 × 1440. Three runs gave 162.3–164.1 FPS (median 162.8), with CPU preparation p95 2.27 ms and under 5 ms in 99.5–100% of frames. Evidence: `out/benchmark-scale-zkxsv9ut`.
- **Reload with retained state:** in the Debug `Raven3DSmoke` run, a compatible edited DLL activated as generation 2 and kept the pickup identity, collected progress and camera selection. A corrupt DLL was rejected without changing the generation, and the edited rule stayed active. Vulkan core and synchronization validation reported nothing. Evidence: `out/validation/windows-3d/x64-debug-bf7bcd47dfcb4d1cb11c9afc7d3cd475`.
- **Live Release session:** `scripts/Test-Windows3DLive.py x64-release` passed. It covers shader, scene, PNG, FBX and game-rule reloads, and rejection of shader syntax/interface errors, malformed scenes and a failed game build with retained state.
  - The game-rule edit was visible 3.99 s after saving; the failed build was rejected after 1.09 s.
  - Its F4 helper now advances until the requested level loads, because the project's scene order includes the benchmark scenes after Collection. Before this fix the script timed out at Benchmark-100, independently of M1.
  - Evidence: `out/live3d-x64-release-c9d646f`.
- **Vulkan validation at scale:** `scripts/Benchmark-Windows3D.py validate --meshes 4000` passed. Evidence: `out/benchmark-validate-tfsuzqs7`.
- **Not run:** the 2D Courtyard fixed-step path. It is built, and the test is on request.

## Migration steps

Each step is one or a few reviewable snippets, and all samples keep working after each step.

1. *(Done 2026-11-21.)* **M0 guards:** add `RegistryTests`, `HierarchyTests` and Registry/world-transform micro-benchmarks against the current code. Record the baseline.
2. *(Done 2026-11-21, `ae263b9`.)* **D1 + D2:** make `Component` an empty, non-virtual base and have the macro generate static information and typed IDs. Template the Registry API on `T`. Storage stays unchanged internally, apart from replacing the string keys. Run the tests, a host rebuild, and a compatible DLL reload.
3. *(Done 2026-11-21.)* **D3:** sparse-set pools behind the same API. Run the tests and benchmarks. Archetypes were not needed: the gate measured 2.1 ms for Transform read+write and 15 ns per random read at 100k entities. The remaining cost is per-entity snapshots and copies, which step 4 removes.
4. *(Done 2026-11-21.)* **D4 + D8 + D9:** views, `TryGet`/`Single`/`Count` and per-slot scene IDs. `SceneAccess::Query` keeps authored order through pool lookups. The interaction rotator/door/goal loops and `MoveSprites` use views. Change ticks move to step 6.
5. *(Done 2026-11-21.)* **D6:** the system table, `FixedUpdate`/`Update` phases and per-system timing. The game DLL's `Update` became five registered systems, and the single entry point is retired. `GameUpdateResult` stays as the fixed-step result in `SystemContext`. Commands (D5) are deferred to first need.
6. *(Done 2026-11-21.)* **D7:** change ticks, cached world transforms and the hierarchy update. Draw gathering uses the cache and `WorldTransformPass` is removed; picking stays live.
7. *(Done 2026-11-21.)* **Exit check:** budgets, the 4,000-mesh benchmark, a compatible and a rejected DLL reload with retained state, and Debug Vulkan validation on the 3D path. Update [EngineModules.md](EngineModules.md) and `AGENTS.md`.

## Contract changes, recorded as each step lands

- Components derive from an empty, non-virtual `ECS::Component` and are stored in their native layout. No virtual functions on component types.
- Generic component access uses `ComponentInfo`/`ComponentRef`. Per-type hooks are registered like systems.
- Borrowed references, spans and views are valid only inside one system call or host phase.
- The host may keep module function pointers only for the loaded generation and clears them before unload.
- Structural changes inside systems go through `Commands`.
- `WorldTransformComponent` is host-managed. Gameplay world queries compose live transforms.

## Out of scope for M1

- Field-level component migration across header changes. This needs the header contract to tolerate component edits and belongs in M5.
- Parallel system execution.
- Archetype storage, unless the D3 gate fails.
- Owning groups, unless the benchmarks require them.
- Reflection for editor UI beyond `ComponentInfo`.
- Prefabs and general serialization (M6).

## Review record (2026-11-20)

| Decision | Outcome |
| --- | --- |
| D1 | Approved as an empty, non-virtual `Component` base. A plain struct without a base was declined. |
| D2–D5, D7, D9 | Approved as written. D5 was later deferred to first need (2026-11-21). |
| D6 | Approved; it states the existing loaded-generation rule for module function pointers. |
| D8 | Deferred on 2026-11-20, then implemented in step 4 (2026-11-21) as per-slot scene IDs after the smoke test's foreign entity showed unfiltered scene views would change gameplay. |
| Budgets | Set from the M0 baseline; the 4,000-mesh stages are the main measurement. |
