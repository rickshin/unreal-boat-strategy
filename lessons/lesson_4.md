# Lesson 4 — The Clock and the Dice: Fixed Ticks & Determinism

Two invisible machines make this whole game trustworthy: a **clock**
that always ticks the same, and **dice** that always roll the same.
This lesson is the deepest "why" in the course — take it slow.

## Part 1: The clock

Your GPU might draw 144 frames per second; a laptop might manage 40.
If the game logic ran "once per frame," a battleship would literally
move faster on a faster computer. Old games really had this bug!

So the referee ignores frames completely and ticks at exactly **20 Hz**.
The magic is in `AACGameMode::Tick` (in `ACGameMode.cpp`):

```cpp
Accumulator += DeltaSeconds;          // pour in real time as it passes
while (Accumulator >= SIM_DT)         // every time 0.05s has piled up...
{
    SimGame->Step();                  // ...run exactly one tick
    Accumulator -= SIM_DT;
}
```

Think of a bucket filling with real time; every time it holds 0.05
seconds, we scoop one tick out. Fast computer: many frames per scoop.
Slow computer: sometimes two scoops per frame. Either way the *game*
advances identically.

**The rule this creates:** every speed in the sim is *per second* and
gets multiplied by `SIM_DT` each tick. Look in `RunMovement`:

```cpp
const float Step = M.Speed * SIM_DT;   // cells this tick = cells/sec × 0.05
```

If you ever write a per-tick number without `SIM_DT`, your feature will
break the moment anyone changes the tick rate. (Challenge 4.3 makes you
feel this in your bones.)

**But wait — doesn't 20 FPS look choppy?** It would! That's why the TV
crew *interpolates*: each boat actor remembers where it was last tick
and where it is now, and draws itself somewhere in between based on how
full the bucket is (`Accumulator / SIM_DT`). Look at
`AUnitActor::SetSimState` and `UpdateVisual` in `UnitActor.cpp` — the
sim jumps in steps; the picture glides.

## Part 2: The dice

Computers can't roll real dice. A "random" number generator is actually
a completely predictable math machine: give it a starting number (the
**seed**) and it produces a long, wild-looking, *but fixed* sequence.

Open `Sim/SimTypes.h` and meet `FSimRng` — a tiny generator called
PCG32, about ten lines of bit-twiddling. Seed it with 7 and it will
produce the same sequence of numbers today, tomorrow, and on your
friend's computer. That's not a weakness. That's the *entire point*:

```
same seed → same map → same AI decisions → same game → same StateHash
```

The referee owns **one** `FSimRng` (`FSimGame::Rng`) and *everything*
random in the rules draws from it, in the same order, every run.

The TV crew is *forbidden* from touching those dice. Fish, rocks, and
geyser decorations use Unreal's `FRandomStream` with their own seeds (look at
`OceanActor.cpp`: `FRandomStream FishRng(1337)`), because if drawing a
fish consumed a number from the referee's dice, the sequence would
shift and *the whole game would change because you looked at a fish.*

🧠 **Dad corner** — the state hash (`FSimGame::StateHash` in
`SimGame.cpp`) is FNV-1a folded over tick count, resources, and every
entity's id/position/hp in sorted order. Floats go in bit-for-bit via
`memcpy` to `uint32` — never hash a float by value. It's a fingerprint,
not cryptography. In lockstep multiplayer, peers exchange hashes every
N ticks; a mismatch means a desync happened somewhere in the last N.
Also worth telling the kid: this is why the sim sorts map keys
everywhere, why sim code uses no `static` mutable state, and why
`FMath::FRand()` (a global RNG) never appears inside `Sim/`.

## The three commandments of sim code

1. **Only the referee's dice.** Never `FMath::RandRange` inside `Sim/`.
2. **Always in ID order.** Loop with `SimSortedKeys`, never raw TMap
   order.
3. **Always per-second × SIM_DT.** Never raw per-tick constants.

Break any one of them and the game still *runs* — it just silently
stops being deterministic, which you might not notice for weeks. The
nastiest bugs are the quiet ones.

---

## 🔧 Challenges

**Challenge 4.1 — The fingerprint test.**
Using your lesson-3 census skills: log `StateHash()` every 30 seconds
(`Tick % 600 == 0`). Format a `uint64` with `%llu`. Run the game twice
with `-Seed=7`, *don't touch anything* either time, and compare the
first two or three hashes between runs. Then run once more but move
your starting boats around — which hashes change, and why is that
correct rather than a bug?

**Challenge 4.2 — Catch the crime.**
This innocent-looking change is a determinism *felony*. Explain the two
separate reasons why, without running it:

```cpp
// in RunCombat, instead of the sorted loop:
for (auto& Pair : G.World.Combat)   // reason 1?
{
    if (FMath::FRand() < 0.1f)      // reason 2? ("10% chance to jam!")
        continue;
    ...
```

**Challenge 4.3 — Slow-motion universe.**
In `SimTypes.h`, change `SIM_DT` to `0.1f` and `SIM_TICKS_PER_SEC` to
`10` (they must agree: 1/0.1 = 10). Rebuild and play. Does the game run
at half speed, or the same speed but chunkier? Why? What did the
interpolation do for you? Put it back afterwards.

**Challenge 4.4 — One second of battleship.**
A battleship moves at 1.8 cells/second. With `SIM_DT = 0.05`, how far
does it move per tick? After exactly 31 ticks? Now answer the real
question: if a laptop lags and the accumulator runs 3 ticks in one
frame, does the battleship end up in a different place than on a fast
PC after the same 31 ticks? Explain.
