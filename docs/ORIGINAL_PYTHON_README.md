# Archipelago Command — Naval/Aerial RTS

A deterministic real-time strategy game of naval warfare, aircraft combat,
and island expansion, implemented in Python 3.12 + pygame-ce on a strict
ECS architecture. Built from the design spec in the "Design Document"
section below; implementation plan in [plan.md](plan.md), progress and
remaining work in [notes.md](notes.md).

## How to run

```bash
python3 -m venv .venv
.venv/bin/pip install pygame-ce
.venv/bin/python main.py                      # play Dominion vs Tempest AI
.venv/bin/python main.py --faction tempest    # play the swarm faction
.venv/bin/python main.py --spectate           # watch AI vs AI, full vision
.venv/bin/python main.py --seed 7             # any seed = a new archipelago
.venv/bin/python main.py --difficulty easy    # gentler AI (easy|normal|hard)
```

Or simply `./play.sh` (same flags), which also sets up the venv on first run.

Tests (headless, no window needed):

```bash
.venv/bin/pip install pytest
SDL_VIDEODRIVER=dummy .venv/bin/python -m pytest tests/
```

## Controls

| Input | Action |
|---|---|
| Left-drag / left-click | Select units (Shift adds) |
| Right-click | Move; on an enemy: attack; on minimap: move there |
| `A` then click | Attack-move (engage everything on the way) |
| `S` | Stop |
| `1`–`9` | Production / build buttons of the selected unit |
| Arrows / WASD, screen edge, minimap click | Camera |
| `Space` | Jump to your HQ |
| `Esc` | Cancel placement/attack-move, clear selection, quit |
| `R` (after game over) | New match |

Build structures by selecting your builder (Worker Barge / Mobile Command
Vessel), choosing a structure button, and clicking a valid (green) site:
mining rigs go next to resource nodes, outposts/colonies next to islands —
placing one claims the island. Destroy the enemy command structure to win.

## System flow

The simulation advances on a fixed 20 Hz tick, fully decoupled from 60 FPS
rendering. Every tick runs the systems in the spec-mandated order:

```
OS events / AI decisions
        │  (Command objects only — the lockstep-ready boundary)
        ▼
 ┌─ Command System   validate & apply queued player/AI commands
 │  Economy System    construction, mining income, production, repair
 │  AI System         strategic → operational → tactical layers
 │  Movement System   waypoints, chase, attack-move hold, separation
 │  Combat System     targeting, firing, projectiles, central damage calc
 │  Fog of War        per-player explored/visible grids (every 5 ticks)
 └─ Cleanup System    free dead entities, island claims, victory check
        │
        ▼
 Render layer (read-only): terrain, entities, fog overlay, HUD, decorations
```

Determinism rules enforced throughout: one seeded RNG owned by the sim,
entity iteration in ascending ID order, all distances/times defined
per-second and scaled by the fixed tick, and the render layer (including
fish/rock decorations, which use separate RNG streams) never writes
simulation state. Same seed + same command stream ⇒ bit-identical state
(`Game.state_hash()`), which is the foundation for lockstep multiplayer.

## Major classes and concepts

| Module | Key classes / functions | Role |
|---|---|---|
| `game/ecs.py` | `World` | Entities as int IDs, per-type component stores, deterministic `each()` iteration |
| `game/components.py` | `Position, Owner, UnitRef, Health, Mover, Combat, Production, Miner, ResourceNode, Structure, BuildTask, Projectile, Repairer, Vision` | Pure-data components; all behavior lives in systems |
| `game/game.py` | `Game`, `PlayerState` | Owns world/map/players, system order, spawn helpers, victory state, `state_hash()` |
| `game/commands.py` | `MoveCmd, AttackCmd, StopCmd, ProduceCmd, BuildCmd` | The only mutation channel into the sim (UI and AI both use it) |
| `game/content.py` | `ContentDB`, `Template`, `WeaponTemplate`, `Faction` | Loads `game/data/*.json`; all unit/structure/weapon stats are data-driven |
| `game/mapgen.py` | `GameMap`, `Island`, `NodeSpec` | Seeded, point-symmetric archipelago; islands, resource nodes, rock decorations |
| `game/pathfinding.py` | `Pathfinder` | A* on the water grid, shared path cache, group-move target spreading |
| `game/systems/*` | `run(game)` per module | The seven tick systems (see flow above) |
| `game/ai/controller.py` | `AIController`, `AIState` | Schedules the three AI layers at staggered cadences |
| `game/ai/strategic.py` | | Economy planning, miner placement, island expansion, army composition |
| `game/ai/operational.py` | | Fleet staging, attack-wave timing, base defense, retreat-and-rebuild |
| `game/ai/tactical.py` | | Per-unit retreat at low HP (focus fire is in Combat targeting) |
| `game/render/renderer.py` | `Renderer` | Terrain baking, entities, projectiles, fog overlay; read-only |
| `game/render/ui.py` | `UI` | Top bar, command panel, production buttons, minimap, alerts, placement ghost |
| `game/render/decorations.py` | `Decorations`, `Fish` | Render-only fish pool (spec §18: zero gameplay impact) |
| `main.py` | `main()` | Input system, fixed-timestep loop, session setup/restart |

