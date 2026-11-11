#include "TorqueLights.h"

#include "Fit/BikeLayout.h"
#include "Fit/FitJson.h"
#include "Shed/TorqueShedCatalogue.h"

#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace TorqueLightsImpl
{
// bike_lamps.gd: a flash a second and a half (real ones are 1 to 2 Hz); the stop lamp from a twentieth
// of either lever; the turn begun leant 12 degrees (or turned 35 slowly round a junction) that way, and
// done back under 5 degrees and barely turning.
constexpr double FlashHz = 1.5;
constexpr double BrakeOn = 0.05;
constexpr double TurnLean = 0.21;
constexpr double TurnYaw = 0.6;
constexpr double UprightLean = 0.09;
constexpr double StraightRate = 0.2;

// The beams (bike_effects.gd): reach in metres, a base plus so much per lumen; the cone's half-angle;
// how far below level it's aimed. Dipped is aimed well down so its hot spot lands on the road a short
// way ahead; main beam is level and reaches to the horizon. The flood is the dipped beam's wide, dim
// fill onto the near road and the verges.
constexpr double LowReach[2] = {58.0, 1.0 / 80.0};
constexpr double HighReach[2] = {150.0, 1.0 / 15.0};
constexpr double LowAim = 5.0, HighAim = 0.6;
// (Main beam was 20 degrees: too narrow, beenster after build 135. Now nearly as wide as dipped, with a
// broad bright middle, so the verges and the bends are lit too, not just a tunnel up the road.)
constexpr float LowCone = 32.0f, HighCone = 30.0f;
constexpr double FloodReach = 16.0;
constexpr float FloodCone = 58.0f, FloodShare = 0.18f;
constexpr double FogReach[2] = {22.0, 1.0 / 40.0};
constexpr double FogAim = 3.5;
// A real lamp's hot spot: about 16 candela a lumen dipped and 40 on main beam (a 1000 lm halogen's
// dipped beam peaks near 16 000 cd), the fog lamps' wide flat spread about 5.
constexpr double LowCdPerLm = 16.0, HighCdPerLm = 40.0, FogCdPerLm = 5.0;
// No headlight part but a lens drawn (a fairing's own lamps): as a plain halogen.
constexpr double DefaultLumens = 1000.0;

// The lenses' glow (the lens finish's Emissive, which is its colour times this), by day; at night a
// fifth of it, so a lit lens reads as a lamp, not a white blot.
constexpr float HeadGlow = 300.0f, MainGlow = 600.0f, FogGlow = 300.0f;
constexpr float TailGlow = 40.0f, StopGlowLens = 400.0f, IndicatorGlowLens = 500.0f;
constexpr float NightGlow = 0.2f;
// The indicators and the rear light are small lamps seen up close: at night (the eye opened right up)
// they glow at a twentieth of their daytime strength, or they bloom into a big haze round the bike
// (beenster, build 132 at night: "a huge diffuse light ... too much").
constexpr float SignalNightGlow = 0.05f;
// ...but the brake light must stand out (build 135: "brakes should be brighter"), and the indicators
// read clearly from behind: their own night levels, between build 132's haze and build 134's.
constexpr float StopNightGlow = 0.2f, IndicatorNightGlow = 0.09f, TailNightGlow = 0.12f;
// The little lamps: an indicator's amber glow round it, the stop lamp's red on the road behind (bike_model.gd
// STOP_GLOW_RANGE 3 m). Candela.
// (Build 132's 40 cd over 2.6 m and 60 cd over 3.2 m lit the whole road round the bike at night: a
// real lamp's spill is a faint tint on the ground just under it.)
constexpr float IndicatorCd = 2.0f, IndicatorRange = 70.0f;
constexpr float StopCd = 12.0f, StopRange = 200.0f;

const FName LensWhite(TEXT("Surf:lens_white")), LensAmber(TEXT("Surf:lens_amber")), LensRed(TEXT("Surf:lens_red"));
const FName DashSlot(TEXT("Slot:dashboard")), HeadSlot(TEXT("Slot:headlight")), FairingSlot(TEXT("Slot:fairing")), FogSlot(TEXT("Slot:fog_lights"));

std::string Std(const FString& S) { return std::string(TCHAR_TO_UTF8(*S)); }

FLinearColor Colour(const TorqueFit::Json* J, const FLinearColor& Else)
{
	if (!J || !J->IsArray() || J->A.size() < 3) return Else;
	return FLinearColor((float)J->A[0].N, (float)J->A[1].N, (float)J->A[2].N);
}

FBox BoxOf(const UStaticMeshComponent* C)
{
	// The mesh's own space is the bike's (MakeBike builds each surface in place; its relative transform
	// on its group undoes the group's rest), so this is where it sits on the bike standing still.
	const UStaticMesh* M = C ? C->GetStaticMesh() : nullptr;
	return M ? M->GetBoundingBox() : FBox(ForceInit);
}

USpotLightComponent* MakeSpot(AActor* Owner, USceneComponent* Parent, const FVector& At, double PitchDown, const FLinearColor& Colour, bool bShadows)
{
	USpotLightComponent* L = NewObject<USpotLightComponent>(Owner);
	L->SetMobility(EComponentMobility::Movable);
	L->SetupAttachment(Parent);
	L->SetRelativeLocationAndRotation(At, FRotator(-PitchDown, 0.0, 0.0));
	L->SetIntensityUnits(ELightUnits::Candelas);
	L->SetLightColor(Colour);
	L->SetCastShadows(bShadows);
	L->SetSourceRadius(3.0f);
	L->SetSoftSourceRadius(3.0f);
	// A lamp lighting the air right at its own lens showed as a white ball round the bike (bike_effects.gd).
	L->SetVolumetricScatteringIntensity(0.0f);
	L->SetVisibility(false);
	L->RegisterComponent();
	return L;
}
} // namespace TorqueLightsImpl

