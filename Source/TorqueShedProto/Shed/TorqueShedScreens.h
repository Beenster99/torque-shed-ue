// The shed's screens, as Torque Shed's TVs draw them (dashboard.gd, tv_channels.gd).
//
// [tv] The five big wall TVs (four on the back wall over the bays, one over the workbench) each
// show one channel (UTorqueBuildSubsystem::TvChannel, shared by everyone in the shed; right mouse at a TV
// changes it):
//   Shed cam: a live camera over each player's shoulder (up to four, in a grid), their name in
//     their colour and what they're doing.
//   Rider cam: a live third-person chase camera on another rider out on their bike in the same
//     world (a different one every 20 s); the TV is off while no mate is out riding.
//   Bikes in the shed: a card per bay: the bike, how it stands, the build, condition and muck,
//     its paint and who's on it.
//   This bay's bike (the back wall's TVs): the bay's bike part by part, Torque Shed's dashboard
//     page; blank while the bike's wheeled out of its bay.
// The cameras are scene captures taken 10 times a second, only while a TV showing them is seen.
//
// The parts computer's monitor: Torque Shed's parts computer, the kinds down the side, the parts of
// one with the picked one lit and its description; what the player at it is choosing, or, with
// no one there, a different kind every few seconds, and how many deliveries are waiting.
//
// Each page is a canvas drawn into a render target, shown by M_ShedScreenPage (Tools/ue_build_shed.py)
// on a quad just in front of the TV's glass (the props' bay_screen and tv_136 models, swaps.json).
// One in the shed; the walker makes it.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TorqueShedScreens.generated.h"

class ATorqueBikePawn;
class ATorqueWalker;
class UCanvas;
class USceneCaptureComponent2D;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UStaticMesh;
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
	// 0..3 the back wall's TVs, 4 the TV over the workbench, 5 the parts computer's monitor, 6 the dyno room's screen.
	UPROPERTY(Transient) TArray<UTextureRenderTarget2D*> Pages;
	UPROPERTY(Transient) TArray<UStaticMeshComponent*> Quads;
	UPROPERTY(Transient) TArray<UMaterialInstanceDynamic*> PageMaterials;
	bool bDirty = true;
	float Clock = 0.0f;
	FDelegateHandle ChangedHandle, LooksHandle, ItemsHandle;
	void Redraw();
	void DrawPage(int32 Index, float Now);
	void DrawComputer(UCanvas* C, float Now) const;
	void DrawDyno(UCanvas* C) const;
	bool bDynoWasLive = false;
	void DrawBay(UCanvas* C, int32 Bay) const;

	// [tv] The channels.
	int32 ShownChannel[5] = {-1, -1, -1, -1, -1};   // what each TV's page was last drawn for
	int32 ChannelOf(int32 Tv) const;
	bool IsLive(int32 Tv) const;
	void DrawPlayers(UCanvas* C, const FString& Note) const;
	void DrawRider(UCanvas* C, float Now) const;
	void DrawBikes(UCanvas* C) const;
	// The live cameras: one per player in the shed, and the rider cam.
	USceneCaptureComponent2D* MakeCam(UTextureRenderTarget2D*& OutRT);
	void UpdatePlayerCams(float Dt);
	void UpdateRiderCam(float Dt, bool bCapture);
	bool bRiderShown = false;   // whether the rider cam's TVs were last drawn with a rider out
	UPROPERTY(Transient) TArray<USceneCaptureComponent2D*> PlayerCams;
	UPROPERTY(Transient) TArray<UTextureRenderTarget2D*> PlayerCamRTs;
	TArray<FVector> PlayerCamAt;
	TArray<TWeakObjectPtr<ATorqueWalker>> FilmedWalkers;
	UPROPERTY(Transient) USceneCaptureComponent2D* RiderCam = nullptr;
	UPROPERTY(Transient) UTextureRenderTarget2D* RiderCamRT = nullptr;
	TWeakObjectPtr<ATorqueBikePawn> Filmed;
	FVector RiderCamAt = FVector::ZeroVector;
	FVector RiderFwd = FVector::ZeroVector;
	float RiderSwap = 0.0f;

	// [tv] The TVs bought from the parts computer (pieces of kit, FTorqueFurniture::Channel): a page
	// each, on a quad riding on the piece's mesh wherever it hangs or is carried, the wall's channels.
	UPROPERTY(Transient) UMaterialInterface* PageBase = nullptr;
	UPROPERTY(Transient) TMap<int32, UTextureRenderTarget2D*> PiecePages;
	UPROPERTY(Transient) TMap<int32, UStaticMeshComponent*> PieceQuads;
	UPROPERTY(Transient) TMap<int32, UMaterialInstanceDynamic*> PieceMaterials;
	UPROPERTY(Transient) TMap<FName, UStaticMesh*> PieceQuadMeshes;
	TMap<int32, int32> PieceShown;   // the channel each was last drawn for
	void SyncPieces();
	void DrawPiece(int32 Id, int32 Channel, float Now);
};
