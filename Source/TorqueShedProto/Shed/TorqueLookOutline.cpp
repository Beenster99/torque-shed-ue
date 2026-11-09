#include "Shed/TorqueLookOutline.h"

#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Materials/MaterialInterface.h"
#include "Shed/TorqueShedBike.h"
#include "Shed/TorqueShedItems.h"
#include "Shed/TorqueShedLayout.h"

bool FTorqueLookOutline::Attach(UCameraComponent* Camera)
{
	if (!Camera || bNoMaterial) return false;
	if (AttachedTo.Get() == Camera) return true;
	static UMaterialInterface* Line = nullptr;
	static bool bLooked = false;
	if (!bLooked)
	{
		bLooked = true;
		Line = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Shed/Materials/M_ShedOutline.M_ShedOutline"));
		if (Line) Line->AddToRoot();   // kept across map travel
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("Torque: no M_ShedOutline (run Tools/ue_build_shed.py): no outline round what you look at"));
		}
	}
	if (!Line)
	{
		bNoMaterial = true;
		return false;
	}
	TArray<FWeightedBlendable>& List = Camera->PostProcessSettings.WeightedBlendables.Array;
	if (!List.ContainsByPredicate([](const FWeightedBlendable& W) { return W.Object == Line; }))
		List.Add(FWeightedBlendable(1.0f, Line));
	AttachedTo = Camera;
	UE_LOG(LogTemp, Log, TEXT("Torque: look outline on (M_ShedOutline, stencil %d)"), Stencil);
	return true;
}

void FTorqueLookOutline::Show(const TArray<UPrimitiveComponent*>& Now)
{
	for (const TWeakObjectPtr<UPrimitiveComponent>& W : Shown)
		if (UPrimitiveComponent* C = W.Get())
			if (!Now.Contains(C))
			{
				C->SetRenderCustomDepth(false);
				C->SetCustomDepthStencilValue(0);
			}
	Shown.Reset();
	for (UPrimitiveComponent* C : Now)
	{
		if (!C) continue;
		if (!C->bRenderCustomDepth) C->SetRenderCustomDepth(true);
		if (C->CustomDepthStencilValue != Stencil) C->SetCustomDepthStencilValue(Stencil);
		Shown.Add(C);
	}
}

const TArray<UPrimitiveComponent*>& FTorqueLookOutline::PlaceMeshes(UWorld* World, const FTorquePlace* Place)
{
	if (Place != CachedPlace)
	{
		CachedPlace = Place;
		CachedMeshes.Reset();
		if (World && Place && Place->Kind != ETorquePlace::Bay)
		{
			const FBox In = Place->Box.ExpandBy(10.0);
			for (TActorIterator<AActor> It(World); It; ++It)
			{
				const AActor* A = *It;
				// The bikes and loose parts are outlined as parts; players never.
				if (!A || A->IsHidden() || A->IsA<ATorqueShedBike>() || A->IsA<ATorqueShedItems>() || A->IsA<APawn>()) continue;
				TInlineComponentArray<UStaticMeshComponent*> Meshes(A);
				for (UStaticMeshComponent* C : Meshes)
				{
					if (!C || !C->GetStaticMesh() || !C->IsVisible() || C->bHiddenInGame) continue;
					const FBox Box = C->Bounds.GetBox();
					// Not the walls, floor or roof the place stands against.
					if (Box.GetExtent().GetMax() > 250.0 || !In.Intersect(Box)) continue;
					const double Volume = Box.GetVolume();
					const bool bMostly = Volume > 1.0 && In.Overlap(Box).GetVolume() / Volume >= 0.5;
					if (bMostly || In.IsInsideOrOn(Box.GetCenter())) CachedMeshes.Add(C);
				}
			}
		}
	}
	PlaceOut.Reset();
	for (const TWeakObjectPtr<UPrimitiveComponent>& W : CachedMeshes)
		if (UPrimitiveComponent* C = W.Get()) PlaceOut.Add(C);
	return PlaceOut;
}