namespace TLI = TorqueLightsImpl;

void FTorqueLights::Setup(const TMap<FString, FString>& Parts)
{
	HeadLumens = FogLumens = 0.0;
	const TorqueFit::BikeLayout* Lay = TorqueShedCatalogue::Layout();
	if (!Lay) return;
	auto PartOf = [&Parts, Lay](const TCHAR* Slot) -> const TorqueFit::Json* {
		const FString* Id = Parts.Find(Slot);
		return Id && !Id->IsEmpty() && *Id != TEXT("none") ? Lay->Part(TLI::Std(*Id)) : nullptr;
	};
	// bike_build.gd: headlight_lumens and fog_lumens, fog_color from the fog lights' "beam".
	if (const TorqueFit::Json* Head = PartOf(TEXT("headlight"))) HeadLumens = Head->Num("lumens", TLI::DefaultLumens);
	if (const TorqueFit::Json* Fog = PartOf(TEXT("fog_lights")))
	{
		FogLumens = Fog->Num("lumens", 900.0);
		FogColour = TLI::Colour(Fog->Get("beam"), FogColour);
	}
	// bike_effects.gd: warm halogen, or the cool white of a bright LED lamp.
	BeamColour = HeadLumens < 2000.0 ? FLinearColor(1.0f, 0.92f, 0.78f) : FLinearColor(0.92f, 0.96f, 1.0f);
}

