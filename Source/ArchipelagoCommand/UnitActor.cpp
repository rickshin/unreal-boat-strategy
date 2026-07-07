#include "UnitActor.h"
#include "ACGameMode.h"
#include "ACTypes.h"
#include "OceanActor.h"
#include "Sim/SimGame.h"
#include "NiagaraSystem.h"
#include "NiagaraFunctionLibrary.h"
#include "ProceduralMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace
{
	UMaterialInstanceDynamic* MakeMID(UObject* Outer, const FLinearColor& Color)
	{
		UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, AC_MAT_BASIC);
		if (!Base) { return nullptr; }
		UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Base, Outer);
		MID->SetVectorParameterValue(TEXT("Color"), Color);
		return MID;
	}

	// Flat-shaded triangle appender (verts duplicated per face).
	struct FMeshBuilder
	{
		TArray<FVector> V;
		TArray<int32> T;
		TArray<FVector> N;
		TArray<FVector2D> UV;

		void Tri(const FVector& A, const FVector& B, const FVector& C)
		{
			const FVector Normal = FVector::CrossProduct(B - A, C - A).GetSafeNormal();
			const int32 I = V.Num();
			V.Append({ A, B, C });
			N.Append({ Normal, Normal, Normal });
			UV.Append({ FVector2D(0, 0), FVector2D(1, 0), FVector2D(0, 1) });
			// UE renders the clockwise-wound side; reverse the index order so
			// the visible face matches the computed outward normal.
			T.Append({ I, I + 2, I + 1 });
		}
		void Quad(const FVector& A, const FVector& B, const FVector& C, const FVector& D)
		{
			Tri(A, B, C);
			Tri(A, C, D);
		}
	};
}

AUnitActor::AUnitActor()
{
	PrimaryActorTick.bCanEverTick = false;   // the game mode drives visuals
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;
	Hull = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Hull"));
	Hull->SetupAttachment(Root);
	Hull->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

UStaticMeshComponent* AUnitActor::AddShape(const TCHAR* MeshPath, const FVector& Offset, const FVector& Scale, const FLinearColor& Color)
{
	UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
	C->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, MeshPath));
	C->SetMaterial(0, MakeMID(this, Color));
	C->SetRelativeLocation(Offset);
	C->SetRelativeScale3D(Scale);
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetupAttachment(Root);
	C->RegisterComponent();
	return C;
}

UStaticMeshComponent* AUnitActor::AddBox(const FVector& Offset, const FVector& Scale, const FLinearColor& Color)
{
	return AddShape(AC_MESH_CUBE, Offset, Scale, Color);
}

void AUnitActor::BuildBoat(float Length, float Beam, float Height, const FLinearColor& Color, int32 Tier)
{
	// Low-poly hull: pointed bow, flared deck, single keel spine. X = forward.
	FMeshBuilder M;
	const float L2 = Length * 0.5f;
	const float B2 = Beam * 0.5f;
	const FVector Bow(L2, 0, Height);
	const FVector DeckSR(Length * 0.12f, -B2, Height);
	const FVector DeckSL(Length * 0.12f, B2, Height);
	const FVector SternR(-L2, -B2 * 0.8f, Height);
	const FVector SternL(-L2, B2 * 0.8f, Height);
	const FVector KeelF(Length * 0.28f, 0, -Height * 0.9f);
	const FVector KeelA(-L2 * 0.9f, 0, -Height * 0.9f);

	// Deck (up).
	M.Tri(Bow, DeckSL, DeckSR);
	M.Quad(DeckSR, DeckSL, SternL, SternR);
	// Starboard side.
	M.Tri(Bow, DeckSR, KeelF);
	M.Quad(DeckSR, SternR, KeelA, KeelF);
	// Port side.
	M.Tri(Bow, KeelF, DeckSL);
	M.Quad(DeckSL, KeelF, KeelA, SternL);
	// Transom.
	M.Tri(SternR, SternL, KeelA);

	Hull->CreateMeshSection_LinearColor(0, M.V, M.T, M.N, M.UV,
		TArray<FLinearColor>(), TArray<FProcMeshTangent>(), false);
	Hull->SetMaterial(0, MakeMID(this, Color));

	// Superstructure grows with tier; turrets on armed tiers.
	const FLinearColor Deck = Color * 0.55f + FLinearColor(0.12f, 0.12f, 0.12f);
	if (Tier >= 1)
	{
		AddBox(FVector(-Length * 0.08f, 0, Height + 34.f),
			FVector(Length * 0.0028f, Beam * 0.0045f, 0.65f), Deck);
	}
	if (Tier >= 2)
	{
		AddBox(FVector(-Length * 0.22f, 0, Height + 96.f),
			FVector(Length * 0.0016f, Beam * 0.0030f, 0.55f), Deck);
		AddShape(AC_MESH_CYLINDER, FVector(Length * 0.22f, 0, Height + 40.f),
			FVector(0.35f, 0.35f, 0.5f), Deck);
	}
	if (Tier >= 3)
	{
		AddShape(AC_MESH_CYLINDER, FVector(-Length * 0.36f, 0, Height + 60.f),
			FVector(0.22f, 0.22f, 1.3f), FLinearColor(0.1f, 0.1f, 0.1f));   // funnel
	}
	RingRadius = Length * 0.62f;
	BobDamp = FMath::Clamp(900.f / Length, 0.35f, 1.f);
	HullLength = Length;
}

