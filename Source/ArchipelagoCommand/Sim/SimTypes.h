#pragma once

#include "CoreMinimal.h"

// Fixed-tick constants. The simulation is fully decoupled from rendering:
// same seed + same command stream => bit-identical state.
inline constexpr float SIM_DT = 0.05f;          // 20 Hz
inline constexpr int32 SIM_TICKS_PER_SEC = 20;
inline constexpr int32 FOG_INTERVAL_TICKS = 5;

using FEntityId = int32;
inline constexpr FEntityId INVALID_ENTITY = -1;

enum class EUnitDomain : uint8 { Naval, Air };

enum class EWeaponType : uint8 { Shell, Missile, Torpedo, AA, Disrupt };

// Naval = surface targets (ships + floating structures). Air = aircraft only.
enum class ETargetFilter : uint8 { Any, Naval, Air };

enum class EStructureKind : uint8 { HQ, MinerWood, MinerIron, Outpost, Colony, Defense };

enum class EResourceType : uint8 { Wood, Iron };

enum class ESimDifficulty : uint8 { Easy, Normal, Hard };

// PCG32: the single seeded RNG family used by the simulation. The render
// layer owns separate instances (decorations) that never touch sim state.
struct FSimRng
{
	uint64 State = 0x853c49e6748fea9bULL;
	uint64 Inc   = 0xda3e39cb94b95bdbULL;

	void Seed(uint64 InSeed, uint64 Stream = 1)
	{
		State = 0;
		Inc = (Stream << 1u) | 1u;
		Next();
		State += InSeed;
		Next();
	}

	uint32 Next()
	{
		const uint64 Old = State;
		State = Old * 6364136223846793005ULL + Inc;
		const uint32 Xor = uint32(((Old >> 18u) ^ Old) >> 27u);
		const uint32 Rot = uint32(Old >> 59u);
		return (Xor >> Rot) | (Xor << ((32u - Rot) & 31u));
	}

	// Inclusive range.
	int32 RangeInt(int32 A, int32 B) { return B <= A ? A : A + int32(Next() % uint32(B - A + 1)); }
	float Frand() { return float(Next() >> 8) * (1.0f / 16777216.0f); }
	float FRange(float A, float B) { return A + (B - A) * Frand(); }
};

// Deterministic iteration helper: component stores are TMaps, so systems
// always walk entities in ascending id order.
template <typename TValue>
TArray<FEntityId> SimSortedKeys(const TMap<FEntityId, TValue>& Map)
{
	TArray<FEntityId> Keys;
	Keys.Reserve(Map.Num());
	for (const auto& Pair : Map) { Keys.Add(Pair.Key); }
	Keys.Sort();
	return Keys;
}

// FNV-1a running hash for Game::StateHash().
struct FSimHash
{
	uint64 H = 1469598103934665603ULL;
	void Add(uint64 V)
	{
		for (int32 i = 0; i < 8; ++i)
		{
			H ^= (V >> (i * 8)) & 0xff;
			H *= 1099511628211ULL;
		}
	}
	void AddF(float V) { uint32 Bits; FMemory::Memcpy(&Bits, &V, 4); Add(Bits); }
};
