#include "Systems.h"
#include "SimGame.h"

namespace
{
	// True if the target entity is something this filter may hit.
	// "Naval" = surface entities: ships and floating structures.
	bool MatchesFilter(const FSimGame& G, ETargetFilter Filter, FEntityId Target)
	{
		if (Filter == ETargetFilter::Any) { return true; }
		const FUnitC* U = G.World.Unit.Find(Target);
		const bool bAir = U && U->Domain == EUnitDomain::Air;
		return (Filter == ETargetFilter::Air) ? bAir : !bAir;
	}

	bool IsEnemyTargetable(const FSimGame& G, int32 Player, FEntityId Target)
	{
		if (!G.World.IsAlive(Target)) { return false; }
		const FOwnerC* O = G.World.Own.Find(Target);
		if (!O || O->Player == Player || O->Player < 0) { return false; }
		const FHealthC* H = G.World.Health.Find(Target);
		if (!H || H->Hp <= 0.f) { return false; }
		const FPosC* P = G.World.Pos.Find(Target);
		return P && G.IsVisibleTo(Player, P->P);
	}

	void SetMoveOrder(FSimGame& G, FEntityId Eid, const FVector2f& Dest, EMoveOrder Order)
	{
		FMoverC* M = G.World.Mover.Find(Eid);
		const FPosC* P = G.World.Pos.Find(Eid);
		const FUnitC* U = G.World.Unit.Find(Eid);
		if (!M || !P || !U) { return; }
		M->Order = Order;
		M->OrderDest = Dest;
		M->ChaseTarget = INVALID_ENTITY;
		if (U->Domain != EUnitDomain::Naval)
		{
			M->Waypoints = { Dest };   // air/ground move straight, ignoring the water grid
		}
		else
		{
			M->Waypoints = G.Pathfinder.FindPath(P->P, Dest);
		}
	}

	// Spawn point for produced units: first free water ring slot around the producer.
	FVector2f FindSpawnSlot(const FSimGame& G, const FVector2f& Around)
	{
		for (int32 i = 0; i < 8; ++i)
		{
			const float Ang = (2.f * PI * i) / 8.f;
			const FVector2f P = Around + FVector2f(FMath::Cos(Ang), FMath::Sin(Ang)) * 2.2f;
			if (G.Map.IsWater(P)) { return P; }
		}
		return G.Map.NearestWater(Around);
	}

	// Retrofit combat/production onto a structure that just finished building.
	void CompleteStructure(FSimGame& G, FEntityId Eid)
	{
		FStructC& S = G.World.Struct[Eid];
		const FOwnerC& O = G.World.Own[Eid];
		const FStructTpl* T = G.Content.Structure(G.Players[O.Player].FactionId, S.Tpl);
		S.bComplete = true;
		S.Progress = 1.f;
		G.World.Health[Eid].Hp = T->MaxHp;

		if (T->Weapons.Num() > 0 && !G.World.Combat.Contains(Eid))
		{
			FCombatC C;
			C.Weapons = T->Weapons;
			C.State.SetNum(T->Weapons.Num());
			for (const FWeaponTpl& W : T->Weapons) { C.MaxRange = FMath::Max(C.MaxRange, W.Range); }
			G.World.Combat.Add(Eid, MoveTemp(C));
		}
		if (T->Produces.Num() > 0 && !G.World.Prod.Contains(Eid))
		{
			FProdC Prod;
			Prod.Options = T->Produces;
			G.World.Prod.Add(Eid, MoveTemp(Prod));
		}
		// Outposts/colonies claim their island on completion.
		if (T->Kind == EStructureKind::Outpost || T->Kind == EStructureKind::Colony)
		{
			const FPosC& P = G.World.Pos[Eid];
			const int32 Island = G.Map.IslandNear(P.P, 2.5f);
			if (Island >= 0 && G.Map.Islands[Island].OwnerPlayer < 0)
			{
				G.Map.Islands[Island].OwnerPlayer = O.Player;
				G.Map.Islands[Island].ClaimStructure = Eid;
				S.IslandId = Island;
			}
		}
	}

	// Render-only muzzle-report cue for the firing entity.
	void PushShotEvent(FSimGame& G, FEntityId Shooter, int32 Player, const FVector2f& At)
	{
		FName Tpl;
		if (const FUnitC* U = G.World.Unit.Find(Shooter)) { Tpl = U->Tpl; }
		else if (const FStructC* SC = G.World.Struct.Find(Shooter)) { Tpl = SC->Tpl; }
		G.PushEvent(FSimEvent::EKind::Shot, Player, At, FString(), Tpl);
	}
}

// ---------------------------------------------------------------------------

