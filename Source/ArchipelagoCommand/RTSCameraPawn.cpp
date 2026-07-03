#include "RTSCameraPawn.h"
#include "ACGameMode.h"
#include "UnitActor.h"
#include "Camera/CameraComponent.h"

ARTSCameraPawn::ARTSCameraPawn()
{
	PrimaryActorTick.bCanEverTick = true;
	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;
	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(Root);
	Camera->SetFieldOfView(55.f);
	// Pin auto exposure: metering a scene that is mostly dark sea pushes
	// the exposure way up and blows out boats and islands to near-white.
	Camera->PostProcessSettings.bOverride_AutoExposureMinBrightness = true;
	Camera->PostProcessSettings.AutoExposureMinBrightness = 1.f;
	Camera->PostProcessSettings.bOverride_AutoExposureMaxBrightness = true;
	Camera->PostProcessSettings.AutoExposureMaxBrightness = 1.f;
	SetActorEnableCollision(false);
}

void ARTSCameraPawn::Pan(const FVector2D& Direction, float DeltaSeconds)
{
	if (IsFollowing()) { return; }   // the chase cam is glued to its unit
	// Pan speed scales with zoom so screen-space feel stays constant.
	const float Speed = Height * 1.1f;
	Focus.X += Direction.X * Speed * DeltaSeconds;
	Focus.Y += Direction.Y * Speed * DeltaSeconds;
}

void ARTSCameraPawn::Zoom(float Steps)
{
	if (IsFollowing())
	{
		FollowDist = FMath::Clamp(FollowDist * FMath::Pow(1.18f, Steps), 600.f, 5200.f);
		return;
	}
	TargetHeight = FMath::Clamp(TargetHeight * FMath::Pow(1.18f, Steps), 1500.f, 42000.f);
}

void ARTSCameraPawn::JumpTo(const FVector2D& WorldXY)
{
	Focus.X = WorldXY.X;
	Focus.Y = WorldXY.Y;
}

void ARTSCameraPawn::EnterFollow(FEntityId Eid)
{
	FollowEid = Eid;
	LookYawOff = 0.f;
	LookPitch = -12.f;
	bFollowJustEntered = true;
	UE_LOG(LogTemp, Log, TEXT("Camera: follow entity %d"), Eid);
}

void ARTSCameraPawn::ExitFollow()
{
	if (!IsFollowing()) { return; }
	UE_LOG(LogTemp, Log, TEXT("Camera: follow ended (entity %d)"), FollowEid);
	FollowEid = INVALID_ENTITY;
	// Return overhead right where the action was.
	Focus.X = FollowFocus.X;
	Focus.Y = FollowFocus.Y;
}

void ARTSCameraPawn::AddLookInput(float YawDelta, float PitchDelta)
{
	LookYawOff = FMath::Fmod(LookYawOff + YawDelta, 360.f);
	LookPitch = FMath::Clamp(LookPitch + PitchDelta, -55.f, 30.f);
}

void ARTSCameraPawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (IsFollowing())
	{
		AACGameMode* GM = GetWorld()->GetAuthGameMode<AACGameMode>();
		AUnitActor* Actor = GM ? GM->FindEntityActor(FollowEid) : nullptr;
		if (!Actor)
		{
			ExitFollow();   // unit died or the match restarted
		}
		else
		{
			const FVector Target = Actor->GetActorLocation();
			const float UnitYaw = Actor->GetActorRotation().Yaw;
			FollowFocus = bFollowJustEntered
				? Target : FMath::VInterpTo(FollowFocus, Target, DeltaSeconds, 8.f);
			bFollowJustEntered = false;

			// Chase cam: swings with the hull as the rudder turns it, with
			// the free-look offset (RMB aim) applied on top.
			ViewYawDeg = UnitYaw + LookYawOff;
			const FRotator ViewRot(LookPitch, ViewYawDeg, 0.f);
			const FVector CamPos = FollowFocus - ViewRot.Vector() * FollowDist + FVector(0, 0, 340.f);
			SetActorLocation(CamPos);
			SetActorRotation(ViewRot);
			return;
		}
	}

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
