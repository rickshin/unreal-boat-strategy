#include "SimContent.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

DEFINE_LOG_CATEGORY_STATIC(LogSimContent, Log, All);

static EWeaponType ParseWeaponType(const FString& S)
{
	if (S == TEXT("missile")) return EWeaponType::Missile;
	if (S == TEXT("torpedo")) return EWeaponType::Torpedo;
	if (S == TEXT("aa")) return EWeaponType::AA;
	if (S == TEXT("disrupt")) return EWeaponType::Disrupt;
	return EWeaponType::Shell;
}

static ETargetFilter ParseFilter(const FString& S)
{
	if (S == TEXT("naval")) return ETargetFilter::Naval;
	if (S == TEXT("air")) return ETargetFilter::Air;
	return ETargetFilter::Any;
}

static EStructureKind ParseKind(const FString& S)
{
	if (S == TEXT("hq")) return EStructureKind::HQ;
	if (S == TEXT("extractor")) return EStructureKind::Extractor;
	if (S == TEXT("colony")) return EStructureKind::Colony;
	if (S == TEXT("defense")) return EStructureKind::Defense;
	return EStructureKind::Outpost;
}

static TArray<FWeaponTpl> ParseWeapons(const TSharedPtr<FJsonObject>& Obj)
{
	TArray<FWeaponTpl> Out;
	const TArray<TSharedPtr<FJsonValue>>* Arr;
	if (!Obj->TryGetArrayField(TEXT("weapons"), Arr)) { return Out; }
	for (const TSharedPtr<FJsonValue>& V : *Arr)
	{
		const TSharedPtr<FJsonObject> W = V->AsObject();
		if (!W.IsValid()) { continue; }
		FWeaponTpl T;
		T.Type = ParseWeaponType(W->GetStringField(TEXT("type")));
		T.Filter = ParseFilter(W->HasField(TEXT("targets")) ? W->GetStringField(TEXT("targets")) : TEXT("any"));
		T.Damage = W->GetNumberField(TEXT("damage"));
		T.Range = W->GetNumberField(TEXT("range"));
		T.Reload = W->GetNumberField(TEXT("reload"));
		T.ProjSpeed = W->HasField(TEXT("projSpeed")) ? W->GetNumberField(TEXT("projSpeed")) : 14.0;
		Out.Add(T);
	}
	return Out;
}

static int32 ParseCost(const TSharedPtr<FJsonObject>& Obj)
{
	// Single-resource economy: "cost": 120 (bare number).
	return Obj->HasField(TEXT("cost")) ? int32(Obj->GetNumberField(TEXT("cost"))) : 0;
}

static TArray<FName> ParseNameArray(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Field)
{
	TArray<FName> Out;
	const TArray<TSharedPtr<FJsonValue>>* Arr;
	if (Obj->TryGetArrayField(Field, Arr))
	{
		for (const TSharedPtr<FJsonValue>& V : *Arr) { Out.Add(FName(*V->AsString())); }
	}
	return Out;
}

