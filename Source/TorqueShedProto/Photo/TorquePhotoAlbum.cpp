#include "Photo/TorquePhotoAlbum.h"

#include "Coop/TorqueKeys.h"
#include "Coop/TorquePlayerController.h"
#include "Coop/TorqueUi.h"
#include "Photo/TorqueDrone.h"
#include "Photo/TorquePhotos.h"
#include "Shed/TorqueBuildSubsystem.h"
#include "Shed/TorqueFurniture.h"
#include "Shed/TorqueWalker.h"
#include "SonyPad.h"
#include "CanvasItem.h"
#include "Components/SceneComponent.h"
#include "Engine/Canvas.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "GameFramework/HUD.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "InputCoreTypes.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"

namespace TorquePhotoAlbumImpl
{
TWeakObjectPtr<ATorquePhotoAlbum> GOpen;
uint64 GClosedFrame = 0;
constexpr int32 Cols = 4;

// A picture drawn inside a box, as big as fits without stretching it. Returns where it went.
FBox2D DrawFit(UCanvas* Canvas, UTexture2D* Tex, float X, float Y, float W, float H)
{
	if (!Tex || !Tex->GetResource() || Tex->GetSizeX() <= 0 || Tex->GetSizeY() <= 0) return FBox2D(FVector2D(X, Y), FVector2D(X + W, Y + H));
	const float A = (float)Tex->GetSizeX() / (float)Tex->GetSizeY();
	float DW = W, DH = W / A;
	if (DH > H)
	{
		DH = H;
		DW = H * A;
	}
	const float DX = X + 0.5f * (W - DW), DY = Y + 0.5f * (H - DH);
	FCanvasTileItem Tile(FVector2D(DX, DY), Tex->GetResource(), FVector2D(DW, DH), FLinearColor::White);
	Tile.BlendMode = SE_BLEND_Opaque;
	Canvas->DrawItem(Tile);
	return FBox2D(FVector2D(DX, DY), FVector2D(DX + DW, DY + DH));
}
} // namespace TorquePhotoAlbumImpl

namespace TPAI = TorquePhotoAlbumImpl;

ATorquePhotoAlbum::ATorquePhotoAlbum()
{
	PrimaryActorTick.bCanEverTick = true;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Album")));
	bReplicates = false;
}

ATorquePhotoAlbum* ATorquePhotoAlbum::For(const AController* Controller)
{
	ATorquePhotoAlbum* A = TPAI::GOpen.Get();
	return A && Controller && A->PC.Get() == Controller && !A->IsActorBeingDestroyed() ? A : nullptr;
}

bool ATorquePhotoAlbum::Open(ATorqueWalker* InWalker)
{
	if (GFrameCounter <= TPAI::GClosedFrame + 1) return true;   // (the press that closed it)
	UWorld* Wd = InWalker ? InWalker->GetWorld() : nullptr;
	APlayerController* P = InWalker ? Cast<APlayerController>(InWalker->GetController()) : nullptr;
	if (!Wd || !P || !P->IsLocalController()) return false;
	if (For(P)) return true;
	ATorquePhotoAlbum* A = Wd->SpawnActor<ATorquePhotoAlbum>(FVector::ZeroVector, FRotator::ZeroRotator);
	if (!A) return false;
	A->PC = P;
	A->Walker = InWalker;
	A->Photos = TorquePhotos::List();
	A->OpenFrame = GFrameCounter;
	{
		const FSonyPadState Now = InWalker->PadState();
		A->PrevPad = Now.bConnected ? Now.Buttons : 0;
	}
	P->bShowMouseCursor = true;
	FInputModeGameAndUI Mode;
	Mode.SetHideCursorDuringCapture(false);
	P->SetInputMode(Mode);
	TPAI::GOpen = A;
	UE_LOG(LogTemp, Log, TEXT("Torque photos: the album open, %d photo(s) in %s"), A->Photos.Num(), *TorquePhotos::Folder());
	return true;
}

void ATorquePhotoAlbum::Close()
{
	if (APlayerController* P = PC.Get())
	{
		P->bShowMouseCursor = false;
		P->SetInputMode(FInputModeGameOnly());
	}
	TPAI::GClosedFrame = GFrameCounter;
	PC = nullptr;
	if (TPAI::GOpen.Get() == this) TPAI::GOpen = nullptr;
	Destroy();
}

void ATorquePhotoAlbum::Say(const FString& Text, double Seconds)
{
	Note = Text;
	NoteFor = Seconds;
}

