// The photo album in the shed (beenster, Nov 18 2026: each picture "available to stick on the wall of
// the shed as a 7inch x 5inch photo"): Y (the D-pad's down) on foot in the shed opens it over the view.
// Your drone photos (TorquePhotos::Folder), newest first, four across; the one picked shown big with
// the 7 x 5 it prints marked on it.
//
// Enter (Cross) prints it: it comes off the printer into your hands, to hang on a wall with F as any
// wall piece is (TorqueFurniture photo_print). Delete (Square), twice, deletes the photo from your PC;
// O (Triangle) shows it in its folder; Esc, Y (Circle) closes the album. The arrows (D-pad) and the
// wheel go through them, and the mouse picks one and presses the buttons.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TorquePhotoAlbum.generated.h"

class AHUD;
class APlayerController;
class ATorqueWalker;
class UCanvas;

// Where something was drawn on the album's page, for the mouse.
struct FTorqueAlbumHit
{
	FBox2D Box;
	int32 What = 0;   // >= 0: a photo; -1 print, -2 delete, -3 folder, -4 close
};

UCLASS()
class ATorquePhotoAlbum : public AActor
{
	GENERATED_BODY()

public:
	ATorquePhotoAlbum();
	virtual void Tick(float DeltaSeconds) override;

	static bool Open(ATorqueWalker* Walker);
	static ATorquePhotoAlbum* For(const AController* Controller);
	void Close();
	void DrawHud(AHUD* Hud, UCanvas* Canvas);

	// --- For hands-off tests (UTorqueShedTest album:...) ---------------------------------------------------
	FString TestStatus() const;
	FString TestPrint(int32 Index);   // prints the Index'th (newest first) as Enter would

private:
	TWeakObjectPtr<APlayerController> PC;
	TWeakObjectPtr<ATorqueWalker> Walker;
	TArray<FString> Photos;
	int32 Sel = 0, Top = 0;          // the one picked, the first row shown
	int32 Rows = 3;                  // rows that fit (as last drawn)
	bool bConfirmDelete = false, bPrinting = false;
	FString Note;
	double NoteFor = 0.0;
	uint32 PrevPad = 0;
	uint64 OpenFrame = 0;
	TArray<FTorqueAlbumHit> Hits;   // where things were drawn, for the mouse

	void ReadInput(float Dt);
	void Pick(int32 Index);
	void Print();
	void Printed(const FString& Id);
	void DeletePicked();
	void ShowInFolder();
	void Do(int32 What);
	void Say(const FString& Text, double Seconds = 3.0);
};