void FTorqueLights::Hook(AActor* Owner, const TArray<UStaticMeshComponent*>& Meshes, bool bShadows)
{
	for (TWeakObjectPtr<USpotLightComponent>* L : {&Beam, &Flood, &StopGlow})
		if (USpotLightComponent* Old = L->Get()) Old->DestroyComponent();
	for (TWeakObjectPtr<USpotLightComponent>& L : FogBeams)
		if (L.IsValid()) L->DestroyComponent();
	for (TWeakObjectPtr<UPointLightComponent>& L : IndicatorGlow)
		if (L.IsValid()) L->DestroyComponent();
	FogBeams.Reset();
	IndicatorGlow.Reset();
	HeadLenses.Reset();
	FogLenses.Reset();
	TailLenses.Reset();
	Indicators.Reset();
	IndicatorSide.Reset();
	Shown = -1;
	if (!Owner) return;
	// The lenses: white ones on the headlight or a fairing's lamps, and on the fog lights; amber the
	// indicators; red the rear light (and a top box's or pannier's reflector strip). The clocks' shift
	// lights and needles use the same lens finishes but are the dash's (FTorqueDash).
	for (UStaticMeshComponent* C : Meshes)
	{
		if (!C || !C->GetStaticMesh() || C->ComponentHasTag(TLI::DashSlot)) continue;
		if (C->ComponentHasTag(TLI::LensWhite))
		{
			if (C->ComponentHasTag(TLI::FogSlot)) FogLenses.Add({C});
			else if (C->ComponentHasTag(TLI::HeadSlot) || C->ComponentHasTag(TLI::FairingSlot)) HeadLenses.Add({C});
		}
		else if (C->ComponentHasTag(TLI::LensAmber))
		{
			Indicators.Add({C});
			IndicatorSide.Add(TLI::BoxOf(C).GetCenter().Y < 0.0 ? -1 : 1);   // (+y is the bike's right)
		}
		else if (C->ComponentHasTag(TLI::LensRed)) TailLenses.Add({C});
	}
	if (HeadLumens <= 0.0 && HeadLenses.Num() > 0) HeadLumens = TLI::DefaultLumens;
	// The headlight's beam from just ahead of the lens (from inside it, the lamp's own shell would
	// shadow it), on whichever part the biggest lens rides on: the bars' lamp turns with them, a
	// frame-mounted fairing's doesn't. Twin or triple lamps share one beam from between them.
	if (HeadLenses.Num() > 0 && HeadLumens > 0.0)
	{
		UStaticMeshComponent* Main = nullptr;
		double Biggest = -1.0;
		for (const FLens& L : HeadLenses)
		{
			const double V = TLI::BoxOf(L.Mesh.Get()).GetVolume() + TLI::BoxOf(L.Mesh.Get()).GetExtent().SizeSquared();
			if (V > Biggest) { Biggest = V; Main = L.Mesh.Get(); }
		}
		FBox Box(ForceInit);
		for (const FLens& L : HeadLenses)
			if (L.Mesh->GetAttachParent() == Main->GetAttachParent()) Box += TLI::BoxOf(L.Mesh.Get());
		const FVector At(Box.Max.X + 3.0, Box.GetCenter().Y, Box.GetCenter().Z);
		Beam = TLI::MakeSpot(Owner, Main, At, TLI::LowAim, BeamColour, bShadows);
		Flood = TLI::MakeSpot(Owner, Main, At, TLI::LowAim + 4.0, BeamColour, false);
		if (USpotLightComponent* F = Flood.Get())
		{
			F->SetOuterConeAngle(TLI::FloodCone);
			F->SetInnerConeAngle(TLI::FloodCone * 0.3f);
			F->SetAttenuationRadius((float)(TLI::FloodReach * 100.0));
			F->SetIntensity((float)(HeadLumens * TLI::LowCdPerLm * TLI::FloodShare));
			F->SetSpecularScale(0.2f);
			F->SetVolumetricScatteringIntensity(0.3f);   // the faint haze a lamp makes in fog and rain
		}
	}
	// A fog beam from each fog lamp: low, very wide and flat, lighting the road's edges and the verges.
	if (FogLumens > 0.0)
		for (const FLens& L : FogLenses)
		{
			const FBox Box = TLI::BoxOf(L.Mesh.Get());
			USpotLightComponent* F = TLI::MakeSpot(Owner, L.Mesh.Get(), FVector(Box.Max.X + 2.0, Box.GetCenter().Y, Box.GetCenter().Z), TLI::FogAim, FogColour, false);
			const float Cone = FogLenses.Num() > 1 ? 55.0f : 48.0f;
			F->SetOuterConeAngle(Cone);
			F->SetInnerConeAngle(Cone * 0.35f);
			F->SetAttenuationRadius((float)((TLI::FogReach[0] + FogLumens * TLI::FogReach[1]) * 100.0));
			F->SetIntensity((float)(FogLumens * TLI::FogCdPerLm / FMath::Max(FogLenses.Num(), 1)));
			FogBeams.Add(F);
		}
	// Each indicator's own amber glow, just outside its lens.
	for (int32 I = 0; I < Indicators.Num(); ++I)
	{
		const FBox Box = TLI::BoxOf(Indicators[I].Mesh.Get());
		UPointLightComponent* P = NewObject<UPointLightComponent>(Owner);
		P->SetMobility(EComponentMobility::Movable);
		P->SetupAttachment(Indicators[I].Mesh.Get());
		P->SetRelativeLocation(Box.GetCenter() + FVector(0.0, IndicatorSide[I] * (Box.GetExtent().Y + 3.0), 0.0));
		P->SetIntensityUnits(ELightUnits::Candelas);
		P->SetIntensity(TLI::IndicatorCd);
		P->SetAttenuationRadius(TLI::IndicatorRange);
		P->SetLightColor(FLinearColor(1.0f, 0.45f, 0.04f));
		P->SetCastShadows(false);
		P->SetSourceRadius(2.0f);
		P->SetVolumetricScatteringIntensity(0.0f);
		P->SetVisibility(false);
		P->RegisterComponent();
		IndicatorGlow.Add(P);
	}
	// The stop lamp's red on the road behind, back and down from the rear light (the lens furthest back).
	UStaticMeshComponent* Rear = nullptr;
	for (const FLens& L : TailLenses)
		if (!Rear || TLI::BoxOf(L.Mesh.Get()).Min.X < TLI::BoxOf(Rear).Min.X) Rear = L.Mesh.Get();
	if (Rear)
	{
		const FBox Box = TLI::BoxOf(Rear);
		USpotLightComponent* S = TLI::MakeSpot(Owner, Rear, FVector(Box.Min.X - 3.0, Box.GetCenter().Y, Box.GetCenter().Z), 0.0, FLinearColor(1.0f, 0.08f, 0.04f), false);
		S->SetRelativeRotation(FRotationMatrix::MakeFromX(FVector(-1.0, 0.0, -0.6)).Rotator());
		S->SetOuterConeAngle(70.0f);
		S->SetInnerConeAngle(30.0f);
		S->SetAttenuationRadius(TLI::StopRange);
		S->SetIntensity(TLI::StopCd);
		StopGlow = S;
	}
	UE_LOG(LogTemp, Log, TEXT("Torque: the bike's lamps: headlight %s (%.0f lm, %d lens surfaces), fog lights %s (%.0f lm, %d), %d indicators (%d left), %d rear light surfaces"),
		Beam.IsValid() ? TEXT("lit") : TEXT("none"), HeadLumens, HeadLenses.Num(), FogBeams.Num() > 0 ? TEXT("lit") : TEXT("none"), FogLumens, FogLenses.Num(),
		Indicators.Num(), IndicatorSide.FilterByPredicate([](int8 S) { return S < 0; }).Num(), TailLenses.Num());
}