void SimSys::RunCommands(FSimGame& G)
{
	TArray<FSimCommand> Cmds = MoveTemp(G.PendingCommands);
	G.PendingCommands.Reset();

	for (const FSimCommand& Cmd : Cmds)
	{
		switch (Cmd.Type)
		{
		case ECmdType::Move:
		case ECmdType::AttackMove:
		{
			int32 Index = 0;
			for (const FEntityId Eid : Cmd.Units)
			{
				const FOwnerC* O = G.World.Own.Find(Eid);
				if (!O || O->Player != Cmd.Player || !G.World.Mover.Contains(Eid)) { continue; }
				const FVector2f Dest = G.Map.NearestWater(Cmd.Target + FSimPathfinder::GroupOffset(Index++));
				SetMoveOrder(G, Eid, Dest,
					Cmd.Type == ECmdType::AttackMove ? EMoveOrder::AttackMove : EMoveOrder::Move);
				if (FHarvestC* HC = G.World.Harvest.Find(Eid)) { HC->State = EHarvestState::None; }
			}
			break;
		}
		case ECmdType::Attack:
		{
			if (!G.World.IsAlive(Cmd.TargetEid)) { break; }
			for (const FEntityId Eid : Cmd.Units)
			{
				const FOwnerC* O = G.World.Own.Find(Eid);
				if (!O || O->Player != Cmd.Player) { continue; }
				if (FMoverC* M = G.World.Mover.Find(Eid))
				{
					M->Order = EMoveOrder::Chase;
					M->ChaseTarget = Cmd.TargetEid;
					M->Waypoints.Reset();
					M->RepathCooldown = 0;
				}
				if (FCombatC* C = G.World.Combat.Find(Eid)) { C->Target = Cmd.TargetEid; }
			}
			break;
		}
		case ECmdType::Stop:
		{
			for (const FEntityId Eid : Cmd.Units)
			{
				const FOwnerC* O = G.World.Own.Find(Eid);
				if (!O || O->Player != Cmd.Player) { continue; }
				if (FMoverC* M = G.World.Mover.Find(Eid))
				{
					M->Order = EMoveOrder::Idle;
					M->Waypoints.Reset();
					M->ChaseTarget = INVALID_ENTITY;
				}
				if (FCombatC* C = G.World.Combat.Find(Eid)) { C->Target = INVALID_ENTITY; }
				if (FHarvestC* HC = G.World.Harvest.Find(Eid)) { HC->State = EHarvestState::None; }
			}
			break;
		}
		case ECmdType::Harvest:
		{
			if (Cmd.TargetEid < 0 || Cmd.TargetEid >= G.Map.Nodes.Num()) { break; }
			const FSimResourceNode& Node = G.Map.Nodes[Cmd.TargetEid];
			const FVector2f NodeP(Node.Cell.X + 0.5f, Node.Cell.Y + 0.5f);
			for (const FEntityId Eid : Cmd.Units)
			{
				const FOwnerC* O = G.World.Own.Find(Eid);
				FHarvestC* HC = G.World.Harvest.Find(Eid);
				if (!O || O->Player != Cmd.Player || !HC) { continue; }
				HC->State = EHarvestState::ToGeyser;
				HC->NodeId = Cmd.TargetEid;
				HC->DockTicksLeft = 0;
				if (FMoverC* M = G.World.Mover.Find(Eid))
				{
					M->Order = EMoveOrder::Move;
					M->Waypoints = { NodeP };
					M->ChaseTarget = INVALID_ENTITY;
				}
			}
			break;
		}
		case ECmdType::Produce:
		{
			FProdC* Prod = G.World.Prod.Find(Cmd.TargetEid);
			const FOwnerC* O = G.World.Own.Find(Cmd.TargetEid);
			if (!Prod || !O || O->Player != Cmd.Player) { break; }
			if (!Prod->Options.Contains(Cmd.TplId) || Prod->Queue.Num() >= 7) { break; }
			const FUnitTpl* T = G.Content.Unit(G.Players[Cmd.Player].FactionId, Cmd.TplId);
			if (!T || !G.CanAfford(Cmd.Player, T->Cost))
			{
				if (!G.Players[Cmd.Player].bIsAI)
				{
					G.PushEvent(FSimEvent::EKind::ProductionDone, Cmd.Player, FVector2f::ZeroVector,
						TEXT("Not enough resources"));
				}
				break;
			}
			G.PayCost(Cmd.Player, T->Cost);
			Prod->Queue.Add(Cmd.TplId);
			break;
		}
		case ECmdType::Morph:
		{
			// Each unit pays the target's cost and becomes a cocoon; the
			// swap happens in Economy when the timer completes.
			const FUnitTpl* Target = G.Content.Unit(G.Players[Cmd.Player].FactionId, Cmd.TplId);
			if (!Target) { break; }
			for (const FEntityId Eid : Cmd.Units)
			{
				const FOwnerC* O = G.World.Own.Find(Eid);
				const FUnitC* U = G.World.Unit.Find(Eid);
				if (!O || O->Player != Cmd.Player || !U) { continue; }
				if (G.World.Morph.Contains(Eid)) { continue; }   // already cocooned
				const FUnitTpl* Self = G.Content.Unit(G.Players[Cmd.Player].FactionId, U->Tpl);
				if (!Self || !Self->bMorph || !Self->MorphInto.Contains(Cmd.TplId)) { continue; }
				if (!G.CanAfford(Cmd.Player, Target->Cost))
				{
					if (!G.Players[Cmd.Player].bIsAI)
					{
						G.PushEvent(FSimEvent::EKind::ProductionDone, Cmd.Player,
							FVector2f::ZeroVector, TEXT("Not enough resources"));
					}
					break;   // no point trying the rest of the selection
				}
				G.PayCost(Cmd.Player, Target->Cost);
				G.World.Morph.Add(Eid, { Cmd.TplId, 0.f, FMath::Max(0.5f, Target->BuildTime) });
				// Cocoons hold still and stop fighting.
				if (FMoverC* M = G.World.Mover.Find(Eid))
				{
					M->Order = EMoveOrder::Idle;
					M->Waypoints.Reset();
					M->ChaseTarget = INVALID_ENTITY;
				}
				if (FCombatC* C = G.World.Combat.Find(Eid)) { C->Target = INVALID_ENTITY; }
				if (FHarvestC* HC = G.World.Harvest.Find(Eid)) { HC->State = EHarvestState::None; }
				G.World.Manual.Remove(Eid);
			}
			break;
		}
		case ECmdType::ManualControl:
		{
			if (Cmd.Units.Num() == 0) { break; }
			const FEntityId Eid = Cmd.Units[0];
			const FOwnerC* O = G.World.Own.Find(Eid);
			if (!O || O->Player != Cmd.Player || !G.World.Mover.Contains(Eid)) { break; }
			if (Cmd.bFlag)
			{
				FManualC MC;
				if (const FPosC* P = G.World.Pos.Find(Eid)) { MC.DesiredFacing = P->Facing; }
				G.World.Manual.Add(Eid, MC);
				// Direct control overrides standing orders.
				FMoverC& M = G.World.Mover[Eid];
				M.Order = EMoveOrder::Idle;
				M.Waypoints.Reset();
				M.ChaseTarget = INVALID_ENTITY;
				if (FCombatC* C = G.World.Combat.Find(Eid)) { C->Target = INVALID_ENTITY; }
			}
			else
			{
				G.World.Manual.Remove(Eid);
			}
			break;
		}
		case ECmdType::ManualInput:
		{
			if (Cmd.Units.Num() == 0) { break; }
			const FEntityId Eid = Cmd.Units[0];
			const FOwnerC* O = G.World.Own.Find(Eid);
			FManualC* MC = G.World.Manual.Find(Eid);
			if (!O || O->Player != Cmd.Player || !MC) { break; }
			MC->Axes = Cmd.Target;
			MC->DesiredFacing = Cmd.FacingRad;
			MC->bFireHeld = Cmd.bFlag;
			break;
		}
		case ECmdType::Build:
		{
			if (Cmd.Units.Num() == 0) { break; }
			const FEntityId Builder = Cmd.Units[0];
			const FOwnerC* O = G.World.Own.Find(Builder);
			const FUnitC* U = G.World.Unit.Find(Builder);
			if (!O || O->Player != Cmd.Player || !U || !U->bBuilder) { break; }
			if (!G.IsValidBuildSite(Cmd.Player, Cmd.TplId, Cmd.Target)) { break; }
			const FStructTpl* T = G.Content.Structure(G.Players[Cmd.Player].FactionId, Cmd.TplId);
			if (!T || !G.CanAfford(Cmd.Player, T->Cost))
			{
				if (!G.Players[Cmd.Player].bIsAI)
				{
					G.PushEvent(FSimEvent::EKind::ProductionDone, Cmd.Player, FVector2f::ZeroVector,
						TEXT("Not enough resources"));
				}
				break;
			}
			G.PayCost(Cmd.Player, T->Cost);
			const FEntityId Site = G.SpawnStructure(Cmd.Player, Cmd.TplId, Cmd.Target, false);
			G.World.BuildTask.Add(Builder, { Site });
			if (FHarvestC* HC = G.World.Harvest.Find(Builder)) { HC->State = EHarvestState::None; }
			SetMoveOrder(G, Builder, G.Map.NearestWater(Cmd.Target + FVector2f(1.8f, 0.f)), EMoveOrder::Move);
			break;
		}
		}
	}
}

