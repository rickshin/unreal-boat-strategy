# Lesson 5 — Solutions

**5.1 — Paper A\*.**
No single "right" trace, but the shape to check: the search hugs the
straight line toward G until it hits the wall at column 4, then the
frontier slides *along* the wall (those cells have growing g but
shrinking h options), pops over the gap at (4,6), and sprints to G.
Cells behind S and in the far corner never get expanded — compare with
BFS, which would have dyed nearly the whole board. If your bottom-left
region stayed uncircled, you ran it right.

**5.2 — Manhattan navy.**

```cpp
const int32 DirX[8] = { 1, -1, 0, 0 };   // just use 4 entries...
const int32 DirY[8] = { 0, 0, 1, -1 };
...
for (int32 D = 0; D < 4; ++D)            // ...and loop to 4
{
    const FIntPoint N(P.X + DirX[D], P.Y + DirY[D]);
    if (!Map->IsWater(N.X, N.Y)) { continue; }
    const float Step = 1.f;              // no diagonals, no 1.4142
```

Boats now sail in L-shaped, right-angle legs, like Manhattan taxis —
and every route not perfectly straight gets noticeably *longer*
(a pure diagonal route costs ~41% more when done as stairs). The
corner-cutting check existed only to stop *diagonal* moves from
slipping between two land cells; with no diagonal moves there's nothing
to cut, so it's dead code. (The string-pulling pass still smooths the
staircase visually — the *distance* penalty remains.)

**5.3 — Kill the crystal ball.**
With `h = 0`, `f = g`: you always expand the *closest-to-start* cell —
that's **Dijkstra's algorithm**, which on an unweighted-ish grid behaves
like the flood-fill/BFS from the start of the lesson. Paths are
**still shortest** (a zero guess never overestimates, so correctness
holds — it's the most pessimistic legal heuristic). What got worse is
work: on a long open-water voyage expect `Expanded` to jump from a few
hundred to many thousands, because the dye floods in all directions
instead of flowing toward the goal. Heuristic = same answer, less work.

Example log placement:

```cpp
if (P == Goal)
{
    UE_LOG(LogTemp, Log, TEXT("A* expanded %d cells"), Expanded);
    ...
```

**5.4 — Parade formation.**
At `3.0f` the fleet spreads into a grand, readable parade — no
shoving, lovely to look at, and big ships stop overlapping visually.
The cost: the formation is now wider than most straits and island gaps,
so boats get assigned target slots *on land or across a channel*, and
`NearestWater` scatters them to odd spots; arrivals look ragged near
coastlines. What big RTS games do: **formation width adapts** — spacing
shrinks when the path squeezes through a chokepoint (or the group moves
in column formation through the gap and re-fans on the far side). Some
also use *flow fields* instead of per-unit paths when groups are huge.
If your answer was "make the spacing depend on how tight the water is,"
you independently invented the real technique. Revert to `1.4f`.
