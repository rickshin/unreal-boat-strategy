# Lesson 5 — Finding the Way: Pathfinding with A*

Right-click across the map and your destroyer sails *around* three
islands to get there. Nobody wrote "go around the islands" — the boat
figured it out. This lesson is about the single most famous algorithm
in games: **A\*** (say "A-star").

## The world is secretly graph paper

The sim's ocean is a 128×128 grid of cells; each cell is either water
or land (`FSimMap::IsWater`). A route is a chain of neighboring water
cells. Boats can step to 8 neighbors (including diagonals — but not
*through* a land corner; the code checks that).

## First idea: flood fill (BFS)

Imagine pouring dye into the start cell. Each second, the dye spreads
to every neighboring water cell. The moment it touches the goal, the
path the dye took is the shortest route. That's **breadth-first
search** — simple, always correct, but it explores in *every*
direction, even directly away from the goal. Wasteful.

(Fun fact: the game *does* use plain BFS for something — measuring how
far each island cell is from the coast, to give islands their 3D height.
Look at the `Dist` loop in `AIslandActor::Build`. When you need
distances to *everywhere*, BFS is the right tool.)

## A*: flood fill with a sense of direction

A\* explores the most *promising* cell first. Each candidate cell gets
a score:

```
f = g + h
    g = distance already sailed from the start   (known, exact)
    h = straight-line guess of distance to goal  (the "heuristic")
```

Low `f` = promising. The open list is kept as a **heap** (a structure
that can always hand you the smallest item fast), so we repeatedly pop
the most promising cell, look at its neighbors, and update their
scores. Cells fully dealt with go in the **closed set** so we never
redo them. When we pop the goal itself — done; walk the `Parent` chain
backwards to read off the route.

The one rule about `h`: it must **never overestimate** the true
remaining distance. Guess too big and A\* can be fooled into a wrong
"shortcut." Guess honestly (straight-line distance can never be longer
than the real sailing route!) and A\* is *guaranteed* to find a
shortest path — while exploring far fewer cells than BFS.

Now read the real thing: `FSimPathfinder::AStar` in
`Sim/Pathfinder.cpp`. Match each piece to the story: `G` array = dye
distances, `Heur` = the guess, `Open.HeapPush/HeapPop` = the promising
pile, `Closed` = the done set, `Parent` = breadcrumbs.

🧠 **Dad corner** — the heuristic is *octile distance*
(`max(dx,dy) + 0.414·min(dx,dy)`): the exact cost of the best-case
8-direction walk on an empty grid, so it's admissible and tight.
Diagonals cost √2 ≈ 1.4142 to keep geometry honest. `TArray::HeapPush`
maintains a binary heap using `FOpenNode::operator<`. The corner-cutting
check (`D >= 4 && ...`) forbids squeezing diagonally between two land
cells. `Expanded < 20000` is a safety valve against pathological
searches.

## Three tricks that make it feel good

The raw A\* path is a staircase of cell centers. `FindPath` polishes it:

1. **Cache** — the same route request (start cell → goal cell) is
   remembered in a `TMap`, so twenty boats ordered to the same island
   don't each re-run A\*. (`ClearCache` exists because... what could
   make a cached path stale?)
2. **String-pulling** — after A\*, we skip ahead: from each waypoint,
   find the *farthest* later waypoint we can sail to in a straight
   water line (`WaterLineOfSight`), and cut out everything between.
   Staircase becomes smooth legs.
3. **Group spreading** — `GroupOffset(i)` hands each boat in a group
   its own slot in rings around the clicked point (6 in the first ring,
   12 in the next...), so a fleet parks in formation instead of
   fighting over one pixel.

And aircraft? They skip *all of this*. Air units get
`Waypoints = { Dest }` — one straight line, terrain ignored. The
cheapest pathfinding is the path you don't find.

---

## 🔧 Challenges

**Challenge 5.1 — Paper A\*** (do this one with Dad, it's the keeper).
Draw a 6×6 grid. Put S at (1,1), G at (6,6), and a wall of land from
(4,1) up to (4,5) — leaving only (4,6) open. Using 4 directions only
and h = straight-line distance, run A\* by hand for ~6 pops: each
round, circle the open cell with lowest f and expand it. Feel how the
search "flows" up and over the wall without flooding the bottom-right
dead zone.

**Challenge 5.2 — Manhattan navy.**
In `AStar`, delete (or comment out) the four diagonal directions — keep
only the first four entries of `DirX`/`DirY` — and fix the `Step` cost
accordingly. Rebuild and order boats around. Describe how movement
changes. Don't forget the corner-cut check is now pointless — why?
Revert afterwards.

**Challenge 5.3 — Kill the crystal ball.**
Make `Heur` return `0.f`. A\* with no guess is another famous algorithm
— which one (you met it in this lesson under a different name, almost)?
The paths that come back: still shortest, or not? What got worse?
Add a temporary `UE_LOG` of `Expanded` before both returns and compare
numbers for a long voyage with and without the heuristic. Revert.

**Challenge 5.4 — Parade formation.**
In `GroupOffset`, change the ring radius factor `1.4f` to `3.0f`,
rebuild, and send 10+ boats across the map together. What improves?
What gets worse when you send them into a narrow strait? This tension
(spread vs squeeze) is a real RTS design problem — say what *you'd* do
about it, then check the solution for what big games do.
