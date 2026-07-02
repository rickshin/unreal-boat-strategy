# Lesson 7 — Solutions

**7.1 — Tropical vacation.**
In `AOceanActor::Build` (`OceanActor.cpp`):

```cpp
MID->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.02f, 0.45f, 0.42f));  // turquoise
```

(The original deep navy is `0.02, 0.11, 0.22`.) And a few lines below,
the fish:

```cpp
if (FishMID) { FishMID->SetVectorParameterValue(TEXT("Color"), FLinearColor(1.0f, 0.78f, 0.05f)); }  // gold
```

Colors here are 0–1 floats per channel (R, G, B), not 0–255. Notice you
changed *parameters on material instances* — the base material asset is
untouched, which is the whole point of MIDs.

**7.2 — Perfect storm.**

```cpp
const FWave GWaves[3] =
{
    { 68.f, 9000.f, 620.f, ... },   // was 34
    { 32.f, 4200.f, 480.f, ... },   // was 16
    { 16.f, 2100.f, 380.f, ... },   // was 8
};
```

The boats ride the monster swell flawlessly because **the ocean mesh
and the boats call the same function** — `AOceanActor::WaveHeight` is
the *single source of truth* for the water surface. The mesh lifts its
vertices with it; `AUnitActor::UpdateVisual` sets hull height (and tilt,
via `WaveGradient`) with it. Change the function, both change together;
they *cannot* disagree. If boats had their own copy of the wave math,
your edit would have desynchronized them instantly. "One source of
truth" is the same principle as the sim's single `ApplyDamage`.

**7.3 — High-altitude doctrine.**
In `ACTypes.h`:

```cpp
inline constexpr float AC_AIR_ALTITUDE = 5200.f;   // was 2600
```

Aircraft now cruise way up. AA tracers follow, because
`AProjectileActor::UpdateVisual` computes their display height as:

```cpp
case EWeaponType::AA: Z = AC_AIR_ALTITUDE * 0.55f; break;
```

— proportional to the same constant. (Worth noticing: altitude is
*pure theater*. The sim fights in 2D; an AA shell "hits" when its 2D
position reaches the target's 2D position. Height exists only in the
TV crew's world.)

**7.4 — Actor census.**

- **Performance:** spawning an actor means allocating the actor, its
  components, its procedural hull mesh, materials, and registering it
  all with the renderer. A scout weaving along your fog border would
  cause spawn/destroy every few seconds *per enemy unit* — hitching
  exactly during tense moments. `SetActorHiddenInGame` just skips
  rendering; it's nearly free. (Same reasoning as the projectile pool.)
- **Information leak:** state lives on the actor! `AUnitActor` carries
  `HpFraction`, selection state, interpolation history. Destroy and
  respawn, and the enemy battleship pops back *with default state* until
  the next sync — and subtler: `DrawHealthBars` skips hidden actors
  (`if (!Actor || Actor->IsHidden()) continue;`), which is what stops
  the HUD from drawing a floating health bar over enemies inside fog.
  With destroy-respawn, any frame-order mistake between spawn and the
  first fog check briefly renders the enemy — a one-frame wallhack.
  Hiding is not just cheaper; it keeps one continuous, correct object.

**7.5 — Your flag, Commander.**
JSON (restart, no rebuild): in `dominion.json`,

```json
"color": [170, 40, 200],
```

...or whatever suits your admiralty (0–255 here — this one gets divided
by 255 in `SimContent.cpp`; spot the inconsistency with 7.1's floats?
Every codebase has these seams — now you know to read before assuming).

Second funnel in `AUnitActor::BuildBoat` (`UnitActor.cpp`), inside the
existing `if (Tier >= 3)` block:

```cpp
if (Tier >= 3)
{
    AddShape(AC_MESH_CYLINDER, FVector(-Length * 0.36f, 0, Height + 60.f),
        FVector(0.22f, 0.22f, 1.3f), FLinearColor(0.1f, 0.1f, 0.1f));   // funnel
    AddShape(AC_MESH_CYLINDER, FVector(-Length * 0.16f, 0, Height + 60.f),
        FVector(0.22f, 0.22f, 1.3f), FLinearColor(0.1f, 0.1f, 0.1f));   // second funnel
}
```

Only battleships (the sole tier-3 Dominion hull) get the second stack —
tier comes from unit *cost*, so if you invented an expensive unit in
lesson 6, it may have just earned funnels too. Emergent consequences of
data-driven rules: sometimes delightful, occasionally surprising.