FString FTorqueLights::ToggleHeadlight(bool& bOk)
{
	bOk = HasHeadlight();
	if (!bOk) return TEXT("No headlight on this bike");
	bHeadlight = !bHeadlight;
	if (!bIgnition) return bHeadlight ? TEXT("Headlight on (with the engine running)") : TEXT("Headlight off");
	return bHeadlight ? (bMainBeam ? TEXT("Headlight on: main beam") : TEXT("Headlight on: dipped beam")) : TEXT("Headlight off");
}

FString FTorqueLights::ToggleMainBeam(bool& bOk)
{
	bOk = HasHeadlight();
	if (!bOk) return TEXT("No headlight on this bike");
	if (!bHeadlight)
	{
		bHeadlight = true;
		bMainBeam = true;
	}
	else bMainBeam = !bMainBeam;
	return bMainBeam ? TEXT("Main beam") : TEXT("Dipped beam");
}

FString FTorqueLights::ToggleFog(bool& bOk)
{
	bOk = HasFog();
	if (!bOk) return TEXT("No fog lights on this bike (fit some in the shed)");
	bFog = !bFog;
	if (bFog && !bHeadlight)
	{
		bHeadlight = true;   // fog lamps only with the headlight on, as on the road
		return TEXT("Fog lights on (and the headlight)");
	}
	return bFog ? TEXT("Fog lights on") : TEXT("Fog lights off");
}

FString FTorqueLights::CycleBeam(bool& bOk)
{
	bOk = HasHeadlight();
	if (!bOk) return TEXT("No headlight on this bike");
	if (!bHeadlight)
	{
		bHeadlight = true;
		bMainBeam = bFog = false;
		return TEXT("Lights: dipped beam");
	}
	if (!bMainBeam && !bFog)
	{
		bMainBeam = true;
		return TEXT("Lights: main beam");
	}
	if (bMainBeam && HasFog())
	{
		// Fog lamps go with the dipped beam (main beam in fog dazzles back off it).
		bMainBeam = false;
		bFog = true;
		return TEXT("Lights: fog lights and dipped beam");
	}
	bHeadlight = bMainBeam = bFog = false;
	return TEXT("Lights: off");
}

