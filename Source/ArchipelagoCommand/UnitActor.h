#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Sim/SimTypes.h"
#include "UnitActor.generated.h"

class UProceduralMeshComponent;
class UStaticMeshComponent;
class AACGameMode;
struct FSimGame;

// Visual proxy for one sim entity (ship, aircraft or structure). Reads sim
// state, never writes it. Hulls are procedural low-poly meshes; ships ride
// the ocean's wave function; superstructures are engine basic shapes.
UCLASS()
class AUnitActor : public AActor
{
	GENERATED_BODY()

public:
	AUnitActor();

	void InitFor(const FSimGame& G, FEntityId InEid);

	// Called by the game mode after every sim tick.
	void SetSimState(const FVector2f& Pos, float Facing, float HpFrac, float BuildProgress);
	// Called by the game mode every frame; Alpha = sub-tick interpolation.
	void UpdateVisual(float Alpha, float WorldTime);

	void SetSelected(bool bSelected);

	FEntityId Eid = INVALID_ENTITY;
	int32 OwnerPlayer = -1;
	bool bIsStructure = false;
	bool bIsAir = false;
	float HpFraction = 1.f;
	FName TplId;
	FString DisplayName;

private:
	void BuildBoat(float Length, float Beam, float Height, const FLinearColor& Color, int32 Tier);
	void BuildAircraft(float Size, const FLinearColor& Color);
	void BuildStructure(EStructureKind Kind, const FLinearColor& Color, float Footprint);
	UStaticMeshComponent* AddBox(const FVector& Offset, const FVector& Scale, const FLinearColor& Color);
	UStaticMeshComponent* AddShape(const TCHAR* MeshPath, const FVector& Offset, const FVector& Scale, const FLinearColor& Color);

	UPROPERTY()
	TObjectPtr<USceneComponent> Root;

	UPROPERTY()
	TObjectPtr<UProceduralMeshComponent> Hull;

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> SelectionRing;

	AACGameMode* GM = nullptr;   // cached for water-surface queries
	FVector2f PrevPos = FVector2f::ZeroVector;
	FVector2f CurrPos = FVector2f::ZeroVector;
	float PrevFacing = 0.f;
	float CurrFacing = 0.f;
	float BuildProgress = 1.f;
	float BobDamp = 1.f;      // big ships ride the swell less
	float RingRadius = 400.f;
};

// Pooled visual for a sim projectile: a small bright sphere, color-coded by
// weapon type (torpedoes run just under the surface).
UCLASS()
class AProjectileActor : public AActor
{
	GENERATED_BODY()

public:
	AProjectileActor();
	void InitFor(EWeaponType Type);
	void SetSimState(const FVector2f& Pos);
	void UpdateVisual(float Alpha, float WorldTime);

	FEntityId Eid = INVALID_ENTITY;

private:
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Ball;

	EWeaponType WeaponType = EWeaponType::Shell;
	bool bJustInit = true;
	AACGameMode* GM = nullptr;
	FVector2f PrevPos = FVector2f::ZeroVector;
	FVector2f CurrPos = FVector2f::ZeroVector;
};
