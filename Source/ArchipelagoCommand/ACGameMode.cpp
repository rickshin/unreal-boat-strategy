#include "ACGameMode.h"
#include "ACTypes.h"
#include "OceanActor.h"
#include "IslandActor.h"
#include "UnitActor.h"
#include "RTSCameraPawn.h"
#include "RTSPlayerController.h"
#include "RTSHUD.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "GameFramework/PlayerStart.h"
#include "Kismet/GameplayStatics.h"

DEFINE_LOG_CATEGORY_STATIC(LogACGame, Log, All);

AACGameMode::AACGameMode()
{
	PrimaryActorTick.bCanEverTick = true;
	DefaultPawnClass = ARTSCameraPawn::StaticClass();
	PlayerControllerClass = ARTSPlayerController::StaticClass();
	HUDClass = ARTSHUD::StaticClass();
}

AActor* AACGameMode::ChoosePlayerStart_Implementation(AController* Player)
{
	if (AActor* Found = Super::ChoosePlayerStart_Implementation(Player)) { return Found; }
	return GetWorld()->SpawnActor<APlayerStart>(
		APlayerStart::StaticClass(), FVector(0, 0, 10000.f), FRotator::ZeroRotator);
}

void AACGameMode::BeginPlay()
{
	Super::BeginPlay();

	// Session options from the command line:
	//   -Seed=7 -Faction=tempest -Difficulty=easy|normal|hard -Spectate
	const TCHAR* CmdLine = FCommandLine::Get();
	uint64 Seed = uint64(FPlatformTime::Cycles64() & 0xFFFFFF);
	FParse::Value(CmdLine, TEXT("Seed="), Seed);
	FString FactionStr;
	if (FParse::Value(CmdLine, TEXT("Faction="), FactionStr) && FactionStr.Equals(TEXT("tempest"), ESearchCase::IgnoreCase))
	{
		PlayerFaction = TEXT("tempest");
		EnemyFaction = TEXT("dominion");
	}
	FString DiffStr;
	if (FParse::Value(CmdLine, TEXT("Difficulty="), DiffStr))
	{
		if (DiffStr.Equals(TEXT("easy"), ESearchCase::IgnoreCase)) { Difficulty = ESimDifficulty::Easy; }
		else if (DiffStr.Equals(TEXT("hard"), ESearchCase::IgnoreCase)) { Difficulty = ESimDifficulty::Hard; }
	}
	bSpectate = FParse::Param(CmdLine, TEXT("Spectate"));

	SpawnEnvironment();
	StartMatch(Seed);
}

void AACGameMode::SpawnEnvironment()
{
	// One holder actor carrying sun, sky, ambient and fog: no map assets needed.
	AActor* Env = GetWorld()->SpawnActor<AActor>();
	USceneComponent* Root = NewObject<USceneComponent>(Env, TEXT("Root"));
	Env->SetRootComponent(Root);
	Root->RegisterComponent();

	UDirectionalLightComponent* Sun = NewObject<UDirectionalLightComponent>(Env, TEXT("Sun"));
	Sun->SetupAttachment(Root);
	Sun->SetWorldRotation(FRotator(-48.f, 35.f, 0.f));
	Sun->SetIntensity(7.f);
	Sun->SetAtmosphereSunLight(true);
	Sun->RegisterComponent();

	USkyAtmosphereComponent* Sky = NewObject<USkyAtmosphereComponent>(Env, TEXT("SkyAtmosphere"));
	Sky->SetupAttachment(Root);
	Sky->RegisterComponent();

	USkyLightComponent* Ambient = NewObject<USkyLightComponent>(Env, TEXT("SkyLight"));
	Ambient->SetupAttachment(Root);
	Ambient->bRealTimeCapture = true;
	Ambient->SetIntensity(1.4f);
	Ambient->RegisterComponent();

	UExponentialHeightFogComponent* Fog = NewObject<UExponentialHeightFogComponent>(Env, TEXT("Fog"));
	Fog->SetupAttachment(Root);
	Fog->SetFogDensity(0.00004f);
	Fog->SetFogHeightFalloff(0.05f);
	Fog->RegisterComponent();
}

