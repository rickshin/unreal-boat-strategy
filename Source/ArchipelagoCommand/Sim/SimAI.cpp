#include "SimAI.h"
#include "SimGame.h"

namespace
{
	// -- shared queries -------------------------------------------------------

	TArray<FEntityId> OwnedUnits(const FSimGame& G, int32 Player, bool bCombatOnly)
	{
		TArray<FEntityId> Out;
		for (const FEntityId Eid : SimSortedKeys(G.World.Unit))
		{
			const FOwnerC* O = G.World.Own.Find(Eid);
			if (!O || O->Player != Player) { continue; }
			if (bCombatOnly && (!G.World.Combat.Contains(Eid) || G.World.Unit[Eid].bBuilder)) { continue; }
			Out.Add(Eid);
		}
		return Out;
	}

	FEntityId FindIdleBuilder(const FSimGame& G, int32 Player)
	{
		for (const FEntityId Eid : SimSortedKeys(G.World.Unit))
		{
			const FOwnerC* O = G.World.Own.Find(Eid);
			if (!O || O->Player != Player || !G.World.Unit[Eid].bBuilder) { continue; }
			if (G.World.BuildTask.Contains(Eid)) { continue; }
			return Eid;
		}
		return INVALID_ENTITY;
	}

	int32 CountStructures(const FSimGame& G, int32 Player, EStructureKind Kind)
	{
		int32 N = 0;
		for (const auto& Pair : G.World.Struct)
		{
			const FOwnerC* O = G.World.Own.Find(Pair.Key);
			if (O && O->Player == Player && Pair.Value.Kind == Kind) { ++N; }
		}
		return N;
	}

	// A water spot from which a miner reaches the node / an outpost reaches the island.
	FVector2f SiteNearCell(const FSimGame& G, const FIntPoint& Cell)
	{
		return G.Map.NearestWater(FVector2f(Cell.X + 0.5f, Cell.Y + 0.5f));
	}

	// -- strategic layer ------------------------------------------------------

