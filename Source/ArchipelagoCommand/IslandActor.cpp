#include "IslandActor.h"
#include "ACTypes.h"
#include "Sim/SimMap.h"
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
}

AIslandActor::AIslandActor()
{
	PrimaryActorTick.bCanEverTick = false;
	Mesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("IslandMesh"));
	RootComponent = Mesh;
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void AIslandActor::Build(const FSimMap& Map, const FSimIsland& Island)
{
	// Bounding box of the island cells, padded so the skirt reaches water.
	FIntPoint Min(MAX_int32, MAX_int32), Max(MIN_int32, MIN_int32);
	TSet<FIntPoint> CellSet;
	for (const FIntPoint& C : Island.Cells)
	{
		CellSet.Add(C);
		Min.X = FMath::Min(Min.X, C.X); Min.Y = FMath::Min(Min.Y, C.Y);
		Max.X = FMath::Max(Max.X, C.X); Max.Y = FMath::Max(Max.Y, C.Y);
	}
	if (CellSet.Num() == 0) { return; }
	Min -= FIntPoint(2, 2);
	Max += FIntPoint(3, 3);
	const int32 W = Max.X - Min.X + 1;
	const int32 H = Max.Y - Min.Y + 1;

	// BFS distance-to-coast transform over the padded grid (in cells).
	TArray<float> Dist;
	Dist.Init(-1.f, W * H);
	TArray<FIntPoint> Queue;
	for (int32 Y = 0; Y < H; ++Y)
	{
		for (int32 X = 0; X < W; ++X)
		{
			if (!CellSet.Contains(FIntPoint(Min.X + X, Min.Y + Y)))
			{
				Dist[Y * W + X] = 0.f;
				Queue.Add(FIntPoint(X, Y));
			}
		}
	}
	for (int32 Head = 0; Head < Queue.Num(); ++Head)
	{
		const FIntPoint P = Queue[Head];
		const float D = Dist[P.Y * W + P.X];
		const FIntPoint Dirs[4] = { {1,0},{-1,0},{0,1},{0,-1} };
		for (const FIntPoint& Dir : Dirs)
		{
			const FIntPoint N = P + Dir;
			if (N.X < 0 || N.Y < 0 || N.X >= W || N.Y >= H) { continue; }
			if (Dist[N.Y * W + N.X] < 0.f)
			{
				Dist[N.Y * W + N.X] = D + 1.f;
				Queue.Add(N);
			}
		}
	}

	// Heightfield vertices at cell corners; coast dips below sea level so the
	// beach meets the water cleanly.
	TArray<FVector> Verts;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	TArray<int32> Tris;
	FRandomStream Rng(Island.Id * 7919 + 3);
	auto HeightAt = [&](int32 X, int32 Y) -> float
	{
		// Bilinear-ish sample of the cell distance field at a corner.
		float D = 0.f;
		int32 N = 0;
		for (int32 DY = -1; DY <= 0; ++DY)
		{
			for (int32 DX = -1; DX <= 0; ++DX)
			{
				const int32 SX = FMath::Clamp(X + DX, 0, W - 1);
				const int32 SY = FMath::Clamp(Y + DY, 0, H - 1);
				D += Dist[SY * W + SX];
				++N;
			}
		}
		D /= N;
		const float T = FMath::Clamp(D / 2.6f, 0.f, 1.f);
		return -90.f + (T * T * (3.f - 2.f * T)) * (Island.bVolcanic ? 620.f : 420.f);
	};
	for (int32 Y = 0; Y <= H; ++Y)
	{
		for (int32 X = 0; X <= W; ++X)
		{
			const float WX = (Min.X + X) * AC_CELL;
			const float WY = (Min.Y + Y) * AC_CELL;
			Verts.Add(FVector(WX, WY, HeightAt(X, Y)));
			UVs.Add(FVector2D(X, Y) * 0.5);
		}
	}
	const int32 RowLen = W + 1;
	for (int32 Y = 0; Y < H; ++Y)
	{
		for (int32 X = 0; X < W; ++X)
		{
			// Skip fully-underwater quads far from the island.
			const int32 I = Y * RowLen + X;
			if (Verts[I].Z < -80.f && Verts[I + 1].Z < -80.f
				&& Verts[I + RowLen].Z < -80.f && Verts[I + RowLen + 1].Z < -80.f)
			{
				continue;
			}
			Tris.Append({ I, I + RowLen, I + 1, I + 1, I + RowLen, I + RowLen + 1 });
		}
	}

	// Smooth normals from the height function.
	Normals.SetNum(Verts.Num());
	for (int32 Y = 0; Y <= H; ++Y)
	{
		for (int32 X = 0; X <= W; ++X)
		{
			const float HL = HeightAt(FMath::Max(X - 1, 0), Y);
			const float HR = HeightAt(FMath::Min(X + 1, W), Y);
			const float HD = HeightAt(X, FMath::Max(Y - 1, 0));
			const float HU = HeightAt(X, FMath::Min(Y + 1, H));
			Normals[Y * RowLen + X] = FVector(HL - HR, HD - HU, 2.f * AC_CELL).GetSafeNormal();
		}
	}

	Mesh->CreateMeshSection_LinearColor(0, Verts, Tris, Normals, UVs,
		TArray<FLinearColor>(), TArray<FProcMeshTangent>(), false);
	const FLinearColor GroundColor = Island.bVolcanic
		? FLinearColor(0.16f, 0.13f, 0.11f)
		: FLinearColor(0.16f, 0.30f, 0.12f);
	Mesh->SetMaterial(0, MakeMID(this, GroundColor));

	// Rock decorations along the coast: static, render-only (doc 18).
	UStaticMesh* Cone = LoadObject<UStaticMesh>(nullptr, AC_MESH_CONE);
	UMaterialInstanceDynamic* RockMID = MakeMID(this, FLinearColor(0.30f, 0.29f, 0.27f));
	const int32 NumRocks = FMath::Clamp(Island.Cells.Num() / 8, 3, 9);
	for (int32 i = 0; i < NumRocks; ++i)
	{
		const FIntPoint C = Island.Cells[Rng.RandRange(0, Island.Cells.Num() - 1)];
		UStaticMeshComponent* Rock = NewObject<UStaticMeshComponent>(this);
		Rock->SetStaticMesh(Cone);
		Rock->SetMaterial(0, RockMID);
		const float S = Rng.FRandRange(0.5f, 1.6f);
		Rock->SetWorldScale3D(FVector(S, S, S * Rng.FRandRange(0.7f, 1.8f)));
		Rock->SetWorldLocation(FVector(
			(C.X + Rng.FRandRange(0.1f, 0.9f)) * AC_CELL,
			(C.Y + Rng.FRandRange(0.1f, 0.9f)) * AC_CELL,
			Rng.FRandRange(0.f, 160.f)));
		Rock->SetWorldRotation(FRotator(Rng.FRandRange(-12.f, 12.f), Rng.FRandRange(0.f, 360.f), 0.f));
		Rock->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Rock->SetupAttachment(RootComponent);
		Rock->RegisterComponent();
	}
}

