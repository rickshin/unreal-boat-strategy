# Lesson 14 — Bang: How a Gunshot Becomes a Sound

The Patrol Boat's cannon now goes *boom* — and the path from "the sim
decided a shell exists" to "your speakers move air" crosses every
architectural boundary this game has. Tracing it end to end is the
best tour of the whole machine since the capstone. Then you'll add
your own sounds.

## The relay race (four runners, one baton)

**Runner 1 — the sim, which must stay deaf.** The referee cannot play
sounds (lesson 1: bit-identical on every machine; lesson 4: no
platform calls). So when `RunCombat` spawns a projectile, it just drops
a note in the outbox — look for `PushShotEvent` in `Systems.cpp`:

```cpp
G.PushEvent(FSimEvent::EKind::Shot, Player, At, FString(), Tpl);
```

Same pattern as the splash events driving the Niagara foam: an event
is a *fact about the past* ("entity of template X fired at position P"),
not an instruction. The sim doesn't know whether anyone listens — that
indifference is what keeps it deterministic.

**Runner 2 — the data, which names the sound.** Which noise does a
patrol boat make? Not the code's business. In `dominion.json`:

```json
"patrol_boat": { ..., "fireSound": "patrol-boat-cannon", ... }
```

`DrainSimEvents` (the render layer, in `ACGameMode.cpp`) receives the
Shot event, looks up the template it names, and reads `FireSound`. No
entry → silence, gracefully. Add an entry → sound, no compile. You've
seen this move three times now (unit stats, mesh overrides in lesson
10, and now audio): *code implements verbs, data supplies nouns*.

**Runner 3 — the decoder, which runs once.** `PlayEffectSound` decodes
`Audio/effects/<name>.mp3` with the same vendored minimp3 the
soundtrack uses — but into a **cache** (`SfxCache`). First shot pays
the decode; the next thousand reuse the PCM. Note the little trick:
a *failed* decode is cached too, so a missing file warns once instead
of spamming the log sixty times a second.

**Runner 4 — the engine, which places it in space.** Each play creates
a fresh `USoundWaveProcedural` (they're single-consumer queues — two
overlapping bangs can't share one) and hands it to
`SpawnSoundAtLocation` with a shared `USoundAttenuation`: full volume
within ~10 cells, fading to silence at about half the map. Battles you
can't see are battles you faintly *hear* — free situational awareness.
A ±6% random pitch keeps thirty identical mp3s from sounding like a
machine gun made of copy-paste.

🧠 **Dad corner** — two things worth the whiteboard. First, the event
throttles: `ShotBudget = 10` per drain plus a fog-of-war visibility
check before playing. Unbounded fan-out from sim events to engine
objects is how "big battle = slideshow + white noise" happens; budgets
at the consumer are the standard fix, and the *sim* stays exact while
the *presentation* degrades gracefully. Second, the visibility check
means audio respects information boundaries — you cannot hear an
invisible enemy fleet reloading. In a competitive game, sounds leak
intel exactly like rendering does; every presentation channel needs
the same fog rules.

## 🔧 Challenges

**14.1 — Arm the swarm.** Give the Tempest Fast Attack Boat a firing
sound. You may not have a second mp3 — reuse `patrol-boat-cannon` and
verify both factions' skirmishes now crackle. How many lines did this
take, and in which language?

**14.2 — The sound of loss.** When any unit dies, the sim already
emits a `UnitLost` event — and (check `FSimEvent`!) it has a `Tpl`
field nobody fills in. Wire it: fill `Tpl` at the Cleanup system's
death site, add a `"lostSound"` field through content schema → parser
→ JSON → `DrainSimEvents`, and reuse `PlayEffectSound`. You are
re-running runners 1-through-4 solo; the solution has the full diff.

**14.3 — Budget experiment.** Set `ShotBudget` to 1000 and spectate a
big battle, then to 1. Describe both failure modes (one assaults your
ears, one is subtler — listen for *which* shots survive). What does
that tell you about which events should win when a budget is tight?

**14.4 — Ears and fog (paper).** A teammate proposes: "play enemy
cannon sounds even inside fog, but at 20% volume — it's atmospheric!"
Argue both sides in two sentences each, then decide. (There is a
defensible yes — think about what real navies hear over the horizon —
but it must be *designed*, not accidental.)

**14.5 (stretch) — The engine room.** Give the direct-control unit
view an engine hum: a looping sound attached to the followed unit,
volume scaled by the throttle. Ingredients you already own: the
ambient re-queue loop (looping), `SpawnSoundAttached` instead of
AtLocation (following), and `FManualC.Axes.X` is in the sim… but you
need it render-side — which of the four runners carries *continuous*
state, and is an event stream the right vehicle for it? (This design
question is the real challenge; the solution takes a position.)