void AACGameMode::StartMatch(uint64 Seed)
{
	CurrentSeed = Seed;
	SimGame = MakeUnique<FSimGame>();

	const FString DataDir = FPaths::ProjectContentDir() / TEXT("Data");
	if (!SimGame->Init(Seed, PlayerFaction, EnemyFaction, Difficulty, DataDir))
	{
		UE_LOG(LogACGame, Error, TEXT("Sim init failed (data dir: %s)"), *DataDir);
		SimGame.Reset();
		return;
	}
	for (int32 P = 0; P < 2; ++P)
	{
		SimGame->Players[P].bIsAI = bSpectate || (P != LocalPlayer());
	}

	// 3D world: ocean sheet, islands, resource decorations.
	Ocean = GetWorld()->SpawnActor<AOceanActor>();
	Ocean->Build(SimGame->Map.Width);
	WorldActors.Add(Ocean);

	for (const FSimIsland& Island : SimGame->Map.Islands)
	{
		AIslandActor* Isl = GetWorld()->SpawnActor<AIslandActor>();
		Isl->Build(SimGame->Map, Island);
		WorldActors.Add(Isl);
	}
	for (const FSimResourceNode& Node : SimGame->Map.Nodes)
	{
		ANodeDecorActor* Decor = GetWorld()->SpawnActor<ANodeDecorActor>();
		Decor->Build(Node);
		WorldActors.Add(Decor);
		NodeDecors.Add(Decor);
	}

	SyncEntityActors();
	UpdateVisuals();

	// Start looking at the player's HQ.
	APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
	if (PC)
	{
		if (ARTSCameraPawn* Cam = Cast<ARTSCameraPawn>(PC->GetPawn()))
		{
			Cam->ClampToMap(SimGame->Map.Width * AC_CELL);
			if (const FPosC* P = SimGame->World.Pos.Find(SimGame->Players[LocalPlayer()].HqEid))
			{
				Cam->JumpTo(SimToWorld2D(P->P));
			}
		}
	}
	UE_LOG(LogACGame, Log, TEXT("Match started: seed %llu, hash %llu"), Seed, SimGame->StateHash());
}

void AACGameMode::TearDownMatch()
{
	for (auto& Pair : EntityActorMap) { if (Pair.Value) { Pair.Value->Destroy(); } }
	EntityActorMap.Reset();
	for (auto& Pair : ProjectileActorMap) { if (Pair.Value) { Pair.Value->Destroy(); } }
	ProjectileActorMap.Reset();
	for (AProjectileActor* P : ProjectilePool) { if (P) { P->Destroy(); } }
	ProjectilePool.Reset();
	for (AActor* A : WorldActors) { if (A) { A->Destroy(); } }
	WorldActors.Reset();
	NodeDecors.Reset();
	Ocean = nullptr;
	ActiveAlerts.Reset();
	Accumulator = 0.f;
	SimGame.Reset();
}

void AACGameMode::RequestRestart()
{
	TearDownMatch();
	StartMatch(CurrentSeed + 1);   // a fresh archipelago every match
}

void AACGameMode::QueueCommand(const FSimCommand& Cmd)
{
	if (SimGame.IsValid()) { SimGame->PendingCommands.Add(Cmd); }
}

void AACGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!SimGame.IsValid()) { return; }

	// Fixed 20 Hz simulation, decoupled from the render rate. Cap catch-up
	// steps so a hitch never spirals.
	Accumulator += FMath::Min(DeltaSeconds, 0.25f);
	bool bStepped = false;
	int32 Steps = 0;
	while (Accumulator >= SIM_DT && Steps < 5)
	{
		SimGame->Step();
		Accumulator -= SIM_DT;
		++Steps;
		bStepped = true;
	}
	if (bStepped)
	{
		SyncEntityActors();
		DrainSimEvents();
	}
	UpdateVisuals();

	const double Now = GetWorld()->GetTimeSeconds();
	ActiveAlerts.RemoveAll([Now](const FACAlert& A) { return Now > A.Expiry; });
}