// ---------------------------------------------------------------------------

ANodeDecorActor::ANodeDecorActor()
{
	PrimaryActorTick.bCanEverTick = false;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;
}

void ANodeDecorActor::Build(const FSimResourceNode& Node)
{
	NodeId = Node.Id;
	FRandomStream Rng(Node.Id * 331 + 17);
	const FVector Base(
		(Node.Cell.X + 0.5f) * AC_CELL, (Node.Cell.Y + 0.5f) * AC_CELL, AC_SEA_LEVEL);
	SetActorLocation(Base);

	if (Node.Type == EResourceType::Wood)
	{
		// Kelp forest: a cluster of thin green cylinders poking out of the sea.
		UStaticMesh* Cyl = LoadObject<UStaticMesh>(nullptr, AC_MESH_CYLINDER);
		UMaterialInstanceDynamic* MID = MakeMID(this, FLinearColor(0.10f, 0.34f, 0.10f));
		for (int32 i = 0; i < 7; ++i)
		{
			UStaticMeshComponent* K = NewObject<UStaticMeshComponent>(this);
			K->SetStaticMesh(Cyl);
			K->SetMaterial(0, MID);
			K->SetWorldScale3D(FVector(0.14f, 0.14f, Rng.FRandRange(0.8f, 1.6f)));
			K->SetRelativeLocation(FVector(Rng.FRandRange(-220.f, 220.f), Rng.FRandRange(-220.f, 220.f), -40.f));
			K->SetRelativeRotation(FRotator(Rng.FRandRange(-14.f, 14.f), Rng.FRandRange(0.f, 360.f), 0.f));
			K->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			K->SetCastShadow(false);
			K->SetupAttachment(Root);
			K->RegisterComponent();
		}
	}
	else
	{
		// Shipwreck / ore field: tilted rusty hulk plus dark ore lumps.
		UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, AC_MESH_CUBE);
		UMaterialInstanceDynamic* Rust = MakeMID(this, FLinearColor(0.33f, 0.17f, 0.08f));
		UStaticMeshComponent* Hulk = NewObject<UStaticMeshComponent>(this);
		Hulk->SetStaticMesh(Cube);
		Hulk->SetMaterial(0, Rust);
		Hulk->SetWorldScale3D(FVector(3.2f, 1.0f, 0.7f));
		Hulk->SetRelativeLocation(FVector(0.f, 0.f, -30.f));
		Hulk->SetRelativeRotation(FRotator(6.f, Rng.FRandRange(0.f, 360.f), 14.f));
		Hulk->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Hulk->SetupAttachment(Root);
		Hulk->RegisterComponent();

		UMaterialInstanceDynamic* Ore = MakeMID(this, FLinearColor(0.12f, 0.12f, 0.15f));
		for (int32 i = 0; i < 4; ++i)
		{
			UStaticMeshComponent* Lump = NewObject<UStaticMeshComponent>(this);
			Lump->SetStaticMesh(Cube);
			Lump->SetMaterial(0, Ore);
			const float S = Rng.FRandRange(0.4f, 0.8f);
			Lump->SetWorldScale3D(FVector(S));
			Lump->SetRelativeLocation(FVector(Rng.FRandRange(-260.f, 260.f), Rng.FRandRange(-260.f, 260.f), -55.f));
			Lump->SetRelativeRotation(FRotator(0.f, Rng.FRandRange(0.f, 360.f), Rng.FRandRange(0.f, 30.f)));
			Lump->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Lump->SetCastShadow(false);
			Lump->SetupAttachment(Root);
			Lump->RegisterComponent();
		}
	}
}

void ANodeDecorActor::SetDepleted(bool bDepleted)
{
	// Depleted fields sink out of sight; the sim node stays for the record.
	SetActorHiddenInGame(bDepleted);
}
