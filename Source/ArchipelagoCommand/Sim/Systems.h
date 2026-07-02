#pragma once

#include "CoreMinimal.h"

struct FSimGame;

// The spec-mandated tick systems (AI lives in SimAI.h). Each runs once per
// fixed tick, mutates only through the world, and iterates deterministically.
namespace SimSys
{
	void RunCommands(FSimGame& G);   // validate & apply queued player/AI commands
	void RunEconomy(FSimGame& G);    // construction, mining income, production, repair
	void RunMovement(FSimGame& G);   // waypoints, chase, attack-move, separation
	void RunCombat(FSimGame& G);     // targeting, firing, projectiles, damage
	void RunFog(FSimGame& G);        // per-player explored/visible grids
	void RunCleanup(FSimGame& G);    // free dead entities, island claims, victory
}
