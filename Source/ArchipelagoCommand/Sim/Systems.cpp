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
		if (U->Domain == EUnitDomain::Air)
		{
			M->Waypoints = { Dest };   // aircraft fly straight, ignoring terrain
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
			if (!T || !G.CanAfford(Cmd.Player, T->CostWood, T->CostIron))
			{
				if (!G.Players[Cmd.Player].bIsAI)
				{
					G.PushEvent(FSimEvent::EKind::ProductionDone, Cmd.Player, FVector2f::ZeroVector,
						TEXT("Not enough resources"));
				}
				break;
			}
			G.PayCost(Cmd.Player, T->CostWood, T->CostIron);
			Prod->Queue.Add(Cmd.TplId);
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
			if (!T || !G.CanAfford(Cmd.Player, T->CostWood, T->CostIron))
			{
				if (!G.Players[Cmd.Player].bIsAI)
				{
					G.PushEvent(FSimEvent::EKind::ProductionDone, Cmd.Player, FVector2f::ZeroVector,
						TEXT("Not enough resources"));
				}
				break;
			}
			G.PayCost(Cmd.Player, T->CostWood, T->CostIron);
			const FEntityId Site = G.SpawnStructure(Cmd.Player, Cmd.TplId, Cmd.Target, false);
			G.World.BuildTask.Add(Builder, { Site });
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

	// Mining income (AI difficulty scales AI income only).
	for (const FEntityId Eid : SimSortedKeys(G.World.Miner))
	{
		const FStructC* S = G.World.Struct.Find(Eid);
		if (!S || !S->bComplete) { continue; }
		FMinerC& M = G.World.Miner[Eid];
		if (M.NodeId < 0 || M.NodeId >= G.Map.Nodes.Num()) { continue; }
		FSimResourceNode& Node = G.Map.Nodes[M.NodeId];
		if (Node.Amount <= 0.f) { continue; }
		const int32 Player = G.World.Own[Eid].Player;
		float Mult = 1.f;
		if (G.Players[Player].bIsAI)
		{
			Mult = (G.Difficulty == ESimDifficulty::Easy) ? 0.6f
				: (G.Difficulty == ESimDifficulty::Hard) ? 1.4f : 1.f;
		}
		const float Take = FMath::Min(Node.Amount, M.Rate * SIM_DT * Mult);
		Node.Amount -= Take;
		if (Node.Type == EResourceType::Wood) { G.Players[Player].Wood += Take; }
		else { G.Players[Player].Iron += Take; }
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
		FMoverC& M = G.World.Mover[Eid];
		FPosC& P = G.World.Pos[Eid];
		const FUnitC& U = G.World.Unit[Eid];
		const bool bAir = U.Domain == EUnitDomain::Air;

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
		if (G.World.Unit[Eid].Domain == EUnitDomain::Air) { continue; }
		const FVector2f& P = G.World.Pos[Eid].P;
		Buckets.FindOrAdd(FMath::FloorToInt32(P.Y) * GridW + FMath::FloorToInt32(P.X)).Add(Eid);
	}
	for (const FEntityId Eid : SimSortedKeys(G.World.Mover))
	{
		if (G.World.Unit[Eid].Domain == EUnitDomain::Air) { continue; }
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
		const FPosC* P = G.World.Pos.Find(Eid);
		if (!P) { continue; }
		const int32 Player = G.World.Own[Eid].Player;

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
