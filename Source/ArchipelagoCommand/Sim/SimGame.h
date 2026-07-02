#pragma once

#include "CoreMinimal.h"
#include "SimTypes.h"
#include "SimContent.h"
#include "SimMap.h"
#include "Pathfinder.h"
#include "SimAI.h"

// ---------------------------------------------------------------------------
// Components (pure data; all behavior lives in the systems)
// ---------------------------------------------------------------------------

struct FPosC
{
	FVector2f P = FVector2f::ZeroVector;   // cell units
	float Facing = 0.f;                    // radians
};

struct FOwnerC { int32 Player = -1; };

struct FUnitC
{
	FName Tpl;
	EUnitDomain Domain = EUnitDomain::Naval;
	bool bBuilder = false;
};

struct FStructC
{
	FName Tpl;
	EStructureKind Kind = EStructureKind::Outpost;
	bool bComplete = false;
	float Progress = 0.f;          // 0..1 construction
	int32 IslandId = -1;           // claimed island (outpost/colony)
	int32 NodeId = -1;             // mined node (miners)
};

struct FHealthC
{
	float Hp = 100.f;
	float MaxHp = 100.f;
	int32 Armor = 0;
};

enum class EMoveOrder : uint8 { Idle, Move, AttackMove, Chase };

struct FMoverC
{
	float Speed = 3.f;
	EMoveOrder Order = EMoveOrder::Idle;
	TArray<FVector2f> Waypoints;
	FVector2f OrderDest = FVector2f::ZeroVector;
	FEntityId ChaseTarget = INVALID_ENTITY;
	int32 RepathCooldown = 0;      // ticks
};

struct FWeaponState { int32 CooldownTicks = 0; };

struct FCombatC
{
	TArray<FWeaponTpl> Weapons;    // copied from template at spawn
	TArray<FWeaponState> State;
	FEntityId Target = INVALID_ENTITY;
	int32 DisruptTicks = 0;        // EW debuff: reload x1.6 while > 0
	float MaxRange = 0.f;
};

struct FProdC
{
	TArray<FName> Queue;           // unit template ids (cost paid on enqueue)
	float Progress = 0.f;          // seconds into Queue[0]
	TArray<FName> Options;         // what this producer may build
};

struct FMinerC { int32 NodeId = -1; float Rate = 5.f; float Accum = 0.f; };

struct FRepairC { float Rate = 15.f; float Range = 4.f; };

struct FVisionC { float Radius = 8.f; };

struct FProjC
{
	FVector2f P = FVector2f::ZeroVector;
	FVector2f LastKnown = FVector2f::ZeroVector;
	FEntityId Target = INVALID_ENTITY;
	float Speed = 14.f;
	float Damage = 10.f;
	EWeaponType Type = EWeaponType::Shell;
	int32 SourcePlayer = -1;
	int32 LifeTicks = 200;
};

// Builder is executing/escorting a construction site.
struct FBuildTaskC
{
	FEntityId Site = INVALID_ENTITY;   // the under-construction structure
};

// ---------------------------------------------------------------------------
// World: entities are ints, one TMap store per component type. Systems must
// iterate via SimSortedKeys() for determinism.
// ---------------------------------------------------------------------------

struct FSimWorld
{
	FEntityId NextId = 1;
	TSet<FEntityId> Alive;

	TMap<FEntityId, FPosC> Pos;
	TMap<FEntityId, FOwnerC> Own;
	TMap<FEntityId, FUnitC> Unit;
	TMap<FEntityId, FStructC> Struct;
	TMap<FEntityId, FHealthC> Health;
	TMap<FEntityId, FMoverC> Mover;
	TMap<FEntityId, FCombatC> Combat;
	TMap<FEntityId, FProdC> Prod;
	TMap<FEntityId, FMinerC> Miner;
	TMap<FEntityId, FRepairC> Repair;
	TMap<FEntityId, FVisionC> Vision;
	TMap<FEntityId, FProjC> Proj;
	TMap<FEntityId, FBuildTaskC> BuildTask;

	FEntityId Create() { const FEntityId Id = NextId++; Alive.Add(Id); return Id; }

