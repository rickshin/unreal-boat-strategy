#include "RTSHUD.h"
#include "ACGameMode.h"
#include "ACTypes.h"
#include "RTSCameraPawn.h"
#include "RTSPlayerController.h"
#include "UnitActor.h"
#include "Sim/SimGame.h"
#include "Engine/Canvas.h"
#include "Engine/Font.h"
#include "Engine/Texture2D.h"
#include "Engine/Engine.h"

namespace
{
	const FLinearColor PanelBg(0.02f, 0.03f, 0.05f, 0.82f);
	const FLinearColor TextMain(0.92f, 0.95f, 1.f);
	const FLinearColor TextDim(0.6f, 0.66f, 0.72f);
	const FLinearColor AlertRed(1.f, 0.35f, 0.3f);
	const FLinearColor GoodGreen(0.35f, 1.f, 0.45f);
	const FLinearColor BadRed(1.f, 0.3f, 0.25f);
}

void ARTSHUD::DrawHUD()
{
	Super::DrawHUD();
	AACGameMode* GM = GetWorld()->GetAuthGameMode<AACGameMode>();
	if (!GM || !GM->Sim() || !Canvas) { return; }

	MinimapSize = FMath::Clamp(Canvas->SizeY * 0.28f, 180.f, 320.f);
	MinimapPos = FVector2D(Canvas->SizeX - MinimapSize - 14.f, Canvas->SizeY - MinimapSize - 14.f);

	// Selection drag rectangle.
	ARTSPlayerController* PC = Cast<ARTSPlayerController>(GetOwningPlayerController());
	if (PC && PC->bDragging)
	{
		const FVector2D A = PC->DragStart, B = PC->DragCurrent;
		const FLinearColor Box(0.3f, 1.f, 0.4f, 0.9f);
		DrawLine(A.X, A.Y, B.X, A.Y, Box, 1.5f);
		DrawLine(B.X, A.Y, B.X, B.Y, Box, 1.5f);
		DrawLine(B.X, B.Y, A.X, B.Y, Box, 1.5f);
		DrawLine(A.X, B.Y, A.X, A.Y, Box, 1.5f);
	}

	DrawHealthBars();
	DrawTopBar();
	DrawSelectionPanel();
	DrawMinimap();
	DrawAlertsAndHints();
}

// ---------------------------------------------------------------------------

void ARTSHUD::DrawTopBar()
{
	AACGameMode* GM = GetWorld()->GetAuthGameMode<AACGameMode>();
	FSimGame& G = *GM->Sim();
	UFont* Font = GEngine->GetMediumFont();

	DrawRect(PanelBg, 0, 0, Canvas->SizeX, 30.f);
	const int32 Me = GM->LocalPlayer();
	const FSimPlayer& Pl = G.Players[Me];

	FString Line = FString::Printf(TEXT("Wood %d    Iron %d    |  %s"),
		FMath::FloorToInt32(Pl.Wood), FMath::FloorToInt32(Pl.Iron),
		*G.FactionOf(Me).DisplayName);
	if (GM->IsSpectating())
	{
		const FSimPlayer& P1 = G.Players[1];
		Line = FString::Printf(TEXT("SPECTATE  |  %s  W%d/I%d   vs   %s  W%d/I%d"),
			*G.FactionOf(0).DisplayName, FMath::FloorToInt32(Pl.Wood), FMath::FloorToInt32(Pl.Iron),
			*G.FactionOf(1).DisplayName, FMath::FloorToInt32(P1.Wood), FMath::FloorToInt32(P1.Iron));
	}
	DrawText(Line, TextMain, 12.f, 7.f, Font);

	const int32 Secs = G.Tick / SIM_TICKS_PER_SEC;
	const FString Right = FString::Printf(TEXT("%02d:%02d   seed %llu   %s"),
		Secs / 60, Secs % 60, G.Seed,
		G.Difficulty == ESimDifficulty::Easy ? TEXT("easy")
		: G.Difficulty == ESimDifficulty::Hard ? TEXT("hard") : TEXT("normal"));
	float TW, TH;
	GetTextSize(Right, TW, TH, Font);
	DrawText(Right, TextDim, Canvas->SizeX - TW - 12.f, 7.f, Font);
}

