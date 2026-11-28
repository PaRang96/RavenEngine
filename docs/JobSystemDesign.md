# Job system: the engine's shared workers

Status: **decided and built on 2026-11-25** as a delegated decision. The owner asked that every design study threads and processes ("if we can make things into multi thread or multi process for better performance, study it", 2026-11-25). The owner also asked to be brought only questions that cost money, change what players or designers see, or change scope. This one does none of those, so it was decided by expandability and business alignment and recorded here. It was step a of the owner's "implement till step d" (2026-11-25).

## Before (checked on 2026-11-25)

| Thread group | Threads | Kind of work |
| --- | --- | --- |
| Main thread | 1 | Game systems, render recording, everything per frame |
| Jolt | CPU count − 1, at most 8 (its `JobSystemThreadPool`) | Compute, inside each physics step |
| Reload service | 1 watcher + 2 workers | File waits, hashing, imports |
| Game build | 1 per build | Waits on an external CMake process |
| Audio | 1 control + 1 device | Device lifecycle, real-time mixing |

C7 adds text shaping, layout and glyph generation. Each subsystem starting its own pool would oversubscribe the laptop's 20 hardware threads, and every pool would sleep while the others are busy.

## Options

| Option | Strengths | Weaknesses |
| --- | --- | --- |
| **A. Keep separate pools** | Nothing to build | Oversubscription grows with every subsystem; no way to keep frame work ahead of background work |
| **B. One shared compute pool (chosen)** | One set of workers sized to the machine. Frame work runs before background work. Jolt joins through its own documented `JobSystemWithBarrier` interface, so Jolt stays unmodified | A plain locked queue is slower per job than Jolt's lock-free queue; measured below |
| **C. A fiber-based task graph** (Naughty Dog's design, enkiTS, Taskflow) | Waits never block a thread; finest-grained parallelism | A large dependency or a large build; fibers complicate debugging and Jolt's waits. Nothing needs it yet |

**Why B wins:** it solves the oversubscription now and lets every later user (C7 text, imports, parallel ECS systems from ECS D9) share the same workers, without a library and without changing Jolt. A fiber scheduler (C) can replace the queue inside `JobSystem` later without changing its users.

## What is built

- **`Private/Core/JobSystem`:** a pool of CPU count − 1 workers, owned by `Application` and passed to its users.
  - **Frame jobs**: work the current frame waits for. Workers take them first. A thread that waits (`ParallelFor`, `Wait`) runs queued frame jobs itself instead of sleeping.
  - **Background jobs**: work nothing waits for this frame, such as glyph generation. They may use every worker but one, so a frame job always finds a free worker.
  - `ParallelFor(count, body)` runs a `noexcept` body over an index range, with the caller taking a share.
  - Owners wait for their own jobs before they are destroyed. The pool is destroyed last.
- **World pass and render-world sync** (FD-27 follow-up, 2026-11-28; [record](RenderingContentDesign.md#fd-27-follow-up--the-world-pass-and-the-sync-on-the-job-system-2026-11-28)): `Scene::UpdateWorldTransforms(workers)` splits each depth of more than 1,024 entities into runs of 1,024 with `ParallelFor`, and `RenderWorld::Sync(..., workers)` places objects that only moved the same way. Runs write only their own entries; structural changes, logging and every other render-world write stay on the main thread, so the results are bit-identical with or without workers.
- **`Private/Physics/JoltJobs`:** Jolt's `JobSystemWithBarrier` implemented on the pool. `PhysicsWorld` takes the host's pool; tests and tools that pass none get a pool of their own (at most 8 workers, as before).
- **Kept on their own threads:** the reload watcher (blocking directory waits), the game build (waits on a process) and audio (real-time device thread, blocking control thread). Their blocking would otherwise hold compute workers. The reload service's two import workers can move onto the pool when imports get heavier; the reload service already orders its own work.

## Processes

Studied with UIDesign.md section 8. Compute work stays in-process: crossing a process boundary costs copies and latency on every frame. Separate processes remain the plan for crash-prone content work (font import, shader compiles, the game build already) and for the later editor/play-session split.

## Measurements

See [the C7 record](GameServicesDesign.md#c7--the-2d-and-ui-early-deliverable-2026-11-25) for the physics step with the shared pool compared with Jolt's own pool, measured on the laptop in Release with `PhysicsBenchmark`.
