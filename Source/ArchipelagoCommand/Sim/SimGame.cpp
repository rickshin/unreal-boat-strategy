#include "SimGame.h"
#include "Systems.h"
#include "SimAI.h"

bool FSimGame::Init(uint64 InSeed, FName Faction0, FName Faction1, ESimDifficulty InDifficulty, const FString& DataDir)
{
	Seed = InSeed;
	Difficulty = InDifficulty;
	Rng.Seed(InSeed, 1);
	if (!Content.LoadFromDir(DataDir)) { return false; }
	if (!Content.Faction(Faction0) || !Content.Faction(Faction1)) { return false; }

	Map.Generate(InSeed);
	Pathfinder.Init(&Map);

	for (int32 P = 0; P < 2; ++P)
	{
		FSimPlayer& Pl = Players[P];
		Pl.FactionId = (P == 0) ? Faction0 : Faction1;
		Pl.Explored.Init(false, Map.Width * Map.Height);
		Pl.Visible.Init(false, Map.Width * Map.Height);

		const FFactionDef& F = FactionOf(P);
		Pl.HqEid = SpawnStructure(P, F.HqId, Map.HomeSpawn[P], true);
		Map.Islands[Map.HomeIsland[P]].OwnerPlayer = P;
		Map.Islands[Map.HomeIsland[P]].ClaimStructure = Pl.HqEid;

		// Starting force: one builder + two scouts/patrols.
		const FVector2f Base = Map.HomeSpawn[P];
		SpawnUnit(P, F.BuilderId, Map.NearestWater(Base + FVector2f(2.5f, 0.f)));
		FName ScoutId = NAME_None;
		int32 BestCost = MAX_int32;
		for (const auto& Pair : F.Units)   // cheapest armed unit as starting escort
		{
			if (Pair.Value.Weapons.Num() > 0 && Pair.Value.Domain == EUnitDomain::Naval
				&& Pair.Value.Value() < BestCost)
			{
				BestCost = Pair.Value.Value();
				ScoutId = Pair.Key;
			}
		}
		if (!ScoutId.IsNone())
		{
			SpawnUnit(P, ScoutId, Map.NearestWater(Base + FVector2f(-2.5f, 1.5f)));
			SpawnUnit(P, ScoutId, Map.NearestWater(Base + FVector2f(0.f, -2.5f)));
		}
	}

	SimSys::RunFog(*this);  // initial vision before the first frame renders
	return true;
}

void FSimGame::Step()
{
	if (Winner >= 0) { return; }
	// Spec 4 system order: Command, Economy, AI, Movement, Combat, Fog, Cleanup.
	SimSys::RunCommands(*this);
	SimSys::RunEconomy(*this);
	SimAI::Run(*this);
	SimSys::RunMovement(*this);
	SimSys::RunCombat(*this);
	if (Tick % FOG_INTERVAL_TICKS == 0) { SimSys::RunFog(*this); }
	SimSys::RunCleanup(*this);
	++Tick;
}

FEntityId FSimGame::SpawnUnit(int32 Player, FName TplId, const FVector2f& At)
{
	const FUnitTpl* T = Content.Unit(Players[Player].FactionId, TplId);
	if (!T) { return INVALID_ENTITY; }
	const FEntityId E = World.Create();
	World.Pos.Add(E, { At, 0.f });
	World.Own.Add(E, { Player });
	World.Unit.Add(E, { TplId, T->Domain, T->bBuilder });
	World.Health.Add(E, { T->MaxHp, T->MaxHp, T->Armor });
	World.Mover.Add(E, { T->Speed });
	World.Vision.Add(E, { T->Vision });
	if (T->Weapons.Num() > 0)
	{
		FCombatC C;
		C.Weapons = T->Weapons;
		C.State.SetNum(T->Weapons.Num());
		for (const FWeaponTpl& W : T->Weapons) { C.MaxRange = FMath::Max(C.MaxRange, W.Range); }
		World.Combat.Add(E, MoveTemp(C));
	}
	if (T->RepairRate > 0.f) { World.Repair.Add(E, { T->RepairRate, 4.f }); }
	if (T->bHarvester) { World.Harvest.Add(E, FHarvestC()); }
	if (T->Builds.Num() > 0 && !T->bBuilder)   // carrier: produces aircraft
	{
		FProdC Prod;
		Prod.Options = T->Builds;
		World.Prod.Add(E, MoveTemp(Prod));
	}
	return E;
}