Gameplay concepts:

- **Factions** — *Dominion Armada* (durable, expensive, slow: battleships,
  carriers, repair vessels, fortified HQ) vs *Tempest Swarm* (fast, cheap,
  fragile: skimmer swarms, torpedoes, EW disruption). Stats in
  `game/data/dominion.json` / `tempest.json`.
- **Economy** — structure-based: mining rigs placed near wood/iron nodes
  extract automatically and deplete the node; no worker harvesting.
- **Expansion** — builders place outposts/colonies adjacent to islands to
  claim them; colonies are secondary production bases.
- **Combat** — centralized damage calc with armor mitigation (6 %/point,
  60 % cap); missiles ignore 30 % armor, torpedoes 50 % but only hit naval;
  AA only hits air; EW "disrupt" hits slow enemy reload. All weapons fire
  simulated homing projectiles.
- **Aircraft** — built by carriers, fly straight ignoring terrain.
- **Victory** — destroy the enemy command structure (HQ).

## Repository layout

```
main.py            entry point (window, input, fixed-tick loop)
game/              simulation + render packages (see table above)
game/data/         faction unit/structure JSON (data-driven content)
tests/             determinism + smoke tests
plan.md            implementation plan and phase breakdown
notes.md           progress log and remaining work
```

---

# Design Document

Island rocks (visual decoration)
All previous systems integrated cleanly
No contradictions or overlapping simulation rules
Strict ECS + deterministic + naval/aerial RTS scope preserved

UNIFIED RTS MASTER DESIGN DOCUMENT (FINAL VERSION)

1. CORE GAME VISION
A deterministic real-time strategy game focused on:
Naval warfare
Aircraft combat
Island expansion and control
Strategic resource management
Tactical fleet engagements
All gameplay occurs on:
A single ocean map with islands and airspace
There are no ground battlefield systems.

2. DESIGN PRINCIPLES
The game must be:
Easy to learn
Difficult to master
Fast-paced (10–20 minute matches)
Highly readable in large battles
Fully deterministic (multiplayer-ready)
ECS-based and data-driven

3. TECHNOLOGY STACK (LOCKED)
Python 3.12+
Pygame-CE (rendering layer)
ECS architecture (mandatory)
Event-driven deterministic simulation
Fixed tick rate simulation

4. CORE SIMULATION RULES
Determinism
All systems must be:
Fixed tick-based
Seeded RNG only
Frame-rate independent
Lockstep multiplayer compatible

Simulation Order (ECS Tick)
Input System
Command System
Economy System
AI System
Movement System
Combat System
Fog of War System
Cleanup System

5. WORLD STRUCTURE
Single continuous map containing:
Ocean regions
Multiple islands
Airspace layer above map
No separate maps or dimensions exist.

6. FACTIONS (EXACTLY TWO)
No additional factions are allowed.

FACTION 1: DOMINION ARMADA
Theme
Industrial naval superpower with armored fleets and fortified island control.
Identity
Heavy warships
Aircraft carriers
Missile fleets
Fortified island bases
Strengths
High durability
Strong late-game scaling
Powerful defensive systems
Weaknesses
Slow expansion
Expensive units
Lower mobility
Units (8–10)
Worker Barge
Patrol Boat
Missile Corvette
Destroyer
Battleship
Aircraft Carrier
Interceptor Jet
Bomber Aircraft
Repair Vessel
Defense Platform

FACTION 2: TEMPEST SWARM
Theme
Fast adaptive bio-maritime swarm civilization.
Identity
Hydrofoil fleets
Organic structures
Swarm drones
Agile aircraft
Strengths
Fast expansion
High mobility
Strong map control
Weaknesses
Low durability
Weak sustained combat
Units (8–10)
Scout Skimmer
Fast Attack Boat
Torpedo Skimmer
Assault Cruiser
Drone Carrier
Gunship
Interceptor Aircraft
Strike Bomber
EW Disruptor Craft
Mobile Command Vessel

7. RESOURCE SYSTEM
Primary Resources
Wood
Light organic material used for:
Early expansion
Fast production
Basic structures
Sources:
Kelp forests
Driftwood fields
Island vegetation zones

