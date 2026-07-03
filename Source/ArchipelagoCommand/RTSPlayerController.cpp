#include "RTSPlayerController.h"
#include "ACGameMode.h"
#include "ACTypes.h"
#include "RTSCameraPawn.h"
#include "RTSHUD.h"
#include "UnitActor.h"
#include "Sim/SimGame.h"
#include "Engine/World.h"
#include "Components/InputComponent.h"
#include "GameFramework/PlayerInput.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

DECLARE_DELEGATE_OneParam(FHotkeyDelegate, int32);

ARTSPlayerController::ARTSPlayerController()
{
	bShowMouseCursor = true;
	bEnableClickEvents = false;
	bEnableMouseOverEvents = false;
	bEdgeScroll = !FParse::Param(FCommandLine::Get(), TEXT("NoEdgeScroll"));
}

AACGameMode* ARTSPlayerController::GM() const
{
	return GetWorld()->GetAuthGameMode<AACGameMode>();
}

ARTSCameraPawn* ARTSPlayerController::CamPawn() const
{
	return Cast<ARTSCameraPawn>(GetPawn());
}

ARTSHUD* ARTSPlayerController::GetRTSHUD() const
{
	return Cast<ARTSHUD>(GetHUD());
}

void ARTSPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	InputComponent->BindAction(TEXT("Select"), IE_Pressed, this, &ARTSPlayerController::OnSelectPressed);
	InputComponent->BindAction(TEXT("Select"), IE_Released, this, &ARTSPlayerController::OnSelectReleased);
	InputComponent->BindAction(TEXT("Command"), IE_Pressed, this, &ARTSPlayerController::OnCommandPressed);
	InputComponent->BindAction(TEXT("Command"), IE_Released, this, &ARTSPlayerController::OnCommandReleased);
	InputComponent->BindAction(TEXT("ToggleUnitView"), IE_Pressed, this, &ARTSPlayerController::OnToggleUnitView);
	InputComponent->BindAction(TEXT("AttackMove"), IE_Pressed, this, &ARTSPlayerController::OnAttackMoveKey);
	InputComponent->BindAction(TEXT("Stop"), IE_Pressed, this, &ARTSPlayerController::OnStopKey);
	InputComponent->BindAction(TEXT("JumpToHQ"), IE_Pressed, this, &ARTSPlayerController::OnSpacePressed);
	InputComponent->BindAction(TEXT("JumpToHQ"), IE_Released, this, &ARTSPlayerController::OnSpaceReleased);
	InputComponent->BindAction(TEXT("CancelEsc"), IE_Pressed, this, &ARTSPlayerController::OnCancelEsc);
	InputComponent->BindAction(TEXT("Restart"), IE_Pressed, this, &ARTSPlayerController::OnRestart);
	for (int32 i = 1; i <= 9; ++i)
	{
		// Payload BindAction overload: passes the hotkey index as payload.
		InputComponent->BindAction<FHotkeyDelegate>(FName(*FString::Printf(TEXT("Hotkey%d"), i)),
			IE_Pressed, this, &ARTSPlayerController::OnHotkey, i);
	}
	InputComponent->BindAxis(TEXT("CameraX"), this, &ARTSPlayerController::AxisCameraX);
	InputComponent->BindAxis(TEXT("CameraY"), this, &ARTSPlayerController::AxisCameraY);
	InputComponent->BindAxis(TEXT("CameraZoom"), this, &ARTSPlayerController::AxisZoom);
	InputComponent->BindAxis(TEXT("LookX"), this, &ARTSPlayerController::AxisLookX);
	InputComponent->BindAxis(TEXT("LookY"), this, &ARTSPlayerController::AxisLookY);
}

bool ARTSPlayerController::IsInUnitView() const
{
	return CamPawn() && CamPawn()->IsFollowing();
}

bool ARTSPlayerController::CursorOnSea(FVector2D& OutWorldXY) const
{
	float MX, MY;
	if (!GetMousePosition(MX, MY)) { return false; }
	FVector Origin, Dir;
	if (!DeprojectScreenPositionToWorld(MX, MY, Origin, Dir)) { return false; }
	if (FMath::IsNearlyZero(Dir.Z)) { return false; }
	const float T = (AC_SEA_LEVEL - Origin.Z) / Dir.Z;
	if (T <= 0.f) { return false; }
	const FVector Hit = Origin + Dir * T;
	OutWorldXY = FVector2D(Hit.X, Hit.Y);
	return true;
}

void ARTSPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);

	// If the followed unit died while looking, restore the cursor.
	if (bLookActive && !IsInUnitView()) { OnCommandReleased(); }
	if (bFireHeld && !IsInUnitView()) { bFireHeld = false; }

	// Direct-control handover: whenever the followed unit changes, tell the
	// sim to take/release manual control (auto-fire off while controlled).
	AACGameMode* Mode = GM();
	const FEntityId Followed = CamPawn() ? CamPawn()->FollowedEntity() : INVALID_ENTITY;
	if (Mode && Followed != PrevFollowed)
	{
		if (PrevFollowed != INVALID_ENTITY)
		{
			FSimCommand Cmd;
			Cmd.Type = ECmdType::ManualControl;
			Cmd.Player = Mode->LocalPlayer();
			Cmd.Units = { PrevFollowed };
			Cmd.bFlag = false;
			Mode->QueueCommand(Cmd);
		}
		if (Followed != INVALID_ENTITY)
		{
			FSimCommand Cmd;
			Cmd.Type = ECmdType::ManualControl;
			Cmd.Player = Mode->LocalPlayer();
			Cmd.Units = { Followed };
			Cmd.bFlag = true;
			Mode->QueueCommand(Cmd);
		}
		PrevFollowed = Followed;
	}

	// Unit view: RMB-held mouse deltas steer the view; WASD and Space are
	// streamed to the sim as this unit's drive/fire input.
	if (IsInUnitView())
	{
		if (bLookActive && !LookAxis.IsNearlyZero())
		{
			CamPawn()->AddLookInput(LookAxis.X * 1.6f, LookAxis.Y * 1.6f);
		}
		if (Mode && Followed != INVALID_ENTITY)
		{
			FSimCommand Cmd;
			Cmd.Type = ECmdType::ManualInput;
			Cmd.Player = Mode->LocalPlayer();
			Cmd.Units = { Followed };
			Cmd.Target = FVector2f(float(CameraAxis.X), float(CameraAxis.Y));  // fwd, strafe
			Cmd.FacingRad = FMath::DegreesToRadians(CamPawn()->GetViewYawDegrees());
			Cmd.bFlag = bFireHeld;
			Mode->QueueCommand(Cmd);
		}
	}
	else
	{
		// Overhead camera: WASD/arrow keys + screen-edge scroll.
		FVector2D Pan = CameraAxis;
		float MX, MY;
		int32 VW, VH;
		GetViewportSize(VW, VH);
		if (bEdgeScroll && GetMousePosition(MX, MY) && VW > 0 && VH > 0)
		{
			const float Edge = 14.f;
			if (MX <= Edge) { Pan.Y -= 1.f; }             // screen left = -world Y
			if (MX >= VW - Edge) { Pan.Y += 1.f; }
			if (MY <= Edge) { Pan.X += 1.f; }             // screen up = +world X
			if (MY >= VH - Edge) { Pan.X -= 1.f; }
		}
		if (!Pan.IsNearlyZero() && CamPawn())
		{
			CamPawn()->Pan(Pan.GetSafeNormal(), DeltaTime);
		}
	}
	float MX, MY;

	if (bDragging)
	{
		if (GetMousePosition(MX, MY)) { DragCurrent = FVector2D(MX, MY); }
	}

	// Placement ghost validity, recomputed against the live sim.
	if (!PlacementTpl.IsNone() && GM() && GM()->Sim())
	{
		FVector2D Sea;
		if (CursorOnSea(Sea))
		{
			FString Why;
			bPlacementValid = GM()->Sim()->IsValidBuildSite(
				GM()->LocalPlayer(), PlacementTpl, WorldToSim2D(FVector(Sea.X, Sea.Y, 0)), &Why);
			PlacementHint = bPlacementValid ? TEXT("Click to place") : Why;
		}
	}

	PruneSelection();
}

void ARTSPlayerController::PruneSelection()
{
	AACGameMode* Mode = GM();
	if (!Mode || !Mode->Sim()) { return; }
	Selection.RemoveAll([&](FEntityId E) { return !Mode->Sim()->World.IsAlive(E); });
}

// ---------------------------------------------------------------------------

