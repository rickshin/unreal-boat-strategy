# Lesson 13 — Solutions

**13.1 — The AA taste test.**
Expected findings: **0 (off)** — hard staircases on every mast and hull
edge; the geyser sparks are crisp but flicker as they cross pixel
boundaries. **1 (FXAA)** — edges soften but so does everything; wave
detail loses its sparkle (FXAA can't tell an edge from detail).
**2 (TAA)** — clean stills, but pan the camera and watch the wave
crests and spark fountains smear; the *fish* are the famous casualty —
small, fast, sub-pixel objects get averaged into invisibility by
temporal history. **4 (TSR)** — close to TAA's clean edges while
keeping motion detail; sparks stay particle-like. This is why the
default changed. (If you found a case where TSR shimmers more than TAA
on the thin extractor tubes: correct, temporal rectification isn't
free — every AA method is a trade.)

**13.2 — Threshold safari.**
The property pair is `bOverride_BloomThreshold` / `BloomThreshold`.
At `3.0`, only pixels brighter than 3.0 bloom: **(a)** the geyser cores
(3.2) *just barely* keep a faint halo, while the extractor collars
(2.6) and harvester intakes (2.2) go flat — the glow hierarchy you
never knew existed becomes visible; **(b)** the sun's water reflection
mostly survives, because specular sun glints are far brighter than any
material color. At `-1`, *everything* blooms — the whole screen gets a
soft dreamy wash (this is how cheap "soft focus" filters work, and why
threshold exists). The lesson: bloom is a *classifier* (bright enough
or not) followed by a blur, and the threshold is the classifier's
opinion of "bright".

**13.3 — Kill the glow, keep the color.**
After normalizing, the geyser core is still perfectly orange — and
completely dead: no halo, no presence at distance, reads like painted
plastic. The one-sentence answer: in an HDR pipeline a color encodes
*how much light*, not just *which* light, so `(3.2, 1.15, 0.10)` and
`(1.0, 0.36, 0.03)` are the same chromaticity at different radiant
energies — and bloom only answers to energy. (Revert! The geysers are
the economy's landmark; they need to be visible from orbit.)

**13.4 — Flare judgment.**
Any justified pick earns full marks. The panel's own verdict for the
record: **0.25** — at 1.0 the flares ghost across the HUD during pans
(post-processing doesn't know the minimap exists — flares happily
overlay UI-adjacent regions and read as smudges), and at 0 the sun
glints lose their sparkle. RTS priority order is readability →
atmosphere → cinema; 0.25 buys atmosphere without taxing readability.
The process point stands regardless: all three screenshots existed
before the opinion did.

**13.5 — The cinematic toggle.**
Route: parse once in `AACGameMode::BeginPlay`
(`bCinematic = FParse::Param(CmdLine, TEXT("Cinematic"))`), expose it
via a getter, and consume it in the three systems:

```cpp
// RTSCameraPawn ctor can't see the GameMode yet - read the flag directly:
const bool bCine = FParse::Param(FCommandLine::Get(), TEXT("Cinematic"));
Camera->PostProcessSettings.BloomIntensity = bCine ? 1.6f : 1.1f;
Camera->PostProcessSettings.LensFlareIntensity = bCine ? 0.6f : 0.25f;
```

```cpp
// ApplyTimeOfDay - force golden hour before the normal roll:
if (FParse::Param(FCommandLine::Get(), TEXT("Cinematic")))
{
	Elevation = Rng.FRandRange(19.f, 24.f);
}
```

Two acceptable architectures: parse the flag independently at each
consumer (shown — simple, three greppable sites) or centralize it on
the game mode and query it (cleaner once a fourth consumer appears).
If you argued for the second *because you expect a fourth consumer*,
you've internalized the whole course: design for the change you can
name, not the one you can imagine.
