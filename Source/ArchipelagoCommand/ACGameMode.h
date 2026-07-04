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
class UAudioComponent;
class USoundWaveProcedural;
class UDirectionalLightComponent;

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
	void ApplyTimeOfDay(uint64 Seed);
	bool TrySpawnPluginOcean();
	void SyncEntityActors();
	void UpdateVisuals();
	void DrainSimEvents();
	void StartMusic();
	void PlayNextTrack();

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

	UPROPERTY()
	TObjectPtr<UDirectionalLightComponent> SunLight;   // re-aimed per seed

	// Soundtrack: mp3s from Audio/soundtrack/, shuffled, decoded off-thread
	// (-NoMusic disables, -MusicVolume=0..1 adjusts).
	UPROPERTY()
	TObjectPtr<UAudioComponent> MusicComp;
	UPROPERTY()
	TObjectPtr<USoundWaveProcedural> MusicWave;
	TArray<FString> MusicPlaylist;
	int32 MusicIndex = 0;
	double MusicEndTime = -1.0;
	float MusicVolume = 0.5f;
	bool bMusicEnabled = true;
	bool bMusicDecoding = false;

	// One-shot effects (Audio/effects/<name>.mp3): decoded once into a
	// cache, played spatialized at the event location.
	void PlayEffectSound(const FString& Name, const FVector2f& SimPos);
	TMap<FString, TSharedPtr<struct FDecodedMusic>> SfxCache;
	UPROPERTY()
	TObjectPtr<class USoundAttenuation> SfxAttenuation;

	// Ambient bed: Audio/ambient/ocean-peaceful.mp3 looped by re-queueing
	// the decoded PCM before the buffer drains (-NoAmbient disables).
	void StartAmbient();
	UPROPERTY()
	TObjectPtr<UAudioComponent> AmbientComp;
	UPROPERTY()
	TObjectPtr<USoundWaveProcedural> AmbientWave;
	TArray<uint8> AmbientPCM;
	float AmbientDuration = 0.f;
	double AmbientRequeueAt = -1.0;
	bool bAmbientEnabled = true;
};
