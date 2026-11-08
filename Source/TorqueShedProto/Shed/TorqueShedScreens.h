// The shed's screens, as Torque Shed's TVs draw them (dashboard.gd, tv_channels.gd): each bay's
// screen on the back wall shows its bike's page, and the crew board over the workbench turns
// between the crew and how to play.
//
// A bay's page is Torque Shed's: the orange header with the bike's name and how it stands (READY TO
// RIDE, 3 PARTS MISSING, 2 PARTS LOOSE, NEEDS FABRICATION), and every part in two columns with a
// condition bar, LOOSE or MISSING. Where Torque Shed's left side lists the bike's numbers (power,
// weight; the shed doesn't work those out yet), this one shows the build: how far along it is,
// fitted and bolted counts, condition, rust and dirt, the fabrication it owes, its paint, the next
// thing to do and which tool does it, and who in the crew is on it right now.
//
// The crew board: everyone in the shed in their colour and what they're doing, each bay's bike and
// how far along it is; then Torque Shed's how-to-play pages, a page every 9 s.
//
// Each page is a canvas drawn into a render target, shown by M_ShedScreenPage (Tools/ue_build_shed.py)
// on a quad just in front of the level's screen glass. It redraws when the shed changes and once a
// second (for the crew). One in the shed; the walker makes it.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TorqueShedScreens.generated.h"

class UCanvas;
class UMaterialInstanceDynamic;
class UStaticMeshComponent;
class UTextureRenderTarget2D;

UCLASS()
class ATorqueShedScreens : public AActor
{
	GENERATED_BODY()

public:
	ATorqueShedScreens();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void Tick(float DeltaSeconds) override;

	// The page sizes: Torque Shed's 1024 x 576, a little wider to fill the shed's screens.
	static constexpr int32 PageW = 1064;
	static constexpr int32 PageH = 576;

private:
	// 0..3 the bays' screens, 4 the crew board.
	UPROPERTY(Transient) TArray<UTextureRenderTarget2D*> Pages;
	UPROPERTY(Transient) TArray<UStaticMeshComponent*> Quads;
	UPROPERTY(Transient) TArray<UMaterialInstanceDynamic*> PageMaterials;
	bool bDirty = true;
	float Clock = 0.0f;
	FDelegateHandle ChangedHandle, LooksHandle, ItemsHandle;
	void Redraw();
	void DrawBay(UCanvas* C, int32 Bay) const;
	void DrawCrew(UCanvas* C) const;
	void DrawHowTo(UCanvas* C, int32 Page) const;
};
