// The drone's photos (beenster, Nov 18 2026: "a pretty decent camera ... like a drone ... zoom, change
// the exposure, have pic filters. Each picture is stored on your PC and available to stick on the wall
// of the shed as a 7inch x 5inch photo").
//
// Where they go: the Windows Pictures folder, under "Torque Shed" (found as Windows finds it, so a
// Pictures folder moved to OneDrive or another drive is the one used), full size JPEGs named by when
// they were taken, with a small copy of each in its "thumbs" folder for the album. Anything else put in
// that folder (a JPEG or PNG) shows in the album too.
//
// Prints: a photo cropped to 7 x 5 (the middle of it), kept as a 512 across PNG beside the decals'
// shared pictures (Saved/Decals/Shared, "p" and 8 hex digits, so co-op friends get it the way they get
// an own decal picture) and a sharp 1400 x 1000 copy for this machine. The print on the wall is a piece
// of shed furniture (photo_print) whose label is that id.
//
// The filters are post-process settings laid over the view the drone sees (and so over the photo).
#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"

class UObject;
class UMaterialInterface;
class UStaticMesh;
class UTexture2D;
struct FPostProcessSettings;

namespace TorquePhotos
{
// The photos' folder (made when it's first written to) and the album's small copies.
FString Folder();
FString ThumbFolder();
// Every photo there (.jpg, .jpeg, .png), newest first: full paths.
TArray<FString> List();
// "18 Nov 2026, 20:45" from a photo's file (its time on disk).
FString When(const FString& Path);

// A frame from the screen (the drone's view, its HUD left off) saved as a JPEG and a small copy, off
// the game thread; Done(the photo's path, "" if it couldn't be written) back on the game thread.
void SaveShot(int32 W, int32 H, TArray<FColor>&& Pixels, TFunction<void(const FString&)> Done);

// A picture file as a texture with its mips (no wider or taller than MaxPx), loaded off the game
// thread: null until it's ready (asked for again each frame it's wanted, which is cheap). A photo with
// no small copy yet has one made. Kept while it's used; the least used go when there are many.
UTexture2D* Image(const FString& Path, int32 MaxPx);
UTexture2D* Thumb(const FString& PhotoPath);
// Whether a file asked for came to nothing (missing or not a picture).
bool Failed(const FString& Path);
// Forgets a file's texture (a photo deleted) and asks again for a missing one later.
void Forget(const FString& Path);
// Deletes a photo and its small copy. False if it couldn't be.
bool Delete(const FString& Path);

// --- Prints --------------------------------------------------------------------------------------------
// A 7 x 5 print of a photo, made off the game thread: Done(its id, "" if it couldn't be) on the game
// thread, the shared PNG and the sharp copy already kept.
void MakePrint(const FString& PhotoPath, TFunction<void(const FString&)> Done);
// The print's picture: the sharp copy when this machine made it, else the shared one; null till it's
// here (a friend's is asked for from the host).
UTexture2D* PrintPicture(UObject* WorldContext, const FString& Id);
// The print's size, cm (7 x 5 inches) and its thickness.
constexpr double PrintW = 17.78, PrintH = 12.70, PrintT = 0.12;
// The print as it hangs: a thin card, its back on the wall at its origin, its face out along +y, the
// picture on its face (material slot 0) and plain paper round its edges and back (slot 1). Made once.
UStaticMesh* PrintMesh();
// The paper (M_ShedDecal, the decals' material, with no picture: white, lit as a satin print), and
// the photo on it.
UMaterialInterface* PaperMaterial();
UMaterialInterface* PrintMaterial(UObject* Outer, UTexture2D* Picture);

// --- Filters -------------------------------------------------------------------------------------------
int32 NumFilters();
FString FilterName(int32 Filter);
// The filter's look laid into Out (only what it changes is overridden).
void ApplyFilter(int32 Filter, FPostProcessSettings& Out);
}