// ---------------------------------------------------------------------------

void SimSys::RunEconomy(FSimGame& G)
{
	// Construction: builder must stay near the site.
	for (const FEntityId Builder : SimSortedKeys(G.World.BuildTask))
	{
		const FEntityId Site = G.World.BuildTask[Builder].Site;
		if (!G.World.IsAlive(Site) || !G.World.Struct.Contains(Site))
		{
			G.World.BuildTask.Remove(Builder);
			continue;
		}
		FStructC& S = G.World.Struct[Site];
		if (S.bComplete) { G.World.BuildTask.Remove(Builder); continue; }
		const FPosC* BP = G.World.Pos.Find(Builder);
		const FPosC* SP = G.World.Pos.Find(Site);
		if (!BP || !SP) { continue; }
		if ((BP->P - SP->P).Size() > 3.f) { continue; }   // still sailing there

		const FOwnerC& O = G.World.Own[Site];
		const FStructTpl* T = G.Content.Structure(G.Players[O.Player].FactionId, S.Tpl);
		S.Progress += SIM_DT / T->BuildTime;
		FHealthC& H = G.World.Health[Site];
		H.Hp = FMath::Min(T->MaxHp, T->MaxHp * (0.1f + 0.9f * S.Progress));
		if (S.Progress >= 1.f)
		{
			CompleteStructure(G, Site);
			G.World.BuildTask.Remove(Builder);
			if (!G.Players[O.Player].bIsAI)
			{
				G.PushEvent(FSimEvent::EKind::ProductionDone, O.Player, SP->P,
					FString::Printf(TEXT("%s complete"), *T->Name));
			}
		}
	}

	// Gas extractors assemble themselves once their crawler unfolds.
	for (const FEntityId Eid : SimSortedKeys(G.World.Struct))
	{
		FStructC& S = G.World.Struct[Eid];
		if (S.Kind != EStructureKind::Extractor || S.bComplete) { continue; }
		const FOwnerC& O = G.World.Own[Eid];
		const FStructTpl* T = G.Content.Structure(G.Players[O.Player].FactionId, S.Tpl);
		if (!T) { continue; }
		S.Progress += SIM_DT / T->BuildTime;
		FHealthC& H = G.World.Health[Eid];
		H.Hp = FMath::Min(T->MaxHp, T->MaxHp * (0.1f + 0.9f * S.Progress));
		if (S.Progress >= 1.f) { CompleteStructure(G, Eid); }
	}

	// Crawlers: crawl onto their geyser and become the extractor.
	for (const FEntityId Eid : SimSortedKeys(G.World.Crawl))
	{
		if (!G.World.IsAlive(Eid)) { continue; }
		const FCrawlC& CC = G.World.Crawl[Eid];
		if (CC.NodeId < 0 || CC.NodeId >= G.Map.Nodes.Num()) { continue; }
		FSimResourceNode& Node = G.Map.Nodes[CC.NodeId];
		const FVector2f NodeP(Node.Cell.X + 0.5f, Node.Cell.Y + 0.5f);
		const FPosC* P = G.World.Pos.Find(Eid);
		if (!P || (P->P - NodeP).Size() > 0.35f) { continue; }
		const int32 Player = G.World.Own[Eid].Player;
		if (Node.ClaimedBy == INVALID_ENTITY)
		{
			FName ExtractorId;
			for (const auto& Pair : G.FactionOf(Player).Structures)
			{
				if (Pair.Value.Kind == EStructureKind::Extractor) { ExtractorId = Pair.Key; break; }
			}
			if (!ExtractorId.IsNone())
			{
				G.SpawnStructure(Player, ExtractorId, NodeP, false);
			}
		}
		G.World.Destroy(Eid);   // the crawler is consumed by the deployment
	}

	// Morph cocoons: when the timer completes, the old unit is consumed
	// and the target unit takes its place (position and facing carry over).
	for (const FEntityId Eid : SimSortedKeys(G.World.Morph))
	{
		if (!G.World.IsAlive(Eid)) { continue; }
		FMorphC& M = G.World.Morph[Eid];
		M.Elapsed += SIM_DT;
		if (M.Elapsed < M.Duration) { continue; }
		const int32 Player = G.World.Own[Eid].Player;
		const FPosC Pos = G.World.Pos[Eid];   // copy before destruction
		const FName Target = M.Target;
		G.World.Destroy(Eid);
		const FEntityId NewEid = G.SpawnUnit(Player, Target, Pos.P);
		if (NewEid != INVALID_ENTITY)
		{
			G.World.Pos[NewEid].Facing = Pos.Facing;
			if (!G.Players[Player].bIsAI)
			{
				const FUnitTpl* T = G.Content.Unit(G.Players[Player].FactionId, Target);
				G.PushEvent(FSimEvent::EKind::ProductionDone, Player, Pos.P,
					FString::Printf(TEXT("%s morph complete"), T ? *T->Name : TEXT("Unit")));
			}
		}
	}

	// Breeders (Hive larvae, Broodlord broodlings): spawn a child when the
	// timer fires and fewer than Max of them live within the leash radius.
	for (const FEntityId Eid : SimSortedKeys(G.World.Spawner))
	{
		if (!G.World.IsAlive(Eid)) { continue; }
		FSpawnerC& Sp = G.World.Spawner[Eid];
		if (--Sp.Cooldown > 0) { continue; }
		Sp.Cooldown = Sp.IntervalTicks;
		const FStructC* S = G.World.Struct.Find(Eid);
		if (S && !S->bComplete) { continue; }
		const int32 Player = G.World.Own[Eid].Player;
		const FPosC* P = G.World.Pos.Find(Eid);
		if (!P) { continue; }
		int32 Nearby = 0;
		for (const auto& Pair : G.World.Unit)
		{
			if (Pair.Value.Tpl != Sp.Unit) { continue; }
			const FOwnerC* O = G.World.Own.Find(Pair.Key);
			const FPosC* OP = G.World.Pos.Find(Pair.Key);
			if (O && O->Player == Player && OP && (OP->P - P->P).Size() < 6.f) { ++Nearby; }
		}
		if (Nearby < Sp.Max)
		{
			G.SpawnUnit(Player, Sp.Unit, FindSpawnSlot(G, P->P));
		}
	}

	// Flying gas harvesters: fly to the geyser, deploy a crawler if the
	// geyser is untapped, dock on the extractor tube, then ferry the gas
	// to the nearest depot. Repeats until the geyser is spent.
	for (const FEntityId Eid : SimSortedKeys(G.World.Harvest))
	{
		FHarvestC& HC = G.World.Harvest[Eid];
		if (HC.State == EHarvestState::None) { continue; }
		FPosC* P = G.World.Pos.Find(Eid);
		FMoverC* M = G.World.Mover.Find(Eid);
		if (!P || !M) { continue; }
		if (HC.NodeId < 0 || HC.NodeId >= G.Map.Nodes.Num()) { HC.State = EHarvestState::None; continue; }
		const int32 Player = G.World.Own[Eid].Player;
		FSimResourceNode& Node = G.Map.Nodes[HC.NodeId];
		const FVector2f NodeP(Node.Cell.X + 0.5f, Node.Cell.Y + 0.5f);
		const FUnitTpl* T = G.Content.Unit(G.Players[Player].FactionId, G.World.Unit[Eid].Tpl);
		if (!T) { HC.State = EHarvestState::None; continue; }

		switch (HC.State)
		{
		case EHarvestState::ToGeyser:
		{
			if (Node.Amount <= 0.f) { HC.State = EHarvestState::None; break; }
			if ((P->P - NodeP).Size() > 1.2f)
			{
				if (M->Waypoints.Num() == 0) { M->Waypoints = { NodeP }; M->Order = EMoveOrder::Move; }
				break;
			}
			if (Node.ClaimedBy != INVALID_ENTITY && G.World.IsAlive(Node.ClaimedBy))
			{
				FStructC& Ext = G.World.Struct[Node.ClaimedBy];
				if (!Ext.bComplete) { HC.State = EHarvestState::WaitBuild; break; }
				if (Ext.DockedHarvester == INVALID_ENTITY || !G.World.IsAlive(Ext.DockedHarvester))
				{
					// Attach to the tube.
					Ext.DockedHarvester = Eid;
					HC.State = EHarvestState::Docked;
					HC.DockTicksLeft = FMath::Max(1, FMath::RoundToInt32(T->DockTime * SIM_TICKS_PER_SEC));
					M->Waypoints.Reset();
					M->Order = EMoveOrder::Idle;
					P->P = NodeP;
				}
				// else: tube busy, hover and wait our turn.
			}
			else
			{
				// Untapped geyser: drop the crawler.
				if (!T->CrawlerId.IsNone() && !G.World.IsAlive(HC.Crawler))
				{
					const FEntityId Crawler = G.SpawnUnit(Player, T->CrawlerId, NodeP + FVector2f(0.9f, 0.6f));
					if (Crawler != INVALID_ENTITY)
					{
						G.World.Crawl.Add(Crawler, { HC.NodeId });
						if (FMoverC* CM = G.World.Mover.Find(Crawler))
						{
							CM->Order = EMoveOrder::Move;
							CM->Waypoints = { NodeP };
						}
						HC.Crawler = Crawler;
					}
				}
				HC.State = EHarvestState::WaitBuild;
			}
			break;
		}
		case EHarvestState::WaitBuild:
		{
			if (Node.Amount <= 0.f) { HC.State = EHarvestState::None; break; }
			if (Node.ClaimedBy != INVALID_ENTITY && G.World.IsAlive(Node.ClaimedBy)
				&& G.World.Struct[Node.ClaimedBy].bComplete)
			{
				HC.State = EHarvestState::ToGeyser;   // dock on the next pass
			}
			else if (Node.ClaimedBy == INVALID_ENTITY && !G.World.IsAlive(HC.Crawler))
			{
				HC.State = EHarvestState::ToGeyser;   // crawler lost: redeploy
			}
			break;
		}
		case EHarvestState::Docked:
		{
			if (Node.ClaimedBy == INVALID_ENTITY || !G.World.IsAlive(Node.ClaimedBy))
			{
				HC.State = EHarvestState::ToGeyser;   // extractor destroyed under us
				break;
			}
			P->P = NodeP;   // pinned to the tube while filling
			if (--HC.DockTicksLeft <= 0)
			{
				FStructC& Ext = G.World.Struct[Node.ClaimedBy];
				if (Ext.DockedHarvester == Eid) { Ext.DockedHarvester = INVALID_ENTITY; }
				HC.GasHeld = FMath::Min(Node.Amount, T->GasCapacity);
				Node.Amount -= HC.GasHeld;
				FVector2f Depot;
				if (HC.GasHeld > 0.f && G.FindDepot(Player, P->P, Depot))
				{
					HC.State = EHarvestState::Deliver;
					M->Waypoints = { Depot };
					M->Order = EMoveOrder::Move;
				}
				else { HC.State = EHarvestState::None; }
			}
			break;
		}
		case EHarvestState::Deliver:
		{
			FVector2f Depot;
			if (!G.FindDepot(Player, P->P, Depot)) { HC.State = EHarvestState::None; break; }
			if ((P->P - Depot).Size() > 2.2f)
			{
				if (M->Waypoints.Num() == 0) { M->Waypoints = { Depot }; M->Order = EMoveOrder::Move; }
				break;
			}
			float Mult = 1.f;
			if (G.Players[Player].bIsAI)
			{
				Mult = (G.Difficulty == ESimDifficulty::Easy) ? 0.6f
					: (G.Difficulty == ESimDifficulty::Hard) ? 1.4f : 1.f;
			}
			G.Players[Player].KiTrin += HC.GasHeld * Mult;
			UE_LOG(LogTemp, Log, TEXT("Harvester %d delivered %.0f KiTrin (player %d now %.0f)"),
				Eid, HC.GasHeld * Mult, Player, G.Players[Player].KiTrin);
			HC.GasHeld = 0.f;
			if (Node.Amount > 0.f)
			{
				HC.State = EHarvestState::ToGeyser;
				M->Waypoints = { NodeP };
				M->Order = EMoveOrder::Move;
			}
			else { HC.State = EHarvestState::None; }
			break;
		}
		default: break;
		}
	}

	// Production queues (HQ, colonies, carriers).
	for (const FEntityId Eid : SimSortedKeys(G.World.Prod))
	{
		FProdC& Prod = G.World.Prod[Eid];
		if (Prod.Queue.Num() == 0) { continue; }
		const FStructC* S = G.World.Struct.Find(Eid);
		if (S && !S->bComplete) { continue; }
		const int32 Player = G.World.Own[Eid].Player;
		const FUnitTpl* T = G.Content.Unit(G.Players[Player].FactionId, Prod.Queue[0]);
		if (!T) { Prod.Queue.RemoveAt(0); Prod.Progress = 0.f; continue; }
		Prod.Progress += SIM_DT;
		if (Prod.Progress >= T->BuildTime)
		{
			const FVector2f At = FindSpawnSlot(G, G.World.Pos[Eid].P);
			G.SpawnUnit(Player, Prod.Queue[0], At);
			Prod.Queue.RemoveAt(0);
			Prod.Progress = 0.f;
		}
	}

	// Repair vessels: heal the most-damaged friendly in range.
	for (const FEntityId Eid : SimSortedKeys(G.World.Repair))
	{
		const FRepairC& R = G.World.Repair[Eid];
		const FPosC* P = G.World.Pos.Find(Eid);
		if (!P) { continue; }
		const int32 Player = G.World.Own[Eid].Player;
		FEntityId Best = INVALID_ENTITY;
		float BestFrac = 1.f;
		for (const FEntityId Other : SimSortedKeys(G.World.Health))
		{
			if (Other == Eid) { continue; }
			const FOwnerC* OO = G.World.Own.Find(Other);
			if (!OO || OO->Player != Player) { continue; }
			const FPosC* OP = G.World.Pos.Find(Other);
			if (!OP || (OP->P - P->P).Size() > R.Range) { continue; }
			const FHealthC& H = G.World.Health[Other];
			const float Frac = H.Hp / H.MaxHp;
			if (Frac < BestFrac && Frac < 1.f) { BestFrac = Frac; Best = Other; }
		}
		if (Best != INVALID_ENTITY)
		{
			FHealthC& H = G.World.Health[Best];
			H.Hp = FMath::Min(H.MaxHp, H.Hp + R.Rate * SIM_DT);
		}
	}
}

