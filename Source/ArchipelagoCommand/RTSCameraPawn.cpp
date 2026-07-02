#include "RTSCameraPawn.h"
#include "Camera/CameraComponent.h"

ARTSCameraPawn::ARTSCameraPawn()
{
	PrimaryActorTick.bCanEverTick = true;
	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;
	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(Root);
	Camera->SetFieldOfView(55.f);
	SetActorEnableCollision(false);
}

void ARTSCameraPawn::Pan(const FVector2D& Direction, float DeltaSeconds)
{
	// Pan speed scales with zoom so screen-space feel stays constant.
	const float Speed = Height * 1.1f;
	Focus.X += Direction.X * Speed * DeltaSeconds;
	Focus.Y += Direction.Y * Speed * DeltaSeconds;
}

void ARTSCameraPawn::Zoom(float Steps)
{
	TargetHeight = FMath::Clamp(TargetHeight * FMath::Pow(1.18f, Steps), 3500.f, 42000.f);
}

void ARTSCameraPawn::JumpTo(const FVector2D& WorldXY)
{
	Focus.X = WorldXY.X;
	Focus.Y = WorldXY.Y;
}

void ARTSCameraPawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Height = FMath::FInterpTo(Height, TargetHeight, DeltaSeconds, 8.f);
	const float Margin = MapSize * 0.06f;
	Focus.X = FMath::Clamp(Focus.X, -Margin, MapSize + Margin);
	Focus.Y = FMath::Clamp(Focus.Y, -Margin, MapSize + Margin);

	// Sit back from the focus point along the pitched view direction.
	const float PitchRad = FMath::DegreesToRadians(-PitchDeg);
	const float Back = Height / FMath::Tan(PitchRad);
	SetActorLocation(FVector(Focus.X - Back, Focus.Y, Height));
	SetActorRotation(FRotator(PitchDeg, 0.f, 0.f));
}
