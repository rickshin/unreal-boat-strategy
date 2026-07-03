#pragma once

#include "CoreMinimal.h"
#include "SimTypes.h"

// Seeded, point-symmetric archipelago on a water grid. Sim positions are in
// cell units (1.0 = one cell); the render layer scales to world units.

struct FSimIsland
{
	int32 Id = -1;
	FVector2f Center = FVector2f::ZeroVector;
	float Radius = 5.f;
	TArray<FIntPoint> Cells;
	int32 OwnerPlayer = -1;      // claimed via outpost/colony
	int32 ClaimStructure = INVALID_ENTITY;
	bool bVolcanic = false;      // iron source flavor
};

// A KiTrin geyser: an orange, star-venting fissure on island land. A gas
// extractor built on top claims it; flying harvesters dock to carry the
// gas home.
struct FSimResourceNode
{
	int32 Id = -1;
	EResourceType Type = EResourceType::KiTrin;
	FIntPoint Cell = FIntPoint::ZeroValue;   // a LAND cell (coastal)
	float Amount = 2500.f;
	FEntityId ClaimedBy = INVALID_ENTITY;    // the extractor on this geyser
};

struct FSimMap
{
	int32 Width = 128;
	int32 Height = 128;
	TArray<uint8> Land;             // Width*Height, 1 = land
	TArray<FSimIsland> Islands;
	TArray<FSimResourceNode> Nodes;
	FVector2f HomeSpawn[2];         // water positions near the two home islands
	int32 HomeIsland[2] = { -1, -1 };

	void Generate(uint64 Seed);

	bool InBounds(int32 X, int32 Y) const { return X >= 0 && Y >= 0 && X < Width && Y < Height; }
	bool IsWater(int32 X, int32 Y) const { return InBounds(X, Y) && Land[Y * Width + X] == 0; }
	bool IsWater(const FVector2f& P) const { return IsWater(FMath::FloorToInt32(P.X), FMath::FloorToInt32(P.Y)); }

	// Island id whose land is within Range cells of P, else -1.
	int32 IslandNear(const FVector2f& P, float Range) const;
	// Geyser index within Range cells of P (with gas remaining), else -1.
	int32 NodeNear(const FVector2f& P, float Range) const;
	// Nearest water cell center to P (spiral search).
	FVector2f NearestWater(const FVector2f& P) const;

private:
	void StampIsland(FSimIsland& Island, FSimRng& Rng);
	void PlaceGeysers(const FSimIsland& Island, FSimRng& Rng, int32 MinCount);
};
