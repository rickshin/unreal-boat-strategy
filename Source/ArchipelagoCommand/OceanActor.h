#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "OceanActor.generated.h"

class UProceduralMeshComponent;
class UStaticMeshComponent;

// 3D rendered water: a CPU-animated Gerstner-wave grid. Purely visual —
// the sim never reads it. Boats sample WaveHeight() so hulls ride the
// same swell the mesh shows. Also owns the pooled fish decorations
// (design doc 18: zero gameplay impact, render-only RNG).
UCLASS()
class AOceanActor : public AActor
{
	GENERATED_BODY()

public:
	AOceanActor();

	// SizeCells: sim map dimension; the sheet extends a margin beyond it.
	// bShowWaterSheet=false keeps only the fish (used when the Epic Water
	// plugin renders the sea; WaveHeight stays available as a fallback).
	void Build(int32 SizeCells, bool bShowWaterSheet = true);

	virtual void Tick(float DeltaSeconds) override;

	// Shared swell function (world XY in UU, returns Z offset in UU).
	static float WaveHeight(const FVector2D& XY, float Time);
	// Approximate wave normal tilt for hull pitch/roll.
	static FVector2D WaveGradient(const FVector2D& XY, float Time);

private:
	UPROPERTY()
	TObjectPtr<UProceduralMeshComponent> Mesh;

	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> Fish;

	TArray<FVector> Vertices;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	bool bSheetVisible = true;
	int32 GridVerts = 0;
	float SheetOrigin = 0.f;
	float SheetStep = 0.f;

	struct FFishState { FVector2D Center; float Radius; float Phase; float Speed; };
	TArray<FFishState> FishStates;
};
