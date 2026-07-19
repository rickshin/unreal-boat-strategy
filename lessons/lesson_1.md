# Lesson 1 — The Big Picture: How This Game Is Put Together

Welcome, Commander! Over these 14 lessons you'll learn how **Archipelago
Command** works on the inside — well enough to change it, break it, fix
it, and add your own ideas to it.

You already know Python. C++ is Python's stricter, faster cousin, and
your Dad knows it well — when a lesson says **🧠 Dad corner**, that part
is for him to read and explain.

## The one big idea: the game is two programs in one

Imagine a chess game on TV. There are two completely different jobs:

1. **The referee** keeps the *real* game state: where every piece is,
   whose turn it is, who won. The referee is precise and follows the
   rules exactly.
2. **The TV crew** films the board, adds slow-motion replays and
   dramatic music. The TV crew *never moves a piece*.

Our game is split exactly like that:

| The referee (the **simulation**) | The TV crew (the **render layer**) |
|---|---|
| `Source/ArchipelagoCommand/Sim/` | `Source/ArchipelagoCommand/` (everything else) |
| Where boats *really* are | Pretty 3D boats bobbing on waves |
| Decides all damage and deaths | Health bars, explosions, colors |
| Runs exactly 20 times per second | Runs as fast as your GPU can (60+ FPS) |
| Plain C++, no Unreal magic | Full of Unreal Engine classes |

The **golden rule** of this codebase: *the TV crew may look at the
board, but only the referee may touch it.* Render code reads the sim.
It never writes to it.

## So how does clicking a boat work then?

Through **commands**. When you right-click, the input code doesn't move
the boat. It writes a little order slip — `FSimCommand` — that says
"player 0 wants units {5, 8} to move to (40, 62)" and drops it in the
referee's inbox. On the next tick, the referee reads the slip, checks
it's legal, and moves the boats itself.

This matters for a surprising reason: if two computers start with the
same map and feed in the same order slips, they compute the *exact same
game*, tick for tick. That's how RTS multiplayer works — you don't send
the whole world over the internet, just the order slips!

🧠 **Dad corner** — this is the same shape as patterns he may know from
work: the sim is a pure state machine (`state × commands → state`), the
render layer is a view over it, and commands are the only mutation
channel — think Redux/event-sourcing, or model-view with a command bus.
The sim under `Sim/` deliberately uses no Unreal gameplay classes, no
globals, no static mutable state, and no wall-clock time, so it could
be lifted into a headless server or a unit test unchanged. When helping
debug later lessons, the first question to ask is always: "is this a
referee bug or a TV-crew bug?" — the layer boundary makes that
answerable.

## The heartbeat: 20 ticks per second

Open `Source/ArchipelagoCommand/Sim/SimTypes.h` and find:

```cpp
inline constexpr float SIM_DT = 0.05f;          // 20 Hz
inline constexpr int32 SIM_TICKS_PER_SEC = 20;
```

Every 1/20th of a second, the referee runs these steps **in this exact
order** (look in `FSimGame::Step()` in `Sim/SimGame.cpp`):

```
Commands → Economy → AI → Movement → Combat → Fog of War → Cleanup
```

Each step is called a **system**. Each system is just a function that
loops over things and updates them. That's it. That's the whole engine
of the game.

## Map of the land

```
ArchipelagoCommand.uproject      "this is an Unreal project" (like pyproject.toml)
Config/                          settings: key bindings, default map
Content/Data/dominion.json       every Dominion unit's stats  ← you'll edit these!
Content/Data/tempest.json        every Tempest unit's stats
Source/ArchipelagoCommand/Sim/   THE REFEREE (pure C++, lesson 3-6)
Source/ArchipelagoCommand/       THE TV CREW (Unreal C++, lesson 7)
lessons/                         you are here
```

## How to build and run (you'll do this a lot)

```bash
cd ~/Projects/unreal-engine-project

# rebuild after ANY .cpp/.h change (takes a few seconds):
~/UnrealEngine/Engine/Build/BatchFiles/Linux/Build.sh \
    ArchipelagoCommandEditor Linux Development \
    -project="$PWD/ArchipelagoCommand.uproject"

# play:
~/UnrealEngine/Engine/Binaries/Linux/UnrealEditor \
    "$PWD/ArchipelagoCommand.uproject" -game -windowed -resx=1600 -resy=900
```

JSON changes don't even need a rebuild — just restart the game.

---

## 🔧 Challenges

**Challenge 1.1 — Two worlds from two numbers.**
Run the game twice: once with `-Seed=42` and once with `-Seed=7` added
to the end of the play command. Look at the minimap each time. What's
the same? What's different? Now run `-Seed=42` *twice*. What do you
notice?

**Challenge 1.2 — Find the judge.**
Somewhere in `Sim/Systems.cpp` there is the exact line of code that
decides who *wins the entire game*. Find it. (Hint: search for the word
`Winner`.) In one sentence, what has to happen for a player to lose?

**Challenge 1.3 — Find the order slips.**
Open `Sim/SimGame.h` and find the `FSimCommand` struct. List all the
kinds of orders a player can give (hint: look at `ECmdType` right above
it). Which order do you think gets used the most in a real match?

**Challenge 1.4 — Proof of the golden rule.**
After a match starts, the log file `Saved/Logs/ArchipelagoCommand.log`
contains a line like `Match started: seed 7, hash 9967607612...`. That
giant number is a *fingerprint* of the entire game state. Run the game
twice with `-Seed=7` and compare the hash both times. Then explain to
your Dad: why does it matter that they match?

**Challenge 1.5 (thinking cap) —**
The fish swimming around the map are drawn by the TV crew, not the
referee. What would go wrong if a fish could block a torpedo?

Answers in `lesson_1_solution.md` — but wrestle with them first!