	void Destroy(FEntityId Id)
	{
		Alive.Remove(Id);
		Pos.Remove(Id); Own.Remove(Id); Unit.Remove(Id); Struct.Remove(Id);
		Health.Remove(Id); Mover.Remove(Id); Combat.Remove(Id); Prod.Remove(Id);
		Miner.Remove(Id); Repair.Remove(Id); Vision.Remove(Id); Proj.Remove(Id);
		BuildTask.Remove(Id);
	}

	bool IsAlive(FEntityId Id) const { return Alive.Contains(Id); }
};

// ---------------------------------------------------------------------------
// Commands: the only mutation channel into the sim. UI and AI both use it,
// which is the lockstep-ready boundary.
// ---------------------------------------------------------------------------

enum class ECmdType : uint8 { Move, Attack, AttackMove, Stop, Produce, Build };

struct FSimCommand
{
	ECmdType Type = ECmdType::Move;
	int32 Player = 0;
	TArray<FEntityId> Units;
	FVector2f Target = FVector2f::ZeroVector;
	FEntityId TargetEid = INVALID_ENTITY;
	FName TplId;                        // Produce: unit id, Build: structure id
};

// ---------------------------------------------------------------------------
// Players & game
// ---------------------------------------------------------------------------

struct FSimPlayer
{
	FName FactionId;
	float Wood = 200.f;
	float Iron = 100.f;
	FEntityId HqEid = INVALID_ENTITY;
	bool bDefeated = false;
	bool bIsAI = false;

	TBitArray<> Explored;
	TBitArray<> Visible;

	// Throttled "under attack" alert bookkeeping.
	int32 LastAttackAlertTick = -10000;
	FVector2f LastAttackPos = FVector2f::ZeroVector;
};

struct FSimEvent
{
	enum class EKind : uint8 { UnderAttack, UnitLost, ProductionDone, Victory } Kind;
	int32 Player = -1;
	FVector2f Pos = FVector2f::ZeroVector;
	FString Text;
};

struct FSimGame
{
	FContentDB Content;
	FSimMap Map;
	FSimPlayer Players[2];
	FSimWorld World;
	FSimRng Rng;
	FSimPathfinder Pathfinder;
	ESimDifficulty Difficulty = ESimDifficulty::Normal;

	int32 Tick = 0;
	int32 Winner = -1;                  // -1 while running
	uint64 Seed = 0;

	TArray<FSimCommand> PendingCommands;
	TArray<FSimEvent> Events;           // drained by the render layer each frame
	SimAI::FAIState AIState[2];         // deterministic AI memory, part of sim state

	// -- lifecycle ----------------------------------------------------------
	bool Init(uint64 InSeed, FName Faction0, FName Faction1, ESimDifficulty InDifficulty, const FString& DataDir);
	void Step();                        // one fixed 20 Hz tick
	uint64 StateHash() const;

	// -- spawn helpers (used by setup, economy and command systems) ---------
	FEntityId SpawnUnit(int32 Player, FName TplId, const FVector2f& At);
	FEntityId SpawnStructure(int32 Player, FName TplId, const FVector2f& At, bool bComplete);

	// -- queries ------------------------------------------------------------
	const FFactionDef& FactionOf(int32 Player) const { return *Content.Faction(Players[Player].FactionId); }
	bool IsVisibleTo(int32 Player, const FVector2f& P) const;
	bool CanAfford(int32 Player, int32 Wood, int32 Iron) const
	{
		return Players[Player].Wood >= Wood && Players[Player].Iron >= Iron;
	}
	void PayCost(int32 Player, int32 Wood, int32 Iron)
	{
		Players[Player].Wood -= Wood;
		Players[Player].Iron -= Iron;
	}
	// Structure placement validation, shared by UI ghost and AI.
	bool IsValidBuildSite(int32 Player, FName TplId, const FVector2f& At, FString* WhyNot = nullptr) const;

	// Central damage calc (spec 10): armor mitigation 6%/point capped at
	// 60%, reduced by weapon armor pierce.
	void ApplyDamage(FEntityId Target, float Damage, EWeaponType Type, int32 SourcePlayer);

	void PushEvent(FSimEvent::EKind Kind, int32 Player, const FVector2f& Pos, const FString& Text)
	{
		Events.Add({ Kind, Player, Pos, Text });
	}
};