FString FTorqueLights::Indicate(int32 Which, bool& bOk)
{
	bOk = HasIndicators() && bIgnition;
	if (!HasIndicators()) return TEXT("No indicators on this bike");
	if (!bIgnition) return TEXT("The indicators need the engine running");
	// The same side again cancels it, the other side switches over; either one ends the hazards.
	if (bHazard)
	{
		bHazard = false;
		SetSide(Which);
	}
	else SetSide(Side == Which ? 0 : Which);
	return Side < 0 ? TEXT("Indicating left") : Side > 0 ? TEXT("Indicating right") : TEXT("Indicators off");
}

FString FTorqueLights::ToggleHazard(bool& bOk)
{
	bOk = HasIndicators() && bIgnition;
	if (!HasIndicators()) return TEXT("No indicators on this bike");
	if (!bIgnition) return TEXT("The hazard lights need the engine running");
	bHazard = !bHazard;
	SetSide(0);
	FlashT = 0.0;
	return bHazard ? TEXT("Hazard lights on") : TEXT("Hazard lights off");
}

void FTorqueLights::SetSide(int32 Which)
{
	Which = FMath::Sign(Which);
	if (Which == Side) return;
	Side = Which;
	FlashT = 0.0;
	bTurning = false;
	Turned = 0.0;
}

bool FTorqueLights::FlashLit() const
{
	return (Side != 0 || bHazard) && FMath::Fmod(FlashT * TLI::FlashHz, 1.0) < 0.5;
}

int32 FTorqueLights::Step(double Dt, bool bEngineRunning, double FrontBrake, double RearBrake, double Lean, double TurnRate,
	bool bSelfCancel, float DayLight)
{
	bIgnition = bEngineRunning;
	bBraking = bIgnition && (FrontBrake > TLI::BrakeOn || RearBrake > TLI::BrakeOn || bTestBrake);
	int32 Heard = 0;
	if (!bIgnition)
	{
		SetSide(0);
		bHazard = false;
	}
	else if (Side != 0 || bHazard)
	{
		const bool bWas = FlashLit();
		FlashT += Dt;
		if (FlashLit() != bWas) Heard = FlashLit() ? 1 : -1;
		if (Side != 0 && bSelfCancel)
		{
			// bike_lamps.gd step: watch for the turn, then for the bike upright and going straight again.
			Turned += TurnRate * Dt * Side;
			if (!bTurning) bTurning = Lean * Side > TLI::TurnLean || Turned > TLI::TurnYaw;
			else if (FMath::Abs(Lean) < TLI::UprightLean && FMath::Abs(TurnRate) < TLI::StraightRate)
			{
				SetSide(0);   // through the turn and upright again: it cancels itself
				Heard = bWas ? -1 : 0;
			}
		}
	}
	Show(DayLight);
	return Heard;
}

void FTorqueLights::SetLens(FLens& L, float Emissive)
{
	UStaticMeshComponent* C = L.Mesh.Get();
	if (!C || L.Lit == Emissive) return;
	L.Lit = Emissive;
	// The lens's own finish (and its road dirt) with its glow turned up: the instance the bike's looks
	// already made, or one made now.
	UMaterialInstanceDynamic* M = Cast<UMaterialInstanceDynamic>(C->GetMaterial(0));
	if (!M) M = C->CreateDynamicMaterialInstance(0);
	if (M) M->SetScalarParameterValue(TEXT("Emissive"), Emissive);
}

