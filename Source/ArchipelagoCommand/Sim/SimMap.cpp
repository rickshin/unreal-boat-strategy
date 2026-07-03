#include "SimMap.h"

void FSimMap::StampIsland(FSimIsland& Island, FSimRng& Rng)
{
	// Radial-noise blob: radius modulated by 3 harmonics so islands read as
	// organic shapes but stay convex enough for readable coastlines.
	float Ph[3], Amp[3];
	for (int32 i = 0; i < 3; ++i)
	{
		Ph[i] = Rng.FRange(0.f, 2.f * PI);
		Amp[i] = Rng.FRange(0.06f, 0.16f);
	}
	const int32 R = FMath::CeilToInt32(Island.Radius * 1.3f);
	const int32 CX = FMath::RoundToInt32(Island.Center.X);
	const int32 CY = FMath::RoundToInt32(Island.Center.Y);
	for (int32 Y = CY - R; Y <= CY + R; ++Y)
	{
		for (int32 X = CX - R; X <= CX + R; ++X)
		{
			if (!InBounds(X, Y)) { continue; }
			const float DX = X + 0.5f - Island.Center.X;
			const float DY = Y + 0.5f - Island.Center.Y;
			const float Dist = FMath::Sqrt(DX * DX + DY * DY);
			const float Ang = FMath::Atan2(DY, DX);
			float RLim = Island.Radius * 0.82f;
			for (int32 i = 0; i < 3; ++i)
			{
				RLim += Island.Radius * Amp[i] * FMath::Sin(Ang * (i + 2) + Ph[i]);
			}
			if (Dist <= RLim)
			{
				if (Land[Y * Width + X] == 0)
				{
					Land[Y * Width + X] = 1;
					Island.Cells.Add(FIntPoint(X, Y));
				}
			}
		}
	}
}

void FSimMap::PlaceGeysers(const FSimIsland& Island, FSimRng& Rng, int32 MinCount)
{
	// Geysers sit on COASTAL land (a land cell touching water) so the
	// terrain stays low there and extractors read clearly from the sea.
	TArray<FIntPoint> Coastal;
	for (const FIntPoint& C : Island.Cells)
	{
		if (IsWater(C.X + 1, C.Y) || IsWater(C.X - 1, C.Y)
			|| IsWater(C.X, C.Y + 1) || IsWater(C.X, C.Y - 1))
		{
			Coastal.Add(C);
		}
	}
	const int32 Want = FMath::Max(MinCount, Island.bVolcanic ? 2 : 1);
	int32 Placed = 0;
	for (int32 Try = 0; Try < 60 && Placed < Want && Coastal.Num() > 0; ++Try)
	{
		const FIntPoint C = Coastal[Rng.RangeInt(0, Coastal.Num() - 1)];
		bool bTooClose = false;
		for (const FSimResourceNode& N : Nodes)
		{
			if (FMath::Abs(N.Cell.X - C.X) + FMath::Abs(N.Cell.Y - C.Y) < 4) { bTooClose = true; break; }
		}
		if (bTooClose) { continue; }
		FSimResourceNode Node;
		Node.Id = Nodes.Num();
		Node.Cell = C;
		Node.Amount = 2500.f;
		Nodes.Add(Node);
		++Placed;
	}
}