FEntityId FSimGame::SpawnStructure(int32 Player, FName TplId, const FVector2f& At, bool bComplete)
{
	const FStructTpl* T = Content.Structure(Players[Player].FactionId, TplId);
	if (!T) { return INVALID_ENTITY; }
	const FEntityId E = World.Create();
	World.Pos.Add(E, { At, 0.f });
	World.Own.Add(E, { Player });
	FStructC S;
	S.Tpl = TplId;
	S.Kind = T->Kind;
	S.bComplete = bComplete;
	S.Progress = bComplete ? 1.f : 0.f;
	World.Struct.Add(E, S);
	World.Health.Add(E, { bComplete ? T->MaxHp : T->MaxHp * 0.1f, T->MaxHp, T->Armor });
	World.Vision.Add(E, { T->Vision });
	if (bComplete && T->Weapons.Num() > 0)
	{
		FCombatC C;
		C.Weapons = T->Weapons;
		C.State.SetNum(T->Weapons.Num());
		for (const FWeaponTpl& W : T->Weapons) { C.MaxRange = FMath::Max(C.MaxRange, W.Range); }
		World.Combat.Add(E, MoveTemp(C));
	}
	if (bComplete && T->Produces.Num() > 0)
	{
		FProdC Prod;
		Prod.Options = T->Produces;
		World.Prod.Add(E, MoveTemp(Prod));
	}
	if (T->Kind == EStructureKind::Extractor)
	{
		const int32 Node = Map.NodeNear(At, 1.5f);
		FStructC& SC = World.Struct[E];
		SC.NodeId = Node;
		if (Node >= 0) { Map.Nodes[Node].ClaimedBy = E; }
	}
	return E;
}

bool FSimGame::FindDepot(int32 Player, const FVector2f& From, FVector2f& OutPos) const
{
	FEntityId Best = INVALID_ENTITY;
	float BestDist = TNumericLimits<float>::Max();
	for (const auto& Pair : World.Struct)
	{
		if (Pair.Value.Kind != EStructureKind::HQ && Pair.Value.Kind != EStructureKind::Colony) { continue; }
		if (!Pair.Value.bComplete) { continue; }
		const FOwnerC* O = World.Own.Find(Pair.Key);
		const FPosC* P = World.Pos.Find(Pair.Key);
		if (!O || O->Player != Player || !P) { continue; }
		const float D = (P->P - From).Size();
		if (D < BestDist || (D == BestDist && Pair.Key < Best)) { BestDist = D; Best = Pair.Key; }
	}
	if (Best == INVALID_ENTITY) { return false; }
	OutPos = World.Pos[Best].P;
	return true;
}

bool FSimGame::IsVisibleTo(int32 Player, const FVector2f& P) const
{
	const int32 X = FMath::FloorToInt32(P.X);
	const int32 Y = FMath::FloorToInt32(P.Y);
	if (!Map.InBounds(X, Y)) { return false; }
	return Players[Player].Visible[Y * Map.Width + X];
}