void FTorqueLights::Show(float DayLight)
{
	const bool bHead = HeadOn(), bMain = MainOn(), bFogOn = FogOn(), bLeft = LeftLit(), bRight = RightLit();
	const bool bTail = bIgnition && (bHead || bBraking);
	const int32 State = (bHead ? 1 : 0) | (bMain ? 2 : 0) | (bFogOn ? 4 : 0) | (bLeft ? 8 : 0) | (bRight ? 16 : 0) | (bBraking ? 32 : 0) | (bTail ? 64 : 0);
	const float Day = FMath::Clamp(DayLight, 0.0f, 1.0f);
	if (State == Shown && FMath::Abs(Day - ShownDay) < 0.02f) return;
	Shown = State;
	ShownDay = Day;
	const float Glow = FMath::Lerp(TLI::NightGlow, 1.0f, Day);
	// (Dark well into dusk: Day squared.)
	const float StopLensK = FMath::Lerp(TLI::StopNightGlow, 1.0f, Day * Day);
	const float TailLensK = FMath::Lerp(TLI::TailNightGlow, 1.0f, Day * Day);
	const float IndicatorLensK = FMath::Lerp(TLI::IndicatorNightGlow, 1.0f, Day * Day);
	for (FLens& L : HeadLenses) SetLens(L, bHead ? (bMain ? TLI::MainGlow : TLI::HeadGlow) * Glow : 0.0f);
	for (FLens& L : FogLenses) SetLens(L, bFogOn ? TLI::FogGlow * Glow : 0.0f);
	for (FLens& L : TailLenses) SetLens(L, bBraking ? TLI::StopGlowLens * StopLensK : bTail ? TLI::TailGlow * TailLensK : 0.0f);
	for (int32 I = 0; I < Indicators.Num(); ++I)
	{
		const bool bOn = IndicatorSide[I] < 0 ? bLeft : bRight;
		SetLens(Indicators[I], bOn ? TLI::IndicatorGlowLens * IndicatorLensK : 0.0f);
		if (UPointLightComponent* P = IndicatorGlow.IsValidIndex(I) ? IndicatorGlow[I].Get() : nullptr) P->SetVisibility(bOn);
	}
	if (USpotLightComponent* B = Beam.Get())
	{
		B->SetVisibility(bHead);
		if (bHead)
		{
			// Dipped: aimed well down, wide, reaching some 70 m; main: level, narrower, out to 200 m and more.
			const double* Reach = bMain ? TLI::HighReach : TLI::LowReach;
			B->SetRelativeRotation(FRotator(-(bMain ? TLI::HighAim : TLI::LowAim), 0.0, 0.0));
			B->SetOuterConeAngle(bMain ? TLI::HighCone : TLI::LowCone);
			B->SetInnerConeAngle((bMain ? TLI::HighCone : TLI::LowCone) * (bMain ? 0.45f : 0.3f));
			B->SetAttenuationRadius((float)((Reach[0] + HeadLumens * Reach[1]) * 100.0));
			B->SetIntensity((float)(HeadLumens * (bMain ? TLI::HighCdPerLm : TLI::LowCdPerLm)));
		}
	}
	if (USpotLightComponent* F = Flood.Get()) F->SetVisibility(bHead && !bMain);
	for (TWeakObjectPtr<USpotLightComponent>& L : FogBeams)
		if (L.IsValid()) L->SetVisibility(bFogOn);
	if (USpotLightComponent* S = StopGlow.Get()) S->SetVisibility(bBraking);
}

