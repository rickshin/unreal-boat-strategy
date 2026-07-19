# Lesson 6 — Solutions

**6.1 — Calculator drills.**

- **(a)** Torpedo vs battleship: effArmor = 8 × (1−0.5) = 4 →
  mitigation = min(0.6, 0.24) = 24% → final = 40 × 0.76 = **30.4**.
- **(b)** Missile vs battleship: effArmor = 8 × 0.7 = 5.6 →
  mitigation = 33.6% → final = 26 × 0.664 = **17.3** (about).
- **(c)** Missile vs armor 0: nothing to pierce, mitigation 0 →
  final = **26**. Pierce only helps against armor — against skimmers a
  missile is just an expensive shell.
- **(d)** Without the cap, armor 15 would give 90% mitigation and
  armor 17 full immunity — a battleship that literally cannot be sunk
  by shells. Caps keep every unit killable, so no strategy is a
  complete dead end. (Notice torpedoes vs armor 8 effectively *dodge*
  the cap by shrinking armor before the formula — that's the designed
  counter to heavy Dominion fleets.)

**6.2 — The Hydra Gunboat.** One tuned version (yours may differ —
what matters is the 1v1 results):

```json
"hydra_gunboat": {
  "name": "Hydra Gunboat", "class": "naval", "hp": 240, "armor": 2,
  "speed": 3.6, "vision": 8, "cost": 120, "buildTime": 14,
  "weapons": [
    { "type": "shell", "damage": 7, "range": 5.5, "reload": 0.9, "projSpeed": 16 },
    { "type": "shell", "damage": 7, "range": 5.5, "reload": 0.9, "projSpeed": 16 },
    { "type": "shell", "damage": 7, "range": 5.5, "reload": 0.9, "projSpeed": 16 }
  ]
}
```

And in `hive_bastion`:

```json
"produces": ["spore_harvester", "scout_skimmer", "fast_attack_boat",
             "torpedo_skimmer", "hydra_gunboat", "assault_cruiser",
             "drone_carrier", "gunship", "ew_disruptor"],
```

Sanity math: ~23 dmg/sec raw vs the cruiser's ~12.5 + AA, but the
cruiser's armor 4 shaves 24% off every Hydra shell and it has nearly
double the hp — cruiser wins comfortably. Against a Fast Attack Boat
(150 hp, armor 1), the Hydra's triple stream deletes it in ~7 seconds
while surviving. If your Hydra beat the cruiser, its dps or hp is too
high for 120 total cost. Note it appeared as button **5** — position in
the `produces` array *is* the hotkey.

**6.3 — The forbidden bomber.**
Delete `"targets": "naval"` and the bomb becomes filter `Any`
(`ParseFilter` defaults to Any when the field is missing). Bombers now
one-shot interceptors (60 damage vs 120 hp — two-shot, actually, but
with splash-free homing it feels like deletion) and become the best
anti-air *and* anti-ship unit in the game. Interceptors exist to
counter bombers; if bombers beat their own counter, there's no reason
to build anything else — that's the definition of a balance disaster.
Rock-paper-scissors needs the paper to lose to scissors.

**6.4 — Design review.**
Three problems: (1) It's faster than a scout (6.0) while having
near-battleship toughness (900 hp, armor 7 = 42% mitigation) — nothing
can catch it *or* kill it. (2) Its torpedo out-ranges every weapon in
the game (9 = battleship main gun range) with a 1.0s reload — ~45
armor-piercing dps at max range means it kills anything before taking a
hit. (3) It costs 100 total — cheaper than an Assault Cruiser. It's a
better everything for less. Fair-ish rescue keeping "tanky torpedo
boat": `speed 2.4, hp 380, armor 4, cost 200, torpedo damage 40,
range 6.0, reload 3.2`. Tough for a torpedo platform, but slow, short-
ranged, and priced like the cruiser it competes with.

**6.5 — Armed lighthouse.**

```json
"outpost": {
  "name": "Resource Outpost", "kind": "outpost", "hp": 400, "armor": 3, "vision": 9,
  "cost": 65, "buildTime": 15,
  "weapons": [
    { "type": "aa", "damage": 6, "range": 6.0, "reload": 1.0, "projSpeed": 20, "targets": "air" }
  ]
}
```

The line that made it work (from `SpawnStructure` / `CompleteStructure`):

```cpp
if (T->Weapons.Num() > 0 && ...)
{
    G.World.Combat.Add(Eid, MoveTemp(C));
}
```

The code never asks "is this an outpost?" — it asks "does the data list
weapons?" Because the rule is generic and the content is data, a JSON
edit *gave a building a new ability*. That's data-driven design paying
off exactly as promised.