Iron
Heavy industrial material used for:
Advanced units
Armor systems
Weapons
Defense structures
Sources:
Shipwreck fields
Volcanic islands
Seabed ore deposits

8. ECONOMY SYSTEM (STRUCTURE-BASED)
Core Concept
Economy is driven by:
Mining structures + island control
No worker-based harvesting economy.

Mining Structures
Automatically extract resources
Require placement near resource nodes
Deplete resources over time
Must be defended

Faction Examples
Dominion Armada
Timber Processing Rig (Wood)
Deep-Sea Mining Platform (Iron)
Tempest Swarm
Bio-Assimilation Node (Wood)
Corrosive Harvest Spire (Iron)

9. ISLAND EXPANSION SYSTEM
Players expand by:
Capturing islands
Building settlements
Establishing resource control zones

Settlement Types
Resource Outpost
Early expansion structure
Grants vision and extraction access
Island Colony
Full secondary base
Allows structures and production

Colony Ships
Special ships used to:
Deploy settlements
Claim islands
Enable expansion

10. COMBAT SYSTEM
Supports:
Naval artillery
Missile systems
Torpedoes
Aircraft combat
Anti-air systems

Combat Rules
Centralized damage calculation system
Armor-based mitigation
Projectile simulation
Fully deterministic outcomes

11. INFANTRY SYSTEM (SHIP-BASED ONLY)
Infantry exists only inside ships.
Roles
Boarding combat
Ship defense
Repair and sabotage
Restrictions
No ground movement
No terrain pathfinding
No land combat system

Boarding System
When ships engage:
Boarding phase may trigger
Infantry resolves abstract internal combat
Results affect ship control and systems

12. PATHFINDING SYSTEM
A* grid navigation
Cached path reuse
Group movement support
Local avoidance
No per-frame recomputation

13. FOG OF WAR
Exploration tracking
Visibility system
Radar-style minimap
Hidden units

14. AI SYSTEM (3 LAYERS)
Strategic Layer
Expansion
Tech progression
Economy planning
Operational Layer
Fleet movement
Attack timing
Reinforcements
Tactical Layer
Target selection
Retreat logic
Focus fire
No monolithic AI system allowed.

15. USER INTERFACE
Includes:
Wood / iron display
Unit panels
Command system
Production queues
Research queues
Minimap (radar style)
Health bars
Alerts
Must remain readable in large fleet battles.

16. MAP DESIGN
Single map with:
Multiple islands
Ocean chokepoints
Open sea lanes
Air corridors
Resource clusters
Example maps:
Archipelago Divide
Rift Strait
Crown Atoll

17. VICTORY CONDITIONS
Primary:
Destroy enemy command structure
Optional:
Island control domination
Resource supremacy
Timed victory

18. VISUAL DECORATION SYSTEM (NON-GAMEPLAY)
CRITICAL RULE
Decorations:
Do NOT affect gameplay
Do NOT affect AI
Do NOT affect pathfinding
Do NOT affect combat
Do NOT affect resources

WATER DECORATION: FISH
Purely visual ocean life
Simple looping movement
No collision or interaction
Low update frequency or render-only
Purpose:
Ocean atmosphere
Visual depth
Map readability

ISLAND DECORATION: ROCKS
Static visual elements
No gameplay interaction
Do not block movement unless explicitly defined as terrain edge art
Purpose:
Island shape definition
Visual variety
Terrain readability

PERFORMANCE RULE
Decorations must use:
Static batching
Object pooling (fish only)
No simulation updates in core ECS systems

19. ARCHITECTURE RULES
ECS ONLY (no hybrid inheritance systems)
Data-driven content (JSON/YAML)
Fully deterministic simulation
Stable entity IDs required
Save/load supported

20. PERFORMANCE TARGETS
50–100 units
60 FPS stable
< 4ms simulation budget per frame
Optimizations:
Object pooling
Spatial partitioning
Cached pathfinding
AI scheduling
No per-frame allocations in hot loops

21. DEVELOPMENT ROADMAP
Phase 1: ECS framework
 Phase 2: Economy system
 Phase 3: Building system
 Phase 4: Unit system
 Phase 5: Combat system
 Phase 6: AI system
 Phase 7: Fog of war
 Phase 8: UI system
 Phase 9: Optimization
 Phase 10: Multiplayer deterministic layer

FINAL SUMMARY
This RTS is a:
Naval + aerial warfare strategy game
Island expansion economy system
Deterministic ECS simulation
Two-faction asymmetric design
Python-based indie RTS engine foundation
Fully performance-safe and multiplayer-ready architecture