void ATorquePhotoAlbum::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	APlayerController* P = PC.Get();
	if (!P || !Walker.IsValid() || P->GetPawn() != Walker.Get())
	{
		Close();
		return;
	}
	NoteFor = FMath::Max(0.0, NoteFor - DeltaSeconds);
	static const bool bNoInput = FParse::Param(FCommandLine::Get(), TEXT("TorqueNoInput"));
	if (!bNoInput) ReadInput(DeltaSeconds);
}

void ATorquePhotoAlbum::Pick(int32 Index)
{
	if (Photos.Num() == 0) return;
	const int32 Was = Sel;
	Sel = FMath::Clamp(Index, 0, Photos.Num() - 1);
	if (Sel != Was) bConfirmDelete = false;
}

void ATorquePhotoAlbum::ReadInput(float Dt)
{
	APlayerController* P = PC.Get();
	ATorqueWalker* W = Walker.Get();
	if (!P || !W) return;
	const FSonyPadState S = W->PadState();
	const uint32 SonyDown = S.bConnected ? S.Buttons : 0;
	const uint32 Hit = SonyDown & ~PrevPad;
	PrevPad = SonyDown;
	if (GFrameCounter <= OpenFrame) return;
	auto Just = [P](const FKey& K) { return P->WasInputKeyJustPressed(K); };
	if (FTorqueKeys::Just(P, ETorqueKey::PhotoAlbum) || Just(EKeys::Escape) || Just(EKeys::Gamepad_FaceButton_Right) || (Hit & ESonyButton::Circle))
	{
		Close();
		return;
	}
	const int32 N = Photos.Num();
	if (Just(EKeys::Right) || Just(EKeys::D) || Just(EKeys::Gamepad_DPad_Right) || (Hit & ESonyButton::DRight)) Pick(Sel + 1);
	if (Just(EKeys::Left) || Just(EKeys::A) || Just(EKeys::Gamepad_DPad_Left) || (Hit & ESonyButton::DLeft)) Pick(Sel - 1);
	if (Just(EKeys::Down) || Just(EKeys::S) || Just(EKeys::Gamepad_DPad_Down) || (Hit & ESonyButton::DDown) || Just(EKeys::MouseScrollDown)) Pick(Sel + TPAI::Cols);
	if (Just(EKeys::Up) || Just(EKeys::W) || Just(EKeys::Gamepad_DPad_Up) || (Hit & ESonyButton::DUp) || Just(EKeys::MouseScrollUp)) Pick(Sel - TPAI::Cols);
	if (Just(EKeys::PageDown) || Just(EKeys::Gamepad_RightShoulder) || (Hit & ESonyButton::R1)) Pick(Sel + TPAI::Cols * Rows);
	if (Just(EKeys::PageUp) || Just(EKeys::Gamepad_LeftShoulder) || (Hit & ESonyButton::L1)) Pick(Sel - TPAI::Cols * Rows);
	if (Just(EKeys::Home)) Pick(0);
	if (Just(EKeys::End)) Pick(N - 1);
	if (Just(EKeys::Enter) || Just(EKeys::Gamepad_FaceButton_Bottom) || (Hit & ESonyButton::Cross)) Do(-1);
	if (Just(EKeys::Delete) || Just(EKeys::Gamepad_FaceButton_Left) || (Hit & ESonyButton::Square)) Do(-2);
	if (Just(EKeys::O) || Just(EKeys::Gamepad_FaceButton_Top) || (Hit & ESonyButton::Triangle)) Do(-3);
	if (Just(EKeys::LeftMouseButton))
	{
		float Mx = 0.0f, My = 0.0f;
		if (P->GetMousePosition(Mx, My))
			for (const FTorqueAlbumHit& H : Hits)
				if (H.Box.IsInside(FVector2D(Mx, My)))
				{
					Do(H.What);
					break;
				}
	}
}

void ATorquePhotoAlbum::Do(int32 What)
{
	if (What >= 0)
	{
		Pick(What);
		return;
	}
	if (What != -2) bConfirmDelete = false;
	switch (What)
	{
	case -1: Print(); break;
	case -2: DeletePicked(); break;
	case -3: ShowInFolder(); break;
	default: Close(); break;
	}
}

void ATorquePhotoAlbum::Print()
{
	if (bPrinting || !Photos.IsValidIndex(Sel)) return;
	const UTorqueBuildSubsystem* B = UTorqueBuildSubsystem::Of(this);
	if (!B)
	{
		Say(TEXT("Prints go up on the shed's walls: print it in the shed."));
		return;
	}
	if (B->FurnitureMovedBy(B->Me()) != 0)
	{
		Say(TEXT("Put down what you're moving first (F), then print it."));
		return;
	}
	bPrinting = true;
	Say(TEXT("Printing..."), 10.0);
	TWeakObjectPtr<ATorquePhotoAlbum> Self(this);
	TorquePhotos::MakePrint(Photos[Sel], [Self](const FString& Id)
	{
		ATorquePhotoAlbum* A = Self.Get();
		if (!A) return;
		A->bPrinting = false;
		if (Id.IsEmpty()) A->Say(TEXT("That photo wouldn't print."));
		else A->Printed(Id);
	});
}

