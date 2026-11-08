#include "Coop/TorqueLook.h"

#include "Coop/TorquePlayerState.h"
#include "Components/StaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "GameFramework/Pawn.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
enum EKit : uint8 { Jacket, Jeans, Skin, Boots, KitCount };

// The rider's kit, made by the level script on the bike's masters (Data/Bike/SpeedTwin/ue_assets.json
// "rider"); flat finishes with Color parameters. Engine's plain shape material where the kit isn't there.
UMaterialInterface* Kit(EKit Which)
{
	static FString Paths[KitCount];
	static bool bRead = false;
	if (!bRead)
	{
		bRead = true;
		FString Text;
		TSharedPtr<FJsonObject> Assets;
		const TSharedPtr<FJsonObject>* Rider = nullptr;
		if (FFileHelper::LoadFileToString(Text, *FPaths::Combine(FPaths::ProjectDir(), TEXT("Data/Bike/SpeedTwin/ue_assets.json")))
			&& FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Assets) && Assets.IsValid()
			&& Assets->TryGetObjectField(TEXT("rider"), Rider))
		{
			const TCHAR* Names[KitCount] = {TEXT("jacket"), TEXT("jeans"), TEXT("skin"), TEXT("boots")};
			for (int32 I = 0; I < KitCount; ++I) (*Rider)->TryGetStringField(Names[I], Paths[I]);
		}
	}
	// (Found at once when already loaded.)
	if (!Paths[Which].IsEmpty())
		if (UMaterialInterface* M = LoadObject<UMaterialInterface>(nullptr, *Paths[Which])) return M;
	return LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
}

// The kit's own colours (linear), for the parts not in the player's colour: the plain material
// needs them, and the kit gets them anyway so a part reads the same whichever is used.
const FLinearColor JeansBlue(0.035f, 0.050f, 0.090f);
const FLinearColor SkinTone(0.45f, 0.30f, 0.24f);
const FLinearColor PeakDark(0.018f, 0.016f, 0.015f);

const FName Dressed(TEXT("TorqueDressed"));
} // namespace

void TorqueLook::Dress(APawn* Pawn)
{
	if (!Pawn || Pawn->IsLocallyControlled() || Pawn->GetNetMode() == NM_DedicatedServer) return;   // you don't see your own
	const ATorquePlayerState* PS = Cast<ATorquePlayerState>(Pawn->GetPlayerState());
	const FColor Want = PS ? PS->ColourValue() : FColor(128, 128, 128);
	const FName Tag(*FString::Printf(TEXT("TorqueLook_%02x%02x%02x"), Want.R, Want.G, Want.B));
	if (Pawn->Tags.Contains(Tag)) return;   // already dressed in this colour
	Pawn->Tags.RemoveAll([](const FName& T) { return T.ToString().StartsWith(TEXT("TorqueLook_")); });
	Pawn->Tags.Add(Tag);

	// A jacket a little darker than the paint chip, so it reads as cloth, not plastic.
	const FLinearColor Own = FLinearColor(Want) * 0.7f;
	TArray<UStaticMeshComponent*> Parts;
	Pawn->GetComponents(Parts);
	for (UStaticMeshComponent* C : Parts)
	{
		const FString Name = C->GetName();
		if (!Name.StartsWith(TEXT("Body_"))) continue;
		EKit Which = Jacket;
		FLinearColor Colour = Own;
		if (Name.StartsWith(TEXT("Body_Leg"))) { Which = Jeans; Colour = JeansBlue; }
		else if (Name == TEXT("Body_Head")) { Which = Skin; Colour = SkinTone; }
		else if (Name == TEXT("Body_CapPeak")) { Which = Boots; Colour = PeakDark; }
		if (!C->ComponentHasTag(Dressed))
		{
			if (UMaterialInterface* M = Kit(Which)) C->SetMaterial(0, M);
			C->ComponentTags.Add(Dressed);
		}
		// The bike's masters (which the kit is made on) blend Color and Color2 by vertex colour.
		if (UMaterialInstanceDynamic* D = C->CreateDynamicMaterialInstance(0))
		{
			D->SetVectorParameterValue(TEXT("Color"), Colour);
			D->SetVectorParameterValue(TEXT("Color2"), Colour);
		}
	}
}