void FSimMap::Generate(uint64 Seed)
{
	FSimRng Rng;
	Rng.Seed(Seed, 11);

	Land.Init(0, Width * Height);
	Islands.Reset();
	Nodes.Reset();

	auto AddIsland = [&](FVector2f Center, float Radius, bool bVolcanic) -> int32
	{
		FSimIsland Isl;
		Isl.Id = Islands.Num();
		Isl.Center = Center;
		Isl.Radius = Radius;
		Isl.bVolcanic = bVolcanic;
		StampIsland(Isl, Rng);
		Islands.Add(MoveTemp(Isl));
		return Islands.Num() - 1;
	};

	// Two home islands near opposite corners.
	HomeIsland[0] = AddIsland(FVector2f(26.f, 26.f), 7.f, true);
	HomeIsland[1] = AddIsland(FVector2f(Width - 26.f, Height - 26.f), 7.f, true);

	// Neutral islands: place in one half, mirror through the map center for
	// point symmetry (fair starts for lockstep play).
	const FVector2f MapCenter(Width * 0.5f, Height * 0.5f);
	int32 Placed = 0;
	for (int32 Try = 0; Try < 200 && Placed < 6; ++Try)
	{
		const FVector2f C(Rng.FRange(14.f, Width - 14.f), Rng.FRange(14.f, Height - 14.f));
		if ((C - MapCenter).SizeSquared() < 100.f) { continue; }           // keep center open sea
		if (C.X + C.Y > Width) { continue; }                                // one half only
		const float Radius = Rng.FRange(3.5f, 6.5f);
		bool bClear = true;
		for (const FSimIsland& I : Islands)
		{
			if ((I.Center - C).Size() < I.Radius + Radius + 8.f) { bClear = false; break; }
		}
		const FVector2f Mirror = MapCenter * 2.f - C;
		for (const FSimIsland& I : Islands)
		{
			if ((I.Center - Mirror).Size() < I.Radius + Radius + 8.f) { bClear = false; break; }
		}
		if (!bClear) { continue; }
		const bool bVolcanic = Rng.Frand() < 0.45f;
		AddIsland(C, Radius, bVolcanic);
		AddIsland(Mirror, Radius, bVolcanic);
		++Placed;
	}

	// Geysers: home islands are guaranteed a working economy.
	for (int32 i = 0; i < Islands.Num(); ++i)
	{
		const bool bHome = (i == HomeIsland[0] || i == HomeIsland[1]);
		PlaceGeysers(Islands[i], Rng, bHome ? 2 : 1);
	}

	// Spawn points: open water offset from each home island toward map center.
	for (int32 P = 0; P < 2; ++P)
	{
		const FSimIsland& Home = Islands[HomeIsland[P]];
		const FVector2f Dir = (MapCenter - Home.Center).GetSafeNormal();
		HomeSpawn[P] = NearestWater(Home.Center + Dir * (Home.Radius + 4.f));
	}
}

int32 FSimMap::IslandNear(const FVector2f& P, float Range) const
{
	const int32 PX = FMath::FloorToInt32(P.X);
	const int32 PY = FMath::FloorToInt32(P.Y);
	const int32 R = FMath::CeilToInt32(Range);
	for (const FSimIsland& Isl : Islands)
	{
		if ((Isl.Center - P).Size() > Isl.Radius * 1.4f + Range + 2.f) { continue; }
		for (const FIntPoint& C : Isl.Cells)
		{
			if (FMath::Abs(C.X - PX) <= R && FMath::Abs(C.Y - PY) <= R) { return Isl.Id; }
		}
	}
	return -1;
}

int32 FSimMap::NodeNear(const FVector2f& P, float Range) const
{
	int32 Best = -1;
	float BestDist = Range;
	for (const FSimResourceNode& N : Nodes)
	{
		if (N.Amount <= 0.f) { continue; }
		const float D = (FVector2f(N.Cell.X + 0.5f, N.Cell.Y + 0.5f) - P).Size();
		if (D <= BestDist) { BestDist = D; Best = N.Id; }
	}
	return Best;
}

FVector2f FSimMap::NearestWater(const FVector2f& P) const
{
	const int32 SX = FMath::Clamp(FMath::FloorToInt32(P.X), 0, Width - 1);
	const int32 SY = FMath::Clamp(FMath::FloorToInt32(P.Y), 0, Height - 1);
	if (IsWater(SX, SY)) { return FVector2f(SX + 0.5f, SY + 0.5f); }
	for (int32 R = 1; R < FMath::Max(Width, Height); ++R)
	{
		for (int32 DY = -R; DY <= R; ++DY)
		{
			for (int32 DX = -R; DX <= R; ++DX)
			{
				if (FMath::Max(FMath::Abs(DX), FMath::Abs(DY)) != R) { continue; }
				if (IsWater(SX + DX, SY + DY)) { return FVector2f(SX + DX + 0.5f, SY + DY + 0.5f); }
			}
		}
	}
	return FVector2f(Width * 0.5f, Height * 0.5f);
}