void AUnitActor::BuildHarvester(const FLinearColor& Color)
{
	// A chunky lifter drone: body, two side gas tanks, rotor disc, and an
	// orange intake that mates with the extractor tube.
	const FLinearColor Dark = Color * 0.5f + FLinearColor(0.1f, 0.1f, 0.1f);
	AddBox(FVector(0, 0, 0), FVector(3.2f, 2.0f, 1.3f), Color);
	AddShape(AC_MESH_CYLINDER, FVector(0, -130.f, -10.f), FVector(0.8f, 0.8f, 1.9f), Dark);
	AddShape(AC_MESH_CYLINDER, FVector(0, 130.f, -10.f), FVector(0.8f, 0.8f, 1.9f), Dark);
	AddShape(AC_MESH_CYLINDER, FVector(0, 0, 95.f), FVector(2.6f, 2.6f, 0.10f), Dark);
	AddShape(AC_MESH_CYLINDER, FVector(0, 0, -90.f), FVector(0.5f, 0.5f, 0.6f),
		FLinearColor(2.2f, 0.85f, 0.08f));   // glowing intake
	RingRadius = 420.f;
	BobDamp = 0.f;
}

void AUnitActor::BuildCrawler(const FLinearColor& Color)
{
	// Low tracked box that unfolds into the extractor.
	const FLinearColor Tread(0.08f, 0.08f, 0.08f);
	AddBox(FVector(0, 0, 55.f), FVector(1.6f, 1.1f, 0.7f), Color);
	AddBox(FVector(0, -70.f, 25.f), FVector(1.9f, 0.5f, 0.5f), Tread);
	AddBox(FVector(0, 70.f, 25.f), FVector(1.9f, 0.5f, 0.5f), Tread);
	AddShape(AC_MESH_SPHERE, FVector(70.f, 0, 95.f), FVector(0.35f),
		FLinearColor(2.2f, 0.85f, 0.08f));
	RingRadius = 260.f;
	BobDamp = 0.f;
}

void AUnitActor::BuildAircraft(float Size, const FLinearColor& Color)
{
	// Fuselage + swept wing + tail, all flattened boxes.
	AddBox(FVector::ZeroVector, FVector(Size * 0.010f, Size * 0.0016f, 0.16f), Color);
	AddBox(FVector(-Size * 0.05f, 0, 0), FVector(Size * 0.0022f, Size * 0.0085f, 0.07f), Color);
	AddBox(FVector(-Size * 0.42f, 0, 24.f), FVector(Size * 0.0014f, Size * 0.0030f, 0.30f), Color * 0.6f);
	RingRadius = Size * 0.55f;
	BobDamp = 0.f;
}