void ATorquePhotoAlbum::Printed(const FString& Id)
{
	UTorqueBuildSubsystem* B = UTorqueBuildSubsystem::Of(this);
	if (!B) return;
	// A joined player's print goes to the host first (as an own decal picture does), then everyone.
	if (ATorquePlayerController* TPC = Cast<ATorquePlayerController>(PC.Get())) TPC->SendDecalPicture(Id);
	FTorqueShedRequest R;
	R.Kind = ETorqueShedAct::OrderFurniture;
	R.Part = TorqueFurniture::PhotoPrintModel();
	R.Text = Id;
	FString WhyNot;
	if (!B->Request(R, WhyNot))
	{
		Say(WhyNot);
		return;
	}
	UE_LOG(LogTemp, Log, TEXT("Torque photos: printed %s as %s"), Photos.IsValidIndex(Sel) ? *FPaths::GetCleanFilename(Photos[Sel]) : TEXT("?"), *Id);
	Close();   // (it's in your hands: look at a wall and F hangs it)
}

void ATorquePhotoAlbum::DeletePicked()
{
	if (!Photos.IsValidIndex(Sel)) return;
	if (!bConfirmDelete)
	{
		bConfirmDelete = true;
		Say(TEXT("Delete it from your PC? Press Delete (Square) again to delete it."), 4.0);
		return;
	}
	bConfirmDelete = false;
	const FString Gone = Photos[Sel];
	if (!TorquePhotos::Delete(Gone))
	{
		Say(TEXT("It couldn't be deleted (is it open somewhere else?)"));
		return;
	}
	UE_LOG(LogTemp, Log, TEXT("Torque photos: deleted %s"), *Gone);
	Photos = TorquePhotos::List();
	Pick(FMath::Min(Sel, Photos.Num() - 1));
	Say(FString::Printf(TEXT("Deleted %s (prints already on the wall stay up)."), *FPaths::GetCleanFilename(Gone)));
}

void ATorquePhotoAlbum::ShowInFolder()
{
	IFileManager::Get().MakeDirectory(*TorquePhotos::Folder(), true);
	FPlatformProcess::ExploreFolder(Photos.IsValidIndex(Sel) ? *Photos[Sel] : *TorquePhotos::Folder());
	Say(TEXT("Opened in its folder (on the desktop)."));
}