// ---------------------------------------------------------------------------

void ARTSHUD::DrawSelectionPanel()
{
	Buttons.Reset();
	AACGameMode* GM = GetWorld()->GetAuthGameMode<AACGameMode>();
	ARTSPlayerController* PC = Cast<ARTSPlayerController>(GetOwningPlayerController());
	if (!PC || PC->Selection.Num() == 0) { return; }
	FSimGame& G = *GM->Sim();
	const int32 Me = GM->LocalPlayer();
	UFont* Font = GEngine->GetSmallFont();

	const float PanelH = 118.f;
	const float PanelY = Canvas->SizeY - PanelH;
	const float PanelW = Canvas->SizeX - MinimapSize - 40.f;
	DrawRect(PanelBg, 0, PanelY, PanelW, PanelH);

	// Info block: first selected entity.
	const FEntityId First = PC->Selection[0];
	const AUnitActor* Actor = GM->FindEntityActor(First);
	const FHealthC* H = G.World.Health.Find(First);
	FString Title = Actor ? Actor->DisplayName : TEXT("Unit");
	if (PC->Selection.Num() > 1) { Title += FString::Printf(TEXT("  (+%d selected)"), PC->Selection.Num() - 1); }
	DrawText(Title, TextMain, 12.f, PanelY + 8.f, Font);
	if (H)
	{
		DrawText(FString::Printf(TEXT("HP %d / %d"), FMath::CeilToInt32(H->Hp), FMath::CeilToInt32(H->MaxHp)),
			TextDim, 12.f, PanelY + 26.f, Font);
	}

	// Options: production list or builder's structure list.
	TArray<FName> Options;
	const FProdC* Prod = nullptr;
	bool bBuilder = false;
	for (const FEntityId E : PC->Selection)
	{
		if ((Prod = G.World.Prod.Find(E)) != nullptr) { Options = Prod->Options; break; }
		const FUnitC* U = G.World.Unit.Find(E);
		if (U && U->bBuilder)
		{
			const FUnitTpl* T = G.Content.Unit(G.Players[Me].FactionId, U->Tpl);
			if (T) { Options = T->Builds; bBuilder = true; }
			break;
		}
	}

	const float BtnW = 148.f, BtnH = 40.f, Gap = 8.f;
	float X = 200.f;
	const float Y = PanelY + 12.f;
	for (int32 i = 0; i < Options.Num() && i < 9; ++i)
	{
		FString Name;
		int32 Wood = 0, Iron = 0;
		if (bBuilder)
		{
			if (const FStructTpl* T = G.Content.Structure(G.Players[Me].FactionId, Options[i]))
			{
				Name = T->Name; Wood = T->CostWood; Iron = T->CostIron;
			}
		}
		else if (const FUnitTpl* T = G.Content.Unit(G.Players[Me].FactionId, Options[i]))
		{
			Name = T->Name; Wood = T->CostWood; Iron = T->CostIron;
		}
		if (Name.IsEmpty()) { continue; }

		const bool bAfford = G.CanAfford(Me, Wood, Iron);
		DrawRect(bAfford ? FLinearColor(0.10f, 0.16f, 0.24f, 0.95f) : FLinearColor(0.12f, 0.06f, 0.06f, 0.95f),
			X, Y, BtnW, BtnH);
		DrawText(FString::Printf(TEXT("%d. %s"), i + 1, *Name), bAfford ? TextMain : TextDim,
			X + 6.f, Y + 5.f, Font);
		DrawText(FString::Printf(TEXT("W%d  I%d"), Wood, Iron), TextDim, X + 6.f, Y + 22.f, Font);
		Buttons.Add({ FVector2D(X, Y), FVector2D(BtnW, BtnH), Name });
		X += BtnW + Gap;
		if (X + BtnW > PanelW) { break; }
	}

	// Production queue status.
	if (Prod && Prod->Queue.Num() > 0)
	{
		const FUnitTpl* T = G.Content.Unit(G.Players[Me].FactionId, Prod->Queue[0]);
		const float Frac = T ? FMath::Clamp(Prod->Progress / T->BuildTime, 0.f, 1.f) : 0.f;
		const float QY = PanelY + 66.f;
		DrawText(FString::Printf(TEXT("Building %s  (queue %d)"),
			T ? *T->Name : TEXT("?"), Prod->Queue.Num()), TextMain, 200.f, QY, Font);
		DrawRect(FLinearColor(0.08f, 0.10f, 0.12f), 200.f, QY + 20.f, 300.f, 10.f);
		DrawRect(GoodGreen, 200.f, QY + 20.f, 300.f * Frac, 10.f);
	}
}

