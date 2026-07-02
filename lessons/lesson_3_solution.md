# Lesson 3 — Solutions

**3.1 — Census taker.**
In `Sim/SimGame.cpp`, at the end of `FSimGame::Step()`:

```cpp
void FSimGame::Step()
{
    if (Winner >= 0) { return; }
    SimSys::RunCommands(*this);
    SimSys::RunEconomy(*this);
    SimAI::Run(*this);
    SimSys::RunMovement(*this);
    SimSys::RunCombat(*this);
    if (Tick % FOG_INTERVAL_TICKS == 0) { SimSys::RunFog(*this); }
    SimSys::RunCleanup(*this);
    ++Tick;

    if (Tick % 200 == 0)
    {
        UE_LOG(LogTemp, Log, TEXT("Census at tick %d: %d entities alive"),
            Tick, World.Alive.Num());
    }
}
```

Fun observation: the number includes projectiles! During a big battle
the census spikes because every cannon shell in flight is an entity.

**3.2 — Healing waters.**
Best home: **RunEconomy** — regeneration is upkeep, like repair vessels
(which already live there). Combat would also be defensible; Movement
would not (it has nothing to do with moving). At the end of
`SimSys::RunEconomy` in `Sim/Systems.cpp`:

```cpp
	// Slow regeneration for units (structures repair via repair logic only).
	for (const FEntityId Eid : SimSortedKeys(G.World.Unit))
	{
		FHealthC* H = G.World.Health.Find(Eid);
		if (H && H->Hp > 0.f && H->Hp < H->MaxHp)
		{
			H->Hp = FMath::Min(H->MaxHp, H->Hp + 1.f * SIM_DT);
		}
	}
```

The key moves: loop over `World.Unit` (only units have that component —
structures are excluded *automatically*, that's ECS thinking!), null
check the `Find`, scale by `SIM_DT` so it's per-second, clamp with
`FMath::Min`. Note we iterate with `SimSortedKeys` even though order
doesn't seem to matter here — it's the house rule, and following it
always is how you never cause a desync accidentally.

**3.3 — Cargo ship design.**
A reasonable design:

```cpp
struct FCargoC
{
    int32 Capacity = 50;
    int32 CarriedWood = 0;
    int32 CarriedIron = 0;
    FEntityId LoadFrom = INVALID_ENTITY;    // structure to pick up at
    FEntityId DeliverTo = INVALID_ENTITY;   // structure to drop at
};
```

Touched systems: **Economy** does the actual loading/unloading when the
ship is near enough (just like construction checks builder distance);
**Command** needs a new order type (or reuse of Move) to set
LoadFrom/DeliverTo; **Cleanup** should drop the cargo (or lose it!) when
the ship dies. Movement needs *nothing* — a cargo ship has a `Mover`
like anyone else. If your design didn't need to touch Movement or
Combat, you've understood ECS: new features mostly mean one new
component plus small additions to the systems that care.

**3.4 — The rig that couldn't shoot.**
In `FSimGame::SpawnStructure`:

```cpp
if (bComplete && T->Weapons.Num() > 0)
{
    FCombatC C;
    ...
    G.World.Combat.Add(E, MoveTemp(C));
}
```

The `Combat` component is only added **if the template's JSON lists
weapons**. `timber_rig` in `dominion.json` has no `"weapons"` array, so
`T->Weapons.Num() > 0` is false, so the rig never receives a `Combat`
component — and `RunCombat` loops over `World.Combat`, so the rig is
simply never visited. It can't shoot for the same reason a rock can't:
it lacks the component. (Bonus insight: give it weapons in JSON and it
*would* shoot, no C++ needed. That's lesson 6.)