void AUnitActor::BuildStructure(EStructureKind Kind, const FLinearColor& Color, float Footprint)
{
	const FLinearColor Dark = Color * 0.45f + FLinearColor(0.08f, 0.08f, 0.08f);
	switch (Kind)
	{
	case EStructureKind::HQ:
		AddBox(FVector(0, 0, 60), FVector(6.0f, 6.0f, 1.6f), Color);
		AddShape(AC_MESH_CYLINDER, FVector(120, 120, 260), FVector(1.0f, 1.0f, 3.2f), Dark);
		AddBox(FVector(-140, -100, 190), FVector(2.2f, 2.2f, 1.4f), Dark);
		AddShape(AC_MESH_SPHERE, FVector(120, 120, 460), FVector(0.9f, 0.9f, 0.9f), FLinearColor(0.9f, 0.85f, 0.5f));
		RingRadius = 1500.f;
		break;
	case EStructureKind::Extractor:
		// Dome over the geyser with the docking TUBE on top: harvesters
		// clamp onto the glowing collar to fill their tanks.
		AddShape(AC_MESH_SPHERE, FVector(0, 0, 60), FVector(2.8f, 2.8f, 1.5f), Color);
		AddShape(AC_MESH_CYLINDER, FVector(0, 0, 330), FVector(0.42f, 0.42f, 4.2f), Dark);
		AddShape(AC_MESH_CYLINDER, FVector(0, 0, 540), FVector(0.65f, 0.65f, 0.35f),
			FLinearColor(2.6f, 1.0f, 0.10f));   // glowing tube collar
		RingRadius = 900.f;
		break;
	case EStructureKind::Outpost:
		AddBox(FVector(0, 0, 50), FVector(2.6f, 2.6f, 1.1f), Color);
		AddShape(AC_MESH_CYLINDER, FVector(0, 0, 240), FVector(0.16f, 0.16f, 2.2f), Dark);
		RingRadius = 800.f;
		break;
	case EStructureKind::Colony:
		AddBox(FVector(-90, -90, 55), FVector(3.0f, 3.0f, 1.2f), Color);
		AddBox(FVector(130, 100, 40), FVector(2.2f, 2.2f, 0.9f), Dark);
		AddBox(FVector(-60, 160, 110), FVector(1.4f, 1.4f, 2.0f), Color);
		RingRadius = 1200.f;
		break;
	case EStructureKind::Defense:
		AddShape(AC_MESH_CYLINDER, FVector(0, 0, 45), FVector(2.6f, 2.6f, 0.9f), Color);
		AddBox(FVector(0, 0, 150), FVector(1.1f, 1.1f, 0.8f), Dark);
		AddBox(FVector(110, 0, 150), FVector(1.6f, 0.35f, 0.30f), FLinearColor(0.1f, 0.1f, 0.1f));
		RingRadius = 850.f;
		break;
	}
	BobDamp = 0.f;   // structures sit rock-steady
}

