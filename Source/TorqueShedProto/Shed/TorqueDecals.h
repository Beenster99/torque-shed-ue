// Decals on a bike's parts (the decals plan, beenster 17 Nov 2026): the built-in library (Data/Decals,
// made by Tools/decals/make_decals.py), typed lettering in the library's faces, the player's own
// pictures (Saved/Decals/*.png), and drawing a part's decals (Fit/BikeDecal text, kept on the part's
// item) on thin skins cut from the part's own triangles, wearing M_ShedDecal (Tools/ue_build_shed.py).
//
// Used by TorqueShedModel::ApplyLooks (the shed's bikes and the ridden bike) and the decal mode (F3,
// ATorqueDecalMode), which places them.
#pragma once

#include "CoreMinimal.h"
#include "Fit/BikeDecal.h"

class UMaterialInterface;
class UMeshComponent;
class UObject;
class UStaticMesh;
class UStaticMeshComponent;
class UTexture;

namespace TorqueDecals
{
// A library picture (decals.json "decals").
struct FLibDecal
{
	FString Id, Shelf, Name, File;
	TArray<FString> Tags;
	int32 W = 512, H = 512, Colours = 1;
	float Aspect() const { return H > 0 ? (float)W / (float)H : 1.0f; }
};
// A lettering face (decals.json "fonts"; the file under Data/Fonts).
struct FFace
{
	FString Id, File, Name, Style;
	bool bCaps = false;
};

const TArray<FLibDecal>& Library();
const TArray<FFace>& Faces();
const FLibDecal* Find(const FString& Id);
const FFace* Face(const FString& Id);
// The player's own pictures (Saved/Decals/*.png, shrunk to 512 across): their names.
TArray<FString> OwnPictures();
FString OwnFolder();

// The slots that take decals (the plan: tanks, fairings, side panels, tail units, mudguards).
bool TakesDecals(FName Slot);

// The text drawn in face FaceId (in capitals for a face set in them).
FString Shown(const FString& Text, const FString& FaceId);

// A decal's picture: a library picture, lettering (made to measure, kept for the last few), or an own
// picture; null if it can't be. OutAspect: its width over its height. bFull: a full-colour picture
// (an own one) rather than the library's fill / ink / shade.
UTexture* Picture(UObject* WorldContext, const TorqueFit::Decals::Decal& D, float& OutAspect, bool& bFull);
// A library picture by id (its fill / ink / shade channels), and its shelf picture for the decal
// mode (as it looks in red and black, 128 across); an own picture by name.
UTexture* LibraryPicture(const FString& Id);
UTexture* Thumb(const FString& Id);
UTexture* OwnPicture(const FString& Name);

// Own pictures in co-op (stage 5): placing one keeps a copy, shrunk to 512 across, in Saved/Decals/
// Shared named by what's in it ("p" and 8 hex digits); that id goes in the decal, so a friend's machine
// asks the host for it once (ATorquePlayerController's decal picture calls) and keeps it. "" if the
// picture won't load.
FString ShareOwn(const FString& Name);
bool IsSharedId(const FString& Id);
FString SharedFolder();
// A shared picture's PNG bytes (false if this machine hasn't got it).
bool SharedBytes(const FString& Id, TArray<uint8>& Out);
// One come over the network: kept if it's a PNG no bigger than 512 and what its id says it is; then
// what's drawn wearing it is drawn again. False (and nothing kept) otherwise.
bool TakeShared(const FString& Id, const TArray<uint8>& Bytes);
constexpr int32 MaxSharedBytes = 1024 * 1024;

// The frame decals on a slot are kept in (Fit/BikeDecal.h): the bike's (its actor's, unscaled) moved
// to the middle of the box round the slot's meshes. Invalid (identity) with no meshes. bOwnFrame: the
// first mesh's own (unscaled) instead of its actor's: the helmet's, which moves with the head.
FTransform PartFrame(const TArray<UStaticMeshComponent*>& SlotMeshes, bool bOwnFrame = false);
// The slot's triangles in that frame, three points each.
std::vector<TorqueSim::V3> PartTriangles(const TArray<UStaticMeshComponent*>& SlotMeshes, const FTransform& Frame);

// Draws Text's decals (Fit/BikeDecal text, "" for none) on the slot whose meshes are SlotMeshes, in
// place of what was drawn there; nothing is redone when it's the same as before. Also clears away
// decals of meshes that have gone (a bike drawn again).
void Apply(const TArray<UStaticMeshComponent*>& SlotMeshes, const FString& Text, bool bOwnFrame = false);
// Every decal-taking slot of a drawn bike (its meshes tagged Slot:...), each with its item's decals.
void ApplyBike(const TArray<UStaticMeshComponent*>& Parts, TFunctionRef<FString(FName Slot)> DecalsOf);

// --- The helmet (stage 4) ---------------------------------------------------------------------------
// This machine's helmet decals (Saved/Decals/helmet.txt; Fit/BikeDecal text in the helmet's own frame),
// sent to friends with the player (ATorquePlayerState::HelmetDecals).
const FString& MyHelmet();
void SetMyHelmet(const FString& Text);
// Gets a helmet mesh's triangles ready to cut decals from (the cooked mesh keeps a CPU copy: see
// Tools/ue_build_level.py helmet_cpu_access). False for a skeletal helmet or one without.
bool ReadyHelmet(UStaticMeshComponent* Helmet);
// Draws Text's decals on a worn helmet (after its paint; nothing redone when it's the same).
void ApplyHelmet(UMeshComponent* Helmet, const FString& Text);
// The helmet the riders wear (ue_assets.json rider_rig.helmet) and its fit (turned and scaled as on
// the head), for the decal mode's stand; null if it's not a static mesh.
// OutKitPaint: the kit's helmet paint (ue_assets.json rider.helmet) for TorqueHelmetLook::ApplyPaint.
UStaticMesh* HelmetMesh(FTransform& OutFit, UMaterialInterface*& OutKitPaint);

FString ToUe(const std::string& S);
std::string ToStd(const FString& S);
}