// ---------------------------------------------------------------------------

void SimSys::RunMovement(FSimGame& G)
{
	for (const FEntityId Eid : SimSortedKeys(G.World.Mover))
	{
		if (G.World.Morph.Contains(Eid)) { continue; }   // cocoons are sessile
		FMoverC& M = G.World.Mover[Eid];
		FPosC& P = G.World.Pos[Eid];
		const FUnitC& U = G.World.Unit[Eid];
		// Air flies over terrain; Ground (crawlers) walks on it. Only naval
		// hulls are constrained to water.
		const bool bAir = U.Domain != EUnitDomain::Naval;

		// Direct player control, boat-style: W/S is the throttle along the
		// hull's heading (reverse at 40%), A/D is the rudder. Rudder
		// authority grows with way on the boat but never quite vanishes,
		// so you can still come about at a standstill.
		if (const FManualC* MC = G.World.Manual.Find(Eid))
		{
			const float Throttle = FMath::Clamp(MC->Axes.X, -1.f, 1.f);
			const float Rudder = FMath::Clamp(MC->Axes.Y, -1.f, 1.f);

			const float TurnRate = bAir ? 1.4f : 1.8f;   // rad/s at full throttle
			const float Authority = 0.35f + 0.65f * FMath::Abs(Throttle);
			P.Facing += Rudder * TurnRate * Authority * SIM_DT;

			if (!FMath::IsNearlyZero(Throttle))
			{
				const float Speed = M.Speed * (Throttle > 0.f ? Throttle : Throttle * 0.4f);
				const FVector2f Fwd(FMath::Cos(P.Facing), FMath::Sin(P.Facing));
				const FVector2f NewP = P.P + Fwd * Speed * SIM_DT;
				if (bAir || G.Map.IsWater(NewP)) { P.P = NewP; }
				else if (G.Map.IsWater(FVector2f(NewP.X, P.P.Y))) { P.P.X = NewP.X; }
				else if (G.Map.IsWater(FVector2f(P.P.X, NewP.Y))) { P.P.Y = NewP.Y; }
			}
			continue;
		}

		// Attack-move: pick up hostiles spotted en route.
		if (M.Order == EMoveOrder::AttackMove && M.ChaseTarget == INVALID_ENTITY)
		{
			const FCombatC* C = G.World.Combat.Find(Eid);
			const FVisionC* V = G.World.Vision.Find(Eid);
			if (C && V)
			{
				const int32 Player = G.World.Own[Eid].Player;
				FEntityId Best = INVALID_ENTITY;
				float BestDist = V->Radius;
				for (const FEntityId Other : SimSortedKeys(G.World.Health))
				{
					if (!IsEnemyTargetable(G, Player, Other)) { continue; }
					bool bAnyWeapon = false;
					for (const FWeaponTpl& W : C->Weapons)
					{
						if (MatchesFilter(G, W.Filter, Other)) { bAnyWeapon = true; break; }
					}
					if (!bAnyWeapon) { continue; }
					const float D = (G.World.Pos[Other].P - P.P).Size();
					if (D < BestDist) { BestDist = D; Best = Other; }
				}
				if (Best != INVALID_ENTITY) { M.ChaseTarget = Best; M.RepathCooldown = 0; }
			}
		}

		// Chasing (explicit attack order or attack-move engagement).
		const bool bChasing = (M.Order == EMoveOrder::Chase || M.ChaseTarget != INVALID_ENTITY);
		if (bChasing)
		{
			if (!G.World.IsAlive(M.ChaseTarget))
			{
				M.ChaseTarget = INVALID_ENTITY;
				if (M.Order == EMoveOrder::AttackMove)
				{
					// Resume the sweep toward the original destination.
					M.Waypoints = bAir ? TArray<FVector2f>{ M.OrderDest }
						: G.Pathfinder.FindPath(P.P, M.OrderDest);
				}
				else
				{
					M.Order = EMoveOrder::Idle;
					M.Waypoints.Reset();
				}
			}
			else
			{
				const FVector2f TP = G.World.Pos[M.ChaseTarget].P;
				const FCombatC* C = G.World.Combat.Find(Eid);
				const float HoldRange = C ? C->MaxRange * 0.9f : 1.5f;
				if ((TP - P.P).Size() <= HoldRange)
				{
					M.Waypoints.Reset();     // in range: hold and let combat fire
					const FVector2f D = TP - P.P;
					if (D.SizeSquared() > KINDA_SMALL_NUMBER) { P.Facing = FMath::Atan2(D.Y, D.X); }
				}
				else if (--M.RepathCooldown <= 0)
				{
					M.RepathCooldown = SIM_TICKS_PER_SEC;
					M.Waypoints = bAir ? TArray<FVector2f>{ TP } : G.Pathfinder.FindPath(P.P, TP);
				}
			}
		}

		// Advance along waypoints.
		if (M.Waypoints.Num() > 0)
		{
			const FVector2f Target = M.Waypoints[0];
			FVector2f Delta = Target - P.P;
			const float Dist = Delta.Size();
			const float Step = M.Speed * SIM_DT;
			if (Dist <= FMath::Max(Step, 0.15f))
			{
				P.P = Target;
				M.Waypoints.RemoveAt(0);
				if (M.Waypoints.Num() == 0 && !bChasing)
				{
					if (M.Order == EMoveOrder::Move) { M.Order = EMoveOrder::Idle; }
				}
			}
			else
			{
				Delta /= Dist;
				const FVector2f NewP = P.P + Delta * Step;
				if (bAir || G.Map.IsWater(NewP)) { P.P = NewP; }
				else if (G.Map.IsWater(FVector2f(NewP.X, P.P.Y))) { P.P.X = NewP.X; }
				else if (G.Map.IsWater(FVector2f(P.P.X, NewP.Y))) { P.P.Y = NewP.Y; }
				else { M.Waypoints.Reset(); }   // wedged: drop the path, next order repaths
				P.Facing = FMath::Atan2(Delta.Y, Delta.X);
			}
		}
	}

	// Local separation for naval units (cheap uniform-grid neighborhood).
	TMap<int32, TArray<FEntityId>> Buckets;
	const int32 GridW = G.Map.Width;
	for (const FEntityId Eid : SimSortedKeys(G.World.Mover))
	{
		if (G.World.Unit[Eid].Domain != EUnitDomain::Naval) { continue; }
		const FVector2f& P = G.World.Pos[Eid].P;
		Buckets.FindOrAdd(FMath::FloorToInt32(P.Y) * GridW + FMath::FloorToInt32(P.X)).Add(Eid);
	}
	for (const FEntityId Eid : SimSortedKeys(G.World.Mover))
	{
		if (G.World.Unit[Eid].Domain != EUnitDomain::Naval) { continue; }
		FPosC& P = G.World.Pos[Eid];
		FVector2f Push = FVector2f::ZeroVector;
		const int32 CX = FMath::FloorToInt32(P.P.X);
		const int32 CY = FMath::FloorToInt32(P.P.Y);
		for (int32 DY = -1; DY <= 1; ++DY)
		{
			for (int32 DX = -1; DX <= 1; ++DX)
			{
				const TArray<FEntityId>* Cell = Buckets.Find((CY + DY) * GridW + (CX + DX));
				if (!Cell) { continue; }
				for (const FEntityId Other : *Cell)
				{
					if (Other == Eid) { continue; }
					const FVector2f D = P.P - G.World.Pos[Other].P;
					const float Dist = D.Size();
					if (Dist < 0.9f)
					{
						Push += (Dist > 0.01f) ? D / Dist * (0.9f - Dist)
							: FVector2f((Eid < Other) ? 0.3f : -0.3f, 0.f);
					}
				}
			}
		}
		if (!Push.IsNearlyZero())
		{
			Push = Push.GetSafeNormal() * FMath::Min(Push.Size(), 1.f) * 1.5f * SIM_DT;
			const FVector2f NewP = P.P + Push;
			if (G.Map.IsWater(NewP)) { P.P = NewP; }
		}
	}
}