bool FSimGame::IsValidBuildSite(int32 Player, FName TplId, const FVector2f& At, FString* WhyNot) const
{
	const FStructTpl* T = Content.Structure(Players[Player].FactionId, TplId);
	if (!T) { if (WhyNot) { *WhyNot = TEXT("Unknown structure"); } return false; }
	if (!Map.IsWater(At)) { if (WhyNot) { *WhyNot = TEXT("Must be placed on water"); } return false; }

	// Keep sites clear of other structures.
	for (const auto& Pair : World.Struct)
	{
		const FPosC* SP = World.Pos.Find(Pair.Key);
		if (SP && (SP->P - At).Size() < 3.f)
		{
			if (WhyNot) { *WhyNot = TEXT("Too close to another structure"); }
			return false;
		}
	}

	switch (T->Kind)
	{
	case EStructureKind::Extractor:
	{
		// Extractors are deployed by crawlers, directly on an open geyser.
		const int32 Node = Map.NodeNear(At, 1.5f);
		if (Node < 0) { if (WhyNot) { *WhyNot = TEXT("Needs a geyser"); } return false; }
		if (Map.Nodes[Node].ClaimedBy != INVALID_ENTITY)
		{
			if (WhyNot) { *WhyNot = TEXT("Geyser already tapped"); }
			return false;
		}
		return true;
	}
	case EStructureKind::Outpost:
	case EStructureKind::Colony:
	{
		const int32 Island = Map.IslandNear(At, 2.5f);
		if (Island < 0) { if (WhyNot) { *WhyNot = TEXT("Must be adjacent to an island"); } return false; }
		if (Map.Islands[Island].OwnerPlayer >= 0)
		{
			if (WhyNot) { *WhyNot = TEXT("Island already claimed"); }
			return false;
		}
		return true;
	}
	default:
		return true;
	}
}

void FSimGame::ApplyDamage(FEntityId Target, float Damage, EWeaponType Type, int32 SourcePlayer)
{
	FHealthC* H = World.Health.Find(Target);
	if (!H || H->Hp <= 0.f) { return; }

	float Pierce = 0.f;
	switch (Type)
	{
	case EWeaponType::Missile: Pierce = 0.30f; break;
	case EWeaponType::Torpedo: Pierce = 0.50f; break;
	default: break;
	}
	const float EffArmor = H->Armor * (1.f - Pierce);
	const float Mitigation = FMath::Min(0.60f, 0.06f * EffArmor);
	H->Hp -= Damage * (1.f - Mitigation);

	// EW disruption slows the victim's reload while active.
	if (Type == EWeaponType::Disrupt)
	{
		if (FCombatC* C = World.Combat.Find(Target)) { C->DisruptTicks = 4 * SIM_TICKS_PER_SEC; }
	}

	// Throttled under-attack alert for the defender.
	const FOwnerC* O = World.Own.Find(Target);
	const FPosC* P = World.Pos.Find(Target);
	if (O && P && O->Player >= 0 && O->Player != SourcePlayer)
	{
		FSimPlayer& Def = Players[O->Player];
		if (Tick - Def.LastAttackAlertTick > 8 * SIM_TICKS_PER_SEC)
		{
			Def.LastAttackAlertTick = Tick;
			Def.LastAttackPos = P->P;
			PushEvent(FSimEvent::EKind::UnderAttack, O->Player, P->P,
				World.Struct.Contains(Target) ? TEXT("Base under attack!") : TEXT("Units under attack!"));
		}
	}
}

uint64 FSimGame::StateHash() const
{
	FSimHash H;
	H.Add(uint64(Tick));
	for (int32 P = 0; P < 2; ++P)
	{
		H.AddF(Players[P].KiTrin);
	}
	// Map state matters too: geyser reserves and island ownership are as
	// much "the game" as entity positions.
	for (const FSimResourceNode& N : Map.Nodes)
	{
		H.Add(uint64(uint32(N.Cell.X)) << 32 | uint32(N.Cell.Y));
		H.AddF(N.Amount);
	}
	for (const FSimIsland& Isl : Map.Islands)
	{
		H.Add(uint64(Isl.OwnerPlayer + 1));
	}
	TArray<FEntityId> Ids = World.Alive.Array();
	Ids.Sort();
	for (const FEntityId Id : Ids)
	{
		H.Add(uint64(Id));
		if (const FPosC* P = World.Pos.Find(Id)) { H.AddF(P->P.X); H.AddF(P->P.Y); }
		if (const FHealthC* Hp = World.Health.Find(Id)) { H.AddF(Hp->Hp); }
		if (const FOwnerC* O = World.Own.Find(Id)) { H.Add(uint64(O->Player + 1)); }
	}
	return H.H;
}
