#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "Sim/SimTypes.h"
#include "RTSHUD.generated.h"

class UTexture2D;
class UFont;

// Immediate-mode Canvas HUD: top resource bar, selection/command panel with
// clickable buttons, radar minimap with fog, health bars, alerts and the
// placement/attack-move hints. Read-only over the sim.
UCLASS()
class ARTSHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;

	// Hit-testing used by the player controller (UI is authoritative first).
	int32 UIButtonAt(const FVector2D& ScreenPos) const;
	bool MinimapToWorld(const FVector2D& ScreenPos, FVector2D& OutWorldXY) const;

private:
	struct FUIButton
	{
		FVector2D Pos = FVector2D::ZeroVector;
		FVector2D Size = FVector2D::ZeroVector;
		FString Label;
	};

	void DrawTopBar();
	void DrawSelectionPanel();
	void DrawMinimap();
	void DrawHealthBars();
	void DrawAlertsAndHints();
	void RefreshMinimapTexture();

	FVector2D MinimapPoint(float SimX, float SimY) const;

	UPROPERTY()
	TObjectPtr<UTexture2D> MinimapTex;

	TArray<FUIButton> Buttons;        // rebuilt every frame
	FVector2D MinimapPos = FVector2D::ZeroVector;
	float MinimapSize = 240.f;
	double NextMinimapRefresh = 0.0;
};