// ---------------------------------------------------------------------------

void SimSys::RunCombat(FSimGame& G)
{
	// Firing.
	for (const FEntityId Eid : SimSortedKeys(G.World.Combat))
	{
		FCombatC& C = G.World.Combat[Eid];
		if (C.DisruptTicks > 0) { --C.DisruptTicks; }
		for (FWeaponState& W : C.State)
		{
			if (W.CooldownTicks > 0) { --W.CooldownTicks; }
		}
		const FStructC* S = G.World.Struct.Find(Eid);
		if (S && !S->bComplete) { continue; }
		if (G.World.Morph.Contains(Eid)) { continue; }   // cocoons don't fight
		const FPosC* P = G.World.Pos.Find(Eid);
		if (!P) { continue; }
		const int32 Player = G.World.Own[Eid].Player;

		// Direct player control: never auto-fire. While the trigger is held,
		// aim-assist onto the visible enemy nearest the view direction
		// (within a 60-degree cone) and fire every ready weapon at it.
		if (const FManualC* MC = G.World.Manual.Find(Eid))
		{
			C.Target = INVALID_ENTITY;
			if (!MC->bFireHeld) { continue; }
			FEntityId Best = INVALID_ENTITY;
			float BestAngle = FMath::DegreesToRadians(60.f);
			for (const FEntityId Other : SimSortedKeys(G.World.Health))
			{
				if (!IsEnemyTargetable(G, Player, Other)) { continue; }
				const FVector2f To = G.World.Pos[Other].P - P->P;
				if (To.Size() > C.MaxRange) { continue; }
				const float Angle = FMath::Abs(FMath::FindDeltaAngleRadians(
					MC->DesiredFacing, FMath::Atan2(To.Y, To.X)));
				if (Angle < BestAngle) { BestAngle = Angle; Best = Other; }
			}
			if (Best == INVALID_ENTITY) { continue; }
			const FVector2f TP2 = G.World.Pos[Best].P;
			const float Dist2 = (TP2 - P->P).Size();
			for (int32 Wi = 0; Wi < C.Weapons.Num(); ++Wi)
			{
				const FWeaponTpl& W = C.Weapons[Wi];
				if (C.State[Wi].CooldownTicks > 0 || Dist2 > W.Range) { continue; }
				if (!MatchesFilter(G, W.Filter, Best)) { continue; }
				const FEntityId ProjId = G.World.Create();
				FProjC PC;
				PC.P = P->P;
				PC.LastKnown = TP2;
				PC.Target = Best;
				PC.Speed = W.ProjSpeed;
				PC.Damage = W.Damage;
				PC.Type = W.Type;
				PC.SourcePlayer = Player;
				G.World.Proj.Add(ProjId, PC);
				PushShotEvent(G, Eid, Player, P->P);
				const float ReloadMult = (C.DisruptTicks > 0) ? 1.6f : 1.f;
				C.State[Wi].CooldownTicks =
					FMath::Max(1, FMath::RoundToInt32(W.Reload * ReloadMult * SIM_TICKS_PER_SEC));
			}
			continue;
		}

		// Prefer an explicit chase target when it is in range.
		const FMoverC* M = G.World.Mover.Find(Eid);
		if (M && M->ChaseTarget != INVALID_ENTITY && IsEnemyTargetable(G, Player, M->ChaseTarget)
			&& (G.World.Pos[M->ChaseTarget].P - P->P).Size() <= C.MaxRange)
		{
			C.Target = M->ChaseTarget;
		}

		// Validate or (re)acquire: lowest HP in range wins => natural focus fire.
		if (!IsEnemyTargetable(G, Player, C.Target)
			|| (G.World.Pos[C.Target].P - P->P).Size() > C.MaxRange * 1.15f)
		{
			C.Target = INVALID_ENTITY;
			float BestHp = TNumericLimits<float>::Max();
			for (const FEntityId Other : SimSortedKeys(G.World.Health))
			{
				if (!IsEnemyTargetable(G, Player, Other)) { continue; }
				if ((G.World.Pos[Other].P - P->P).Size() > C.MaxRange) { continue; }
				bool bAnyWeapon = false;
				for (const FWeaponTpl& W : C.Weapons)
				{
					if (MatchesFilter(G, W.Filter, Other)) { bAnyWeapon = true; break; }
				}
				if (!bAnyWeapon) { continue; }
				const float Hp = G.World.Health[Other].Hp;
				if (Hp < BestHp) { BestHp = Hp; C.Target = Other; }
			}
		}
		if (C.Target == INVALID_ENTITY) { continue; }

		const FVector2f TP = G.World.Pos[C.Target].P;
		const float Dist = (TP - P->P).Size();
		for (int32 Wi = 0; Wi < C.Weapons.Num(); ++Wi)
		{
			const FWeaponTpl& W = C.Weapons[Wi];
			if (C.State[Wi].CooldownTicks > 0 || Dist > W.Range) { continue; }
			if (!MatchesFilter(G, W.Filter, C.Target)) { continue; }

			const FEntityId Proj = G.World.Create();
			FProjC PC;
			PC.P = P->P;
			PC.LastKnown = TP;
			PC.Target = C.Target;
			PC.Speed = W.ProjSpeed;
			PC.Damage = W.Damage;
			PC.Type = W.Type;
			PC.SourcePlayer = Player;
			G.World.Proj.Add(Proj, PC);
			PushShotEvent(G, Eid, Player, P->P);

			const float ReloadMult = (C.DisruptTicks > 0) ? 1.6f : 1.f;
			C.State[Wi].CooldownTicks = FMath::Max(1, FMath::RoundToInt32(W.Reload * ReloadMult * SIM_TICKS_PER_SEC));
		}
	}

	// Homing projectiles.
	for (const FEntityId Eid : SimSortedKeys(G.World.Proj))
	{
		FProjC& P = G.World.Proj[Eid];
		if (G.World.IsAlive(P.Target))
		{
			P.LastKnown = G.World.Pos[P.Target].P;
		}
		const FVector2f Delta = P.LastKnown - P.P;
		const float Dist = Delta.Size();
		const float Step = P.Speed * SIM_DT;
		if (Dist <= FMath::Max(Step, 0.35f))
		{
			if (G.World.IsAlive(P.Target))
			{
				G.ApplyDamage(P.Target, P.Damage, P.Type, P.SourcePlayer);
			}
			// Surface impacts cue a foam splash (AA bursts pop in the air).
			if (P.Type != EWeaponType::AA)
			{
				G.PushEvent(FSimEvent::EKind::Splash, P.SourcePlayer, P.P, FString());
			}
			G.World.Destroy(Eid);
			continue;
		}
		P.P += Delta / Dist * Step;
		if (--P.LifeTicks <= 0) { G.World.Destroy(Eid); }
	}
}

