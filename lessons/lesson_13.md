# Lesson 13 — After the Render: TSR, Bloom, and Lens Flares

The 3D scene you've built — sea, islands, ships — isn't what reaches
the screen. It's the *input* to a second pipeline: **post-processing**,
a chain of image filters that runs on the finished frame every frame.
This lesson tours the stops on that chain that this game actually uses,
including the exposure system that once turned our whole fleet white.

## The chain, in the order it runs

```
3D scene → [exposure] → [bloom] → [lens flares] → [tonemap] → [anti-aliasing] → screen
```

Each stage reads the image the previous stage produced. Everything here
is configured from two places in our codebase — renderer-wide settings
in `Config/DefaultEngine.ini`, and per-camera settings in
`ARTSCameraPawn`'s constructor (`Camera->PostProcessSettings`, a struct
of hundreds of knobs where every knob has a matching `bOverride_` flag;
nothing applies unless its flag is set).

**Exposure** — the camera's pupil. Auto-exposure meters the frame and
brightens/darkens it to a comfortable average. Our war story: a scene
that is mostly dark sea meters *low*, auto-exposure cranks up, and
every light-colored hull clips to white — the "washed out" bug from the
Water-plugin days. The fix is pinned exposure
(`AutoExposureMinBrightness == MaxBrightness`), trading adaptability
for consistency — the right trade for an RTS where readability rules.

**Bloom** — light bleeding past its edges. The filter finds pixels
brighter than a threshold and smears them outward in a soft halo.
This is *the* trick behind every glow in this game: the KiTrin
geysers, extractor collars and harvester intakes have colors above 1.0
(`FLinearColor(3.2f, 1.15f, 0.10f)`) — impossible for a screen, but
after bloom they halo like real light sources. We raised
`BloomIntensity` to 1.1 so they read at RTS distance. No emissive
materials, no lights — just arithmetic on bright pixels.

**Lens flares** — an *artifact simulator*. Real camera lenses scatter
bright light into ghost blobs; games add them back on purpose because
audiences read them as "cinematic." Ours sit at a subtle 0.25 — sun
glints on the water sparkle, without the JJ Abrams treatment.

**Anti-aliasing** — the jagged-edge fix, and the biggest change in this
update. The ini now says `r.AntiAliasingMethod=4`:

| # | Method | One-line verdict |
|---|---|---|
| 0 | none | staircase city |
| 1 | FXAA | blur the stairs (cheap, smeary) |
| 2 | TAA | blend with previous frames (good, ghosts in motion) |
| 4 | **TSR** | TAA's successor: temporal *and* upscaling-aware, far sharper in motion |

TSR matters here because our screen is full of TAA's worst enemies:
thin masts, sub-pixel geyser sparks, and a whole ocean of moving wave
detail that temporal blending loves to smear into mush.

🧠 **Dad corner** — the deep reason over-bright colors work: the scene
is computed in HDR (scene-referred linear radiance, unbounded), and
only the **tonemapper** maps it to the display's 0–1. Bloom runs
*before* tonemapping, so a 3.2-radiance pixel has 3.2× the energy to
smear — "brighter than white" is physically meaningful all the way
down the chain. This is also why exposure interacts with bloom (12.4's
night-glow effect): exposure scales radiance before the threshold.
And TSR vs TAA is worth two minutes: both amortize sampling over time
via jittered history; TSR's contribution is history *rectification*
good enough to upscale from lower internal resolutions — the same
lineage as DLSS/FSR, minus the neural net.

## 🔧 Challenges

**13.1 — The AA taste test.** Launch with `-ExecCmds="r.AntiAliasingMethod 0"`,
then 1, 2, 4 (or type them into the console at runtime if you've
enabled it). Take the same `-AutoShotAt` screenshot each time and
compare the geyser sparks and mast edges. Which method makes the fish
disappear entirely?

**13.2 — Threshold safari.** In the camera constructor, override
`BloomThreshold` (find the property and its `bOverride_` twin in
`Scene.h`) to `3.0`, rebuild, and explain what happened to (a) the
geysers, (b) the sun's reflection on the water. Then try `-1` (a
special value: bloom everything). Revert.

**13.3 — Kill the glow, keep the color.** Change the geyser core color
in `IslandActor.cpp` from `(3.2, 1.15, 0.10)` to `(1.0, 0.36, 0.03)` —
the *same hue*, normalized. Screenshot before/after. Write one sentence
on why "orange" and "glowing orange" are different numbers, not
different colors.

**13.4 — Flare judgment (design, no code).** Lens flares at 0.25 vs
1.0 vs 0: capture all three in a sunny unit view. Which would you ship?
There is no correct answer, but there *is* a wrong process — deciding
without looking. Justify your pick in two sentences.

**13.5 (stretch) — The cinematic toggle.** Add a `-Cinematic` flag
that pushes the settings toward drama: bloom 1.6, flares 0.6, and (from
lesson 12's stretch) force golden hour. One flag, three systems —
you've built enough of this codebase to route it end to end without
hints now.
