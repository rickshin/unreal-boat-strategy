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
			// A harvester mid-cycle is not idle; don't yank it off its geyser.
			const FHarvestC* HC = G.World.Harvest.Find(Eid);
			if (HC && HC->State != EHarvestState::None) { continue; }
			return Eid;
		}
		return INVALID_ENTITY;
	}

	// -- strategic layer ------------------------------------------------------

	void Strategic(FSimGame& G, int32 Player)
	{
		SimAI::FAIState& S = G.AIState[Player];
		const FFactionDef& F = G.FactionOf(Player);
		const FSimPlayer& Pl = G.Players[Player];
		const FVector2f Home = G.Map.HomeSpawn[Player];

		// Structure ids by kind, from data.
		FName OutpostId, DefenseId;
		for (const auto& Pair : F.Structures)
		{
			switch (Pair.Value.Kind)
			{
			case EStructureKind::Outpost:   OutpostId = Pair.Key; break;
			case EStructureKind::Defense:   DefenseId = Pair.Key; break;
			default: break;
			}
		}

		// Count harvesters and decide whether one should be held back to
		// found an outpost this pass (otherwise they all fly off to gas).
		int32 Harvesters = 0;
		for (const FEntityId Eid : SimSortedKeys(G.World.Harvest))
		{
			const FOwnerC* O = G.World.Own.Find(Eid);
			if (O && O->Player == Player) { ++Harvesters; }
		}
		bool bAnyFreeIsland = false;
		for (const FSimIsland& Isl : G.Map.Islands)
		{
			if (Isl.OwnerPlayer < 0) { bAnyFreeIsland = true; break; }
		}
		const FStructTpl* OutpostT = OutpostId.IsNone() ? nullptr
			: G.Content.Structure(Pl.FactionId, OutpostId);
		const bool bWantExpand = Harvesters >= 2 && bAnyFreeIsland && OutpostT
			&& G.CanAfford(Player, OutpostT->Cost + 40);

		// 1) Economy: put idle harvesters on the nearest open geyser,
		// reserving one for expansion when an outpost is on the cards.
		bool bReservedBuilder = false;
		for (const FEntityId Eid : SimSortedKeys(G.World.Harvest))
		{
			const FOwnerC* O = G.World.Own.Find(Eid);
			if (!O || O->Player != Player) { continue; }
			const FHarvestC& HC = G.World.Harvest[Eid];
			if (HC.State != EHarvestState::None || G.World.BuildTask.Contains(Eid)) { continue; }
			if (bWantExpand && !bReservedBuilder) { bReservedBuilder = true; continue; }
			int32 BestNode = -1;
			float BestDist = TNumericLimits<float>::Max();
			for (const FSimResourceNode& N : G.Map.Nodes)
			{
				if (N.Amount <= 0.f) { continue; }
				const float Dist = (FVector2f(N.Cell.X + 0.5f, N.Cell.Y + 0.5f) - Home).Size();
				if (Dist < BestDist) { BestDist = Dist; BestNode = N.Id; }
			}
			if (BestNode >= 0)
			{
				FSimCommand Cmd;
				Cmd.Type = ECmdType::Harvest;
				Cmd.Player = Player;
				Cmd.Units = { Eid };
				Cmd.TargetEid = BestNode;
				G.PendingCommands.Add(Cmd);
			}
		}

		// 2) Expansion: outpost on the nearest unclaimed island once the gas
		// is flowing (two or more harvesters ferrying).
		const FEntityId Builder = FindIdleBuilder(G, Player);
		if (Builder != INVALID_ENTITY)
		{
			if (bWantExpand)
			{
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

		// 2.5) Morph factions: idle larvae (weaponless morphers) are the
		// production queue. Builders first when short, then army via the
		// rotating cursor.
		int32 OwnedIslandsM = 0;
		for (const FSimIsland& Isl : G.Map.Islands)
		{
			if (Isl.OwnerPlayer == Player) { ++OwnedIslandsM; }
		}
		const int32 WantHarvestersM = 2 + FMath::Max(0, OwnedIslandsM - 1);
		for (const FEntityId Eid : SimSortedKeys(G.World.Unit))
		{
			const FOwnerC* O = G.World.Own.Find(Eid);
			if (!O || O->Player != Player || G.World.Morph.Contains(Eid)) { continue; }
			const FUnitTpl* T = G.Content.Unit(Pl.FactionId, G.World.Unit[Eid].Tpl);
			if (!T || !T->bMorph || T->Weapons.Num() > 0 || T->MorphInto.Num() == 0) { continue; }

			FName Choice;
			const FUnitTpl* BT = G.Content.Unit(Pl.FactionId, F.BuilderId);
			if (Harvesters < WantHarvestersM && T->MorphInto.Contains(F.BuilderId)
				&& BT && G.CanAfford(Player, BT->Cost))
			{
				Choice = F.BuilderId;
				++Harvesters;
			}
			else
			{
				for (int32 Try = 0; Try < T->MorphInto.Num(); ++Try)
				{
					const FName Opt = T->MorphInto[(S.CompositionCursor + Try) % T->MorphInto.Num()];
					const FUnitTpl* MT = G.Content.Unit(Pl.FactionId, Opt);
					if (!MT || MT->bBuilder || MT->Weapons.Num() == 0) { continue; }
					if (MT->Value() > 250 && Pl.KiTrin < MT->Cost + 100) { continue; }
					if (!G.CanAfford(Player, MT->Cost)) { continue; }
					Choice = Opt;
					S.CompositionCursor = (S.CompositionCursor + Try + 1)
						% FMath::Max(1, T->MorphInto.Num());
					break;
				}
			}
			if (!Choice.IsNone())
			{
				FSimCommand Cmd;
				Cmd.Type = ECmdType::Morph;
				Cmd.Player = Player;
				Cmd.Units = { Eid };
				Cmd.TplId = Choice;
				G.PendingCommands.Add(Cmd);
			}
		}

		// 3) Army composition: keep every production queue busy. Walk the
		// producer's option list with a rotating cursor weighted toward cheap
		// units early and capital ships once the gas flows.
		for (const FEntityId Eid : SimSortedKeys(G.World.Prod))
		{
			const FOwnerC* O = G.World.Own.Find(Eid);
			if (!O || O->Player != Player) { continue; }
			const FProdC& Prod = G.World.Prod[Eid];
			if (Prod.Queue.Num() >= 2 || Prod.Options.Num() == 0) { continue; }

			// Grow the harvester fleet: 2 baseline, +1 per owned island.
			if (Prod.Options.Contains(F.BuilderId))
			{
				int32 OwnedIslands = 0;
				for (const FSimIsland& Isl : G.Map.Islands)
				{
					if (Isl.OwnerPlayer == Player) { ++OwnedIslands; }
				}
				const int32 WantHarvesters = 2 + FMath::Max(0, OwnedIslands - 1);
				if (Harvesters < WantHarvesters)
				{
					const FUnitTpl* BT = G.Content.Unit(Pl.FactionId, F.BuilderId);
					if (BT && G.CanAfford(Player, BT->Cost))
					{
						FSimCommand Cmd;
						Cmd.Type = ECmdType::Produce;
						Cmd.Player = Player;
						Cmd.TargetEid = Eid;
						Cmd.TplId = F.BuilderId;
						G.PendingCommands.Add(Cmd);
						++Harvesters;   // count the one we just queued
						continue;
					}
				}
			}

			// Pick an affordable combat option, rotating for mixed fleets.
			for (int32 Try = 0; Try < Prod.Options.Num(); ++Try)
			{
				const FName Opt = Prod.Options[(S.CompositionCursor + Try) % Prod.Options.Num()];
				const FUnitTpl* T = G.Content.Unit(Pl.FactionId, Opt);
				if (!T || T->bBuilder || T->Weapons.Num() == 0) { continue; }
				// Save up before splurging on capital ships.
				if (T->Value() > 250 && Pl.KiTrin < T->Cost + 100) { continue; }
				if (!G.CanAfford(Player, T->Cost)) { continue; }
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