// ---------------------------------------------------------------------------

void SimSys::RunFog(FSimGame& G)
{
	const int32 W = G.Map.Width;
	const int32 H = G.Map.Height;
	for (int32 Player = 0; Player < 2; ++Player)
	{
		FSimPlayer& Pl = G.Players[Player];
		Pl.Visible.Init(false, W * H);
	}
	for (const FEntityId Eid : SimSortedKeys(G.World.Vision))
	{
		const FOwnerC* O = G.World.Own.Find(Eid);
		const FPosC* P = G.World.Pos.Find(Eid);
		if (!O || !P || O->Player < 0) { continue; }
		FSimPlayer& Pl = G.Players[O->Player];
		const float R = G.World.Vision[Eid].Radius;
		const int32 IR = FMath::CeilToInt32(R);
		const int32 CX = FMath::FloorToInt32(P->P.X);
		const int32 CY = FMath::FloorToInt32(P->P.Y);
		const float R2 = R * R;
		for (int32 DY = -IR; DY <= IR; ++DY)
		{
			for (int32 DX = -IR; DX <= IR; ++DX)
			{
				if (float(DX * DX + DY * DY) > R2) { continue; }
				const int32 X = CX + DX, Y = CY + DY;
				if (X < 0 || Y < 0 || X >= W || Y >= H) { continue; }
				Pl.Visible[Y * W + X] = true;
				Pl.Explored[Y * W + X] = true;
			}
		}
	}
}

