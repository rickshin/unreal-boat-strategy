# Lesson 12 — Solutions

**12.1 — Seed hunting.**
Any seed whose derived elevation lands under 28° counts — they're about
19% of seeds (elevation < 28 means `Sqrt(FRand()) < 10/52`, i.e.
`FRand() < 0.037`… wait, check that: `18 + 52·√u < 28` → `√u < 0.192` →
`u < 0.037`, so ~4% of seeds. If your hunt took a while, that's why —
and that scarcity is a *choice* encoded in the distribution; challenge
12.3 exists because reasonable designers could want it otherwise.) The
fastest hunt: loop seeds and grep the log line rather than eyeballing:

```bash
for S in $(seq 1 40); do
  timeout 30 .../UnrealEditor .../ArchipelagoCommand.uproject -game \
    -Seed=$S -NoInput -NoMusic -NoAmbient >/dev/null 2>&1
  grep -h "golden hour" Saved/Logs/ArchipelagoCommand.log && echo "seed $S"
done
```

(Even faster: copy the three math lines into a tiny Python script and
scan a million seeds in a second — the formula is the same.)

**12.2 — The forbidden version.**
Drawing two numbers from `G.Rng` *before* map generation shifts the
RNG sequence, so **everything downstream changes**: the archipelago
shape (different island positions), the AI's decisions (different map
→ different geysers → different assignments), and therefore the state
hash — same seed number, different game. It wouldn't *desync* two
players (both run the same shifted sequence), but it would break the
promise that seed 7 today equals seed 7 from last week's bug report,
and it would entangle cosmetics with rules forever after.
"The fish" is the trick: fish already use their *own* `FRandomStream`
(lesson 4), so they'd be completely unaffected — the question tests
whether you remember that the render layer never touched the sim's
dice in the first place.

**12.3 — Weighted hours.**

```cpp
	float Elevation;
	if (Rng.FRand() < 0.5f)
	{
		Elevation = Rng.FRandRange(18.f, 28.f);    // golden hour
	}
	else
	{
		Elevation = Rng.FRandRange(28.f, 70.f);    // business hours
	}
```

Which is better? For *this* spec ("half golden hour"), the `if` wins
outright: the 50% is visible in the code, greppable, and tweakable by
a designer who's never heard of inverse transform sampling. `Sqrt` won
earlier because the spec was vaguer ("mostly midday, occasionally
low"). Rule of thumb: use math shaping when you care about a smooth
*curve*, use explicit branches when you care about *quotas*. Clever
compresses; clear communicates. Default to clear.

**12.4 — Moonlight sonata.**

```cpp
	if (Rng.FRand() < 0.1f)
	{
		// Night raid: low silver moon instead of a sun.
		Elevation = Rng.FRandRange(20.f, 35.f);
		SunLight->SetIntensity(0.4f);
		SunLight->SetLightColor(FLinearColor(0.65f, 0.75f, 1.0f));
	}
```

(Also reset intensity/color to daytime values in the non-night branch —
`ApplyTimeOfDay` runs again on every restart, and a "sticky moon" from
a previous match is exactly the kind of bug that teaches you why.)

At night the KiTrin geysers **appear to glow much brighter** — but
their material didn't change. Their color is over-bright (values above
1.0), and bloom spills light around anything that ends up brighter
than its surroundings *after* exposure. In a dim scene the exposure
rises, the geysers tower over everything else on screen, and the bloom
pass smears them into halos. Same trick neon signs pull at dusk in the
real world; lesson 13 explains the pipeline that makes it happen.