void ARTSPlayerController::OnSelectPressed()
{
	float MX, MY;
	if (!GetMousePosition(MX, MY)) { return; }
	const FVector2D Screen(MX, MY);
	ARTSHUD* Hud = GetRTSHUD();
	AACGameMode* Mode = GM();
	if (!Mode || !Mode->Sim()) { return; }

	// UI first: buttons, then minimap camera jump.
	if (Hud)
	{
		const int32 Button = Hud->UIButtonAt(Screen);
		if (Button >= 0) { OnUIButton(Button); return; }
		FVector2D World;
		if (Hud->MinimapToWorld(Screen, World))
		{
			if (CamPawn())
			{
				CamPawn()->ExitFollow();   // minimap jump implies overhead
				CamPawn()->JumpTo(World);
			}
			return;
		}
	}

	// In unit view, clicks on the world are ignored (UI still works above).
	if (IsInUnitView()) { return; }

	// Placement mode: this click places the structure.
	if (!PlacementTpl.IsNone())
	{
		FVector2D Sea;
		if (CursorOnSea(Sea) && bPlacementValid)
		{
			FEntityId Builder = INVALID_ENTITY;
			for (const FEntityId E : Selection)
			{
				const FUnitC* U = Mode->Sim()->World.Unit.Find(E);
				if (U && U->bBuilder) { Builder = E; break; }
			}
			if (Builder != INVALID_ENTITY)
			{
				FSimCommand Cmd;
				Cmd.Type = ECmdType::Build;
				Cmd.Player = Mode->LocalPlayer();
				Cmd.Units = { Builder };
				Cmd.Target = WorldToSim2D(FVector(Sea.X, Sea.Y, 0));
				Cmd.TplId = PlacementTpl;
				Mode->QueueCommand(Cmd);
			}
			PlacementTpl = NAME_None;
		}
		return;
	}

	// Attack-move pending: this click is the destination.
	if (bAttackMovePending)
	{
		FVector2D Sea;
		if (CursorOnSea(Sea)) { IssuePointCommand(Sea, true); }
		bAttackMovePending = false;
		return;
	}

	bDragging = true;
	DragStart = DragCurrent = Screen;
}

void ARTSPlayerController::OnSelectReleased()
{
	if (!bDragging) { return; }
	bDragging = false;
	const bool bIsClick = (DragCurrent - DragStart).Size() < 8.f;
	FinishSelection(bIsClick);
}

void ARTSPlayerController::FinishSelection(bool bIsClick)
{
	AACGameMode* Mode = GM();
	if (!Mode || !Mode->Sim()) { return; }
	FSimGame& G = *Mode->Sim();
	const int32 Me = Mode->LocalPlayer();
	const bool bAdd = IsInputKeyDown(EKeys::LeftShift) || IsInputKeyDown(EKeys::RightShift);
	if (!bAdd) { Selection.Reset(); }

	if (bIsClick)
	{
		FVector2D Sea;
		if (CursorOnSea(Sea))
		{
			const FEntityId Hit = PickEntityAt(Sea, false);
			if (Hit != INVALID_ENTITY)
			{
				const FOwnerC* O = G.World.Own.Find(Hit);
				if (O && O->Player == Me) { Selection.AddUnique(Hit); }
			}
		}
	}
	else
	{
		// Box select own mobile units by projected screen position.
		const FVector2D Lo(FMath::Min(DragStart.X, DragCurrent.X), FMath::Min(DragStart.Y, DragCurrent.Y));
		const FVector2D Hi(FMath::Max(DragStart.X, DragCurrent.X), FMath::Max(DragStart.Y, DragCurrent.Y));
		for (const FEntityId Eid : SimSortedKeys(G.World.Unit))
		{
			const FOwnerC* O = G.World.Own.Find(Eid);
			if (!O || O->Player != Me) { continue; }
			const AUnitActor* Actor = Mode->FindEntityActor(Eid);
			if (!Actor) { continue; }
			FVector2D Screen;
			if (ProjectWorldLocationToScreen(Actor->GetActorLocation(), Screen)
				&& Screen.X >= Lo.X && Screen.X <= Hi.X && Screen.Y >= Lo.Y && Screen.Y <= Hi.Y)
			{
				Selection.AddUnique(Eid);
			}
		}
	}

	// Sync selection rings.
	for (const auto& Pair : Mode->EntityActors())
	{
		Pair.Value->SetSelected(Selection.Contains(Pair.Key));
	}
}