// ---------------------------------------------------------------------------

void SimSys::RunCleanup(FSimGame& G)
{
	TArray<FEntityId> Dead;
	for (const FEntityId Eid : SimSortedKeys(G.World.Health))
	{
		if (G.World.Health[Eid].Hp <= 0.f) { Dead.Add(Eid); }
	}
	for (const FEntityId Eid : Dead)
	{
		const int32 Player = G.World.Own[Eid].Player;
		if (const FStructC* S = G.World.Struct.Find(Eid))
		{
			// Release node and island claims.
			if (S->NodeId >= 0 && S->NodeId < G.Map.Nodes.Num()
				&& G.Map.Nodes[S->NodeId].ClaimedBy == Eid)
			{
				G.Map.Nodes[S->NodeId].ClaimedBy = INVALID_ENTITY;
			}
			for (FSimIsland& Isl : G.Map.Islands)
			{
				if (Isl.ClaimStructure == Eid)
				{
					Isl.OwnerPlayer = -1;
					Isl.ClaimStructure = INVALID_ENTITY;
				}
			}
			if (Eid == G.Players[Player].HqEid) { G.Players[Player].bDefeated = true; }
		}
		G.World.Destroy(Eid);
	}

	if (G.Winner < 0)
	{
		for (int32 P = 0; P < 2; ++P)
		{
			if (G.Players[P].bDefeated)
			{
				G.Winner = 1 - P;
				G.PushEvent(FSimEvent::EKind::Victory, G.Winner, FVector2f::ZeroVector,
					FString::Printf(TEXT("%s wins!"), *G.FactionOf(G.Winner).DisplayName));
			}
		}
	}
}
