# Lesson 14 — Solutions

**14.1 — Arm the swarm.**
One line, in JSON — `tempest.json`:

```json
"fast_attack_boat": {
  "name": "Fast Attack Boat", "class": "naval", "hp": 150, "armor": 1,
  "speed": 4.2, "vision": 8, "cost": 50, "buildTime": 8,
  "fireSound": "patrol-boat-cannon",
  ...
```

Zero C++. That's the payoff of runner 2: the pipeline was built once
for the patrol boat, and every other shooter in the game inherits it
for the cost of a JSON field. (Purists may object to a Tempest boat
firing a Dominion cannon sound — correct objection, wrong week; record
a `fast-attack-chatter.mp3` whenever you like.)

**14.2 — The sound of loss.**
Four stops, exactly like the lesson promised:

*Runner 1 — fill `Tpl` at the death site*, `Systems.cpp`,
`RunCleanup`, where `UnitLost` events are pushed (or where deaths are
processed — find the `Dead` loop):

```cpp
		FName LostTpl;
		if (const FUnitC* U = G.World.Unit.Find(Eid)) { LostTpl = U->Tpl; }
		else if (const FStructC* SC = G.World.Struct.Find(Eid)) { LostTpl = SC->Tpl; }
		G.PushEvent(FSimEvent::EKind::UnitLost, Player, G.World.Pos[Eid].P,
			FString(), LostTpl);
```

(If your codebase's Cleanup doesn't currently push UnitLost per death,
adding the push *is* part of the exercise — position must be read
before `World.Destroy(Eid)`.)

*Runner 2 — schema + parser*: `FString LostSound;` beside `FireSound`
in both template structs, and the twin parser lines reading
`"lostSound"`.

*Runner 3+4 — consume*, in `DrainSimEvents`:

```cpp
		if (E.Kind == FSimEvent::EKind::UnitLost)
		{
			const bool bSeen = bSpectate || E.Player == LocalPlayer()
				|| SimGame->IsVisibleTo(LocalPlayer(), E.Pos);
			if (bSeen && SplashBudget-- > 0)   // share the splash budget
			{
				const FName Faction = SimGame->Players[E.Player].FactionId;
				const FUnitTpl* UT = SimGame->Content.Unit(Faction, E.Tpl);
				const FStructTpl* ST = UT ? nullptr : SimGame->Content.Structure(Faction, E.Tpl);
				PlayEffectSound(UT ? UT->LostSound : ST ? ST->LostSound : FString(), E.Pos);
			}
			continue;
		}
```

Then JSON: `"lostSound": "ship-explosion"` on whichever units you have
audio for. Self-check: sink your own scout and listen; then check a
fogged enemy death makes no sound.

**14.3 — Budget experiment.**
At 1000: every shot in a fleet battle plays — dozens of overlapping
copies clip into a wall of noise, the mixer starts voice-stealing, and
frame time dips from audio component churn. At 1: you hear exactly one
shot per drain, and here's the subtle part — it's always the *lowest
entity id* that fires that tick (events are pushed in `SimSortedKeys`
order), so the battle "belongs" to the oldest surviving unit; newer
reinforcements are inaudible. Lesson: budgets don't just cap volume,
they *select* — and selection by id is arbitrary. A better policy
would prioritize by distance to camera, or your own units over enemies.
If you proposed either, gold star.

**14.4 — Ears and fog.**
*For:* real naval combat is full of over-the-horizon sound; muffled
distant thunder from unexplored sea is atmospheric AND a deliberate,
tunable intel channel — plenty of RTS games leak *some* audio
(StarCraft's "your forces are under attack" fires from fogged map).
*Against:* it creates an information asymmetry that's invisible in
replays and impossible to reason about competitively; players can't
tell designed leaks from bugs, and "I heard reloading in the fog" is
miserable to balance. Verdict either way is fine if it lands on:
*make it an explicit rule, not a side effect* — e.g., a distinct
muffled "distant battle" cue that carries no direction or template
information, rather than the real per-unit sounds at low volume.

**14.5 — The engine room.**
The design question first: an event stream is the *wrong* vehicle for
continuous state — you'd emit "throttle is 0.73" twenty times a second
per unit, which is state-sync wearing an event costume. The right
vehicle is what units already use for position: the render layer
*reads* sim state each sync. `AACGameMode::SyncEntityActors` already
looks up `FHarvestC` per entity; reading `World.Manual` the same way
is symmetrical and costs nothing.

Sketch: on entering unit view, `SpawnSoundAttached` a looping wave
(build it like the ambient loop: queue PCM, re-queue before it
drains — or simpler, a `USoundWaveProcedural` fed a short looped hum)
on the followed actor; each `SyncEntityActors`, if the entity has a
`Manual` component, set the audio component's volume to
`0.3f + 0.7f * FMath::Abs(MC->Axes.X)` and pitch to
`0.9f + 0.25f * Abs(Axes.X)`; destroy the component when follow ends
(the `PrevFollowed` handover in the player controller already knows
the exact moment). Rev it with W and listen to the pitch climb —
that's continuous state flowing sim→render through reads, while the
bangs stay events. Knowing which channel is which is the lesson.