	void Strategic(FSimGame& G, int32 Player)
	{
		SimAI::FAIState& S = G.AIState[Player];
		const FFactionDef& F = G.FactionOf(Player);
		const FSimPlayer& Pl = G.Players[Player];
		const FVector2f Home = G.Map.HomeSpawn[Player];

		// Structure ids by kind, from data.
		FName MinerWoodId, MinerIronId, OutpostId, DefenseId;
		for (const auto& Pair : F.Structures)
		{
			switch (Pair.Value.Kind)
			{
			case EStructureKind::MinerWood: MinerWoodId = Pair.Key; break;
			case EStructureKind::MinerIron: MinerIronId = Pair.Key; break;
			case EStructureKind::Outpost:   OutpostId = Pair.Key; break;
			case EStructureKind::Defense:   DefenseId = Pair.Key; break;
			default: break;
			}
		}

		// 1) Economy: claim the nearest free node with a mining structure.
		const FEntityId Builder = FindIdleBuilder(G, Player);
		if (Builder != INVALID_ENTITY)
		{
			struct FCandidate { FName Tpl; FVector2f At; float Dist; };
			TOptional<FCandidate> Best;
			for (const FSimResourceNode& N : G.Map.Nodes)
			{
				if (N.ClaimedBy != INVALID_ENTITY || N.Amount <= 0.f) { continue; }
				const FName Tpl = (N.Type == EResourceType::Wood) ? MinerWoodId : MinerIronId;
				if (Tpl.IsNone()) { continue; }
				const FVector2f At = SiteNearCell(G, N.Cell);
				const float Dist = (At - Home).Size();
				if (Dist > 45.f) { continue; }   // don't stretch across the map early
				if (!G.IsValidBuildSite(Player, Tpl, At)) { continue; }
				const FStructTpl* T = G.Content.Structure(Pl.FactionId, Tpl);
				if (!T || !G.CanAfford(Player, T->CostWood, T->CostIron)) { continue; }
				if (!Best.IsSet() || Dist < Best->Dist) { Best = FCandidate{ Tpl, At, Dist }; }
			}
			if (Best.IsSet())
			{
				FSimCommand Cmd;
				Cmd.Type = ECmdType::Build;
				Cmd.Player = Player;
				Cmd.Units = { Builder };
				Cmd.Target = Best->At;
				Cmd.TplId = Best->Tpl;
				G.PendingCommands.Add(Cmd);
				return;   // one strategic action per pass keeps decisions readable
			}

			// 2) Expansion: outpost on the nearest unclaimed island once economy runs.
			const int32 Miners = CountStructures(G, Player, EStructureKind::MinerWood)
				+ CountStructures(G, Player, EStructureKind::MinerIron);
			if (Miners >= 2 && !OutpostId.IsNone())
			{
				const FStructTpl* T = G.Content.Structure(Pl.FactionId, OutpostId);
				if (T && G.CanAfford(Player, T->CostWood + 40, T->CostIron))
				{
					int32 BestIsland = -1;
					float BestDist = TNumericLimits<float>::Max();
					for (const FSimIsland& Isl : G.Map.Islands)
					{
						if (Isl.OwnerPlayer >= 0) { continue; }
						const float Dist = (Isl.Center - Home).Size();
						if (Dist < BestDist) { BestDist = Dist; BestIsland = Isl.Id; }
					}
					if (BestIsland >= 0)
					{
						const FSimIsland& Isl = G.Map.Islands[BestIsland];
						const FVector2f Dir = (Home - Isl.Center).GetSafeNormal();
						const FVector2f At = G.Map.NearestWater(Isl.Center + Dir * (Isl.Radius + 1.5f));
						if (G.IsValidBuildSite(Player, OutpostId, At))
						{
							FSimCommand Cmd;
							Cmd.Type = ECmdType::Build;
							Cmd.Player = Player;
							Cmd.Units = { Builder };
							Cmd.Target = At;
							Cmd.TplId = OutpostId;
							G.PendingCommands.Add(Cmd);
							return;
						}
					}
				}
			}
		}

		// 3) Army composition: keep every production queue busy. Walk the
		// producer's option list with a rotating cursor weighted toward cheap
		// units early and capital ships once iron flows.
		for (const FEntityId Eid : SimSortedKeys(G.World.Prod))
		{
			const FOwnerC* O = G.World.Own.Find(Eid);
			if (!O || O->Player != Player) { continue; }
			const FProdC& Prod = G.World.Prod[Eid];
			if (Prod.Queue.Num() >= 2 || Prod.Options.Num() == 0) { continue; }

			// Keep one builder alive.
			if (Prod.Options.Contains(F.BuilderId))
			{
				bool bHasBuilder = false;
				for (const FEntityId U : OwnedUnits(G, Player, false))
				{
					if (G.World.Unit[U].bBuilder) { bHasBuilder = true; break; }
				}
				if (!bHasBuilder)
				{
					FSimCommand Cmd;
					Cmd.Type = ECmdType::Produce;
					Cmd.Player = Player;
					Cmd.TargetEid = Eid;
					Cmd.TplId = F.BuilderId;
					G.PendingCommands.Add(Cmd);
					continue;
				}
			}

			// Pick an affordable combat option, rotating for mixed fleets.
			for (int32 Try = 0; Try < Prod.Options.Num(); ++Try)
			{
				const FName Opt = Prod.Options[(S.CompositionCursor + Try) % Prod.Options.Num()];
				const FUnitTpl* T = G.Content.Unit(Pl.FactionId, Opt);
				if (!T || T->bBuilder || T->Weapons.Num() == 0) { continue; }
				// Save iron for capital ships only when rich.
				if (T->Value() > 250 && Pl.Iron < T->CostIron + 100) { continue; }
				if (!G.CanAfford(Player, T->CostWood, T->CostIron)) { continue; }
				FSimCommand Cmd;
				Cmd.Type = ECmdType::Produce;
				Cmd.Player = Player;
				Cmd.TargetEid = Eid;
				Cmd.TplId = Opt;
				G.PendingCommands.Add(Cmd);
				S.CompositionCursor = (S.CompositionCursor + Try + 1) % FMath::Max(1, Prod.Options.Num());
				break;
			}
		}
	}

	// -- operational layer ----------------------------------------------------

