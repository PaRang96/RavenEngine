# User data

Per-user files that outlive a play session: progress, unlocks, best scores, settings and key bindings. Decided in [GameServicesDesign.md](GameServicesDesign.md#fd-33--save-data-and-user-files) (FD-33) and built in M6C step C2 (2026-11-25).

## Where files live

| Host | Folder |
| --- | --- |
| Player build | The OS per-user folder: Windows `%LOCALAPPDATA%\RavenEngine\<Game>`, macOS `~/Library/Application Support/RavenEngine/<Game>`, Linux `$XDG_DATA_HOME/RavenEngine/<Game>` (default `~/.local/share`) |
| Development (`--game`, with or without `--no-watch`) | `out/User/<Game>/` in the repository, ignored by Git, so test runs never touch a player's profile and wiping `out/` resets them |
| Host without game builds | `User/<Game>/` beside the executable |

The host logs the folder at startup. `RavenEngine` stands in for the studio name until the project file names one.

## What game code sees

`SystemContext::Services.GetUserData()` returns the `UserDataStore` (`Raven/Core/UserData.hpp`):

```cpp
auto& data = context.Services.GetUserData();
auto progress = data.Get<Progress>("Profile");          // defaults when nothing is stored
progress.BestScore = std::max(progress.BestScore, score);
data.Set("Profile", progress);
data.Set("Profile", "Level-2", levelResult);            // one record per key
data.Save("Profile");                                   // an explicit save point
```

- **Records** are values of registered component types (`SystemTable::Component<T>()`), so they follow component rules: plain values and fixed arrays.
- **Keys** let one file hold a record per level, slot or profile. `GetKeys` lists them and `Remove` deletes one.
- **Storage.** Records live in the host, outside the ECS and the game module, so a module reload never loses them.
- **Field migration.** Each read matches stored fields against the current field table, by name, old name (`RAVEN_COMPONENT_ALIASES`) and type. Renamed and added fields migrate as they do in scene files, and a write replaces the record stored under an old type name.
- **Saving.**
  - Changes stay in memory until `Save(file)` or shutdown, when the host saves every changed file.
  - A failed save logs one line, returns false and keeps the data for the next try.

Session state (this run's score, timers) belongs in the ECS, not here ([FD-34](GameServicesDesign.md#fd-34--smaller-services)).

## File formats

Every file is a pipeline: record, encoding, integrity, then crash-safe write. Compression, signing and encryption are later optional stages.

- **Binary** (the default, `<name>.ravendata`):
  - The header is `RVNUDATA`, a version, a reserved word and a 64-bit FNV-1a checksum of the payload.
  - The payload is the generic scene codec, one entity per key, labeled with the key.
  - `RavenSceneTool dump <file>` reads binary user-data files directly. It checks the checksum and prints the text form.
- **Text** (`<name>.ravendata.txt`, chosen with `SetFormat`): the scene text form, so people can read and fix the file by hand. It carries no checksum, because hand edits are expected.
- **Format changes.** A file found in the other format loads, then converts on its next save.

## Safety

- **Crash-safe writes.** Each save copies the current file to `.bak`, writes a temporary file and atomically replaces the target (`Files::WriteAtomic`).
- **Damaged files.** A file that fails its checksum or does not parse is logged and renamed to `.damaged-<UTC time>` (with a counter when several are set aside in the same second), so the evidence survives. Loading then falls back to the other format, then to each `.bak`. When nothing is readable, records take their defaults (FD-06, recoverable).
- **File names** use 1–64 letters, digits, `-` or `_`, so a name can never leave the folder.

## Saved key bindings

- **File.** The host keeps `Bindings.ravendata.txt` with one `InputBindingRecord` per slot of every rebound action, keyed `<context>/<action>#<slot>`. Developer-context bindings are included.
- **Loading.** Saved bindings apply once the game's contexts are registered. Entries for actions that no longer exist are logged and skipped.
- **Saving.** Any rebinding or reset rewrites the file after the frame.

## Guard

`Tests/UserDataTests.cpp` (CPU) covers:
- keyed round trips across store instances;
- invalid file names;
- the text form;
- migration across a renamed type and field;
- `.bak` recovery after a checksum failure;
- defaults when everything is damaged, with every damaged file kept;
- saved bindings, including a developer binding and a reset.

## Developer quick-save

A developer-context pair (FD-33 D, built in C9 on 2026-11-25):
- **Ctrl+F5** snapshots the live world: every entity and registered component, the simulation random numbers, the entity-ID sequence and the fixed tick.
- **Ctrl+F9** restores the snapshot and play continues from that tick. The same seed and input continue the same way, except physics (below).

**Rules:**
- **Storage:** the snapshot is kept in memory for the session and never written to disk. A new Ctrl+F5 replaces it.
- **Scope:** it loads only into the scene it came from. After F4 to another scene, Ctrl+F9 logs a warning and does nothing.
- **Code changes:** component layouts that changed meanwhile migrate by field name, as scene files do.
- **Physics:** it restarts from the components. Positions and velocities carry over; Jolt's contact and sleep state does not, so a sensor touching at the save reports Begin again after the load.
- **Not included:** the profile store, bindings and the game's pause state, which are not world state.

Mid-session world saves for players are designed in FD-33 D and built when a game needs them, on the same capture.