FEntityId ARTSPlayerController::PickEntityAt(const FVector2D& WorldXY, bool bEnemyOnly) const
{
	AACGameMode* Mode = GM();
	if (!Mode || !Mode->Sim()) { return INVALID_ENTITY; }
	FSimGame& G = *Mode->Sim();
	const int32 Me = Mode->LocalPlayer();
	const FVector2f SimP = WorldToSim2D(FVector(WorldXY.X, WorldXY.Y, 0));

	FEntityId Best = INVALID_ENTITY;
	float BestDist = TNumericLimits<float>::Max();
	for (const FEntityId Eid : SimSortedKeys(G.World.Pos))
	{
		if (G.World.Proj.Contains(Eid)) { continue; }
		const FOwnerC* O = G.World.Own.Find(Eid);
		if (!O) { continue; }
		if (bEnemyOnly)
		{
			if (O->Player == Me) { continue; }
			if (!Mode->IsSpectating() && !G.IsVisibleTo(Me, G.World.Pos[Eid].P)) { continue; }
		}
		const float PickRadius = G.World.Struct.Contains(Eid) ? 2.4f : 1.3f;
		const float D = (G.World.Pos[Eid].P - SimP).Size();
		if (D < PickRadius && D < BestDist) { BestDist = D; Best = Eid; }
	}
	return Best;
}

// ---------------------------------------------------------------------------

void ARTSPlayerController::OnCommandPressed()
{
	// In unit view the right button drives the free-look instead of orders.
	if (IsInUnitView())
	{
		bLookActive = true;
		bShowMouseCursor = false;
		return;
	}
	AACGameMode* Mode = GM();
	if (!Mode || !Mode->Sim() || Mode->IsSpectating()) { return; }
	float MX, MY;
	if (!GetMousePosition(MX, MY)) { return; }

	// Right-click on the minimap: move there.
	if (ARTSHUD* Hud = GetRTSHUD())
	{
		FVector2D World;
		if (Hud->MinimapToWorld(FVector2D(MX, MY), World))
		{
			IssuePointCommand(World, false);
			return;
		}
	}

	FVector2D Sea;
	if (CursorOnSea(Sea)) { IssuePointCommand(Sea, false); }
}

void ARTSPlayerController::IssuePointCommand(const FVector2D& WorldXY, bool bAttackMove)
{
	AACGameMode* Mode = GM();
	if (!Mode || Selection.Num() == 0) { return; }
	FSimGame& G = *Mode->Sim();
	const int32 Me = Mode->LocalPlayer();

	TArray<FEntityId> Movers;
	for (const FEntityId E : Selection)
	{
		if (G.World.Mover.Contains(E)) { Movers.Add(E); }
	}
	if (Movers.Num() == 0) { return; }

	const FVector2f SimTarget = WorldToSim2D(FVector(WorldXY.X, WorldXY.Y, 0));

	// Right-click on a geyser: selected harvesters start the gas cycle.
	if (!bAttackMove)
	{
		const int32 Node = G.Map.NodeNear(SimTarget, 2.f);
		if (Node >= 0)
		{
			TArray<FEntityId> Harvesters;
			for (const FEntityId E : Movers)
			{
				if (G.World.Harvest.Contains(E)) { Harvesters.Add(E); }
			}
			if (Harvesters.Num() > 0)
			{
				FSimCommand HCmd;
				HCmd.Type = ECmdType::Harvest;
				HCmd.Player = Me;
				HCmd.Units = Harvesters;
				HCmd.TargetEid = Node;
				Mode->QueueCommand(HCmd);
				Movers.RemoveAll([&](FEntityId E) { return Harvesters.Contains(E); });
				if (Movers.Num() == 0) { return; }
			}
		}
	}

	FSimCommand Cmd;
	Cmd.Player = Me;
	Cmd.Units = Movers;
	Cmd.Target = SimTarget;

	const FEntityId Enemy = PickEntityAt(WorldXY, true);
	if (!bAttackMove && Enemy != INVALID_ENTITY)
	{
		Cmd.Type = ECmdType::Attack;
		Cmd.TargetEid = Enemy;
	}
	else
	{
		Cmd.Type = bAttackMove ? ECmdType::AttackMove : ECmdType::Move;
	}
	Mode->QueueCommand(Cmd);
}

void ARTSPlayerController::OnCommandReleased()
{
	if (bLookActive)
	{
		bLookActive = false;
		bShowMouseCursor = true;
	}
}

