# Lesson 9 — Reference Implementation: The Lighthouse

Compare, don't copy. If yours differs but passes the checklist, yours
is correct too.

## Stage 1 — sim vocabulary

`Sim/SimTypes.h`:

```cpp
enum class EStructureKind : uint8 { HQ, Extractor, Outpost, Colony, Defense, Lighthouse };
```

(New value goes at the **end** — the enum is stored as `uint8` in
components; inserting in the middle renumbers existing kinds. It
wouldn't break a fresh game, but it's the habit that never breaks a
save file.)

`Sim/SimContent.cpp`, in `ParseKind`:

```cpp
if (S == TEXT("defense")) return EStructureKind::Defense;
if (S == TEXT("lighthouse")) return EStructureKind::Lighthouse;
return EStructureKind::Outpost;
```

## Stage 2 — rules

`Sim/SimGame.cpp`, `IsValidBuildSite`, in the `switch`:

```cpp
case EStructureKind::Outpost:
case EStructureKind::Colony:
case EStructureKind::Lighthouse:
{
    const int32 Island = Map.IslandNear(At, 2.5f);
    ...
```

`Sim/Systems.cpp`, `CompleteStructure`:

```cpp
if (T->Kind == EStructureKind::Outpost || T->Kind == EStructureKind::Colony
    || T->Kind == EStructureKind::Lighthouse)
{
    // island claiming
```

(If you noticed these two both mean "is an island-claimer" and wrote a
tiny helper — `bool ClaimsIsland(EStructureKind)` in SimTypes.h — extra
credit; next time there's only *one* place to update.)

## Stage 3 — data

`dominion.json` structures:

```json
"lighthouse": {
  "name": "Lighthouse", "kind": "lighthouse", "hp": 250, "armor": 1, "vision": 14,
  "cost": 50, "buildTime": 10
}
```

Builder: `"builds": [..., "defense_platform", "lighthouse"]`

`tempest.json`:

```json
"coral_beacon": {
  "name": "Coral Beacon", "kind": "lighthouse", "hp": 220, "armor": 0, "vision": 14,
  "cost": 45, "buildTime": 9
}
```

Builder: `"builds": [..., "reef_bastion", "coral_beacon"]`

The stage-3 mystery ("it builds but looks like nothing much"): an
unknown kind falls through `BuildStructure`'s switch without adding any
meshes — the actor exists but is *invisible* except its selection ring.
The sim half worked perfectly the whole time; only the costume was
missing. Sim/render separation demonstrated one final time.

## Stage 4 — the look

`UnitActor.cpp`, in `AUnitActor::BuildStructure`:

```cpp
case EStructureKind::Lighthouse:
{
    // Rock base, tall white tower with red band, glowing lamp.
    AddShape(AC_MESH_CYLINDER, FVector(0, 0, 30), FVector(2.4f, 2.4f, 0.7f),
        FLinearColor(0.35f, 0.34f, 0.32f));
    AddShape(AC_MESH_CYLINDER, FVector(0, 0, 330), FVector(0.75f, 0.75f, 5.6f),
        FLinearColor(0.92f, 0.92f, 0.90f));
    AddShape(AC_MESH_CYLINDER, FVector(0, 0, 430), FVector(0.78f, 0.78f, 0.9f),
        FLinearColor(0.75f, 0.12f, 0.10f));
    AddShape(AC_MESH_SPHERE, FVector(0, 0, 640), FVector(0.75f, 0.75f, 0.75f),
        FLinearColor(1.0f, 0.9f, 0.3f));
    RingRadius = 700.f;
    break;
}
```

## Stage 5 — a real playtest note (yours will differ)

At 50 KiTrin, one lighthouse per sea lane is automatic — too automatic;
it's never a *decision*. Raising to 75 put it in genuine competition
with "half a warship," which made placing one feel like
a choice. Kept vision 14: the fun of the unit *is* the absurd sight
range; nerfing that would delete the identity instead of the excess.
General principle: balance by making costs hurt, not by making the
cool thing less cool.

## Determinism check

Two `-Seed=7` runs both printed the same hash — e.g.
`Match started: seed 7, hash 1483...` twice. Expected: we added a new
*kind* of content but no new randomness, no unsorted iteration, no
per-tick constants. If your hashes differ, diff your sim changes
against this file — the usual culprit is testing with a leftover
lesson-edit still in the code (healing waters from 3.2, say).

---

## Course debrief — what you now know

- **Architecture:** sim/render split, the command boundary, why
  determinism is the backbone of RTS multiplayer.
- **C++:** types, headers, structs, references and pointers,
  `TArray`/`TMap`, const-correctness, reading compiler errors —
  learned against 4,600 lines of real code, not toy examples.
- **Patterns:** ECS, fixed timestep + interpolation, seeded RNG, object
  pooling, single source of truth, data-driven content, layered AI.
- **Algorithms:** BFS, A\* with heuristics, and where each belongs.
- **Unreal:** actors, components, UCLASS/UPROPERTY reflection and GC,
  procedural meshes, dynamic material instances, the game framework.
- **Craft:** small stages, compile early, revert experiments, playtest
  with intent, commit with a message that explains *why*.

The game is yours now. The stretch goals are unsolved. The design doc
(`docs/ORIGINAL_PYTHON_README.md`) still lists features nobody has
built — boarding combat, research, saved games, real multiplayer.

Get to work, Commander. 🚢
