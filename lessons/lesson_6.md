# Lesson 6 — Data-Driven Design: Invent Your Own Unit (No C++!)

Here's a secret about this game: the C++ code doesn't know what a
"Battleship" is. Search all of `Sim/` — the word appears nowhere. The
code only knows *rules*: things have hit points, weapons have ranges,
armor reduces damage. **What things exist** lives entirely in the JSON
files in `Content/Data/` — `dominion.json`, `tempest.json`, and
`vanguard.json` (yes, a whole third faction is just another file).

This is called **data-driven design**, and it's why you can add a whole
new warship to the game today, in ten minutes, without compiling
anything.

## Anatomy of a unit

```json
"destroyer": {
  "name": "Destroyer", "class": "naval", "hp": 520, "armor": 5,
  "speed": 2.6, "vision": 9, "cost": 185, "buildTime": 22,
  "weapons": [
    { "type": "shell", "damage": 22, "range": 7.0, "reload": 1.8, "projSpeed": 15 },
    { "type": "aa", "damage": 7, "range": 6.0, "reload": 0.8, "projSpeed": 20, "targets": "air" }
  ]
}
```

- `class`: `"naval"` (a boat), `"air"` (aircraft), `"harvester"` (the
  flying gas-worker, which doubles as the builder), `"ground"` (geyser
  crawlers).
- `speed`/`range`/`vision` are in **cells per second / cells** — sim
  units, remember lesson 4.
- `weapons` is a list — the destroyer has a main gun *and* an AA gun,
  each with its own reload timer.
- Weapon `type` matters: `shell` (plain), `missile` (ignores 30% of
  armor), `torpedo` (ignores 50%, but `targets: naval` only), `aa`
  (air only), `disrupt` (slows the victim's reload — electronic
  warfare).

A unit only appears in the game if some producer lists it: find
`"produces": [...]` on the HQ and colony, and `"builds": [...]` on
carriers (aircraft) and builders (structures). **That list is also the
1–9 button order in the command panel.**

The loading pipeline, if you want to trace it: `FContentDB::LoadFromDir`
(in `Sim/SimContent.cpp`) reads every `*.json` at startup into
`FUnitTpl`/`FStructTpl` structs, and `SpawnUnit` copies stats from the
template into fresh components. JSON → structs → components. That's the
whole chain.

🧠 **Dad corner** — `SimContent.cpp` is worth a read together as a study
in *boundary code*: it validates at the edge (missing files and bad
JSON fail loudly at startup, not mid-match), fills in defaults for
optional fields (`HasField ? GetNumberField : default` — that pattern is
why deleting `"targets"` in challenge 6.3 works instead of crashing),
and converts stringly-typed data into enums exactly once, so the rest
of the sim never touches a string. Also a good moment for the classic
trade-off talk: data-driven means designers iterate without compiling,
but the compiler can no longer catch a typo'd `"torpedoe"` — it just
silently becomes a default `shell` (look at `ParseWeaponType`'s
fallthrough). Where should that line between code and data sit? Real
studios argue about this constantly.

## The combat math (get your calculator)

All damage flows through one function — `FSimGame::ApplyDamage` in
`SimGame.cpp` (one function so the rules can't disagree with
themselves). The formula:

```
pierce     = 0.30 for missiles, 0.50 for torpedoes, 0 otherwise
effArmor   = armor × (1 − pierce)
mitigation = min(0.60, 0.06 × effArmor)      ← 6% per armor point, capped at 60%
final      = damage × (1 − mitigation)
```

Worked example — patrol boat shell (10 dmg) vs destroyer (armor 5):
mitigation = min(0.6, 0.3) = 30% → final = **7.0** damage.

## Balance: the invisible game design

Numbers aren't free. A rough fairness rule this game's data follows:
**power ≈ cost.** A unit's `Value` (its KiTrin cost) should roughly buy
its combination of toughness (hp × armor effect) and hurt
(damage/reload × range). The Tempest faction *deliberately* pays less
for more speed and less hp — fragile-but-fast is their identity.
When you invent a unit, you're not just editing JSON; you're doing
game design. Playtest it. If you always build it, it's too strong. If
you never do, it's too weak.

---

## 🔧 Challenges

**Challenge 6.1 — Calculator drills.**
(a) Torpedo (40 dmg) hits a battleship (armor 8) — final damage?
(b) Missile (26 dmg) hits the same battleship — final damage?
(c) The same missile hits a scout skimmer (armor 0) — final damage?
(d) Why does the 60% cap exist? Imagine armor 15 without it.

**Challenge 6.2 — Ship it: the Hydra Gunboat.**
Add a brand-new Tempest naval unit: mid-priced, *three* small guns (yes,
three weapon entries — they'll each fire on their own reload). Wire it
into the Hive Bastion's `produces` list, restart (no rebuild!), select
your HQ and build one. Watch it fight: three tracer streams! Balance
target: it should lose 1-on-1 to an Assault Cruiser but beat a Fast
Attack Boat. Tune numbers until that's true.

**Challenge 6.3 — The forbidden bomber.**
Bombers can't hit aircraft (`"targets": "naval"`). Remove that line
from the Dominion bomber, restart, and build bombers + enemy
interceptors situation (spectate mode with `-Spectate -Seed=5` works,
or fight Tempest and wait for their air). What happens in an air battle
now, and why is a 60-damage anti-everything weapon a balance disaster?
Put it back.

**Challenge 6.4 — Design review (paper).**
A friend proposes: `"speed": 6.0, "hp": 900, "armor": 7,`
`"cost": 100`, one torpedo weapon, damage 45,
range 9, reload 1.0. Using the fairness rule and what you know about
existing units, write three sentences on why this breaks the game, and
propose fixed numbers that keep the *idea* (a tanky torpedo boat) fair.

**Challenge 6.5 (stretch) — Armed lighthouse.**
Give the Dominion **outpost** a small AA gun in JSON, restart, and
verify outposts now pepper passing Tempest aircraft. Which C++ line
from lesson 3's solution made this work with zero code changes?
