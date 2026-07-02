# Lesson 1 — Solutions

**1.1 — Two worlds from two numbers.**
`-Seed=42` and `-Seed=7` give completely different island layouts, but
both maps are *symmetric*: spin the map 180° around its center and it
lands on itself. That's on purpose so neither player gets a luckier
start. Running `-Seed=42` twice gives the **identical** map — the seed
is the recipe, and the same recipe bakes the same cake every time.
(Lesson 4 explains exactly how.)

**1.2 — Find the judge.**
In `Sim/Systems.cpp`, at the bottom of `SimSys::RunCleanup`:

```cpp
if (G.Players[P].bDefeated)
{
    G.Winner = 1 - P;
    ...
}
```

A player is marked `bDefeated` a few lines earlier, when the structure
that dies is their HQ (`if (Eid == G.Players[Player].HqEid)`). So: **you
lose the moment your HQ's hit points reach zero**, and the winner is
`1 - P` — the *other* player (a neat trick: if player 0 loses, `1-0=1`
wins; if player 1 loses, `1-1=0` wins).

**1.3 — Find the order slips.**

```cpp
enum class ECmdType : uint8 { Move, Attack, AttackMove, Stop, Produce, Build };
```

Six kinds of orders: **Move**, **Attack** (a specific enemy),
**AttackMove** (march and fight anything on the way), **Stop**,
**Produce** (build a unit), **Build** (place a structure). In a real
match `Move` is almost certainly the most common — you reposition boats
constantly but only build things occasionally. Notice how small this
list is: *every single thing* that happens in the game funnels through
these six slips.

**1.4 — Proof of the golden rule.**
Both runs print the same hash (`9967607612076615024` for seed 7). Why it
matters: the hash proves the referee is **deterministic** — same seed,
same rules, same result, every time, on every computer. For multiplayer,
two machines each run their own referee and just swap order slips; every
few seconds they can compare hashes, and if the hashes ever differ, the
game knows something desynced. Without determinism you'd have to stream
the entire world state over the network instead.

**1.5 — The fish.**
If a fish could block a torpedo, the fish would be part of the *rules*,
so the referee would have to simulate every fish — position, timing,
everything — identically on all machines and inside the hash. But fish
are drawn by the TV crew using its own random numbers that differ every
run. Two players would see fish in different places, so a torpedo might
hit a fish on your screen and miss it on mine — the games would
disagree about who's still alive. That's a **desync**, the worst bug an
RTS can have. Rule 18 of the design doc exists exactly for this:
decorations must never affect gameplay.
