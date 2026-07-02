# Lesson 8 — The Enemy Commander: How the AI Thinks

You've been fighting it for seven lessons. Time to read its mind.

## Three officers, not one brain

A single giant "decide everything" function becomes spaghetti fast. The
design doc *forbids* it, and instead the AI is three layers — think of
three officers with different jobs and different rhythms
(`Sim/SimAI.cpp`):

| Officer | Question it answers | How often |
|---|---|---|
| **Strategic** ("the mayor") | What should we *build*? Economy, expansion, army mix | every 2 s |
| **Operational** ("the general") | Where should the *fleet* go? Attack now or keep massing? | every 1 s |
| **Tactical** ("the sergeant") | Should *this one ship* run away? | every 0.25 s |

Look at `SimAI::Run` at the bottom of the file — it's just a scheduler:

```cpp
if ((G.Tick + Offset) % (2 * SIM_TICKS_PER_SEC) == 0) { Strategic(G, Player); }
if ((G.Tick + Offset) % SIM_TICKS_PER_SEC == 0)       { Operational(G, Player); }
if ((G.Tick + Offset) % (SIM_TICKS_PER_SEC / 4) == 0) { Tactical(G, Player); }
```

(The `Offset` staggers the two AIs in spectate mode so they never think
on the same tick — spreading CPU cost. And notice: the AI issues
**the same command slips you do**. `SimAI` builds `FSimCommand`s and
drops them in the same inbox. The AI has no cheat channel into the
referee — it plays the same game, by the same rules, through the same
API.)

## The mayor: Strategic

Read `Strategic()` top to bottom; it's a priority list —

1. **Economy first:** if a builder is free and an unclaimed resource
   node is nearby and affordable → order a mining rig on it. *One
   action per pass, then return* — a small trick that keeps behavior
   calm and debuggable.
2. **Then expansion:** with 2+ miners running, claim the nearest free
   island with an outpost.
3. **Then the army:** keep every production queue busy, rotating
   through affordable options (`CompositionCursor`) so it builds a
   *mix* instead of 40 identical boats, and saving iron before
   splurging on capital ships.

## The general: Operational

The heart of it is the **attack threshold**:

```cpp
int32 Threshold = 350 + Minutes * 120;
if (Difficulty == Easy) { Threshold += 250; }
if (Difficulty == Hard) { Threshold -= 120; }
```

The general adds up its army's value (cost of every warship). Below the
threshold: gather at a staging point between home and the front.
Above it: **attack-move the whole fleet at your HQ**. If the wave gets
ground down to a third of the threshold, retreat, rebuild, and set a
timer before trying again. And defense overrides everything: if home
was hit in the last 6 seconds, the entire army boomerangs back.

That one number *is* the AI's personality. Low threshold = reckless
rusher. High = turtle that shows up at minute 15 with a death fleet.

## The sergeant: Tactical

Ten lines: any warship under 22% health that isn't already fleeing gets
a Move order home. That's it. Focus fire — the other classic tactical
behavior — isn't here, because it lives in the Combat system itself
(remember lesson 3: targeting prefers the lowest-HP enemy in range, so
*everyone* focus-fires, including your units).

🧠 **Dad corner** — this is a utility-lite *behavior layering* pattern:
cheap rules at staggered cadences, communicating only through the world
state and command queue (blackboard-style). No behavior trees or
planners needed at this scale — and because it runs *inside* the sim,
every decision is deterministic: same seed, same AI "thoughts."
`FAIState` lives in `FSimGame`, part of the hashed state.

## Reading an AI like a detective

Practical skill: when the AI does something dumb (it will), find
*which officer* owns the mistake. Fleet suicides into a defense
platform? The general's threshold ignores enemy strength. Never
builds a colony? Search Strategic for "colony" — it's simply not
written. AI weirdness is almost never mysterious once you know whose
desk the decision sits on.

---

## 🔧 Challenges

**Challenge 8.1 — The rush from hell.**
Make the AI attack early and often: threshold `120 + Minutes * 40`.
Rebuild and survive it (try `-Difficulty=easy` first — the income
multiplier still applies). Where does the rush strategy *itself*
fall apart if you hold the first wave? Revert.

**Challenge 8.2 — The coward patch.**
Change the sergeant so ships flee at **50%** health instead of 22%.
Play against it. It sounds smarter — preserve your units! Report what
actually goes wrong in fights (watch a battle near the staging point
closely). Revert.

**Challenge 8.3 — Kneecap the mayor.**
Comment out the *entire* expansion block (step 2) in Strategic.
Play a long game. Describe the exact chain of failure: what runs out,
what stops being produced, when the general's waves stop coming. This
teaches the real dependency chain of an RTS economy. Revert.

**Challenge 8.4 — Whose desk? (paper)**
For each behavior, name the officer (or *not the AI at all*!) who
should own it, and one sentence why:
(a) "Build a defense platform at whichever of our islands was attacked
most recently."
(b) "Torpedo boats should prefer shooting battleships over scouts."
(c) "Stop making bombers if the enemy has 10+ interceptors."
(d) "Damaged ships near a repair vessel should stay instead of fleeing
home."

**Challenge 8.5 (stretch, real feature) — Teach the mayor colonies.**
Strategic never builds a Colony even though the data supports it. Add
step 2.5: if the AI owns 2+ islands, has 300+ wood, and a free builder,
place a colony next to an owned island *that doesn't already have one*.
Hints: you can find owned islands via `G.Map.Islands` and
`Isl.OwnerPlayer == Player`; validity is the same
`IsValidBuildSite` call the outpost code uses — but careful, it rejects
claimed islands... read what it checks for `Colony` kind and think
about *which island* you can legally target. This is a genuinely tricky
one — the solution walks through it.
