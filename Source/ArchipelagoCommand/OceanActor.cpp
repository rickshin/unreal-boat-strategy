#include "OceanActor.h"
#include "ACTypes.h"
#include "ProceduralMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace
{
	struct FWave { float Amp; float WaveLen; float Speed; FVector2D Dir; };
	// Three swells: one long primary, two shorter cross-chops.
	const FWave GWaves[3] =
	{
		{ 34.f, 9000.f, 620.f, FVector2D(1.f, 0.25f).GetSafeNormal() },
		{ 16.f, 4200.f, 480.f, FVector2D(-0.35f, 1.f).GetSafeNormal() },
		{ 8.f,  2100.f, 380.f, FVector2D(0.7f, -0.7f).GetSafeNormal() },
	};
}

float AOceanActor::WaveHeight(const FVector2D& XY, float Time)
{
	float Z = 0.f;
	for (const FWave& W : GWaves)
	{
		const float K = 2.f * PI / W.WaveLen;
		Z += W.Amp * FMath::Sin(K * (FVector2D::DotProduct(XY, W.Dir) - W.Speed * Time));
	}
	return Z;
}

FVector2D AOceanActor::WaveGradient(const FVector2D& XY, float Time)
{
	FVector2D Grad = FVector2D::ZeroVector;
	for (const FWave& W : GWaves)
	{
		const float K = 2.f * PI / W.WaveLen;
		const float C = W.Amp * K * FMath::Cos(K * (FVector2D::DotProduct(XY, W.Dir) - W.Speed * Time));
		Grad += W.Dir * C;
	}
	return Grad;
}

AOceanActor::AOceanActor()
{
	PrimaryActorTick.bCanEverTick = true;
	Mesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("OceanMesh"));
	RootComponent = Mesh;
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetCastShadow(false);
}

void AOceanActor::Build(int32 SizeCells, bool bShowWaterSheet)
{
	const float MapSize = SizeCells * AC_CELL;
	bSheetVisible = bShowWaterSheet;
	const float Margin = MapSize * 0.25f;
	SheetOrigin = -Margin;
	GridVerts = 97;
	SheetStep = (MapSize + 2.f * Margin) / (GridVerts - 1);

	Vertices.Reset();
	Normals.Reset();
	UVs.Reset();
	TArray<int32> Triangles;
	for (int32 Y = 0; Y < GridVerts; ++Y)
	{
		for (int32 X = 0; X < GridVerts; ++X)
		{
			Vertices.Add(FVector(SheetOrigin + X * SheetStep, SheetOrigin + Y * SheetStep, 0.f));
			Normals.Add(FVector::UpVector);
			UVs.Add(FVector2D(X, Y) * 0.25);
		}
	}
	for (int32 Y = 0; Y < GridVerts - 1; ++Y)
	{
		for (int32 X = 0; X < GridVerts - 1; ++X)
		{
			const int32 I = Y * GridVerts + X;
			Triangles.Append({ I, I + GridVerts, I + 1, I + 1, I + GridVerts, I + GridVerts + 1 });
		}
	}
	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, AC_MAT_BASIC);
	if (bSheetVisible)
	{
		Mesh->CreateMeshSection_LinearColor(0, Vertices, Triangles, Normals, UVs,
			TArray<FLinearColor>(), TArray<FProcMeshTangent>(), false);
		if (Base)
		{
			UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Base, this);
			MID->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.02f, 0.11f, 0.22f));
			Mesh->SetMaterial(0, MID);
		}
	}

	// Fish pool: render-only RNG stream, never touches the sim.
	FRandomStream FishRng(1337);
	UStaticMesh* ConeMesh = LoadObject<UStaticMesh>(nullptr, AC_MESH_CONE);
	UMaterialInstanceDynamic* FishMID = Base ? UMaterialInstanceDynamic::Create(Base, this) : nullptr;
	if (FishMID) { FishMID->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.5f, 0.55f, 0.6f)); }
	for (int32 i = 0; i < 24; ++i)
	{
		UStaticMeshComponent* F = NewObject<UStaticMeshComponent>(this);
		F->SetStaticMesh(ConeMesh);
		F->SetMaterial(0, FishMID);
		F->SetWorldScale3D(FVector(0.25f, 0.25f, 0.6f));
		F->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		F->SetCastShadow(false);
		F->SetupAttachment(RootComponent);
		F->RegisterComponent();
		Fish.Add(F);
		FFishState S;
		S.Center = FVector2D(FishRng.FRandRange(0.f, MapSize), FishRng.FRandRange(0.f, MapSize));
		S.Radius = FishRng.FRandRange(400.f, 1400.f);
		S.Phase = FishRng.FRandRange(0.f, 2.f * PI);
		S.Speed = FishRng.FRandRange(0.25f, 0.7f);
		FishStates.Add(S);
	}
}

void AOceanActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (Vertices.Num() == 0) { return; }
	const float T = GetWorld()->GetTimeSeconds();

	if (bSheetVisible)
	{
		for (int32 i = 0; i < Vertices.Num(); ++i)
		{
			const FVector2D XY(Vertices[i].X, Vertices[i].Y);
			Vertices[i].Z = WaveHeight(XY, T);
			const FVector2D Grad = WaveGradient(XY, T);
			Normals[i] = FVector(-Grad.X, -Grad.Y, 1.f).GetSafeNormal();
		}
		Mesh->UpdateMeshSection_LinearColor(0, Vertices, Normals, UVs,
			TArray<FLinearColor>(), TArray<FProcMeshTangent>());
	}

	// Fish: lazy loops just under the surface, occasionally breaching.
	for (int32 i = 0; i < Fish.Num(); ++i)
	{
		const FFishState& S = FishStates[i];
		const float A = S.Phase + T * S.Speed;
		const FVector2D P = S.Center + FVector2D(FMath::Cos(A), FMath::Sin(A)) * S.Radius;
		const float Breach = FMath::Sin(A * 3.f) * 60.f;
		const float Z = WaveHeight(P, T) - 60.f + FMath::Max(0.f, Breach);
		Fish[i]->SetWorldLocation(FVector(P.X, P.Y, Z));
		Fish[i]->SetWorldRotation(FRotator(0.f, FMath::RadiansToDegrees(A) + 180.f, 90.f));
	}
}
