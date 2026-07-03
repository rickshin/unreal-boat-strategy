# Lesson 11 — Where Does a Shape Come From? Tracing the Patrol Boat

A deceptively simple question: *"Where is the code or config that
controls the shape of the Patrol Boat?"* There's no `patrol_boat.fbx`,
no model file, nothing in `Content/` — yet there it is on the water,
pointed bow and all. This lesson teaches the detective skill of
**tracing a visual back to its source**, then uses what you find to
reshape ships.

## The trace (do this along with the text — open the files!)

Every trace starts from what you know and follows the calls:

1. *What am I looking at?* A unit → so its actor is `AUnitActor`
   (lesson 7: one visual actor per sim entity).
2. *Where does an actor decide what it looks like?* Its setup function:
   `AUnitActor::InitFor` in `UnitActor.cpp`. Read the unit branch — no
   mention of "patrol boat" anywhere. Instead:

```cpp
const int32 Tier = Value < 110 ? 0 : Value < 250 ? 1 : Value < 420 ? 2 : 3;
const float Length = 560.f + Tier * 330.f;
const float Beam = Length * (0.30f - Tier * 0.02f);
BuildBoat(Length, Beam, 55.f + Tier * 18.f, ..., Tier);
```

3. *So the shape comes from… the unit's cost?!* `Value()` is the
   KiTrin cost from the faction JSON. Patrol Boat costs **K65** →
   **Tier 0** → a bare 560-unit hull. The battleship costs K370 →
   Tier 2 → longer, taller, and decorated.
4. *And the hull itself?* Follow the call to
   `AUnitActor::BuildBoat` — the bottom of the rabbit hole.

## Anatomy of a hull: seven points and a needle

`BuildBoat` places seven vertices and stitches them with triangles:

```
      TOP VIEW  (X = forward →)              SIDE VIEW
                                             deck (z = +Height)
  SternL______DeckSL                          ____________
    |               \                        |            \
    |                >  Bow          KeelA ●-●------● Bow  >
    |______________ /                        keel (z = -0.9 × Height)
  SternR      DeckSR                              KeelF
```

- `Bow` — a single point at the front: that's why the bow is pointed.
- `DeckSL`/`DeckSR` — the widest part of the deck, 12% of the way back.
- `SternL`/`SternR` — the back corners, pinched to 80% of the beam.
- `KeelF`/`KeelA` — a two-point spine *below* the waterline; every hull
  side triangle slopes down to meet it.

The `FMeshBuilder` helper right above does the stitching. Notice two
things you've met before: each triangle gets its **own copies** of its
vertices with one shared face normal (flat shading — crisp low-poly
facets instead of smooth blobs), and the index order is *reversed*
(`{I, I+2, I+1}`) — the winding-order fix from the "boats look like
floating triangles" bug. History lives in code.

Above Tier 0, the same function bolts on engine-primitive extras:
Tier 1+ a deckhouse block, Tier 2+ a second block and a turret
cylinder, Tier 3+ a funnel. Below it all, `RingRadius` (selection
circle) and `BobDamp` (how much the hull rides the swell — big ships
sway less) are *also* derived from Length. One function, whole fleet.

## The design question hiding in line 248

Deriving looks from **cost** is a real RTS technique: in the chaos of a
battle, *silhouette size = investment* is instant readability — you
know at a glance which enemy ship is the expensive one. But derivation
has a price: the number `65` in `dominion.json` now feeds *four*
different systems (affordability, AI army value, hull size, bob
damping). Change it for one reason, and the other three change too —
challenge 11.4 makes you feel this coupling directly.

🧠 **Dad corner** — this is the "derived vs declared" trade-off that
shows up in every schema design. Derived attributes (`size = f(cost)`)
guarantee consistency, prevent drift, and give designers one lever —
but they create hidden coupling, and *data changes can silently orphan
code paths that the derivation used to reach*. There's a live specimen
in this repo: check challenge 11.2. The professional fix is neither
"derive everything" nor "declare everything" but making the derivation
*visible*: a named function (`TierForCost`), a comment at the data
site, or a startup assertion that every tier is reachable.

## Three ways to reshape a ship (know which job needs which)

1. **Edit `BuildBoat`'s vertices** — changes *every* hull. Right when
   the whole fleet's look needs work (sleeker bows for everyone).
2. **A bespoke branch in `InitFor`** keyed on the template id —
   right when *one* unit needs a custom look built from code.
3. **A real 3D model via the lesson-10 mesh override** — right when
   code-built geometry stops being enough.

And remember the wall between worlds: all of this is costume. The sim
treats every ship as a point with a pick radius. You cannot make a
boat *harder to hit* by making it smaller here — and that's a feature.

---

## 🔧 Challenges

**Challenge 11.1 — Paper naval architect.**
Using the formulas from `InitFor`, compute Tier, Length, Beam and
Height for: the Patrol Boat (K65), the Missile Corvette (K120), and
the Battleship (K370). Which Tempest ship is the *largest*, and does
that match its battlefield importance?

**Challenge 11.2 — The orphaned funnel.**
`BuildBoat` has a `Tier >= 3` block that adds a funnel. Go through
*both* faction JSONs and list every unit that reaches Tier 3 under the
current KiTrin costs. What do you find, when did this happen (check
`git log -- Content/Data/dominion.json`), and what are two different
ways to fix it?

**Challenge 11.3 — Racing stripes.**
Give the Patrol Boat — and only the Patrol Boat — a bespoke detail: a
white stripe along the hull (a thin flattened box) and a small mast.
You'll need a template-id check in `InitFor`. Verify the Destroyer is
unchanged.

**Challenge 11.4 — One number, four systems.**
In `dominion.json`, change the Patrol Boat's cost from 65 to **115**
and play. List everything that changed (there are at least four,
across two of the three AI officers, your wallet, and your eyes).
Which of those did a designer who "just wanted a bigger boat" *not*
intend? Revert.

**Challenge 11.5 (stretch) — The catamaran.**
Make the Tempest Fast Attack Boat a twin-hull: two slim hulls side by
side under one deck. Hint: `BuildBoat` writes mesh **section 0** of
the `Hull` component — nothing stops a second section, or building
both hulls into one `FMeshBuilder` with Y offsets. The solution
sketches the cleaner path.
