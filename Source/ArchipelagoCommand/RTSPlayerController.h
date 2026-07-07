#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "Sim/SimTypes.h"
#include "RTSPlayerController.generated.h"

class AACGameMode;
class ARTSCameraPawn;
class ARTSHUD;

// Input side of the game. Every gameplay action becomes an FSimCommand
// queued on the game mode; this class never mutates sim state directly.
UCLASS()
class ARTSPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	ARTSPlayerController();

	virtual void SetupInputComponent() override;
	virtual void PlayerTick(float DeltaTime) override;

	// -- state the HUD reads --------------------------------------------------
	TArray<FEntityId> Selection;
	bool bDragging = false;
	FVector2D DragStart = FVector2D::ZeroVector;
	FVector2D DragCurrent = FVector2D::ZeroVector;
	bool bAttackMovePending = false;
	FName PlacementTpl;                 // structure being placed, NAME_None if idle
	bool bPlacementValid = false;
	FString PlacementHint;

	// Ground point under the cursor (sea plane).
	bool CursorOnSea(FVector2D& OutWorldXY) const;

	// The HUD calls this when a production/build button is clicked.
	void OnUIButton(int32 Index);

private:
	void OnSelectPressed();
	void OnSelectReleased();
	void OnCommandPressed();
	void OnCommandReleased();
	void OnToggleUnitView();
	void OnSelectAllLarvae();
	void OnAttackMoveKey();
	void OnStopKey();
	void OnSpacePressed();    // overhead: jump to HQ; unit view: fire held
	void OnSpaceReleased();
	void OnFireAltPressed();  // Backspace: fire only (unit view)
	void OnFireAltReleased();
	void OnCancelEsc();
	void OnRestart();
	void OnHotkey(int32 Index);
	void AxisCameraX(float V);
	void AxisCameraY(float V);
	void AxisZoom(float V);
	void AxisLookX(float V);
	void AxisLookY(float V);

	void FinishSelection(bool bIsClick);
	void IssuePointCommand(const FVector2D& WorldXY, bool bAttackMove);
	FEntityId PickEntityAt(const FVector2D& WorldXY, bool bEnemyOnly) const;
	void PruneSelection();

	AACGameMode* GM() const;
	ARTSCameraPawn* CamPawn() const;
	ARTSHUD* GetRTSHUD() const;

	FVector2D CameraAxis = FVector2D::ZeroVector;
	FVector2D LookAxis = FVector2D::ZeroVector;
	bool bLookActive = false;  // RMB held while in unit (follow) view
	bool bFireHeld = false;    // Space held while in unit (follow) view
	FEntityId PrevFollowed = INVALID_ENTITY;   // detects enter/exit for sim handover
	bool bEdgeScroll = true;   // -NoEdgeScroll disables (automated captures)
	bool bInputEnabled = true; // -NoInput disables all bindings (captures)

public:
	bool IsInUnitView() const;
};