bool FContentDB::LoadFromDir(const FString& Dir)
{
	TArray<FString> Files;
	IFileManager::Get().FindFiles(Files, *(Dir / TEXT("*.json")), true, false);
	if (Files.Num() == 0)
	{
		UE_LOG(LogSimContent, Error, TEXT("No faction json found in %s"), *Dir);
		return false;
	}

	for (const FString& File : Files)
	{
		FString Raw;
		if (!FFileHelper::LoadFileToString(Raw, *(Dir / File)))
		{
			UE_LOG(LogSimContent, Error, TEXT("Cannot read %s"), *File);
			return false;
		}
		TSharedPtr<FJsonObject> Root;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Raw);
		if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
		{
			UE_LOG(LogSimContent, Error, TEXT("Bad json in %s"), *File);
			return false;
		}

		FFactionDef F;
		F.Id = FName(*Root->GetStringField(TEXT("faction")));
		F.DisplayName = Root->GetStringField(TEXT("display"));
		F.HqId = FName(*Root->GetStringField(TEXT("hq")));
		F.BuilderId = FName(*Root->GetStringField(TEXT("builder")));
		const TArray<TSharedPtr<FJsonValue>>* Col;
		if (Root->TryGetArrayField(TEXT("color"), Col) && Col->Num() >= 3)
		{
			F.Color = FLinearColor(
				(*Col)[0]->AsNumber() / 255.0, (*Col)[1]->AsNumber() / 255.0, (*Col)[2]->AsNumber() / 255.0);
		}

		const TSharedPtr<FJsonObject> Units = Root->GetObjectField(TEXT("units"));
		for (const auto& Pair : Units->Values)
		{
			const TSharedPtr<FJsonObject> U = Pair.Value->AsObject();
			FUnitTpl T;
			T.Id = FName(*Pair.Key);
			T.Name = U->GetStringField(TEXT("name"));
			const FString Cls = U->GetStringField(TEXT("class"));
			// "harvester" = flying builder that works geysers; "ground" = crawler.
			T.Domain = (Cls == TEXT("air") || Cls == TEXT("harvester")) ? EUnitDomain::Air
				: (Cls == TEXT("ground")) ? EUnitDomain::Ground : EUnitDomain::Naval;
			T.bBuilder = (Cls == TEXT("builder") || Cls == TEXT("harvester"));
			T.bHarvester = (Cls == TEXT("harvester"));
			T.MaxHp = U->GetNumberField(TEXT("hp"));
			T.Armor = int32(U->HasField(TEXT("armor")) ? U->GetNumberField(TEXT("armor")) : 0.0);
			T.Speed = U->GetNumberField(TEXT("speed"));
			T.Vision = U->HasField(TEXT("vision")) ? U->GetNumberField(TEXT("vision")) : 8.0;
			T.BuildTime = U->GetNumberField(TEXT("buildTime"));
			T.RepairRate = U->HasField(TEXT("repairRate")) ? U->GetNumberField(TEXT("repairRate")) : 0.0;
			T.Cost = ParseCost(U);
			if (U->HasField(TEXT("crawler"))) { T.CrawlerId = FName(*U->GetStringField(TEXT("crawler"))); }
			T.GasCapacity = U->HasField(TEXT("gasCapacity")) ? U->GetNumberField(TEXT("gasCapacity")) : 100.0;
			T.DockTime = U->HasField(TEXT("dockTime")) ? U->GetNumberField(TEXT("dockTime")) : 10.0;
			T.FireSound = U->HasField(TEXT("fireSound")) ? U->GetStringField(TEXT("fireSound")) : FString();
			T.Weapons = ParseWeapons(U);
			T.Builds = ParseNameArray(U, TEXT("builds"));
			// Morphers: "produces" lists what this unit can become.
			T.bMorph = U->HasField(TEXT("morph")) && U->GetBoolField(TEXT("morph"));
			if (T.bMorph) { T.MorphInto = ParseNameArray(U, TEXT("produces")); }
			// Breeders: auto-spawn children up to a cap (Broodlord-style).
			const TSharedPtr<FJsonObject>* Larva;
			if (U->TryGetObjectField(TEXT("larva"), Larva))
			{
				T.SpawnUnit = FName(*(*Larva)->GetStringField(TEXT("unit")));
				T.SpawnMax = int32((*Larva)->GetNumberField(TEXT("max")));
				T.SpawnInterval = (*Larva)->GetNumberField(TEXT("interval"));
			}
			F.Units.Add(T.Id, T);
		}

		const TSharedPtr<FJsonObject> Structs = Root->GetObjectField(TEXT("structures"));
		for (const auto& Pair : Structs->Values)
		{
			const TSharedPtr<FJsonObject> S = Pair.Value->AsObject();
			FStructTpl T;
			T.Id = FName(*Pair.Key);
			T.Name = S->GetStringField(TEXT("name"));
			T.Kind = ParseKind(S->GetStringField(TEXT("kind")));
			T.MaxHp = S->GetNumberField(TEXT("hp"));
			T.Armor = int32(S->HasField(TEXT("armor")) ? S->GetNumberField(TEXT("armor")) : 0.0);
			T.Vision = S->HasField(TEXT("vision")) ? S->GetNumberField(TEXT("vision")) : 8.0;
			T.BuildTime = S->GetNumberField(TEXT("buildTime"));
			T.Cost = ParseCost(S);
			T.FireSound = S->HasField(TEXT("fireSound")) ? S->GetStringField(TEXT("fireSound")) : FString();
			T.Weapons = ParseWeapons(S);
			T.Produces = ParseNameArray(S, TEXT("produces"));
			const TSharedPtr<FJsonObject>* Larva;
			if (S->TryGetObjectField(TEXT("larva"), Larva))
			{
				T.SpawnUnit = FName(*(*Larva)->GetStringField(TEXT("unit")));
				T.SpawnMax = int32((*Larva)->GetNumberField(TEXT("max")));
				T.SpawnInterval = (*Larva)->GetNumberField(TEXT("interval"));
			}
			F.Structures.Add(T.Id, T);
		}

		Factions.Add(F.Id, F);
		UE_LOG(LogSimContent, Log, TEXT("Loaded faction %s: %d units, %d structures"),
			*F.Id.ToString(), F.Units.Num(), F.Structures.Num());
	}
	return Factions.Num() >= 2;
}
