#pragma once

#include "CoreMinimal.h"
#include "SimTypes.h"

// Data-driven content loaded from Content/Data/<faction>.json.
// All unit/structure/weapon stats live in data; code only interprets kinds.

struct FWeaponTpl
{
	EWeaponType Type = EWeaponType::Shell;
	ETargetFilter Filter = ETargetFilter::Any;
	float Damage = 10.f;
	float Range = 6.f;        // sim cells
	float Reload = 1.5f;      // seconds
	float ProjSpeed = 14.f;   // cells/sec

	// Armor penetration by weapon type (spec 10): missiles ignore 30% of
	// armor, torpedoes 50%.
	float ArmorPierce() const
	{
		switch (Type)
		{
		case EWeaponType::Missile: return 0.30f;
		case EWeaponType::Torpedo: return 0.50f;
		default: return 0.f;
		}
	}
};

struct FUnitTpl
{
	FName Id;
	FString Name;
	EUnitDomain Domain = EUnitDomain::Naval;
	bool bBuilder = false;
	bool bHarvester = false;  // flying gas harvester (drops a crawler on geysers)
	float MaxHp = 100.f;
	int32 Armor = 0;
	float Speed = 3.f;        // cells/sec
	float Vision = 8.f;       // cells
	int32 Cost = 0;           // KiTrin
	float BuildTime = 10.f;   // seconds
	float RepairRate = 0.f;   // hp/sec (repair vessels)
	FName CrawlerId;          // harvester: crawler unit it deploys
	float GasCapacity = 100.f;// harvester: KiTrin per load
	float DockTime = 10.f;    // harvester: seconds attached to the tube
	FString FireSound;        // Audio/effects/<name>.mp3 on every shot (render-only)
	TArray<FWeaponTpl> Weapons;
	TArray<FName> Builds;     // builder: structure ids; carrier: aircraft ids
	int32 Value() const { return Cost; }
};

struct FStructTpl
{
	FName Id;
	FString Name;
	EStructureKind Kind = EStructureKind::Outpost;
	float MaxHp = 400.f;
	int32 Armor = 2;
	float Vision = 8.f;
	int32 Cost = 0;           // KiTrin
	float BuildTime = 15.f;
	FString FireSound;        // Audio/effects/<name>.mp3 on every shot (render-only)
	TArray<FName> Produces;   // unit ids
	TArray<FWeaponTpl> Weapons;
};

struct FFactionDef
{
	FName Id;
	FString DisplayName;
	FLinearColor Color = FLinearColor::White;
	FName HqId;
	FName BuilderId;
	TMap<FName, FUnitTpl> Units;
	TMap<FName, FStructTpl> Structures;
};

struct FContentDB
{
	TMap<FName, FFactionDef> Factions;

	// Loads every *.json in Dir. Returns false (and logs) on parse errors.
	bool LoadFromDir(const FString& Dir);

	const FFactionDef* Faction(FName Id) const { return Factions.Find(Id); }
	const FUnitTpl* Unit(FName FactionId, FName TplId) const
	{
		const FFactionDef* F = Factions.Find(FactionId);
		return F ? F->Units.Find(TplId) : nullptr;
	}
	const FStructTpl* Structure(FName FactionId, FName TplId) const
	{
		const FFactionDef* F = Factions.Find(FactionId);
		return F ? F->Structures.Find(TplId) : nullptr;
	}
};
