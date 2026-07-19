# Lesson 8 — Solutions

**8.1 — The rush from hell.**

```cpp
int32 Threshold = 120 + Minutes * 40;
```

First wave arrives around the 1–2 minute mark — a handful of skimmers
or patrol boats. If you hold it (defense platform / your starting
escorts + first production), the rush strategy collapses on its own
economics: the AI spent its early KiTrin on doomed warships instead of
harvesters, so *your* economy is now permanently ahead, and its next waves
(rebuilt from a weaker economy against your growing defense) arrive
smaller relative to you each time. Rushing is all-in: it wins now or it
loses later. This is true in real RTS games, and now you've seen why in
the code — every warship morphed or built is a harvester that wasn't.

**8.2 — The coward patch.**

```cpp
if (H.Hp > H.MaxHp * 0.50f) { continue; }   // was 0.22f
```

What goes wrong: at 50%, ships flee while still *strong* — so in an
even battle, half the AI fleet turns tail mid-fight, the abandoned half
gets focus-fired down (your ships target lowest HP — the fleeing ones
are gone, so healthy ones melt one by one), and the runners come back
later alone, in dribbles, to die individually. Retreat thresholds are a
trade-off: 22% saves a ship that's nearly dead anyway; 50% *loses the
battle to save the ships*. The sergeant's ten lines also have no idea
whether the fight is being *won* — a smarter rule would compare fleet
strengths before fleeing. (Feel free to try writing that as a bonus.)

**8.3 — Kneecap the mayor.**
Failure chain, in order: (1) The AI mines out its home island's 2–3
geysers, usually several minutes in. (2) Income flatlines;
`CanAfford` starts failing in the production step, so queues sit empty.
(3) The general's army value stops growing, never reaches the rising
threshold (`350 + Minutes*120` keeps climbing!), so waves *stop
entirely* — the formula assumed a growing economy. (4) You stroll over
whenever you feel like it. Deepest lesson: the officers depend on each
other through the *world*, not through code — nobody told the general
the mayor was lobotomized; his math just quietly stopped triggering.
Systems fail as ecosystems.

**8.4 — Whose desk?**

- **(a) Strategic.** It's a build decision driven by slow-changing
  information (where attacks happened); the mayor already owns builders
  and placement. (`Pl.LastAttackPos` is even already tracked.)
- **(b) Not the AI at all** — that's target *preference*, which lives in
  the Combat system's targeting loop (like focus-fire does). If you put
  it in the AI, only AI torpedo boats would be smart; in Combat, yours
  are too.
- **(c) Strategic** — army composition is explicitly the mayor's job
  (the `CompositionCursor` loop is where the check would go). Needs
  scouting info, which is a nice wrinkle: the sim would only "know"
  about interceptors it has *seen* (visibility check), or the AI cheats.
- **(d) Tactical** — it's a per-ship, per-moment decision, a smarter
  version of the existing flee rule: "if a friendly repairer is within
  range, don't run." Cheap check, quarter-second cadence, sergeant's
  desk.

**8.5 — Teach the mayor colonies.**
The trap, as promised: `IsValidBuildSite` for `Colony` requires the
island to be **unclaimed** (`OwnerPlayer >= 0` → rejected) — colonies,
like outposts, *claim* their island. So "add a colony to an island we
own" is illegal by the game's own rules. The correct feature: when
rich, claim the next island with a **colony instead of an outpost**
(bonus: colonies produce ships — a forward base). In `Strategic()`,
inside the expansion block, choose the template by wealth:

```cpp
// 2) Expansion: outpost normally, full colony when wealthy.
int32 Harvesters = 0;   // count own units with a Harvest component
for (const FEntityId Eid : SimSortedKeys(G.World.Harvest))
{
    const FOwnerC* O = G.World.Own.Find(Eid);
    if (O && O->Player == Player) { ++Harvesters; }
}
if (Harvesters >= 2)
{
    FName ColonyId;
    for (const auto& Pair : F.Structures)
    {
        if (Pair.Value.Kind == EStructureKind::Colony) { ColonyId = Pair.Key; }
    }
    const bool bRich = Pl.KiTrin >= 300.f && !ColonyId.IsNone();
    const FName ExpandTpl = bRich ? ColonyId : OutpostId;
    if (!ExpandTpl.IsNone())
    {
        const FStructTpl* T = G.Content.Structure(Pl.FactionId, ExpandTpl);
        if (T && G.CanAfford(Player, T->Cost + 40))
        {
            // ... (identical island search + IsValidBuildSite + Build
            //      command as the existing outpost code, using ExpandTpl)
        }
    }
}
```

(Full marks if you extracted the island-search-and-order code into a
small helper instead of copy-pasting it twice.) Test in `-Spectate`:
within a few minutes the richer AI plants Spawning Colonies / Island
Colonies on new islands, and its waves start arriving from *two*
directions — an emergent consequence, since production structures spawn
ships where they stand. One data-informed decision by the mayor changed
the general's war without anyone telling the general. That's layered AI
working as designed — and that's the last lesson before you build your
own feature from scratch.
