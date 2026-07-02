#pragma once

#include "CoreMinimal.h"
#include "SimTypes.h"

struct FSimGame;

// Three-layer AI (spec 14), scheduled at staggered cadences per player:
//   Strategic  (every 2.0s): economy planning, expansion, army composition
//   Operational(every 1.0s): staging, attack waves, base defense
//   Tactical   (every 0.25s): per-unit retreat at low HP
// Focus fire lives in Combat targeting, not here.
namespace SimAI
{
	struct FAIState
	{
		FVector2f StagingPoint = FVector2f::ZeroVector;
		bool bAttacking = false;
		int32 NextWaveTick = 0;
		int32 CompositionCursor = 0;
	};

	void Run(FSimGame& G);
}
