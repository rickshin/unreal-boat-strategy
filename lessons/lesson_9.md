# Lesson 9 — Capstone: Build the Lighthouse

Final mission, Commander. You're going to add a complete new feature —
a **Lighthouse** structure — touching every layer you've learned:
the sim's rules, the data files, and the 3D world. When it works,
you'll have done real game development, start to finish.

## The feature spec (read like a real design doc)

> **Lighthouse** — a cheap coastal structure with *enormous* vision
> (14 cells), no weapons, low hp. Placed next to an island like an
> outpost, and claims it. Strategic role: an early-warning picket that
> watches sea lanes for incoming waves. Visual: tall white tower with
> a glowing yellow light on top. Both factions get one.

Why this feature is the perfect capstone: it needs a new **structure
kind** (sim enum + parser), **placement rules** (reuse outpost logic —
deciding what to reuse is the skill), **JSON entries** (both factions),
and a **custom visual** (your first from-scratch structure look).

## Battle plan (do it in this order — compile after each stage!)

**Stage 1 — Teach the sim the word "lighthouse."**
- `Sim/SimTypes.h`: add `Lighthouse` to `enum class EStructureKind`.
- `Sim/SimContent.cpp`: teach `ParseKind` to turn the string
  `"lighthouse"` into your new enum value.

Compile. Nothing changed visibly — but the referee now has a word for
it. *(Small stages, always compiling — that's professional rhythm.)*

**Stage 2 — Placement and claiming rules.**
Two places currently treat outposts and colonies as "the island
claimers." Find them and add Lighthouse:
- `FSimGame::IsValidBuildSite` (in `SimGame.cpp`) — the `switch` on
  `T->Kind`. A lighthouse should follow the same rule as
  outpost/colony: must be adjacent to an unclaimed island.
- `CompleteStructure` (in `Systems.cpp`) — the island-claiming `if`.

🧠 **Dad corner** — this is the cost of `switch`-on-enum designs: adding
an enum value means hunting every switch. The compiler can be made to
help: a `switch` with no `default` that handles every enum value will
*warn* when a new value appears. Both sites here use `default:`/plain
`if`, so the hunt is manual — a good moment to discuss the trade-off.
`grep -rn "EStructureKind::Outpost" Source/` finds every candidate
fast.

**Stage 3 — Data.**
Add to **both** faction JSONs (Dominion version shown; invent your own
Tempest name — Coral Beacon?):

- id `lighthouse`, kind `"lighthouse"`, hp ~250, armor 1,
  **vision 14**, cost ~40 wood / 10 iron, buildTime ~10, no weapons.
- Add `"lighthouse"` to each builder's `"builds"` list.

Restart and *test the sim half*: you should already be able to build it
(it'll look like... nothing much? It has no visual case yet — see what
the code does with an unknown kind, that's a bug-hunt lesson in
itself: check `BuildStructure`'s switch in `UnitActor.cpp`). Its huge
vision circle should already punch a hole in your fog and light up the
minimap.

**Stage 4 — The look.**
In `AUnitActor::BuildStructure` (`UnitActor.cpp`), add a
`case EStructureKind::Lighthouse:` — build it from `AddShape`/`AddBox`
parts (lesson 7 gave you the vocabulary):
a wide base, a **tall white cylinder**, and a glowing top — a yellow
sphere (look at how the HQ does its gold sphere). Set a sensible
`RingRadius`. Make it *tall* — it should read from across the map.

**Stage 5 — Playtest like a designer.**
Build lighthouses on the sea lanes toward your base. Does the AI's
attack wave now show up on your minimap 20 seconds earlier? Is 40 wood
too cheap for that power? (What would a pro player do — spam them on
every island? Is that fun or degenerate?) Adjust one number and say
why. There is no solution-file answer to "is it balanced" — welcome to
game design.

## Victory conditions (check yourself)

- [ ] Compiles clean after every stage
- [ ] Lighthouse appears on the builder's button panel with its cost
- [ ] Green/red placement ghost obeys the island rule
- [ ] Building one claims the island (island won't accept an enemy outpost)
- [ ] Fog opens a 14-cell circle; minimap shows it
- [ ] Looks like a lighthouse from max zoom
- [ ] Both factions have one
- [ ] `-Seed=7` twice → same `Match started` hash (you didn't break
      determinism — you touched sim code this time, so *prove* it)

## Stretch goals (pick any, no solutions provided — you're beyond that)

1. **Rotating beam** — a long thin white box on the lighthouse that
   spins slowly. (Where does per-frame visual animation live? Lesson 7
   knows. Careful: TV crew only!)
2. **The mayor buys real estate** — teach the Strategic AI to build a
   lighthouse on its second island (lesson 8.5 taught you the pattern).
3. **Island Domination victory** — new win condition: own 75% of all
   islands for 60 consecutive seconds. (Whose desk? `RunCleanup` does
   the victory check today. You'll need a counter in `FSimGame` — and
   should it be part of `StateHash`? Think hard: is it game state?)

---

When the checklist is green: `git add -A`, `git commit`, and write a
commit message that would make a stranger understand what you built.
You've earned the last solution file — it contains a full reference
implementation to compare against yours. Yours doesn't have to match.
It has to *work* — and you have to be able to explain every line.
