# Archipelago Command — Naval/Aerial RTS (Unreal Engine 5)

A deterministic real-time strategy game of naval warfare, aircraft combat,
and island expansion — the UE5 / C++ adaptation of the original Python +
pygame-ce game (design preserved in
[docs/ORIGINAL_PYTHON_README.md](docs/ORIGINAL_PYTHON_README.md)).

The port keeps the strict ECS simulation, fixed 20 Hz tick, and
data-driven content of the original, and replaces the 2D renderer with a
full 3D presentation: Gerstner-wave animated ocean, procedurally
generated 3D islands, low-poly boat hulls that pitch and roll on the
swell, aircraft at altitude, and a classic RTS camera + Canvas HUD.

**Everything is C++ — zero Blueprints, zero binary content assets.** The
ocean, islands, ships, structures, sky, lighting and UI are all built at
runtime from code and engine built-ins, so the repo stays diffable and
the project opens clean on a fresh UE 5.x install.

## How to build & run

Built and verified against **Unreal Engine 5.8.0 (prebuilt Linux binary)**;
anything 5.4+ should work. `EngineAssociation` in
`ArchipelagoCommand.uproject` is set to `5.8` — edit it (or use your
source-build GUID) if you run a different version.

### 1. Install the engine (Linux, one time)

There is **no Epic Games Launcher on Linux** — download the engine zip
directly from <https://www.unrealengine.com/en-US/linux> (free Epic
account required; ~38 GB zip, ~62 GB extracted). Then:

```bash
unzip -q ~/Downloads/Linux_Unreal_Engine_5.8.0.zip -d ~/UnrealEngine
# the zip has a top-level folder — flatten it so ~/UnrealEngine/Engine/ exists:
mv ~/UnrealEngine/Linux_Unreal_Engine_5.8.0/* ~/UnrealEngine/
rmdir ~/UnrealEngine/Linux_Unreal_Engine_5.8.0

# sanity check — both must exist:
ls ~/UnrealEngine/Engine/Binaries/Linux/UnrealEditor
ls ~/UnrealEngine/Engine/Build/BatchFiles/Linux/Build.sh
```

No system compiler is needed: the prebuilt engine bundles its own clang
toolchain and .NET SDK. You do need working Vulkan drivers (the NVIDIA
proprietary driver or `mesa-vulkan-drivers`).

### 2. Build the game module

From this project directory:

```bash
cd ~/Projects/unreal-engine-project
~/UnrealEngine/Engine/Build/BatchFiles/Linux/Build.sh \
    ArchipelagoCommandEditor Linux Development \
    -project="$PWD/ArchipelagoCommand.uproject"
```

First build takes a minute or two (compiles the shared PCH); rebuilds
after code changes are a few seconds. `Result: Succeeded` means you're
done. (Prebuilt engine = no `GenerateProjectFiles.sh` step; that script
only exists in source checkouts.)

### 3. Start the game

```bash
# launch straight into a match vs the AI:
~/UnrealEngine/Engine/Binaries/Linux/UnrealEditor \
    "$PWD/ArchipelagoCommand.uproject" -game -windowed -resx=1600 -resy=900

# …or open the project in the editor and press Play:
~/UnrealEngine/Engine/Binaries/Linux/UnrealEditor "$PWD/ArchipelagoCommand.uproject"
```

The very first launch compiles shaders, so allow a few minutes before
the window shows a picture; later launches are quick. Logs land in
`Saved/Logs/ArchipelagoCommand.log` — a healthy start prints the loaded
factions and a `Match started: seed N, hash …` line.

Session flags (append to the `-game` command line):

```
-Seed=7               # any seed = a new archipelago (printed in the top bar)
-Faction=tempest      # play the swarm faction (default: dominion)
-Difficulty=easy      # gentler AI (easy | normal | hard)
-Spectate             # watch AI vs AI with full vision
-ClassicOcean         # procedural Gerstner sea instead of the Water plugin
-NoMusic              # silence the soundtrack
-MusicVolume=0.3      # soundtrack volume (0..1, default 0.5)
```