void AUnitActor::InitFor(const FSimGame& G, FEntityId InEid)
{
	Eid = InEid;
	OwnerPlayer = G.World.Own[Eid].Player;
	const FLinearColor Color = G.FactionOf(OwnerPlayer).Color;

	if (const FStructC* S = G.World.Struct.Find(Eid))
	{
		bIsStructure = true;
		TplId = S->Tpl;
		const FStructTpl* T = G.Content.Structure(G.Players[OwnerPlayer].FactionId, S->Tpl);
		DisplayName = T ? T->Name : TEXT("Structure");
		BuildStructure(S->Kind, Color, 2.f);
		// Extractors stand on the geyser's coastal terrain, not at sea level.
		if (S->Kind == EStructureKind::Extractor) { StructBaseZ = 70.f; }
	}
	else
	{
		const FUnitC& U = G.World.Unit[Eid];
		TplId = U.Tpl;
		const FUnitTpl* T = G.Content.Unit(G.Players[OwnerPlayer].FactionId, U.Tpl);
		DisplayName = T ? T->Name : TEXT("Unit");
		bIsAir = U.Domain == EUnitDomain::Air;
		bIsGround = U.Domain == EUnitDomain::Ground;
		if (bIsGround)
		{
			BuildCrawler(Color);
		}
		else if (bIsAir && T && T->bHarvester)
		{
			BuildHarvester(Color);
		}
		else if (bIsAir)
		{
			BuildAircraft(700.f, Color);
		}
		else
		{
			// Size tier from unit value: bigger investment, bigger silhouette.
			const int32 Value = T ? T->Value() : 100;
			const int32 Tier = Value < 110 ? 0 : Value < 250 ? 1 : Value < 420 ? 2 : 3;
			const float Length = 560.f + Tier * 330.f;
			const float Beam = Length * (0.30f - Tier * 0.02f);
			BuildBoat(Length, Beam, 55.f + Tier * 18.f, U.bBuilder ? Color * 0.7f + FLinearColor(0.2f, 0.18f, 0.1f) : Color, Tier);
		}
	}

	// Selection ring: flat disc at the waterline, toggled by the controller.
	// Absolute rotation keeps it level while the hull pitches on the swell.
	SelectionRing = AddShape(AC_MESH_CYLINDER,
		FVector(0, 0, bIsAir ? -30.f : 12.f),
		FVector(RingRadius / 50.f, RingRadius / 50.f, 0.05f),
		FLinearColor(0.1f, 1.f, 0.2f));
	SelectionRing->SetUsingAbsoluteRotation(true);
	SelectionRing->SetVisibility(false);

	const FPosC& P = G.World.Pos[Eid];
	PrevPos = CurrPos = P.P;
	PrevFacing = CurrFacing = P.Facing;
}

void AUnitActor::SetSimState(const FVector2f& Pos, float Facing, float HpFrac, float InBuildProgress)
{
	PrevPos = CurrPos;
	PrevFacing = CurrFacing;
	CurrPos = Pos;
	CurrFacing = Facing;
	HpFraction = HpFrac;
	BuildProgress = InBuildProgress;
}

void AUnitActor::UpdateVisual(float Alpha, float WorldTime)
{
	const FVector2f SimP = FMath::Lerp(PrevPos, CurrPos, Alpha);
	const FVector2D XY = SimToWorld2D(SimP);

	float Z = AC_SEA_LEVEL;
	const float FacingLerp = PrevFacing + FMath::FindDeltaAngleRadians(PrevFacing, CurrFacing) * Alpha;
	FRotator Rot(0.f, FMath::RadiansToDegrees(FacingLerp), 0.f);

	if (bIsGround)
	{
		Z = 90.f;   // crawlers hug the (low, coastal) terrain
	}
	else if (bIsAir)
	{
		// Ease toward the target altitude: harvesters sink onto the tube.
		CurAirZ += (AirTargetZ - CurAirZ) * 0.045f;
		Z = CurAirZ + FMath::Sin(WorldTime * 1.7f + Eid) * (AirTargetZ < 2000.f ? 8.f : 40.f);
	}
	else if (BobDamp > 0.f)
	{
		// Ride a damped version of the rendered water surface (Water plugin
		// waves when active, classic waves otherwise): lift with the swell
		// and lean gently along the surface normal.
		if (!GM) { GM = GetWorld()->GetAuthGameMode<AACGameMode>(); }
		FVector SurfLoc(XY.X, XY.Y, AC_SEA_LEVEL);
		FVector SurfN = FVector::UpVector;
		if (GM) { GM->SampleOceanSurface(XY, WorldTime, SurfLoc, SurfN); }
		Z = SurfLoc.Z * BobDamp * 0.5f;
		const FVector TiltN = FMath::Lerp(FVector::UpVector, SurfN, BobDamp * 0.45f).GetSafeNormal();
		const FVector Fwd(FMath::Cos(FacingLerp), FMath::Sin(FacingLerp), 0.f);
		Rot = FRotationMatrix::MakeFromZX(TiltN, Fwd).Rotator();
	}

	// Structures sit on their base height and rise while under construction.
	if (bIsStructure)
	{
		Z += StructBaseZ;
		if (BuildProgress < 1.f)
		{
			Z -= (1.f - BuildProgress) * 220.f;
		}
	}

	SetActorLocationAndRotation(FVector(XY.X, XY.Y, Z), Rot);

	// Wake: churned foam off the stern while under way. Burst cadence and
	// size scale with speed; hidden (fogged) hulls leave no visible trail.
	if (HullLength > 0.f && !IsHidden() && GM && WorldTime >= NextWakeTime)
	{
		const float SimSpeed = (CurrPos - PrevPos).Size() / SIM_DT;   // cells/sec
		if (SimSpeed > 1.2f)
		{
			NextWakeTime = WorldTime + FMath::FRandRange(0.55f, 0.75f) / FMath::Min(SimSpeed / 2.f, 2.f);
			if (UNiagaraSystem* FX = GM->GetSplashFX())
			{
				const FVector Stern = GetActorLocation()
					- GetActorForwardVector() * (HullLength * 0.5f);
				const float Scale = FMath::Clamp(0.22f + SimSpeed * 0.05f, 0.25f, 0.55f);
				UNiagaraFunctionLibrary::SpawnSystemAtLocation(GetWorld(), FX,
					FVector(Stern.X, Stern.Y, Z + 25.f), FRotator(90.f, 0.f, 0.f),
					FVector(Scale), true);
			}
		}
		else
		{
			NextWakeTime = WorldTime + 0.3;   // idle: re-check soon, spawn nothing
		}
	}
}

