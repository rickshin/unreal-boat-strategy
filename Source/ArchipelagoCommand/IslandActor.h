#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "IslandActor.generated.h"

class UProceduralMeshComponent;
struct FSimIsland;
struct FSimMap;
struct FSimResourceNode;

// One 3D island: a smooth heightfield raised from the sim's land cells,
// with render-only rock decorations scattered along the coast (doc 18).
UCLASS()
class AIslandActor : public AActor
{
	GENERATED_BODY()

public:
	AIslandActor();
	void Build(const FSimMap& Map, const FSimIsland& Island);

private:
	UPROPERTY()
	TObjectPtr<UProceduralMeshComponent> Mesh;
};

// Visual marker for a KiTrin geyser: a rocky vent with a shining orange
// core that sprays glittering star-like motes (Niagara, render-only).
UCLASS()
class ANodeDecorActor : public AActor
{
	GENERATED_BODY()

public:
	ANodeDecorActor();
	void Build(const FSimResourceNode& Node);
	void SetDepleted(bool bDepleted);

	int32 NodeId = -1;

private:
	UPROPERTY()
	TObjectPtr<USceneComponent> Root;

	UPROPERTY()
	TObjectPtr<class UNiagaraComponent> Stars;
};