void ATorquePhotoAlbum::DrawHud(AHUD* Hud, UCanvas* Canvas)
{
	if (!Canvas) return;
	const float S = TorqueUi::Scale(Canvas);
	const float CW = Canvas->ClipX, CH = Canvas->ClipY;
	Hits.Reset();
	TorqueUi::Backdrop(Canvas, 0.7f);
	const float X0 = 60.0f * S, Y0 = 60.0f * S, W = CW - 120.0f * S, H = CH - 120.0f * S;
	TorqueUi::Rect(Canvas, X0, Y0, W, H, TorqueUi::Panel);
	TorqueUi::Rect(Canvas, X0, Y0, 3.0f * S, H, TorqueUi::Accent);
	TorqueUi::Text(Canvas, TEXT("Photo album"), X0 + 30.0f * S, Y0 + 22.0f * S, 30.0f * S, TorqueUi::Ink, true);
	TorqueUi::Text(Canvas, FString::Printf(TEXT("%d photo%s in Pictures > Torque Shed. Print one at 7 x 5 and hang it on the shed wall."), Photos.Num(),
		Photos.Num() == 1 ? TEXT("") : TEXT("s")), X0 + 30.0f * S, Y0 + 62.0f * S, 17.0f * S, TorqueUi::Dim);
	// Along the bottom: the keys.
	{
		float KX = X0 + 30.0f * S;
		const float KY = Y0 + H - 46.0f * S;
		KX += TorqueUi::KeyHint(Canvas, TEXT("Enter / Cross"), TEXT("print 7 x 5"), KX, KY, S) + 24.0f * S;
		KX += TorqueUi::KeyHint(Canvas, TEXT("Del / Square"), TEXT("delete"), KX, KY, S) + 24.0f * S;
		KX += TorqueUi::KeyHint(Canvas, TEXT("O / Triangle"), TEXT("show in folder"), KX, KY, S) + 24.0f * S;
		KX += TorqueUi::KeyHint(Canvas, TEXT("Arrows / D-pad"), TEXT("pick"), KX, KY, S) + 24.0f * S;
		TorqueUi::KeyHint(Canvas, TEXT("Esc / Circle"), TEXT("close"), KX, KY, S);
	}
	if (Photos.Num() == 0)
	{
		TorqueUi::TextWrap(Canvas, FString::Printf(TEXT("No photos yet. Send the drone up (%s, or the View button on a pad) and take some: Cross or the left mouse."),
			*FTorqueKeys::Text(ETorqueKey::Drone)), X0 + 30.0f * S, Y0 + 130.0f * S, 22.0f * S, W - 60.0f * S, TorqueUi::Ink);
		return;
	}
	// The grid on the left: four across, as many rows as fit.
	const float Gap = 14.0f * S;
	const float GX = X0 + 30.0f * S, GY = Y0 + 104.0f * S, GW = W * 0.55f, GH = H - 104.0f * S - 80.0f * S;
	const float TW = (GW - Gap * (TPAI::Cols - 1)) / TPAI::Cols, TH = TW * 9.0f / 16.0f;
	Rows = FMath::Max(1, (int32)((GH + Gap) / (TH + Gap)));
	const int32 SelRow = Sel / TPAI::Cols;
	if (SelRow < Top) Top = SelRow;
	if (SelRow >= Top + Rows) Top = SelRow - Rows + 1;
	const int32 TotalRows = (Photos.Num() + TPAI::Cols - 1) / TPAI::Cols;
	Top = FMath::Clamp(Top, 0, FMath::Max(0, TotalRows - Rows));
	for (int32 I = Top * TPAI::Cols; I < FMath::Min(Photos.Num(), (Top + Rows) * TPAI::Cols); ++I)
	{
		const int32 Row = I / TPAI::Cols - Top, Col = I % TPAI::Cols;
		const float TX = GX + Col * (TW + Gap), TY = GY + Row * (TH + Gap);
		TorqueUi::Rect(Canvas, TX, TY, TW, TH, FLinearColor(0.0f, 0.0f, 0.0f, 0.5f));
		if (UTexture2D* Thumb = TorquePhotos::Thumb(Photos[I])) TPAI::DrawFit(Canvas, Thumb, TX, TY, TW, TH);
		else TorqueUi::Text(Canvas, TorquePhotos::Failed(TorquePhotos::ThumbFolder() / FPaths::GetCleanFilename(Photos[I])) ? TEXT("?") : TEXT("..."),
			TX + TW * 0.5f, TY + TH * 0.5f - 10.0f * S, 18.0f * S, TorqueUi::Faint, false, TorqueUi::EAlign::Centre);
		if (I == Sel) TorqueUi::Frame(Canvas, TX - 3.0f * S, TY - 3.0f * S, TW + 6.0f * S, TH + 6.0f * S, TorqueUi::Accent, 3.0f * S);
		Hits.Add(FTorqueAlbumHit{FBox2D(FVector2D(TX, TY), FVector2D(TX + TW, TY + TH)), I});
	}
	if (TotalRows > Rows)
		TorqueUi::Text(Canvas, FString::Printf(TEXT("Rows %d - %d of %d (the wheel, the arrows, L1 / R1 a page)"), Top + 1, FMath::Min(TotalRows, Top + Rows), TotalRows),
			GX, GY + Rows * (TH + Gap), 15.0f * S, TorqueUi::Dim);

	// The one picked, big, with the 7 x 5 it prints marked on it.
	if (!Photos.IsValidIndex(Sel)) return;
	const FString& Path = Photos[Sel];
	const float RX = GX + GW + 40.0f * S, RW = X0 + W - 30.0f * S - RX, RY = GY;
	const float PH = FMath::Min(RW * 9.0f / 16.0f, H * 0.5f);
	TorqueUi::Rect(Canvas, RX, RY, RW, PH, FLinearColor(0.0f, 0.0f, 0.0f, 0.5f));
	UTexture2D* Big = TorquePhotos::Image(Path, 1600);
	if (!Big) Big = TorquePhotos::Thumb(Path);   // (the small one while the big one loads)
	if (Big)
	{
		const FBox2D Drawn = TPAI::DrawFit(Canvas, Big, RX, RY, RW, PH);
		// What a 7 x 5 print keeps: its middle, the rest shaded.
		const float DW = (float)Drawn.GetSize().X, DH = (float)Drawn.GetSize().Y, DX = (float)Drawn.Min.X, DY = (float)Drawn.Min.Y;
		constexpr float Aspect = (float)(TorquePhotos::PrintW / TorquePhotos::PrintH);
		const FLinearColor Shade(0.0f, 0.0f, 0.0f, 0.5f);
		if (DW / FMath::Max(DH, 1.0f) > Aspect)
		{
			const float KW = DH * Aspect, Side = 0.5f * (DW - KW);
			TorqueUi::Rect(Canvas, DX, DY, Side, DH, Shade);
			TorqueUi::Rect(Canvas, DX + DW - Side, DY, Side, DH, Shade);
			TorqueUi::Frame(Canvas, DX + Side, DY, KW, DH, FLinearColor(1.0f, 1.0f, 1.0f, 0.7f), FMath::Max(1.0f, S));
		}
		else
		{
			const float KH = DW / Aspect, Band = 0.5f * (DH - KH);
			TorqueUi::Rect(Canvas, DX, DY, DW, Band, Shade);
			TorqueUi::Rect(Canvas, DX, DY + DH - Band, DW, Band, Shade);
			TorqueUi::Frame(Canvas, DX, DY + Band, DW, KH, FLinearColor(1.0f, 1.0f, 1.0f, 0.7f), FMath::Max(1.0f, S));
		}
	}
	float Y = RY + PH + 14.0f * S;
	TorqueUi::TextFit(Canvas, FPaths::GetBaseFilename(Path), RX, Y, 20.0f * S, RW, TorqueUi::Ink, true);
	Y += 30.0f * S;
	TorqueUi::Text(Canvas, FString::Printf(TEXT("Taken %s   (the lit box is what a 7 x 5 print keeps)"), *TorquePhotos::When(Path)), RX, Y, 16.0f * S, TorqueUi::Dim);
	Y += 40.0f * S;
	// The buttons.
	struct FButton
	{
		const TCHAR* Label;
		int32 What;
		bool bMain;
	};
	const FButton Buttons[] = {{bPrinting ? TEXT("Printing...") : TEXT("Print 7 x 5"), -1, true}, {bConfirmDelete ? TEXT("Delete it?") : TEXT("Delete"), -2, false},
		{TEXT("Show in folder"), -3, false}, {TEXT("Close"), -4, false}};
	float BX = RX;
	float BY = Y;
	for (const FButton& Btn : Buttons)
	{
		const float Px = 18.0f * S;
		const float BW = (float)TorqueUi::Measure(Btn.Label, Px, true).X + 36.0f * S, BH = 44.0f * S;
		if (BX + BW > RX + RW)
		{
			BX = RX;
			BY += BH + 12.0f * S;
		}
		const bool bWarn = Btn.What == -2 && bConfirmDelete;
		TorqueUi::Rect(Canvas, BX, BY, BW, BH, Btn.bMain ? TorqueUi::Accent : (bWarn ? TorqueUi::Warn : FLinearColor(1.0f, 1.0f, 1.0f, 0.08f)));
		TorqueUi::Text(Canvas, Btn.Label, BX + BW * 0.5f, BY + 0.5f * (BH - Px * 1.2f), Px, Btn.bMain || bWarn ? FLinearColor(0.06f, 0.05f, 0.04f, 1.0f) : TorqueUi::Ink, true,
			TorqueUi::EAlign::Centre, false);
		Hits.Add(FTorqueAlbumHit{FBox2D(FVector2D(BX, BY), FVector2D(BX + BW, BY + BH)), Btn.What});
		BX += BW + 12.0f * S;
	}
	Y = BY + 60.0f * S;
	if (NoteFor > 0.0 && !Note.IsEmpty()) TorqueUi::TextWrap(Canvas, Note, RX, Y, 18.0f * S, RW, bConfirmDelete ? TorqueUi::Warn : TorqueUi::Accent);
}

// --- Tests -----------------------------------------------------------------------------------------------------

FString ATorquePhotoAlbum::TestStatus() const
{
	return FString::Printf(TEXT("album: %d photo(s) in %s, %s picked%s"), Photos.Num(), *TorquePhotos::Folder(),
		Photos.IsValidIndex(Sel) ? *FPaths::GetCleanFilename(Photos[Sel]) : TEXT("none"), bPrinting ? TEXT(", printing") : TEXT(""));
}

FString ATorquePhotoAlbum::TestPrint(int32 Index)
{
	if (!Photos.IsValidIndex(Index)) return FString::Printf(TEXT("album: no photo %d (there are %d)"), Index, Photos.Num());
	Pick(Index);
	Print();
	return FString::Printf(TEXT("album: printing %s"), *FPaths::GetCleanFilename(Photos[Index]));
}
