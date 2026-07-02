# Lesson 7 — The TV Crew: Programming on Top of Unreal Engine

So far we've lived in the referee's world of pure C++. Now we cross
into Unreal territory — the code that turns `{Pos: (40, 62)}` into a
battleship pitching on sunlit waves.

## Unreal's building blocks

- **`AActor`** — a *thing placed in the world*. Our ocean, each island,
  each boat and each projectile is an actor. (Prefix `A`.)
- **`UActorComponent` / `USceneComponent`** — parts bolted onto an
  actor: a mesh, a light, a camera. An actor is mostly a bag of
  components — sound familiar? Unreal had ECS-ish ideas too.
- **`UCLASS()`, `GENERATED_BODY()`, `UPROPERTY()`** — the magic
  stickers. More below.
- Game framework roles: **GameMode** (referee's manager — one per
  match), **PlayerController** (your mouse and keyboard),
  **HUD** (draws 2D over the screen), **Pawn** (a possessable body —
  ours is just a flying camera).

Open `ACGameMode.h` and look at the class line:

```cpp
UCLASS()
class AACGameMode : public AGameModeBase
{
    GENERATED_BODY()
```

🧠 **Dad corner** — those macros aren't templates and aren't standard
C++. Before compiling, Unreal runs its own preprocessor (**UHT**, the
Unreal Header Tool) over every header. It parses `UCLASS`/`UPROPERTY`
markers and *generates additional C++* (the `.generated.h` you see
included, plus `Module.ArchipelagoCommand.gen.cpp` you saw in the build
log). That generated code registers the class with Unreal's
**reflection** system — so the engine can list a class's fields at
runtime, which powers the editor UI, serialization, networking, and the
**garbage collector**. The practical rule to teach: any pointer to a
`UObject` stored in a class member should be marked `UPROPERTY()` (or
be a `TObjectPtr`) so the GC can *see* it — an invisible pointer may
have its object deleted from under it. Raw C++ members (ints, TArrays
of numbers, our whole sim!) don't participate and don't need it. It's
codegen, not metaprogramming — closer to Python decorators + a build
step than to template wizardry.

## How the sim becomes pixels

The bridge is `AACGameMode` (read `SyncEntityActors` in
`ACGameMode.cpp`). After each tick:

1. For every sim entity with a position, ensure a matching `AUnitActor`
   exists (`EntityActorMap`), telling it the new sim state.
2. Destroy actors whose entities died.
3. Hide enemy actors the local player can't see — **fog of war is done
   by hiding actors**, the sim data is all still there.
4. Projectiles reuse a **pool** of actors instead of spawn/destroy
   churn — spawning actors is expensive; recycling is a classic game
   pattern.

Then every *frame*, `UpdateVisuals` interpolates positions (lesson 4)
and lets each actor do its cosmetics.

## Meshes from math

There are no 3D model files in this project. Every visible shape is
either an engine primitive (cube/sphere/cylinder/cone) or a
**procedural mesh** — a list of triangles computed in C++:

- The **ocean** (`OceanActor.cpp`) is a 97×97 grid of vertices whose
  heights follow `WaveHeight()` — three overlapping sine waves. Every
  frame the CPU re-lifts the vertices; normals come from the wave's
  slope so light glints correctly.
- **Islands** (`IslandActor.cpp`) turn the sim's land cells into a
  heightfield: BFS distance-from-coast (lesson 5!) becomes altitude —
  cells far from water rise higher, so islands are naturally
  dome-shaped, and volcanic ones rise steeper.
- **Boats** (`UnitActor.cpp`, `BuildBoat`) are hand-built triangles: a
  pointed bow, a deck, a keel — with bigger tiers adding
  superstructure blocks and funnels. Boats *bob*: `UpdateVisual` samples
  the same `WaveHeight` function as the ocean mesh, so hulls ride the
  visible swell exactly. The sim never knows.

Colors: there are no textures either. One engine material is tinted per
use via a **Material Instance Dynamic** — `MakeMID(this, Color)` — like
one t-shirt design printed in many colors.

## Input → command slips

`ARTSPlayerController` turns your mouse into geometry: it fires a ray
from the camera through the cursor and intersects it with the sea plane
(`CursorOnSea`) to learn *which ocean cell you meant*. Then it fills in
an `FSimCommand` and calls `GM()->QueueCommand(...)`. It never touches
the sim directly — even the UI obeys the golden rule.

---

## 🔧 Challenges

All of these are compile-and-look challenges. Revert each when done
(or keep the ones you like — it's your game now).

**Challenge 7.1 — Tropical vacation.**
Make the ocean Caribbean turquoise. Find where the ocean's material
color is set and change it. While you're in that file: make the fish
**gold**.

**Challenge 7.2 — Perfect storm.**
Double the amplitude of all three waves in `GWaves`. Rebuild and watch
the boats. Explain *why* the boats still sit perfectly on the crazy
waves instead of clipping through them — which design decision from
this lesson saved you?

**Challenge 7.3 — High-altitude doctrine.**
Make aircraft fly at twice the height. One constant. Where do
projectiles aimed at them go — and can you find the line that decides
an AA tracer's altitude?

**Challenge 7.4 — Actor census.**
The fog hides enemy actors with `SetActorHiddenInGame(true)`. Suppose
instead someone "optimized" by *destroying* hidden enemy actors and
respawning them when seen again. It would look identical. Give two
reasons it's worse (hint: one is performance, one is a subtle
information leak the HUD would create... look at what `DrawHealthBars`
skips).

**Challenge 7.5 — Your flag, Commander (mini-project).**
Give your faction a custom look: change the Dominion color in JSON to
your favorite color, then in `BuildBoat`, make **Tier 3 capital ships**
get a second funnel. Screenshot your fleet.