FString FTorqueLights::Describe() const
{
	auto OnOff = [](bool b) { return b ? TEXT("on") : TEXT("off"); };
	auto Glows = [](const TArray<FLens>& Lenses) {
		TArray<FString> Bits;
		for (const FLens& L : Lenses) Bits.Add(L.Mesh.IsValid() ? FString::Printf(TEXT("%.0f"), FMath::Max(L.Lit, 0.0f)) : FString(TEXT("gone")));
		return Bits.Num() > 0 ? FString::Join(Bits, TEXT("/")) : FString(TEXT("none"));
	};
	auto Spot = [](const TWeakObjectPtr<USpotLightComponent>& W) {
		const USpotLightComponent* L = W.Get();
		if (!L) return FString(TEXT("none"));
		return FString::Printf(TEXT("%s %.0f cd, cone %.0f, reach %.0f m, aim %.1f down"), L->IsVisible() ? TEXT("shining") : TEXT("dark"),
			L->Intensity, L->OuterConeAngle, L->AttenuationRadius / 100.0f, -L->GetRelativeRotation().Pitch);
	};
	FString Out = FString::Printf(TEXT("lamps: ignition %s; switches headlight %s, main beam %s, fog %s, indicators %s, hazards %s; braking %s"),
		OnOff(bIgnition), OnOff(bHeadlight), OnOff(bMainBeam), OnOff(bFog), Side < 0 ? TEXT("left") : Side > 0 ? TEXT("right") : TEXT("off"),
		OnOff(bHazard), bBraking ? TEXT("yes") : TEXT("no"));
	if (HeadLenses.Num() + FogLenses.Num() + TailLenses.Num() + Indicators.Num() == 0)
		Out += TEXT("; NO LENSES FOUND (not a part-list bike, or MakeBike's Surf: tags missing)");
	Out += FString::Printf(TEXT("; lens glow: headlight %s, fog %s, rear %s, indicators "), *Glows(HeadLenses), *Glows(FogLenses), *Glows(TailLenses));
	TArray<FString> Ind;
	for (int32 I = 0; I < Indicators.Num(); ++I)
		Ind.Add(FString::Printf(TEXT("%s %.0f"), IndicatorSide[I] < 0 ? TEXT("L") : TEXT("R"), FMath::Max(Indicators[I].Lit, 0.0f)));
	Out += Ind.Num() > 0 ? FString::Join(Ind, TEXT("/")) : FString(TEXT("none"));
	int32 FogLit = 0, GlowLit = 0;
	for (const TWeakObjectPtr<USpotLightComponent>& L : FogBeams) FogLit += L.IsValid() && L->IsVisible() ? 1 : 0;
	for (const TWeakObjectPtr<UPointLightComponent>& L : IndicatorGlow) GlowLit += L.IsValid() && L->IsVisible() ? 1 : 0;
	Out += FString::Printf(TEXT("; beam %s; flood %s; fog beams %d of %d shining; indicator glows %d of %d; stop glow %s"),
		*Spot(Beam), *Spot(Flood), FogLit, FogBeams.Num(), GlowLit, IndicatorGlow.Num(), *Spot(StopGlow));
	// The tell-tales (DrawTellTales): drawn while the headlight's on or the indicators are going.
	TArray<FString> Tells;
	if (HeadOn()) Tells.Add(MainOn() ? TEXT("main beam (blue)") : TEXT("dipped (green)"));
	if (FogOn()) Tells.Add(TEXT("fog (green)"));
	if (Indicating()) Tells.Add(FString::Printf(TEXT("arrows (left %s, right %s)"), LeftLit() ? TEXT("lit") : TEXT("dark"), RightLit() ? TEXT("lit") : TEXT("dark")));
	Out += TEXT("; HUD tell-tales: ") + (Tells.Num() > 0 ? FString::Join(Tells, TEXT(", ")) : FString(TEXT("none")));
	return Out;
}

uint8 FTorqueLights::NetBits() const
{
	if (!bIgnition) return 0;
	return (bHeadlight ? 1 : 0) | (bMainBeam ? 2 : 0) | (bFog ? 4 : 0) | (Side < 0 ? 8 : 0) | (Side > 0 ? 16 : 0) | (bHazard ? 32 : 0) | 64;
}

void FTorqueLights::FromNet(uint8 Bits)
{
	bIgnition = (Bits & 64) != 0;
	bHeadlight = (Bits & 1) != 0;
	bMainBeam = (Bits & 2) != 0;
	bFog = (Bits & 4) != 0;
	const bool bHaz = (Bits & 32) != 0;
	if (bHaz != bHazard)
	{
		bHazard = bHaz;
		FlashT = 0.0;
	}
	// The flash runs here from when the side was switched on, so it keeps time without being sent.
	SetSide((Bits & 8) ? -1 : (Bits & 16) ? 1 : 0);
}

const std::vector<float>& FTorqueLights::Click(bool bTick)
{
	// bike_lamps.gd click: a short sine with a little noise, dying away fast; the tock a little lower.
	static std::vector<float> Made[2];
	std::vector<float>& Out = Made[bTick ? 1 : 0];
	if (!Out.empty()) return Out;
	const int32 N = (int32)(ClickRate * 0.035);
	Out.resize(N);
	FRandomStream Rng(bTick ? 7 : 11);
	const double Pitch = bTick ? 2400.0 : 1700.0;
	for (int32 I = 0; I < N; ++I)
	{
		const double T = (double)I / ClickRate;
		const double Env = FMath::Exp(-T * 260.0);
		const double V = (FMath::Sin(2.0 * UE_DOUBLE_PI * Pitch * T) * 0.6 + Rng.FRandRange(-1.0f, 1.0f) * 0.4) * Env;
		Out[I] = (float)FMath::Clamp(V * 0.8, -1.0, 1.0);
	}
	return Out;
}
