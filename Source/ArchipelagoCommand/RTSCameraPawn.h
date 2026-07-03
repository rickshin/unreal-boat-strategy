#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "Sim/SimTypes.h"
#include "RTSCameraPawn.generated.h"

class UCameraComponent;

// Two view modes:
//  - Overhead: classic RTS camera, pans on the sea plane, zooms by height.
//  - Follow: over-the-shoulder chase cam behind one unit (V to toggle);
//    hold RMB to orbit, mouse wheel adjusts the follow distance.
UCLASS()
class ARTSCameraPawn : public APawn
{
	GENERATED_BODY()

public:
	ARTSCameraPawn();

	// -- overhead ------------------------------------------------------------
	void Pan(const FVector2D& Direction, float DeltaSeconds);
	void Zoom(float Steps);
	void JumpTo(const FVector2D& WorldXY);
	void ClampToMap(float MapSizeUU) { MapSize = MapSizeUU; }
	FVector2D FocusPoint() const { return FVector2D(Focus); }

	// -- follow --------------------------------------------------------------
	void EnterFollow(FEntityId Eid);
	void ExitFollow();
	bool IsFollowing() const { return FollowEid != INVALID_ENTITY; }
	FEntityId FollowedEntity() const { return FollowEid; }
	void AddLookInput(float YawDelta, float PitchDelta);

	virtual void Tick(float DeltaSeconds) override;

private:
	UPROPERTY()
	TObjectPtr<UCameraComponent> Camera;

	// Overhead state.
	FVector Focus = FVector::ZeroVector;   // point on the sea the camera looks at
	float Height = 14000.f;
	float TargetHeight = 14000.f;
	float MapSize = 76800.f;
	static constexpr float PitchDeg = -58.f;

	// Follow state.
	FEntityId FollowEid = INVALID_ENTITY;
	FVector FollowFocus = FVector::ZeroVector;
	float FollowDist = 1600.f;
	float LookYawOff = 0.f;
	float LookPitch = -12.f;
	bool bFollowJustEntered = false;
};