int32 ARTSHUD::UIButtonAt(const FVector2D& ScreenPos) const
{
	for (int32 i = 0; i < Buttons.Num(); ++i)
	{
		const FUIButton& B = Buttons[i];
		if (ScreenPos.X >= B.Pos.X && ScreenPos.X <= B.Pos.X + B.Size.X
			&& ScreenPos.Y >= B.Pos.Y && ScreenPos.Y <= B.Pos.Y + B.Size.Y)
		{
			return i;
		}
	}
	return -1;
}

// ---------------------------------------------------------------------------

FVector2D ARTSHUD::MinimapPoint(float SimX, float SimY) const
{
	AACGameMode* GM = GetWorld()->GetAuthGameMode<AACGameMode>();
	const float MapW = GM->Sim()->Map.Width;
	// Camera space: screen-up = +world X, screen-right = +world Y.
	return FVector2D(
		MinimapPos.X + (SimY / MapW) * MinimapSize,
		MinimapPos.Y + (1.f - SimX / MapW) * MinimapSize);
}

bool ARTSHUD::MinimapToWorld(const FVector2D& ScreenPos, FVector2D& OutWorldXY) const
{
	if (ScreenPos.X < MinimapPos.X || ScreenPos.Y < MinimapPos.Y
		|| ScreenPos.X > MinimapPos.X + MinimapSize || ScreenPos.Y > MinimapPos.Y + MinimapSize)
	{
		return false;
	}
	AACGameMode* GM = GetWorld()->GetAuthGameMode<AACGameMode>();
	if (!GM || !GM->Sim()) { return false; }
	const float MapW = GM->Sim()->Map.Width;
	const float SimY = (ScreenPos.X - MinimapPos.X) / MinimapSize * MapW;
	const float SimX = (1.f - (ScreenPos.Y - MinimapPos.Y) / MinimapSize) * MapW;
	OutWorldXY = FVector2D(SimX * AC_CELL, SimY * AC_CELL);
	return true;
}

void ARTSHUD::RefreshMinimapTexture()
{
	AACGameMode* GM = GetWorld()->GetAuthGameMode<AACGameMode>();
	FSimGame& G = *GM->Sim();
	const int32 W = G.Map.Width;
	const int32 Me = GM->LocalPlayer();
	const bool bAll = GM->IsSpectating();

	MinimapTex = UTexture2D::CreateTransient(W, W, PF_B8G8R8A8);
	TArray<FColor> Pixels;
	Pixels.SetNumUninitialized(W * W);
	for (int32 Py = 0; Py < W; ++Py)
	{
		for (int32 Px = 0; Px < W; ++Px)
		{
			// Pixel row = -world X, pixel col = +world Y (matches camera).
			const int32 CX = W - 1 - Py;
			const int32 CY = Px;
			const int32 Idx = CY * W + CX;
			FColor C;
			const bool bExplored = bAll || G.Players[Me].Explored[Idx];
			const bool bVisible = bAll || G.Players[Me].Visible[Idx];
			if (!bExplored) { C = FColor(8, 10, 14); }
			else if (G.Map.Land[Idx]) { C = bVisible ? FColor(52, 84, 38) : FColor(30, 46, 24); }
			else { C = bVisible ? FColor(16, 42, 74) : FColor(10, 24, 42); }
			Pixels[Py * W + Px] = C;
		}
	}
	// Resource nodes on explored water.
	for (const FSimResourceNode& N : G.Map.Nodes)
	{
		if (N.Amount <= 0.f) { continue; }
		const int32 Idx = N.Cell.Y * W + N.Cell.X;
		if (!bAll && !G.Players[Me].Explored[Idx]) { continue; }
		const int32 Px = N.Cell.Y;
		const int32 Py = W - 1 - N.Cell.X;
		Pixels[Py * W + Px] = (N.Type == EResourceType::Wood) ? FColor(80, 200, 60) : FColor(220, 140, 40);
	}

	void* Data = MinimapTex->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
	FMemory::Memcpy(Data, Pixels.GetData(), Pixels.Num() * sizeof(FColor));
	MinimapTex->GetPlatformData()->Mips[0].BulkData.Unlock();
	MinimapTex->UpdateResource();
}

