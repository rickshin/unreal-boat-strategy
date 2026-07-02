# Lesson 4 — Solutions

**4.1 — The fingerprint test.**

```cpp
if (Tick % 600 == 0)
{
    UE_LOG(LogTemp, Log, TEXT("Tick %d state hash: %llu"), Tick, StateHash());
}
```

Two untouched `-Seed=7` runs print **identical** hashes at every
checkpoint — same seed, same commands (none), same universe. In the
third run, hashes match until the moment you issue your first order,
then differ forever after. That's correct: the promise was never "same
seed = same game no matter what," it's "same seed **and same command
stream**." Your mouse clicks are part of the input. In multiplayer both
machines would see your clicks as command slips, so they'd stay
matched.

**4.2 — Catch the crime.**

- **Reason 1:** `for (auto& Pair : G.World.Combat)` iterates in hash-map
  order, which can differ between runs/machines/allocations. If unit 8
  fires before unit 5 on my machine and after it on yours, target HP
  differs mid-tick, focus-fire picks different victims, and the games
  diverge.
- **Reason 2:** `FMath::FRand()` is a *global* RNG that isn't seeded by
  the sim (the render layer and even the engine itself pull numbers
  from global generators unpredictably). The jam chance would differ
  every run — and worse, it wouldn't even be reproducible with the same
  seed. If you want a 10% jam chance, it must be
  `G.Rng.Frand() < 0.1f`, *and* the sorted loop, so every machine rolls
  the same dice in the same order for the same gun.

**4.3 — Slow-motion universe.**
Same speed, chunkier. The game does **not** slow down: speeds are
per-second and each of the 10 ticks now moves things `Speed * 0.1` — 
twice as far, half as often. Total distance per real second is
unchanged; a 10-minute match is still 10 minutes. What you *do* feel:
commands respond up to 100 ms late instead of 50, and combat feels
grainier (reload times round to fewer, coarser tick counts). The
interpolation is doing heroic work — boats still *glide* because the TV
crew blends between the (now farther-apart) tick positions. Turn your
eyes to a projectile to see the chunkiness best. This experiment is
exactly why the "always × SIM_DT" commandment exists: because everything
obeyed it, changing the tick rate changed smoothness, not gameplay.

**4.4 — One second of battleship.**

- Per tick: `1.8 × 0.05 = 0.09` cells.
- After 31 ticks: `31 × 0.09 = 2.79` cells.
- Laggy laptop vs fast PC: **exactly the same place.** The accumulator
  changes *when* ticks run (three scoops in one frame vs one per
  frame), but never *how many* ticks run or what each tick computes.
  After tick #31, both machines have executed the identical 31 steps of
  math. Frame rate affects what you see, never what happened. That's
  the whole trick of the fixed timestep.
