#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "Sim/SimGame.h"
#include "ACGameMode.generated.h"

class AUnitActor;
class AProjectileActor;
class AOceanActor;
class AIslandActor;
class ANodeDecorActor;
class AWaterZone;
class AWaterBody;
class UWaterBodyComponent;
class UNiagaraSystem;

struct FACAlert
{
	FString Text;
	double Expiry = 0.0;
};

// Owns the deterministic sim and the fixed 20 Hz loop, spawns the 3D world
// (sky, ocean, islands) and keeps one visual actor per sim entity. The
// render side only ever reads the sim; all writes go through QueueCommand.
UCLASS()
class AACGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AACGameMode();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	// The engine Entry map ships without a PlayerStart; provide one.
	virtual AActor* ChoosePlayerStart_Implementation(AController* Player) override;

	FSimGame* Sim() const { return SimGame.Get(); }
	int32 LocalPlayer() const { return 0; }
	bool IsSpectating() const { return bSpectate; }
	void QueueCommand(const FSimCommand& Cmd);
	void RequestRestart();

	AUnitActor* FindEntityActor(FEntityId Eid) const
	{
		AUnitActor* const* Found = EntityActorMap.Find(Eid);
		return Found ? *Found : nullptr;
	}
	const TMap<FEntityId, AUnitActor*>& EntityActors() const { return EntityActorMap; }

	const TArray<FACAlert>& Alerts() const { return ActiveAlerts; }

	// Water surface at a world XY: Epic Water plugin waves when active,
	// otherwise the classic procedural wave function. Render-only.
	void SampleOceanSurface(const FVector2D& XY, float WorldTime, FVector& OutLocation, FVector& OutNormal) const;

private:
	void StartMatch(uint64 Seed);
	void TearDownMatch();
	void SpawnEnvironment();
	bool TrySpawnPluginOcean();
	void SyncEntityActors();
	void UpdateVisuals();
	void DrainSimEvents();

	TUniquePtr<FSimGame> SimGame;
	float Accumulator = 0.f;
	bool bSpectate = false;
	FName PlayerFaction = TEXT("dominion");
	FName EnemyFaction = TEXT("tempest");
	ESimDifficulty Difficulty = ESimDifficulty::Normal;
	uint64 CurrentSeed = 0;

	// Spawned actors are kept alive by the level; plain pointers are fine here.
	TMap<FEntityId, AUnitActor*> EntityActorMap;
	TMap<FEntityId, AProjectileActor*> ProjectileActorMap;
	TArray<AProjectileActor*> ProjectilePool;
	AOceanActor* Ocean = nullptr;
	AWaterZone* WaterZone = nullptr;
	AWaterBody* PluginOcean = nullptr;
	UWaterBodyComponent* PluginOceanComp = nullptr;
	bool bClassicOcean = false;               // -ClassicOcean disables the Water plugin sea
	float AutoShotAt = -1.f;                  // -AutoShotAt=N saves a screenshot N seconds in
	TArray<AActor*> WorldActors;              // islands, node decor, environment
	TArray<ANodeDecorActor*> NodeDecors;
	TArray<FACAlert> ActiveAlerts;

	UPROPERTY()
	TObjectPtr<UNiagaraSystem> SplashFX;      // water-impact foam burst
};