The soundtrack is played straight from the mp3 files in
`Audio/soundtrack/` — a shuffled playlist decoded at runtime with the
vendored [minimp3](https://github.com/lieff/minimp3) (CC0) into a
procedural sound wave, so there are still no binary uassets. Drop more
mp3s in that folder and they join the rotation.

Example: replay the exact map we verified, as the swarm, vs a hard AI:

```bash
~/UnrealEngine/Engine/Binaries/Linux/UnrealEditor \
    "$PWD/ArchipelagoCommand.uproject" -game -windowed -resx=1600 -resy=900 \
    -Seed=7 -Faction=tempest -Difficulty=hard
```

**Windows/Mac:** right-click the `.uproject` → *Generate Project Files*,
build the `ArchipelagoCommandEditor` target, open, press Play.

## Controls

| Input | Action |
|---|---|
| Left-drag / left-click | Select units (Shift adds) |
| Right-click | Move; on an enemy: attack; on minimap: move there |
| `A` then click | Attack-move (engage everything on the way) |
| `S` | Stop |
| `1`–`9` (or panel buttons) | Production / build options of the selection |
| Arrow keys, screen edge, minimap click | Camera pan / jump |
| Mouse wheel | Zoom |
| `Space` | Jump to your HQ |
| `Esc` | Cancel placement/attack-move, clear selection, quit |
| `R` (after game over) | New match |

*(WASD camera from the Python build gave way to arrow keys: `A` and `S`
are command hotkeys.)*

Build structures by selecting your builder (Worker Barge / Mobile Command
Vessel), pressing a structure hotkey, and clicking a valid site: mining
rigs go next to resource nodes (kelp = wood, wrecks = iron), outposts and
colonies next to islands — completing one claims the island. Destroy the
enemy command structure to win.

## Architecture: sim / render split

The deterministic simulation is plain C++ (no UObjects) under
`Source/ArchipelagoCommand/Sim/`. It advances on a fixed 20 Hz tick,
fully decoupled from rendering, running the spec-mandated system order:

```
Player input / AI decisions
        │  (FSimCommand objects only — the lockstep-ready boundary)
        ▼
 ┌─ Command System   validate & apply queued player/AI commands
 │  Economy System    construction, mining income, production, repair
 │  AI System         strategic → operational → tactical layers
 │  Movement System   waypoints, chase, attack-move, separation
 │  Combat System     targeting, firing, homing projectiles, damage calc
 │  Fog of War        per-player explored/visible grids (every 5 ticks)
 └─ Cleanup System    free dead entities, island claims, victory check
        │
        ▼
 UE5 render layer (read-only): ocean, islands, unit actors, HUD, decorations
```

Determinism rules enforced throughout: one seeded PCG32 RNG owned by the
sim, entity iteration in ascending ID order (`SimSortedKeys`), all
distances/speeds defined per-second and scaled by the fixed tick, and the
render layer (including fish/rock decorations, which use separate
`FRandomStream`s) never writes simulation state. Same seed + same command
stream ⇒ identical `FSimGame::StateHash()` — the foundation for lockstep
multiplayer.

The UE layer holds one visual actor per sim entity and interpolates
between the last two sim ticks, so 20 Hz simulation renders smoothly at
any frame rate. Ships sample the same wave function the ocean mesh
renders, so hulls ride the visible swell — pure presentation, invisible
to the sim.

## Major classes

| File | Key types | Role |
|---|---|---|
| `Sim/SimTypes.h` | `FSimRng`, `SimSortedKeys`, `FSimHash` | Fixed-tick constants, seeded RNG, deterministic iteration |
| `Sim/SimGame.h/.cpp` | `FSimWorld`, `FSimGame`, `FSimPlayer`, components | Entities as int IDs, per-type component stores, tick order, damage calc, `StateHash()` |
| `Sim/SimContent.h/.cpp` | `FContentDB`, `FUnitTpl`, `FStructTpl`, `FWeaponTpl` | Loads `Content/Data/*.json`; all stats are data-driven |
| `Sim/SimMap.h/.cpp` | `FSimMap`, `FSimIsland`, `FSimResourceNode` | Seeded, point-symmetric archipelago + resource nodes |
| `Sim/Pathfinder.h/.cpp` | `FSimPathfinder` | A* on the water grid, path cache, LOS smoothing, group spread |
| `Sim/Systems.h/.cpp` | `SimSys::Run*` | The tick systems (see flow above) |
| `Sim/SimAI.h/.cpp` | `SimAI::Run`, `FAIState` | Strategic / operational / tactical layers at staggered cadences |
| `ACGameMode.h/.cpp` | `AACGameMode` | Owns the sim, fixed-tick loop, entity-actor sync, environment spawn, restart |
| `OceanActor.h/.cpp` | `AOceanActor` | 3D Gerstner-wave water (ProceduralMesh) + pooled fish decorations |
| `IslandActor.h/.cpp` | `AIslandActor`, `ANodeDecorActor` | Island heightfield meshes, coastal rocks, kelp/wreck node markers |
| `UnitActor.h/.cpp` | `AUnitActor`, `AProjectileActor` | Procedural boat hulls, aircraft, structures; wave bobbing; projectile pool |
| `RTSCameraPawn.h/.cpp` | `ARTSCameraPawn` | Pan/zoom RTS camera |
| `RTSPlayerController.h/.cpp` | `ARTSPlayerController` | Selection, orders, placement; everything becomes an `FSimCommand` |
| `RTSHUD.h/.cpp` | `ARTSHUD` | Canvas HUD: top bar, command panel, radar minimap with fog, health bars, alerts |

Gameplay concepts (unchanged from the original design):

- **Factions** — *Dominion Armada* (durable, expensive, slow: battleships,
  carriers, repair vessels, fortified HQ) vs *Tempest Swarm* (fast, cheap,
  fragile: skimmer swarms, torpedoes, EW disruption). Stats in
  `Content/Data/dominion.json` / `tempest.json`.
- **Economy** — structure-based: mining rigs placed near wood/iron nodes
  extract automatically and deplete the node; no worker harvesting.
- **Expansion** — builders place outposts/colonies adjacent to islands to
  claim them; colonies are secondary production bases.
- **Combat** — centralized damage calc with armor mitigation (6 %/point,
  60 % cap); missiles ignore 30 % armor, torpedoes 50 % but only hit
  surface targets; AA only hits air; EW "disrupt" slows enemy reload.
  All weapons fire simulated homing projectiles.
- **Aircraft** — built by carriers, fly straight ignoring terrain.
- **Victory** — destroy the enemy command structure (HQ).

## Repository layout

```
ArchipelagoCommand.uproject   UE5 project (ProceduralMeshComponent enabled)
Config/                       engine/input/game ini (default map, bindings)
Content/Data/                 faction unit/structure JSON (data-driven content)
Source/ArchipelagoCommand/    game module: UE actors + HUD + game mode
Source/ArchipelagoCommand/Sim/  engine-agnostic deterministic simulation
docs/ORIGINAL_PYTHON_README.md  the original Python design + readme
```

## Adaptation notes & roadmap

Deliberate scope decisions in this first UE5 cut, relative to the
original design document (§ numbers refer to it):

- **Implemented:** ECS sim, two asymmetric factions, structure-based
  economy (§8), island claiming (§9), full combat rules (§10),
  pathfinding with cache + group moves (§12), fog of war with radar
  minimap (§13), three-layer AI (§14), HUD (§15), procedural symmetric
  maps (§16), HQ-kill victory (§17), fish + rock decorations exactly per
  the §18 rules, 20 Hz determinism + state hash (§4, §19). 
- **Views:** Allow the player to select a unit, press, v, an over the shoulder
  view from the perspective of the unit. If they hit v again, overhead
  view comes back. Also, swap out the arrow keys with awsd for moving around
  map.
- **Visuals:** stylized low-poly props over **Epic's Water plugin ocean**
  (done): a `WaterZone` + `WaterBodyOcean` are spawned from C++ at runtime
  with the plugin's ocean material, Gerstner wave asset and underwater
  post-process; islands and a generated seabed register as water terrain
  for shoreline depth blending; boats query the real water surface
  (`TryQueryWaterInfoClosestToWorldLocation`) so hulls ride the rendered
  swell; projectile water impacts spawn Niagara foam bursts. Runtime
  notes discovered the hard way: an ocean spline polygon marks *land*
  (keep it tiny), water body info meshes must be rebuilt via
  `UpdateWaterBodyRenderData()` after spawning (editor builds only —
  packaged builds and `-ClassicOcean` fall back to the procedural
  Gerstner sheet), and the sea needs a seabed or depth-based opacity
  renders it invisible.
- **Deferred (design hooks in place):** ship boarding/infantry (§11) —
  abstract resolution slots naturally into the Combat system; research
  queues (§15); optional victory modes (§17); save/load (§19) — the sim
  is already a self-contained POD snapshot; lockstep networking (§21
  phase 10) — commands are the only mutation channel and `StateHash()`
  verifies sync.
- **Performance (§20):** CPU wave animation and O(n²) target scans are
  fine at the spec's 50–100 units; if you push beyond, move wave
  displacement into a material WPO and add a spatial grid to Combat
  targeting (Movement already has one).

