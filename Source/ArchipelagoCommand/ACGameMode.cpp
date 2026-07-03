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
#include "WaterZoneActor.h"
#include "WaterBodyOceanActor.h"
#include "WaterBodyOceanComponent.h"
#include "WaterBodyComponent.h"
#include "WaterSplineComponent.h"
#include "WaterWaves.h"
#include "WaterTerrainComponent.h"
#include "NiagaraSystem.h"
#include "NiagaraFunctionLibrary.h"
#include "UnrealClient.h"
#include "Engine/StaticMesh.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"

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
	bClassicOcean = FParse::Param(CmdLine, TEXT("ClassicOcean"));
	FParse::Value(CmdLine, TEXT("AutoShotAt="), AutoShotAt);

	SplashFX = LoadObject<UNiagaraSystem>(nullptr,
		TEXT("/Niagara/DefaultAssets/Templates/Systems/DirectionalBurst.DirectionalBurst"));
	if (!SplashFX)
	{
		UE_LOG(LogACGame, Warning, TEXT("Splash Niagara template not found; impacts will have no foam burst"));
	}

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
	Sun->SetIntensity(3.f);
	Sun->SetAtmosphereSunLight(true);
	Sun->RegisterComponent();

	USkyAtmosphereComponent* Sky = NewObject<USkyAtmosphereComponent>(Env, TEXT("SkyAtmosphere"));
	Sky->SetupAttachment(Root);
	Sky->RegisterComponent();

	USkyLightComponent* Ambient = NewObject<USkyLightComponent>(Env, TEXT("SkyLight"));
	Ambient->SetupAttachment(Root);
	Ambient->bRealTimeCapture = true;
	Ambient->SetIntensity(0.8f);
	Ambient->RegisterComponent();

	UExponentialHeightFogComponent* Fog = NewObject<UExponentialHeightFogComponent>(Env, TEXT("Fog"));
	Fog->SetupAttachment(Root);
	// Keep haze subtle: at RTS camera distances a dense height fog washes
	// the whole scene toward pastel blue.
	Fog->SetFogDensity(0.000006f);
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

	// 3D world: Epic Water plugin sea when available (classic procedural
	// sheet as fallback), then islands and resource decorations. The
	// AOceanActor always spawns: it owns the fish and the fallback waves.
	const bool bPluginSea = !bClassicOcean && TrySpawnPluginOcean();
	Ocean = GetWorld()->SpawnActor<AOceanActor>();
	Ocean->Build(SimGame->Map.Width, !bPluginSea);
	WorldActors.Add(Ocean);
	UE_LOG(LogACGame, Log, TEXT("Ocean renderer: %s"),
		bPluginSea ? TEXT("Epic Water plugin") : TEXT("classic procedural"));

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

bool AACGameMode::TrySpawnPluginOcean()
{
#if !WITH_EDITOR
	// The water body info meshes can only be built by the editor-side mesh
	// builder; packaged builds use the classic procedural sea instead.
	return false;
#else
	// Mirror what the editor's water actor factory assigns on placement:
	// ocean surface material, underwater post-process, Gerstner wave asset.
	UMaterialInterface* OceanMat = LoadObject<UMaterialInterface>(nullptr,
		TEXT("/Water/Materials/WaterSurface/Water_Material_Ocean.Water_Material_Ocean"));
	UMaterialInterface* UnderwaterMat = LoadObject<UMaterialInterface>(nullptr,
		TEXT("/Water/Materials/PostProcessing/M_UnderWater_PostProcess_Volume.M_UnderWater_PostProcess_Volume"));
	UWaterWavesAsset* WavesAsset = LoadObject<UWaterWavesAsset>(nullptr,
		TEXT("/Water/Waves/GerstnerWaves_Ocean.GerstnerWaves_Ocean"));
	if (!OceanMat || !WavesAsset)
	{
		UE_LOG(LogACGame, Warning, TEXT("Water plugin content unavailable; using classic ocean"));
		return false;
	}

	const float MapSize = SimGame->Map.Width * AC_CELL;
	const FVector Center(MapSize * 0.5f, MapSize * 0.5f, AC_SEA_LEVEL);

	WaterZone = GetWorld()->SpawnActor<AWaterZone>(AWaterZone::StaticClass(), Center, FRotator::ZeroRotator);
	AWaterBodyOcean* OceanBody =
		GetWorld()->SpawnActor<AWaterBodyOcean>(AWaterBodyOcean::StaticClass(), Center, FRotator::ZeroRotator);
	if (!WaterZone || !OceanBody)
	{
		UE_LOG(LogACGame, Warning, TEXT("Water plugin actors failed to spawn; using classic ocean"));
		if (WaterZone) { WaterZone->Destroy(); WaterZone = nullptr; }
		if (OceanBody) { OceanBody->Destroy(); }
		return false;
	}
	WaterZone->SetZoneExtent(FVector2D(MapSize * 2.f, MapSize * 2.f));

	UWaterWavesAssetReference* WavesRef = NewObject<UWaterWavesAssetReference>(OceanBody);
	WavesRef->SetWaterWavesAsset(WavesAsset);
	OceanBody->SetWaterWaves(WavesRef);

	UWaterBodyComponent* Comp = OceanBody->GetWaterBodyComponent();
	Comp->SetWaterAndUnderWaterPostProcessMaterial(OceanMat, UnderwaterMat);

	// An ocean body needs a closed spline, but the polygon marks LAND (the
	// hole in the ocean; see FWaterQuadTree::AddOceanRecursive) — water
	// fills the zone outside it. Park a tiny triangle under the first home
	// island so effectively the whole zone is water.
	if (UWaterSplineComponent* Spline = Comp->GetWaterSpline())
	{
		const FVector2D IslandW = SimToWorld2D(
			SimGame->Map.Islands[SimGame->Map.HomeIsland[0]].Center);
		const FVector Rel(IslandW.X - Center.X, IslandW.Y - Center.Y, 0.0);
		Spline->ResetSpline({
			Rel + FVector(900, 0, 0),
			Rel + FVector(-450, 780, 0),
			Rel + FVector(-450, -780, 0) });
	}
	if (UWaterBodyOceanComponent* OceanComp = Cast<UWaterBodyOceanComponent>(Comp))
	{
		OceanComp->SetOceanExtent(FVector2D(MapSize * 2.f, MapSize * 2.f));
		OceanComp->SetCollisionExtents(FVector(MapSize, MapSize, 800.f));
	}

	// Seabed: Epic's water derives its color and opacity from water depth
	// (water info texture = ground vs surface), so the sea needs a floor.
	// A big sandy plane below sea level, registered as water terrain.
	{
		AActor* Seabed = GetWorld()->SpawnActor<AActor>();
		UStaticMeshComponent* Floor = NewObject<UStaticMeshComponent>(Seabed, TEXT("SeabedMesh"));
		Seabed->SetRootComponent(Floor);
		Floor->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane")));
		UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, AC_MAT_BASIC);
		if (Base)
		{
			UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Base, Seabed);
			MID->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.045f, 0.075f, 0.07f));
			Floor->SetMaterial(0, MID);
		}
		Floor->SetWorldLocationAndRotation(FVector(Center.X, Center.Y, -1600.f), FRotator::ZeroRotator);
		Floor->SetWorldScale3D(FVector(MapSize * 2.f / 100.f, MapSize * 2.f / 100.f, 1.f));
		Floor->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Floor->RegisterComponent();
		UWaterTerrainComponent* Terrain = NewObject<UWaterTerrainComponent>(Seabed);
		Terrain->RegisterComponent();
		WorldActors.Add(Seabed);
	}

	// The water mesh quadtree skips any body without built info meshes, and
	// those are normally built in the editor and saved with the level. For a
	// runtime-spawned ocean we must build them ourselves: Movable mobility
	// (dynamic data changes are blocked for registered Static components),
	// then an explicit render-data rebuild.
	Comp->SetMobility(EComponentMobility::Movable);
	Comp->UpdateWaterBodyRenderData();

	// Force a full rebuild now that shape, waves and materials are set.
	FOnWaterBodyChangedParams ChangedParams;
	ChangedParams.bShapeOrPositionChanged = true;
	ChangedParams.bUserTriggered = true;
	Comp->UpdateAll(ChangedParams);

	PluginOcean = OceanBody;
	PluginOceanComp = Comp;
	WorldActors.Add(WaterZone);
	WorldActors.Add(OceanBody);
	return true;
