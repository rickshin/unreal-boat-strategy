# Lesson 10 — Your Own Ship Models: Bringing Real 3D Assets Into the Game

Every boat you've sailed so far is a hand-coded wedge of triangles from
`BuildBoat()`. That was a deliberate choice — the whole game builds from
code, nothing to download — but real games are full of *authored*
models: ships with railings, turrets, rigging, texture detail. This
lesson teaches you the full pipeline for replacing the procedural hull
of **one specific unit** with a real 3D model, without touching any of
the others.

## Where models come from

Three honest sources:

1. **You make them.** [Blender](https://www.blender.org) is free and
   professional-grade. A low-poly boat is a genuinely good first
   Blender project (a few hours of tutorial-following).
2. **Free asset packs.** [Kenney.nl](https://kenney.nl) (search "Pirate
   Kit" or "Watercraft") and [Quaternius](https://quaternius.com) give
   away hundreds of low-poly models as **CC0** — public domain, use for
   anything. [Sketchfab](https://sketchfab.com) has filters for CC0 and
   CC-BY downloads.
3. **Marketplaces.** Epic's [Fab](https://fab.com) store (many free
   items each month).

⚠️ **The license matters.** CC0 = do anything. CC-BY = credit the
author (in your README or credits screen). Marketplace licenses usually
allow use *in a shipped game* but not re-sharing the raw files — which
includes pushing them to a public GitHub repo! Read before you commit.

🧠 **Dad corner** — the interchange pipeline: DCC tools (Blender, Maya)
save their own project formats, but engines ingest *interchange*
formats — **FBX** (the old industry workhorse) or **glTF** (the modern
open standard; prefer it when offered). On import, Unreal converts
either into its internal `UStaticMesh` inside a binary `.uasset` — the
engine's cooked, GPU-ready representation. That conversion is why
import happens *in the editor*, not at runtime: it's expensive and
editor-only code (we hit the same wall with the Water plugin's mesh
builder in the ocean upgrade — same principle, `WITH_EDITOR`).

## Road A: import through the editor (the normal way)

1. Open the project in the editor (not `-game`):
   `~/UnrealEngine/Engine/Binaries/Linux/UnrealEditor ArchipelagoCommand.uproject`
2. In the **Content Browser**, make a folder `Models`.
3. Drag your `.fbx`/`.gltf` file in (or click **Import**). Accept the
   defaults; skip materials if the model has none.
4. You now have an asset at the path `/Game/Models/YourShip` —
   `/Game/` is code for "this project's `Content/` folder."
5. C++ can now load it exactly like we load engine shapes:

```cpp
UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Models/YourShip.YourShip"));
```

(Yes, the name appears twice — `Package.ObjectName`. A package file can
hold several objects; for simple imports the object is named like its
package.)

**The ethos question:** this creates a *binary* `.uasset` in the repo —
the first non-reviewable file in `Content/`. That's not wrong, it's a
*trade-off*: every real game repo is full of binary assets (often
managed with Git LFS once they get big). Our "zero binary assets" rule
was a teaching choice, and you're now senior enough to break it
deliberately. Note we already bent it for the soundtrack — but MP3s
stayed playable-from-disk because we vendored a *runtime* decoder.
That's Road B…

## Road B: runtime loading (the ethos-preserving way)

Just like `MusicDecoder.cpp` decodes MP3s at runtime, you *can* parse a
model file at runtime — OBJ is simple enough to parse by hand
(plain text: `v x y z` vertex lines, `f 1 2 3` face lines) and feed
into a `UProceduralMeshComponent`, exactly like `BuildBoat` does with
its hand-made arrays. No editor, no uassets, fully diffable. The cost:
no engine-optimized meshes, no LODs, you write the parser. We won't do
it in this lesson, but challenge 10.5 sketches it — and after lesson 7,
you already know every ingredient.

## The code hook: data-driven mesh overrides

Where do unit visuals come from today? `AUnitActor::InitFor` in
`UnitActor.cpp` — the unit branch computes a size tier and calls
`BuildBoat(...)`. We want:

> *If the unit's JSON says it has a custom mesh, use it; otherwise,
> procedural hull as before.*

Which means touching, in order:

1. **Schema** — teach `FUnitTpl` (in `Sim/SimContent.h`) three new
   optional fields: `MeshPath`, `MeshScale`, `MeshYawDeg`.
2. **Parser** — read them in `SimContent.cpp` (look at how optional
   fields like `repairRate` handle "not present").
3. **Renderer** — in `InitFor`, try the custom mesh first; fall back to
   `BuildBoat` if the path is empty or the load fails (never crash over
   a missing model!).
4. **Data** — add `"mesh": "/Game/Models/YourShip"` to one unit in the
   faction JSON.

Pause on a design smell: *the mesh path lives in the sim's content
database — isn't that render data inside the referee's world?* Yes,
and it's fine: the ContentDB is shared reference data, and only the
render layer ever *reads* the mesh fields. The sim's math never touches
them, so two machines with different models installed would still
compute identical games. (Challenge 10.4 makes you defend this.)

## Orientation, scale, and other gremlins

Every imported model surprises you at least once:

- **Forward axis.** Our game says forward = **+X**. Blender exports
  often face **−Y** or **+Y** → the ship sails sideways. Fix with
  `MeshYawDeg` (usually `90` or `-90`), not by re-exporting.
- **Scale.** UE units are centimeters; a "1 unit = 1 meter" model
  imports 100× too small. Don't guess: read the mesh's real size with
  `Mesh->GetBounds().BoxExtent` (half-sizes!) and compute the scale
  that makes it the length you want.
- **Pivot.** If the model's origin is at its stern instead of its
  center, it will orbit strangely on the waves. Fixable in Blender
  (`Set Origin → Center of Mass`) or by nudging the component's
  relative location.

---

## 🔧 Challenges

**Challenge 10.1 — Acquire a ship.**
Get a CC0 boat model (Kenney's Pirate Kit is perfect), import it in the
editor to `/Game/Models/`, and prove it exists: find the `.uasset` file
under `Content/Models/` in the terminal. Check the license file that
came with the pack — what are you allowed to do with it?

**Challenge 10.2 — The full pipeline.**
Implement the mesh override end-to-end (schema → parser → renderer →
data) and give the **Patrol Boat** your imported model. Requirements:
a missing/wrong path must log a warning and fall back to the procedural
hull, and the selection ring must roughly fit the new model
(hint: `RingRadius`, and `Mesh->GetBounds()` is your friend).

**Challenge 10.3 — The gremlin hunt.**
Your ship is probably sideways and the wrong size. Fix both *from JSON
only* (no re-import): compute the correct `meshScale` so the model is
as long as the old procedural hull (~900 units for a tier-0 boat), and
find the right `meshYaw` by experiment. Show your scale arithmetic.

**Challenge 10.4 — Defend the architecture (paper).**
A skeptical code reviewer says: "You put a *rendering* concern (mesh
path) into the *simulation's* content database. That violates the
sim/render split." Write three sentences defending the design, and name
the concrete test that would prove them wrong about a desync risk.

**Challenge 10.5 (stretch) — Road B for real.**
Write a minimal OBJ loader: parse `v` and `f` lines from a file in
`Audio/../Models/boat.obj` (make a cube in Blender, export OBJ), build
arrays, and feed `Hull->CreateMeshSection_LinearColor` in place of
`BuildBoat`. ~60 lines. The solution sketches it; lesson 7 +
`MusicDecoder.cpp` are your references. If you pull this off, you've
written an asset importer from scratch — most professional game
programmers never have.