void ARTSHUD::DrawMinimap()
{
	AACGameMode* GM = GetWorld()->GetAuthGameMode<AACGameMode>();
	FSimGame& G = *GM->Sim();
	const int32 Me = GM->LocalPlayer();
	const bool bAll = GM->IsSpectating();

	const double Now = GetWorld()->GetTimeSeconds();
	if (!MinimapTex || Now >= NextMinimapRefresh)
	{
		RefreshMinimapTexture();
		NextMinimapRefresh = Now + 0.5;
	}

	DrawRect(FLinearColor(0, 0, 0, 0.9f), MinimapPos.X - 3, MinimapPos.Y - 3, MinimapSize + 6, MinimapSize + 6);
	if (MinimapTex)
	{
		DrawTexture(MinimapTex, MinimapPos.X, MinimapPos.Y, MinimapSize, MinimapSize, 0, 0, 1, 1);
	}

	// Entity dots.
	for (const FEntityId Eid : SimSortedKeys(G.World.Pos))
	{
		if (G.World.Proj.Contains(Eid)) { continue; }
		const FOwnerC* O = G.World.Own.Find(Eid);
		if (!O) { continue; }
		const FVector2f P = G.World.Pos[Eid].P;
		const bool bMine = O->Player == Me;
		if (!bMine && !bAll && !G.IsVisibleTo(Me, P)) { continue; }
		const FVector2D Dot = MinimapPoint(P.X, P.Y);
		const float S = G.World.Struct.Contains(Eid) ? 4.f : 2.5f;
		FLinearColor C = bMine ? GoodGreen : BadRed;
		if (bAll) { C = (O->Player == 0) ? GoodGreen : BadRed; }
		DrawRect(C, Dot.X - S * 0.5f, Dot.Y - S * 0.5f, S, S);
	}

	// Camera view marker.
	if (ARTSPlayerController* PC = Cast<ARTSPlayerController>(GetOwningPlayerController()))
	{
		if (ARTSCameraPawn* Cam = Cast<ARTSCameraPawn>(PC->GetPawn()))
		{
			const FVector2D Focus = Cam->FocusPoint();
			const FVector2D M = MinimapPoint(
				float(Focus.X / AC_CELL), float(Focus.Y / AC_CELL));
			const FLinearColor Frame(1.f, 1.f, 1.f, 0.7f);
			DrawLine(M.X - 12, M.Y - 9, M.X + 12, M.Y - 9, Frame, 1.f);
			DrawLine(M.X + 12, M.Y - 9, M.X + 12, M.Y + 9, Frame, 1.f);
			DrawLine(M.X + 12, M.Y + 9, M.X - 12, M.Y + 9, Frame, 1.f);
			DrawLine(M.X - 12, M.Y + 9, M.X - 12, M.Y - 9, Frame, 1.f);
		}
	}
}

// ---------------------------------------------------------------------------