void AUnitActor::SetSelected(bool bSelected)
{
	if (SelectionRing) { SelectionRing->SetVisibility(bSelected); }
}

// ---------------------------------------------------------------------------

AProjectileActor::AProjectileActor()
{
	PrimaryActorTick.bCanEverTick = false;
	Ball = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Ball"));
	RootComponent = Ball;
	Ball->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Ball->SetCastShadow(false);
}

void AProjectileActor::InitFor(EWeaponType Type)
{
	WeaponType = Type;
	bJustInit = true;
	Ball->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, AC_MESH_SPHERE));
	FLinearColor Color;
	float Scale = 0.18f;
	switch (Type)
	{
	case EWeaponType::Missile: Color = FLinearColor(1.f, 0.45f, 0.05f); Scale = 0.22f; break;
	case EWeaponType::Torpedo: Color = FLinearColor(0.1f, 0.9f, 1.f); Scale = 0.26f; break;
	case EWeaponType::AA:      Color = FLinearColor(1.f, 0.95f, 0.4f); Scale = 0.12f; break;
	case EWeaponType::Disrupt: Color = FLinearColor(0.75f, 0.2f, 1.f); Scale = 0.24f; break;
	default:                   Color = FLinearColor(1.f, 0.9f, 0.6f); break;
	}
	Ball->SetMaterial(0, MakeMID(this, Color));
	Ball->SetWorldScale3D(FVector(Scale));
	SetActorHiddenInGame(false);
}

void AProjectileActor::SetSimState(const FVector2f& Pos)
{
	PrevPos = bJustInit ? Pos : CurrPos;
	bJustInit = false;
	CurrPos = Pos;
}

void AProjectileActor::UpdateVisual(float Alpha, float WorldTime)
{
	const FVector2D XY = SimToWorld2D(FMath::Lerp(PrevPos, CurrPos, Alpha));
	float Z;
	switch (WeaponType)
	{
	case EWeaponType::Torpedo:
	{
		if (!GM) { GM = GetWorld()->GetAuthGameMode<AACGameMode>(); }
		FVector SurfLoc(XY.X, XY.Y, AC_SEA_LEVEL);
		FVector SurfN;
		if (GM) { GM->SampleOceanSurface(XY, WorldTime, SurfLoc, SurfN); }
		Z = SurfLoc.Z - 45.f;   // torpedoes run just under the surface
		break;
	}
	case EWeaponType::AA:      Z = AC_AIR_ALTITUDE * 0.55f; break;
	default:                   Z = 220.f; break;
	}
	SetActorLocation(FVector(XY.X, XY.Y, Z));
}