	void Operational(FSimGame& G, int32 Player)
	{
		SimAI::FAIState& S = G.AIState[Player];
		const FVector2f Home = G.Map.HomeSpawn[Player];
		const FVector2f EnemyHome = G.Map.HomeSpawn[1 - Player];
		S.StagingPoint = G.Map.NearestWater(Home + (EnemyHome - Home) * 0.22f);

		// Base defense first: if home is under attack, everyone comes back.
		const FSimPlayer& Pl = G.Players[Player];
		const bool bHomeThreatened =
			(G.Tick - Pl.LastAttackAlertTick) < 6 * SIM_TICKS_PER_SEC
			&& (Pl.LastAttackPos - Home).Size() < 30.f;

		TArray<FEntityId> Army = OwnedUnits(G, Player, true);
		int32 ArmyValue = 0;
		for (const FEntityId Eid : Army)
		{
			const FUnitTpl* T = G.Content.Unit(Pl.FactionId, G.World.Unit[Eid].Tpl);
			if (T) { ArmyValue += T->Value(); }
		}

		if (bHomeThreatened)
		{
			S.bAttacking = false;
			FSimCommand Cmd;
			Cmd.Type = ECmdType::AttackMove;
			Cmd.Player = Player;
			Cmd.Units = Army;
			Cmd.Target = Pl.LastAttackPos;
			G.PendingCommands.Add(Cmd);
			return;
		}

		// Attack waves: threshold grows with game time so waves escalate.
		const int32 Minutes = G.Tick / (60 * SIM_TICKS_PER_SEC);
		int32 Threshold = 350 + Minutes * 120;
		if (G.Difficulty == ESimDifficulty::Easy) { Threshold += 250; }
		if (G.Difficulty == ESimDifficulty::Hard) { Threshold -= 120; }

		if (!S.bAttacking && ArmyValue >= Threshold && G.Tick >= S.NextWaveTick)
		{
			S.bAttacking = true;
			FSimCommand Cmd;
			Cmd.Type = ECmdType::AttackMove;
			Cmd.Player = Player;
			Cmd.Units = Army;
			Cmd.Target = EnemyHome;
			G.PendingCommands.Add(Cmd);
		}
		else if (S.bAttacking && ArmyValue < Threshold / 3)
		{
			// Wave spent: retreat, rebuild, try again later.
			S.bAttacking = false;
			S.NextWaveTick = G.Tick + 45 * SIM_TICKS_PER_SEC;
			FSimCommand Cmd;
			Cmd.Type = ECmdType::Move;
			Cmd.Player = Player;
			Cmd.Units = Army;
			Cmd.Target = S.StagingPoint;
			G.PendingCommands.Add(Cmd);
		}
		else if (!S.bAttacking)
		{
			// Gather idle combat units at the staging point.
			TArray<FEntityId> Idle;
			for (const FEntityId Eid : Army)
			{
				const FMoverC* M = G.World.Mover.Find(Eid);
				if (M && M->Order == EMoveOrder::Idle
					&& (G.World.Pos[Eid].P - S.StagingPoint).Size() > 8.f)
				{
					Idle.Add(Eid);
				}
			}
			if (Idle.Num() > 0)
			{
				FSimCommand Cmd;
				Cmd.Type = ECmdType::AttackMove;
				Cmd.Player = Player;
				Cmd.Units = Idle;
				Cmd.Target = S.StagingPoint;
				G.PendingCommands.Add(Cmd);
			}
		}
	}

	// -- tactical layer -------------------------------------------------------

	void Tactical(FSimGame& G, int32 Player)
	{
		// Per-unit retreat at low HP; focus fire is handled by Combat targeting.
		const FVector2f Home = G.Map.HomeSpawn[Player];
		TArray<FEntityId> Wounded;
		for (const FEntityId Eid : OwnedUnits(G, Player, true))
		{
			const FHealthC& H = G.World.Health[Eid];
			if (H.Hp > H.MaxHp * 0.22f) { continue; }
			const FMoverC* M = G.World.Mover.Find(Eid);
			if (!M) { continue; }
			if (M->Order == EMoveOrder::Move && (M->OrderDest - Home).Size() < 6.f) { continue; } // already fleeing
			Wounded.Add(Eid);
		}
		if (Wounded.Num() > 0)
		{
			FSimCommand Cmd;
			Cmd.Type = ECmdType::Move;
			Cmd.Player = Player;
			Cmd.Units = Wounded;
			Cmd.Target = Home;
			G.PendingCommands.Add(Cmd);
		}
	}
}

void SimAI::Run(FSimGame& G)
{
	if (G.Winner >= 0) { return; }
	for (int32 Player = 0; Player < 2; ++Player)
	{
		if (!G.Players[Player].bIsAI) { continue; }
		// Staggered cadences, offset per player so both AIs never think on
		// the same tick (keeps the per-tick budget flat).
		const int32 Offset = Player * 3;
		if ((G.Tick + Offset) % (2 * SIM_TICKS_PER_SEC) == 0) { Strategic(G, Player); }
		if ((G.Tick + Offset) % SIM_TICKS_PER_SEC == 0) { Operational(G, Player); }
		if ((G.Tick + Offset) % (SIM_TICKS_PER_SEC / 4) == 0) { Tactical(G, Player); }
	}
}
