#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "RTSCameraPawn.generated.h"

class UCameraComponent;

// Top-down RTS camera: pans on the sea plane, zooms by height. Input is
// routed here by the player controller (keys, edge scroll, minimap jumps).
UCLASS()
class ARTSCameraPawn : public APawn
{
	GENERATED_BODY()

public:
	ARTSCameraPawn();

	void Pan(const FVector2D& Direction, float DeltaSeconds);
	void Zoom(float Steps);
	void JumpTo(const FVector2D& WorldXY);
	void ClampToMap(float MapSizeUU) { MapSize = MapSizeUU; }

	// Ground-plane point currently centered on screen.
	FVector2D FocusPoint() const { return FVector2D(Focus); }

	virtual void Tick(float DeltaSeconds) override;

private:
	UPROPERTY()
	TObjectPtr<UCameraComponent> Camera;

	FVector Focus = FVector::ZeroVector;   // point on the sea the camera looks at
	float Height = 14000.f;
	float TargetHeight = 14000.f;
	float MapSize = 76800.f;
	static constexpr float PitchDeg = -58.f;
};
