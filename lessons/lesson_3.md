# Lesson 3 — ECS: The Game's Skeleton

Time to meet the referee's filing system. This game uses an
**Entity-Component-System** (ECS) design, and once you see it, you'll
see it everywhere in game development.

## The weird idea: a boat is just a number

In Python you'd probably model a boat like this:

```python
class Boat:
    def __init__(self):
        self.position = (10, 20)
        self.hp = 260
        self.speed = 3.4
    def move(self): ...
    def shoot(self): ...
```

ECS smashes that class into three separate pieces:

- **Entity** — just an ID number. Boat #17. Nothing else. Really.
- **Components** — plain data structs, stored in maps keyed by entity
  ID: *entity 17 has a position*, *entity 17 has health*.
- **Systems** — functions that loop over components and do all the
  behavior. There is no `boat.move()`; there is a Movement *system*
  that moves every entity that has a `Mover` component.

In Python terms, the entire game world is basically:

```python
world = {
    "Pos":    {17: (10, 20), 18: (44, 3)},   # dict per component type
    "Health": {17: {"hp": 260}, 23: {"hp": 2500}},
    "Mover":  {17: {"speed": 3.4}},          # 23 is a building: no Mover!
}
```

Now open `Sim/SimGame.h` and find `struct FSimWorld`. It's exactly that:

```cpp
struct FSimWorld
{
    FEntityId NextId = 1;
    TSet<FEntityId> Alive;

    TMap<FEntityId, FPosC>    Pos;
    TMap<FEntityId, FHealthC> Health;
    TMap<FEntityId, FMoverC>  Mover;
    TMap<FEntityId, FCombatC> Combat;
    ...
```

**What you *are* is what components you have.** A structure is an entity
with `Struct` + `Health` but no `Mover` (buildings don't walk). A
projectile has `Proj` and nothing else. A repair vessel is a boat that
also has a `Repair` component. No inheritance tree, no
`class Battleship(Ship(Unit(GameObject)))` — just mix-and-match data.

## Why do it this way?

1. **Systems stay simple.** Movement doesn't care if it's moving a
   scout or a battleship. If it has a `Mover`, it moves. New unit types
   need *zero* new movement code.
2. **It's fast.** Looping over a packed map of small structs is much
   friendlier to the CPU than chasing scattered Python objects.
3. **It's saveable and hashable.** The world is pure data — you can
   checksum it (our `StateHash`!) or write it to disk without worrying
   about live objects.

## Reading a real system

Open `Sim/Systems.cpp`, function `SimSys::RunCleanup` — the simplest
system. In pseudocode:

```
for every entity that has a Health component (in ID order!):
    if hp <= 0: remember it as dead
for every dead entity:
    release the things it owned (island claims, geyser claims)
    if it was someone's HQ: that player is defeated
    delete all its components
if someone is defeated: the other player wins
```

Note the pattern that every system uses:

```cpp
for (const FEntityId Eid : SimSortedKeys(G.World.Health))
```

`SimSortedKeys` gives the entity IDs **in ascending order**. A `TMap`,
like a Python dict pre-3.7, doesn't promise any particular order — and
if two computers looped in different orders, damage could land in a
different sequence and the games would drift apart. Sorting makes the
loop deterministic. (More on this next lesson.)

🧠 **Dad corner** — `TMap<FEntityId, FPosC>` is a hash map, Unreal's
`unordered_map` equivalent. `SimSortedKeys` (in `SimTypes.h`) is a tiny
function template: it takes a `const TMap<FEntityId, TValue>&` for any
`TValue`, copies the keys out, sorts them, returns them. One definition,
stamped out by the compiler for each component type it's used with —
ordinary template stuff, no metaprogramming voodoo. Cost is O(n log n)
per system per tick, which at ~200 entities is nothing.

## How an entity is born

Find `FSimGame::SpawnUnit` in `Sim/SimGame.cpp`. Watch it work: grab a
fresh ID from `World.Create()`, then `Add` a component to each map the
unit needs — position, owner, health, mover, vision — and only add
`Combat` if the template actually has weapons. A geyser crawler simply
never gets a `Combat` component, which is *why* it can't shoot. Delete
a component and the ability disappears. That's ECS.

---

## 🔧 Challenges

**Challenge 3.1 — Census taker.**
In `FSimGame::Step()` (in `SimGame.cpp`), after the systems run, log the
number of living entities once every 10 seconds (that is: when
`Tick % 200 == 0`). Hints: the count is `World.Alive.Num()`; there's no
log category in that file yet, so use `UE_LOG(LogTemp, Log, TEXT(...))`;
you'll need `#include "Logging/LogMacros.h"` — or just try it, it's
usually already reachable. Watch the census climb as both sides build
armies.

**Challenge 3.2 — Healing waters.**
Give every *unit* (not structures) slow health regeneration: +1 HP per
second, never above MaxHp. Decide which system this belongs in (think:
is it economy-ish? combat-ish?), then write the loop. Remember: per
second means `* SIM_DT` per tick. Test by damaging a boat in a fight,
pulling it back, and watching its health bar crawl up.

**Challenge 3.3 — Paper design (no code).**
You want cargo ships that can carry 50 KiTrin across the map (loaded
at one island, unloaded at another). Design the component: what fields
would `FCargoC` have? Which *existing* systems would need to know about
it, and which new system (if any) would you write? Discuss with Dad;
compare with the solution.

**Challenge 3.4 — Spot the missing component.**
The HQ shoots back when attacked (it has weapons in its JSON), but a
gas extractor doesn't. Using only what you learned about
`SpawnStructure` in `SimGame.cpp`, explain *mechanically* why the
extractor never fires — which component does it not receive, and which
line of code decides that?