void AACGameMode::SyncEntityActors()
{
	FSimGame& G = *SimGame;

	// Units & structures.
	for (const FEntityId Eid : SimSortedKeys(G.World.Pos))
	{
		if (G.World.Proj.Contains(Eid)) { continue; }
		AUnitActor*& Actor = EntityActorMap.FindOrAdd(Eid);
		if (!Actor)
		{
			Actor = GetWorld()->SpawnActor<AUnitActor>();
			Actor->InitFor(G, Eid);
		}
		const FPosC& P = G.World.Pos[Eid];
		const FHealthC* H = G.World.Health.Find(Eid);
		const FStructC* S = G.World.Struct.Find(Eid);
		Actor->SetSimState(P.P, P.Facing,
			H ? H->Hp / H->MaxHp : 1.f,
			S ? S->Progress : 1.f);

		// Fog of war: hide enemies outside the local player's vision.
		const bool bMine = G.World.Own[Eid].Player == LocalPlayer();
		const bool bVisible = bSpectate || bMine || G.IsVisibleTo(LocalPlayer(), P.P);
		Actor->SetActorHiddenInGame(!bVisible);
	}
	// Remove actors whose entities died.
	for (auto It = EntityActorMap.CreateIterator(); It; ++It)
	{
		if (!G.World.IsAlive(It.Key()))
		{
			if (It.Value()) { It.Value()->Destroy(); }
			It.RemoveCurrent();
		}
	}

	// Projectiles ride a small actor pool.
	for (const FEntityId Eid : SimSortedKeys(G.World.Proj))
	{
		const FProjC& Proj = G.World.Proj[Eid];
		AProjectileActor*& Actor = ProjectileActorMap.FindOrAdd(Eid);
		if (!Actor)
		{
			Actor = ProjectilePool.Num() > 0 ? ProjectilePool.Pop() : GetWorld()->SpawnActor<AProjectileActor>();
			Actor->Eid = Eid;
			Actor->InitFor(Proj.Type);
		}
		Actor->SetSimState(Proj.P);
		const bool bVisible = bSpectate || Proj.SourcePlayer == LocalPlayer()
			|| G.IsVisibleTo(LocalPlayer(), Proj.P);
		Actor->SetActorHiddenInGame(!bVisible);
	}
	for (auto It = ProjectileActorMap.CreateIterator(); It; ++It)
	{
		if (!G.World.Proj.Contains(It.Key()))
		{
			if (It.Value())
			{
				It.Value()->SetActorHiddenInGame(true);
				It.Value()->Eid = INVALID_ENTITY;
				ProjectilePool.Add(It.Value());
			}
			It.RemoveCurrent();
		}
	}

	// Depleted resource fields disappear.
	for (ANodeDecorActor* Decor : NodeDecors)
	{
		if (Decor && Decor->NodeId >= 0 && Decor->NodeId < G.Map.Nodes.Num())
		{
			Decor->SetDepleted(G.Map.Nodes[Decor->NodeId].Amount <= 0.f);
		}
	}
}

void AACGameMode::UpdateVisuals()
{
	const float Alpha = FMath::Clamp(Accumulator / SIM_DT, 0.f, 1.f);
	const float Time = GetWorld()->GetTimeSeconds();
	for (auto& Pair : EntityActorMap)
	{
		if (Pair.Value) { Pair.Value->UpdateVisual(Alpha, Time); }
	}
	for (auto& Pair : ProjectileActorMap)
	{
		if (Pair.Value) { Pair.Value->UpdateVisual(Alpha, Time); }
	}
}

void AACGameMode::DrainSimEvents()
{
	const double Now = GetWorld()->GetTimeSeconds();
	for (const FSimEvent& E : SimGame->Events)
	{
		// Only surface the local player's alerts (victory is for everyone).
		if (E.Kind == FSimEvent::EKind::Victory || E.Player == LocalPlayer() || bSpectate)
		{
			ActiveAlerts.Insert({ E.Text, Now + 5.0 }, 0);
		}
	}
	if (ActiveAlerts.Num() > 4) { ActiveAlerts.SetNum(4); }
	SimGame->Events.Reset();
}
