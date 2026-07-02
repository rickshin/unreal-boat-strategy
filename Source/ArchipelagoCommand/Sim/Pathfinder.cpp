#include "Pathfinder.h"
#include "SimMap.h"
#include "Algo/Reverse.h"

namespace
{
	struct FOpenNode
	{
		int32 Cell;
		float F;
		bool operator<(const FOpenNode& O) const { return F == O.F ? Cell < O.Cell : F < O.F; }
	};
}

bool FSimPathfinder::WaterLineOfSight(const FVector2f& A, const FVector2f& B) const
{
	const float Dist = (B - A).Size();
	const int32 Steps = FMath::Max(1, FMath::CeilToInt32(Dist * 2.f));
	for (int32 i = 0; i <= Steps; ++i)
	{
		const FVector2f P = FMath::Lerp(A, B, float(i) / Steps);
		if (!Map->IsWater(P)) { return false; }
	}
	return true;
}

FVector2f FSimPathfinder::GroupOffset(int32 IndexInGroup)
{
	if (IndexInGroup == 0) { return FVector2f::ZeroVector; }
	// Rings of 6, 12, 18... at radius 1.4, 2.8, ...
	int32 Ring = 1, First = 1;
	while (IndexInGroup >= First + Ring * 6) { First += Ring * 6; ++Ring; }
	const int32 Slot = IndexInGroup - First;
	const float Ang = (2.f * PI * Slot) / (Ring * 6);
	const float R = Ring * 1.4f;
	return FVector2f(FMath::Cos(Ang) * R, FMath::Sin(Ang) * R);
}

TArray<FIntPoint> FSimPathfinder::AStar(FIntPoint Start, FIntPoint Goal)
{
	const int32 W = Map->Width;
	const int32 H = Map->Height;
	auto Idx = [W](FIntPoint P) { return P.Y * W + P.X; };

	TArray<float> G;
	G.Init(TNumericLimits<float>::Max(), W * H);
	TArray<int32> Parent;
	Parent.Init(-1, W * H);
	TSet<int32> Closed;

	// Sorted-array open list; N is small (<=16k cells) and paths are short.
	TArray<FOpenNode> Open;
	auto Heur = [Goal](FIntPoint P)
	{
		const float DX = FMath::Abs(float(P.X - Goal.X)), DY = FMath::Abs(float(P.Y - Goal.Y));
		return FMath::Max(DX, DY) + 0.4142f * FMath::Min(DX, DY);
	};

	G[Idx(Start)] = 0.f;
	Open.HeapPush({ Idx(Start), Heur(Start) });

	const int32 DirX[8] = { 1, -1, 0, 0, 1, 1, -1, -1 };
	const int32 DirY[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };
	int32 Expanded = 0;

	while (Open.Num() > 0 && Expanded < 20000)
	{
		FOpenNode Node;
		Open.HeapPop(Node, EAllowShrinking::No);
		if (Closed.Contains(Node.Cell)) { continue; }
		Closed.Add(Node.Cell);
		++Expanded;

		const FIntPoint P(Node.Cell % W, Node.Cell / W);
		if (P == Goal)
		{
			TArray<FIntPoint> Path;
			int32 C = Node.Cell;
			while (C != -1 && C != Idx(Start))
			{
				Path.Add(FIntPoint(C % W, C / W));
				C = Parent[C];
			}
			Algo::Reverse(Path);
			return Path;
		}

		for (int32 D = 0; D < 8; ++D)
		{
			const FIntPoint N(P.X + DirX[D], P.Y + DirY[D]);
			if (!Map->IsWater(N.X, N.Y)) { continue; }
			// No diagonal corner cutting.
			if (D >= 4 && (!Map->IsWater(P.X + DirX[D], P.Y) || !Map->IsWater(P.X, P.Y + DirY[D]))) { continue; }
			const float Step = (D >= 4) ? 1.4142f : 1.f;
			const float NG = G[Idx(P)] + Step;
			if (NG < G[Idx(N)])
			{
				G[Idx(N)] = NG;
				Parent[Idx(N)] = Idx(P);
				Open.HeapPush({ Idx(N), NG + Heur(N) });
			}
		}
	}
	return {};
}

TArray<FVector2f> FSimPathfinder::FindPath(const FVector2f& From, const FVector2f& To)
{
	FVector2f Start = Map->IsWater(From) ? From : Map->NearestWater(From);
	FVector2f Goal = Map->IsWater(To) ? To : Map->NearestWater(To);

	if (WaterLineOfSight(Start, Goal)) { return { Goal }; }

	const FIntPoint SC(FMath::FloorToInt32(Start.X), FMath::FloorToInt32(Start.Y));
	const FIntPoint GC(FMath::FloorToInt32(Goal.X), FMath::FloorToInt32(Goal.Y));
	const uint64 Key = (uint64(uint16(SC.X)) << 48) | (uint64(uint16(SC.Y)) << 32)
		| (uint64(uint16(GC.X)) << 16) | uint64(uint16(GC.Y));

	TArray<FIntPoint>* Cached = Cache.Find(Key);
	TArray<FIntPoint> Cells;
	if (Cached) { Cells = *Cached; }
	else
	{
		Cells = AStar(SC, GC);
		if (Cache.Num() > 4096) { Cache.Reset(); }
		Cache.Add(Key, Cells);
	}
	if (Cells.Num() == 0) { return {}; }

	// Line-of-sight smoothing: drop intermediate cells we can sail past.
	TArray<FVector2f> Way;
	FVector2f Cur = Start;
	int32 i = 0;
	while (i < Cells.Num())
	{
		int32 Far = i;
		for (int32 j = Cells.Num() - 1; j > i; --j)
		{
			const FVector2f P(Cells[j].X + 0.5f, Cells[j].Y + 0.5f);
			if (WaterLineOfSight(Cur, P)) { Far = j; break; }
		}
		const FVector2f P(Cells[Far].X + 0.5f, Cells[Far].Y + 0.5f);
		Way.Add(P);
		Cur = P;
		i = Far + 1;
	}
	if (Way.Num() > 0) { Way.Last() = Goal; }
	return Way;
}
