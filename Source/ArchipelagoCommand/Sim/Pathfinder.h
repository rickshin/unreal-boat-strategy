#pragma once

#include "CoreMinimal.h"
#include "SimTypes.h"

struct FSimMap;

// A* on the water grid with a shared path cache and line-of-sight smoothing.
// Aircraft never call this (they fly straight, ignoring terrain).
struct FSimPathfinder
{
	const FSimMap* Map = nullptr;

	void Init(const FSimMap* InMap) { Map = InMap; Cache.Reset(); }
	void ClearCache() { Cache.Reset(); }

	// Waypoints in cell-center coordinates, excluding the start cell.
	// Empty result = unreachable (caller keeps the unit in place).
	TArray<FVector2f> FindPath(const FVector2f& From, const FVector2f& To);

	// Straight water line between two points (for path smoothing / firing arcs).
	bool WaterLineOfSight(const FVector2f& A, const FVector2f& B) const;

	// Deterministic ring offsets so group moves spread instead of stacking.
	static FVector2f GroupOffset(int32 IndexInGroup);

private:
	TMap<uint64, TArray<FIntPoint>> Cache;
	TArray<FIntPoint> AStar(FIntPoint Start, FIntPoint Goal);
};
