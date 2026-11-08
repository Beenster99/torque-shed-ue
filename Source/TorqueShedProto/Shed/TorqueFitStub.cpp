#include "Shed/TorqueFitStub.h"
#include "Shed/TorqueFitApi.h"

#if TORQUE_FIT_STUB

#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
bool LoadStubJson(const FString& Rel, TSharedPtr<FJsonObject>& Out)
{
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *FPaths::Combine(FPaths::ProjectDir(), Rel))) return false;
	return FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Out) && Out.IsValid();
}

// The parts each slot owns as modelled (speedtwin.json names).
const TArray<FName>& SlotParts(FName Slot)
{
	static const TArray<FName> Exhaust = {TEXT("silencers"), TEXT("silencer_caps")};
	static const TArray<FName> Bars = {TEXT("handlebar"), TEXT("grips"), TEXT("grips_switches"), TEXT("lever_brake"),
		TEXT("lever_clutch"), TEXT("mirrors"), TEXT("mirror_stalks"), TEXT("mirror_glass")};
	static const TArray<FName> None;
	if (Slot == TEXT("exhaust")) return Exhaust;
	if (Slot == TEXT("handlebars")) return Bars;
	return None;
}

// How a stand-in option moves a modelled part (pawn-local cm, x forward, y right, z up).
bool StubDelta(FName Slot, FName PartId, FName ModelPart, FTransform& Out)
{
	if (Slot == TEXT("exhaust") && PartId == TEXT("peashooter_twins"))
	{
		// 30% shorter and a little slimmer, about the silencers' front end (27 cm behind the
		// model's middle), and the end caps brought forward to the new tail (17.6 cm on, 2.4 down).
		if (ModelPart == TEXT("silencers"))
		{
			const FVector S(0.7, 1.0, 0.85), P(-27.0, 0.0, 0.0);
			Out = FTransform(FQuat::Identity, P - S * P, S);
			return true;
		}
		if (ModelPart == TEXT("silencer_caps"))
		{
			Out = FTransform(FVector(17.6, 0.0, -2.4));
			return true;
		}
	}
	if (Slot == TEXT("handlebars") && PartId == TEXT("touring_bend_bar"))
	{
		// The same bar 40 mm higher and 15 mm back, with everything clamped to it.
		Out = FTransform(FVector(-1.5, 0.0, 4.0));
		return true;
	}
	return false;
}
} // namespace

namespace TorqueFit
{
TArray<FName> Slots(FName Bike)
{
	return {TEXT("exhaust"), TEXT("handlebars")};
}

TArray<FTorqueFitOption> Options(FName Bike, FName Slot)
{
	TArray<FTorqueFitOption> Out;
	auto Add = [&Out, Slot](const TCHAR* Id, const TCHAR* Name, bool bStock)
	{
		FTorqueFitOption O;
		O.PartId = Id;
		O.Name = FText::FromString(Name);
		O.CatalogueKind = Slot;
		O.CatalogueId = Id;
		O.bStock = bStock;
		if (!bStock)
		{
			O.ReplacesModelParts = SlotParts(Slot);
			for (const FName& P : SlotParts(Slot))
			{
				FTorqueFitMesh M;
				M.ModelPart = P;
				M.Group = Slot == TEXT("handlebars") ? FName(TEXT("steer")) : FName(TEXT("body"));
				O.Meshes.Add(M);
			}
		}
		Out.Add(O);
	};
	if (Slot == TEXT("exhaust"))
	{
		Add(TEXT("stock"), TEXT("Stock silencers"), true);
		Add(TEXT("peashooter_twins"), TEXT("Peashooter twins (stand-in: short stock silencers)"), false);
	}
	else if (Slot == TEXT("handlebars"))
	{
		Add(TEXT("stock"), TEXT("Stock handlebar"), true);
		Add(TEXT("touring_bend_bar"), TEXT("Touring bend bar (stand-in: stock bar 40 mm up)"), false);
	}
	return Out;
}

bool CanFit(const FTorqueBuild& Build, FName Slot, FName PartId, FText& WhyNot)
{
	for (const FTorqueFitOption& O : Options(Build.Bike, Slot))
		if (O.PartId == PartId) return true;
	WhyNot = FText::FromString(TEXT("That part doesn't fit this bike."));
	return false;
}

bool ResolveModel(const FTorqueBuild& Build, TArray<FTorqueResolvedPart>& Out, TArray<FTorqueFitClash>* Clashes)
{
	Out.Reset();
	TSharedPtr<FJsonObject> Assets, Spec;
	const TSharedPtr<FJsonObject>* PartsObj = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* List = nullptr;
	if (!LoadStubJson(TEXT("Data/Bike/SpeedTwin/ue_assets.json"), Assets) ||
		!LoadStubJson(TEXT("Data/Bike/SpeedTwin/speedtwin.json"), Spec) ||
		!Assets->TryGetObjectField(TEXT("parts"), PartsObj) || !Spec->TryGetArrayField(TEXT("parts"), List))
		return false;
	for (const TSharedPtr<FJsonValue>& V : *List)
	{
		const TSharedPtr<FJsonObject> P = V->AsObject();
		if (!P.IsValid()) continue;
		const FString Name = P->GetStringField(TEXT("name"));
		const TSharedPtr<FJsonObject>* Where = nullptr;
		if (!(*PartsObj)->TryGetObjectField(Name, Where)) continue;
		FTorqueResolvedPart R;
		R.Name = FName(*Name);
		R.Mesh = (*Where)->GetStringField(TEXT("mesh"));
		(*Where)->TryGetStringField(TEXT("material"), R.Material);
		R.Group = FName(*P->GetStringField(TEXT("group")));
		for (const FName& Slot : Slots(Build.Bike))
		{
			if (!SlotParts(Slot).Contains(R.Name)) continue;
			R.Slot = Slot;
			const FName* Fitted = Build.Fitted.Find(Slot);
			FTransform D;
			if (Fitted && StubDelta(Slot, *Fitted, R.Name, D)) R.Delta = D;
		}
		Out.Add(R);
	}
	return Out.Num() > 0;
}
} // namespace TorqueFit

#endif
