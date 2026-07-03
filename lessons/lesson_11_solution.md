# Lesson 11 — Solutions

**11.1 — Paper naval architect.**

| Ship | Cost | Tier | Length | Beam | Height |
|---|---|---|---|---|---|
| Patrol Boat | 65 | 0 | 560 | 560 × 0.30 = **168** | 55 |
| Missile Corvette | 120 | 1 | 890 | 890 × 0.28 = **249** | 73 |
| Battleship | 370 | 2 | 1220 | 1220 × 0.26 = **317** | 91 |

Largest Tempest ship: the **Drone Carrier** (K240 → Tier 1, same 890
hull as the Assault Cruiser at K165 — they tie on tier, so they tie on
size). Does that match importance? Arguably not: the carrier is
Tempest's capital investment but reads no bigger than a cruiser —
a direct consequence of coarse tier buckets. (If you argued the
buckets should be finer, or that carriers deserve a bespoke hull:
correct instinct, that's exactly what options 2 and 3 are for.)

**11.2 — The orphaned funnel.**
Under current costs, **no unit in either faction reaches Tier 3** —
the most expensive ship in the game is the Battleship at K370, and
Tier 3 starts at 420. `git log` shows how: the KiTrin economy commit
("Replace wood/iron economy...") rebalanced all costs to a single
resource; under the old two-resource costs the Battleship's *combined*
value was 160+300 = 460 → Tier 3. The rebalance quietly stranded the
funnel code — no compiler warning, no runtime error, just a code path
no data reaches anymore. This is the "derived attributes drift" from
the Dad corner, caught in the wild.

Two fixes, both legitimate: (a) **change the data's meaning** — lower
the Tier 3 threshold (e.g., `Value < 350 ? 2 : 3`) so battleships
funnel again; (b) **change the code's source of truth** — stop
deriving tiers from cost and put an explicit `"tier": 3` field in the
JSON (declared beats derived once the derivation misleads). A third
answer worth full marks: add a startup log/assert that warns when any
tier bucket is empty, so the *next* rebalance can't drift silently.

**11.3 — Racing stripes.**
In `AUnitActor::InitFor`, after the existing `BuildBoat(...)` call in
the naval branch:

```cpp
			BuildBoat(Length, Beam, 55.f + Tier * 18.f,
				U.bBuilder ? Color * 0.7f + FLinearColor(0.2f, 0.18f, 0.1f) : Color, Tier);

			// Bespoke detail: the patrol boat gets a stripe and a mast.
			if (U.Tpl == TEXT("patrol_boat"))
			{
				AddBox(FVector(0, 0, 62.f),
					FVector(Length * 0.0038f, Beam * 0.0058f, 0.10f),
					FLinearColor(0.95f, 0.95f, 0.95f));
				AddShape(AC_MESH_CYLINDER, FVector(Length * 0.05f, 0, 150.f),
					FVector(0.10f, 0.10f, 1.6f), FLinearColor(0.15f, 0.15f, 0.15f));
			}
```

Notes for self-grading: the check must be on `U.Tpl` (the template
FName), not `DisplayName`; the stripe's box scale derives from
Length/Beam so it stays proportional if the hull formula changes; and
the Destroyer is untouched because its `Tpl` differs — run and verify,
don't assume. (Tempest has no "patrol_boat", so this is Dominion-only —
intended.)

**11.4 — One number, four systems.**
Changing 65 → 115 changes, at minimum:

1. **Your wallet** — patrol boats cost K115; your opening build order
   slows (150 starting KiTrin buys one instead of two).
2. **The hull** — Value 115 crosses the 110 threshold: Tier 1. The
   boat grows to 890 units, gains a deckhouse, and sways less
   (BobDamp derives from Length).
3. **The mayor (strategic AI)** — affordability checks now gate the
   unit harder; the AI's build rotation reaches it less often early.
4. **The general (operational AI)** — army *value* sums unit costs, so
   the same fleet of patrol boats now counts ~77% higher toward the
   attack threshold: the AI attacks with objectively weaker waves.

A designer who "just wanted a bigger boat" intended #2 and got #1, #3
and #4 for free — the general attacking earlier with weaker fleets is
the nastiest, because nothing visibly errors; the AI just plays worse.
That's coupling. (The sergeant is untouched — retreat logic reads HP,
not cost.)

**11.5 — The catamaran.**
Cleanest path: build both hulls into the *same* `FMeshBuilder` so it
stays one mesh section. Extract the seven-vertex hull into a lambda
that takes a Y offset, call it twice with `±Beam * 0.55f` and a
narrower per-hull beam, then bridge the deck:

```cpp
	// Inside BuildBoat, replacing the single-hull block for this ship:
	auto AddHull = [&M, Length, Height](float B2, float YOff)
	{
		const float L2 = Length * 0.5f;
		const FVector Bow(L2, YOff, Height);
		const FVector DeckSR(Length * 0.12f, YOff - B2, Height);
		const FVector DeckSL(Length * 0.12f, YOff + B2, Height);
		const FVector SternR(-L2, YOff - B2 * 0.8f, Height);
		const FVector SternL(-L2, YOff + B2 * 0.8f, Height);
		const FVector KeelF(Length * 0.28f, YOff, -Height * 0.9f);
		const FVector KeelA(-L2 * 0.9f, YOff, -Height * 0.9f);
		M.Tri(Bow, DeckSL, DeckSR);
		M.Quad(DeckSR, DeckSL, SternL, SternR);
		M.Tri(Bow, DeckSR, KeelF);
		M.Quad(DeckSR, SternR, KeelA, KeelF);
		M.Tri(Bow, KeelF, DeckSL);
		M.Quad(DeckSL, KeelF, KeelA, SternL);
		M.Tri(SternR, SternL, KeelA);
	};
	AddHull(Beam * 0.18f, -Beam * 0.55f);
	AddHull(Beam * 0.18f, +Beam * 0.55f);
	// Bridge deck spanning the two hulls:
	M.Quad(FVector(Length * 0.30f, -Beam * 0.55f, Height),
	       FVector(Length * 0.30f, Beam * 0.55f, Height),
	       FVector(-Length * 0.40f, Beam * 0.55f, Height),
	       FVector(-Length * 0.40f, -Beam * 0.55f, Height));
```

Wire it to the Fast Attack Boat with a template check (11.3's trick),
or a `bCatamaran` flag if you want it data-driven (lesson 10's trick).
If your bridge deck renders facing *down*, flip the quad's vertex
order — you know exactly why by now. Extra credit if you noticed the
bridge deck has no underside: from the water nobody can tell, and
knowing which corners you're allowed to cut is also a graphics skill.
