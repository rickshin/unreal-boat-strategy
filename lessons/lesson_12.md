# Lesson 12 — Painting With Light: Time-of-Day From a Seed

Since the last graphics update, every seed fights at its own hour: some
matches happen under a high noon sun, some in golden-hour side light.
Nobody drew any of it — the *sun's position* is the entire feature.
This short lesson is about how much atmosphere you can buy with one
rotation, and about a determinism trap we deliberately stepped around.

## One rotator, whole mood

Read `AACGameMode::ApplyTimeOfDay` in `ACGameMode.cpp` — it's ~10 lines:

```cpp
FRandomStream Rng(int32(Seed % 0x7fffffff));
const float Elevation = 18.f + 52.f * FMath::Sqrt(Rng.FRand());
const float Azimuth = Rng.FRandRange(0.f, 360.f);
SunLight->SetWorldRotation(FRotator(-Elevation, Azimuth, 0.f));
```

Three things worth unpacking:

**1. Where did the orange go?** Notice what the code *doesn't* do: it
never sets a color. Our sun is flagged as the sky atmosphere's sun
(`SetAtmosphereSunLight(true)` in `SpawnEnvironment`), so Unreal runs
the light through an atmosphere model — a low sun crosses more air, the
blue scatters away (the same Rayleigh scattering that makes the sky
blue), and what survives is warm gold. Physics gives us sunset colors
for free, and they're *consistent* with the sky, clouds, god rays and
water, because they all consume the same atmosphere. One source of
truth again — this time the source is the actual sky.

**2. Why `Sqrt(FRand())`?** A plain `FRand()` spreads elevations
evenly, so a third of all matches would be squint-inducing dawn
battles. Taking the square root of a uniform 0–1 number biases it
toward 1 — mostly sensible midday suns, with the occasional dramatic
low one. Shaping a distribution with a cheap function is a trick
you'll reuse constantly (loot tables, spawn timing, terrain noise).

**3. Why a fresh `FRandomStream` and not the sim's `FSimRng`?**
This is the determinism trap. The lighting must be *per-seed stable*
(two players on the same seed should see the same sky — and they do,
both derive it from the seed). But if we had drawn these two numbers
from the **sim's** RNG, we'd have shifted every subsequent random
decision the referee makes — island shapes, AI choices — changing the
game because of a *cosmetic*. Remember lesson 4's rule: you can't even
*look* at the referee's dice. So the render layer rolls its own dice,
seeded from the same seed. Same look, zero sim impact: the state hash
is identical whatever the light does.

🧠 **Dad corner** — this is "derived presentation state": cosmetics
computed from shared inputs rather than synchronized. It's the same
reasoning as deriving hull size from cost (lesson 11) but across the
sim/render wall: anything both clients can *recompute* from the seed
never needs to be simulated, hashed, or networked. The failure mode to
watch for is someone later making gameplay depend on it ("units hide
better at dusk!") — the moment lighting affects rules, it must move
inside the sim and its RNG.

## 🔧 Challenges

**12.1 — Seed hunting.** Find a golden-hour seed (elevation < 28 logs
"(golden hour)"). Take a unit-view screenshot facing the sun — with the
god rays and light shafts from the same update, this is the prettiest
the game gets. Report your best seed number.

**12.2 — The forbidden version.** Write (on paper, don't run it) what
would go wrong if `ApplyTimeOfDay` used `G.Rng.FRange(...)` instead of
its own stream. Specifically: which of these change — the archipelago
shape, the AI's first build order, the state hash, the fish? Why is
"the fish" a trick question?

**12.3 — Weighted hours.** Change the distribution so ~half of all
matches are golden hour (elevation 18–28°) and half spread over the
rest. Hint: you don't need `Sqrt` — an `if (Rng.FRand() < 0.5f)` choosing
between two ranges is honest and readable. Which do you prefer, and
when does clever beat clear?

**12.4 — Moonlight sonata (stretch).** Add a rare (1-in-10) *night*
match: elevation stays low, sun intensity drops to ~0.4, and its color
is overridden toward cool blue-silver (`SetLightColor`). Check what
happens to the KiTrin geyser glow at night — and explain why it
happens using lesson 13's bloom knowledge once you've read it.