#endif // WITH_EDITOR
}

void AACGameMode::SampleOceanSurface(const FVector2D& XY, float WorldTime, FVector& OutLocation, FVector& OutNormal) const
{
	if (PluginOceanComp)
	{
		auto Result = PluginOceanComp->TryQueryWaterInfoClosestToWorldLocation(
			FVector(XY.X, XY.Y, AC_SEA_LEVEL),
			EWaterBodyQueryFlags::ComputeLocation | EWaterBodyQueryFlags::ComputeNormal
			| EWaterBodyQueryFlags::IncludeWaves);
		if (Result.HasValue())
		{
			OutLocation = Result.GetValue().GetWaterSurfaceLocation();
			OutNormal = Result.GetValue().GetWaterSurfaceNormal();
			return;
		}
	}
	OutLocation = FVector(XY.X, XY.Y, AOceanActor::WaveHeight(XY, WorldTime));
	const FVector2D Grad = AOceanActor::WaveGradient(XY, WorldTime);
	OutNormal = FVector(-Grad.X, -Grad.Y, 1.f).GetSafeNormal();
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
	WaterZone = nullptr;
	PluginOcean = nullptr;
	PluginOceanComp = nullptr;
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

	// Debug capture for automated visual checks (-AutoShotAt=N).
	if (AutoShotAt > 0.f && Now >= AutoShotAt)
	{
		AutoShotAt = -1.f;
		FScreenshotRequest::RequestScreenshot(TEXT("AutoShot.png"), true, false);
		UE_LOG(LogACGame, Log, TEXT("Auto screenshot requested"));
	}
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
	int32 SplashBudget = 8;   // cap FX spawned per drain so big battles stay smooth
	for (const FSimEvent& E : SimGame->Events)
	{
		if (E.Kind == FSimEvent::EKind::Splash)
		{
			const bool bSeen = bSpectate || E.Player == LocalPlayer()
				|| SimGame->IsVisibleTo(LocalPlayer(), E.Pos);
			if (SplashFX && bSeen && SplashBudget-- > 0)
			{
				const FVector2D XY = SimToWorld2D(E.Pos);
				FVector Loc, Normal;
				SampleOceanSurface(XY, float(Now), Loc, Normal);
				UNiagaraFunctionLibrary::SpawnSystemAtLocation(GetWorld(), SplashFX,
					Loc, FRotator(90.f, 0.f, 0.f), FVector(1.5f), true);
			}
			continue;
		}
		// Only surface the local player's alerts (victory is for everyone).
		if (E.Kind == FSimEvent::EKind::Victory || E.Player == LocalPlayer() || bSpectate)
		{
			ActiveAlerts.Insert({ E.Text, Now + 5.0 }, 0);
		}
	}
	if (ActiveAlerts.Num() > 4) { ActiveAlerts.SetNum(4); }
	SimGame->Events.Reset();
}