void ARTSHUD::DrawHealthBars()
{
	AACGameMode* GM = GetWorld()->GetAuthGameMode<AACGameMode>();
	ARTSPlayerController* PC = Cast<ARTSPlayerController>(GetOwningPlayerController());
	for (const auto& Pair : GM->EntityActors())
	{
		const AUnitActor* Actor = Pair.Value;
		if (!Actor || Actor->IsHidden()) { continue; }
		const bool bSelected = PC && PC->Selection.Contains(Pair.Key);
		if (Actor->HpFraction >= 1.f && !bSelected) { continue; }

		const FVector Screen = Canvas->K2_Project(Actor->GetActorLocation() + FVector(0, 0, 420.f));
		if (Screen.Z <= 0.f) { continue; }   // behind camera
		const float BarW = 46.f, BarH = 5.f;
		const float X = Screen.X - BarW * 0.5f, Y = Screen.Y;
		DrawRect(FLinearColor(0, 0, 0, 0.7f), X - 1, Y - 1, BarW + 2, BarH + 2);
		const float Frac = FMath::Clamp(Actor->HpFraction, 0.f, 1.f);
		const FLinearColor C = Frac > 0.6f ? GoodGreen : Frac > 0.3f ? FLinearColor(1.f, 0.85f, 0.2f) : BadRed;
		DrawRect(C, X, Y, BarW * Frac, BarH);
	}
}

// ---------------------------------------------------------------------------

void ARTSHUD::DrawAlertsAndHints()
{
	AACGameMode* GM = GetWorld()->GetAuthGameMode<AACGameMode>();
	ARTSPlayerController* PC = Cast<ARTSPlayerController>(GetOwningPlayerController());
	FSimGame& G = *GM->Sim();
	UFont* Font = GEngine->GetMediumFont();

	// Alerts, newest at the top.
	float Y = 42.f;
	for (const FACAlert& A : GM->Alerts())
	{
		float TW, TH;
		GetTextSize(A.Text, TW, TH, Font);
		DrawText(A.Text, AlertRed, (Canvas->SizeX - TW) * 0.5f, Y, Font);
		Y += 20.f;
	}

	// Mode hints at cursor.
	float MX, MY;
	if (PC && PC->GetMousePosition(MX, MY))
	{
		UFont* Small = GEngine->GetSmallFont();
		if (!PC->PlacementTpl.IsNone())
		{
			DrawRect(PC->bPlacementValid ? FLinearColor(0.2f, 1.f, 0.3f, 0.5f) : FLinearColor(1.f, 0.2f, 0.2f, 0.5f),
				MX - 14.f, MY - 14.f, 28.f, 28.f);
			DrawText(PC->PlacementHint, PC->bPlacementValid ? GoodGreen : BadRed, MX + 20.f, MY - 6.f, Small);
			DrawText(TEXT("Esc to cancel"), TextDim, MX + 20.f, MY + 10.f, Small);
		}
		else if (PC->bAttackMovePending)
		{
			DrawText(TEXT("Attack-move: click target"), AlertRed, MX + 20.f, MY - 6.f, Small);
		}
	}

	// Game over banner.
	if (G.Winner >= 0)
	{
		const int32 Me = GM->LocalPlayer();
		const bool bWon = (G.Winner == Me) && !GM->IsSpectating();
		const FString Big = GM->IsSpectating()
			? FString::Printf(TEXT("%s WINS"), *G.FactionOf(G.Winner).DisplayName)
			: bWon ? TEXT("VICTORY") : TEXT("DEFEAT");
		float TW, TH;
		GetTextSize(Big, TW, TH, Font, 2.5f);
		DrawRect(FLinearColor(0, 0, 0, 0.65f), 0, Canvas->SizeY * 0.5f - 60.f, Canvas->SizeX, 120.f);
		DrawText(Big, bWon || GM->IsSpectating() ? GoodGreen : BadRed,
			(Canvas->SizeX - TW) * 0.5f, Canvas->SizeY * 0.5f - 40.f, Font, 2.5f);
		const FString Sub = TEXT("Press R for a new match");
		GetTextSize(Sub, TW, TH, Font);
		DrawText(Sub, TextMain, (Canvas->SizeX - TW) * 0.5f, Canvas->SizeY * 0.5f + 20.f, Font);
	}
}