void ARTSPlayerController::OnToggleUnitView()
{
	ARTSCameraPawn* Cam = CamPawn();
	if (!Cam) { return; }
	if (Cam->IsFollowing())
	{
		Cam->ExitFollow();
		OnCommandReleased();
		return;
	}
	if (Selection.Num() > 0)
	{
		PlacementTpl = NAME_None;
		bAttackMovePending = false;
		Cam->EnterFollow(Selection[0]);
	}
}

void ARTSPlayerController::OnAttackMoveKey()
{
	if (Selection.Num() > 0 && !IsInUnitView()) { bAttackMovePending = true; }
}

void ARTSPlayerController::OnStopKey()
{
	AACGameMode* Mode = GM();
	if (!Mode || Selection.Num() == 0) { return; }
	FSimCommand Cmd;
	Cmd.Type = ECmdType::Stop;
	Cmd.Player = Mode->LocalPlayer();
	Cmd.Units = Selection;
	Mode->QueueCommand(Cmd);
}

void ARTSPlayerController::OnSpacePressed()
{
	// Unit view: Space is the trigger.
	if (IsInUnitView())
	{
		bFireHeld = true;
		return;
	}
	AACGameMode* Mode = GM();
	if (!Mode || !Mode->Sim() || !CamPawn()) { return; }
	const FEntityId Hq = Mode->Sim()->Players[Mode->LocalPlayer()].HqEid;
	if (const FPosC* P = Mode->Sim()->World.Pos.Find(Hq))
	{
		CamPawn()->JumpTo(SimToWorld2D(P->P));
	}
}

void ARTSPlayerController::OnSpaceReleased()
{
	bFireHeld = false;
}

void ARTSPlayerController::OnCancelEsc()
{
	if (IsInUnitView()) { OnToggleUnitView(); return; }
	if (!PlacementTpl.IsNone()) { PlacementTpl = NAME_None; return; }
	if (bAttackMovePending) { bAttackMovePending = false; return; }
	if (Selection.Num() > 0)
	{
		Selection.Reset();
		if (AACGameMode* Mode = GM())
		{
			for (const auto& Pair : Mode->EntityActors()) { Pair.Value->SetSelected(false); }
		}
		return;
	}
	ConsoleCommand(TEXT("quit"));
}

void ARTSPlayerController::OnRestart()
{
	AACGameMode* Mode = GM();
	if (Mode && Mode->Sim() && Mode->Sim()->Winner >= 0)
	{
		Selection.Reset();
		PlacementTpl = NAME_None;
		Mode->RequestRestart();
	}
}

void ARTSPlayerController::OnHotkey(int32 Index)
{
	OnUIButton(Index - 1);
}

void ARTSPlayerController::OnUIButton(int32 Index)
{
	AACGameMode* Mode = GM();
	if (!Mode || !Mode->Sim() || Mode->IsSpectating()) { return; }
	FSimGame& G = *Mode->Sim();
	const int32 Me = Mode->LocalPlayer();

	// Producer selected: queue unit production. Builder selected: start placement.
	for (const FEntityId E : Selection)
	{
		if (const FProdC* Prod = G.World.Prod.Find(E))
		{
			if (Index < Prod->Options.Num())
			{
				FSimCommand Cmd;
				Cmd.Type = ECmdType::Produce;
				Cmd.Player = Me;
				Cmd.TargetEid = E;
				Cmd.TplId = Prod->Options[Index];
				Mode->QueueCommand(Cmd);
			}
			return;
		}
		const FUnitC* U = G.World.Unit.Find(E);
		if (U && U->bBuilder)
		{
			const FUnitTpl* T = G.Content.Unit(G.Players[Me].FactionId, U->Tpl);
			if (T && Index < T->Builds.Num())
			{
				PlacementTpl = T->Builds[Index];
				bAttackMovePending = false;
			}
			return;
		}
	}
}

void ARTSPlayerController::AxisCameraX(float V) { CameraAxis.Y = V; }
void ARTSPlayerController::AxisCameraY(float V) { CameraAxis.X = V; }
void ARTSPlayerController::AxisLookX(float V) { LookAxis.X = V; }
void ARTSPlayerController::AxisLookY(float V) { LookAxis.Y = V; }

void ARTSPlayerController::AxisZoom(float V)
{
	if (!FMath::IsNearlyZero(V) && CamPawn()) { CamPawn()->Zoom(V); }
}
