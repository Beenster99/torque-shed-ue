#include "TorqueDash.h"

#include "Sim/BikeSim.h"
#include "Fit/BikeLayout.h"
#include "Fit/FitJson.h"
#include "Shed/TorqueToolModels.h"

#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace TorqueDashImpl
{
// bike_build.gd's petrol.
constexpr double FuelPerKwh = 0.30;
constexpr double FrictionBar = 0.9;
constexpr double FrictionBarPerKrpm = 0.13;
constexpr double PumpingBar = 0.75;
constexpr double FrictionPerCylinder = 0.08;
constexpr double FullThrottleRich = 0.15;
constexpr double TwoStrokeThirst = 1.35;
constexpr double AirDensity = 1.2;
constexpr double Tau = 6.283185307179586;
// fuel_stop.gd: the low fuel light under this much of the tank.
constexpr double LowFuel = 0.15;

std::string Std(const FString& S) { return std::string(TCHAR_TO_UTF8(*S)); }
}
namespace TDI = TorqueDashImpl;

void FTorqueDash::Setup(const TMap<FString, FString>& Parts)
{
	const TorqueFit::BikeLayout* Lay = TorqueShedCatalogue::Layout();
	if (!Lay) return;
	auto PartOf = [&Parts, Lay](const TCHAR* Slot) -> const TorqueFit::Json* {
		const FString* Id = Parts.Find(Slot);
		return Id && !Id->IsEmpty() && *Id != TEXT("none") ? Lay->Part(TDI::Std(*Id)) : nullptr;
	};
	// BikeBuild: dash_features is the dashboard part's features (["speed"] with none).
	if (const TorqueFit::Json* Dash = PartOf(TEXT("dashboard")))
	{
		Style = UTF8_TO_TCHAR(Dash->Str("style", "analog").c_str());
		if (const TorqueFit::Json* F = Dash->Get("features"); F && F->IsArray())
		{
			Features.Reset();
			for (const TorqueFit::Json& J : F->A)
				if (J.IsString()) Features.Add(UTF8_TO_TCHAR(J.S.c_str()));
		}
	}
	// The tank and any auxiliary tank (BikeBuild: fuel_capacity = main_capacity + aux_capacity).
	FuelCapacity = 0.0;
	if (const TorqueFit::Json* Tank = PartOf(TEXT("tank"))) FuelCapacity += Tank->Num("capacity", 15.0);
	if (const TorqueFit::Json* Aux = PartOf(TEXT("aux_tank"))) FuelCapacity += Aux->Num("capacity", 0.0);
	if (FuelCapacity <= 0.0) FuelCapacity = 15.0;
	FuelLitres = FuelCapacity;
	// The engine's thirst (bike_build.gd _compute_engine).
	const TorqueFit::Json* Block = PartOf(TEXT("engine"));
	const TorqueFit::Json* Head = PartOf(TEXT("head"));
	const TorqueFit::Json* Fuel = PartOf(TEXT("fuel"));
	if (Block)
	{
		Displacement = Block->Num("displacement", 700.0);
		bTwoStroke = Block->Str("layout") == "2stroke";
	}
	FrictionMult = (Head ? Head->Num("thirst", 1.0) : 1.0) * (1.0 + TDI::FrictionPerCylinder * ((Block ? Block->Num("cylinders", 2.0) : 2.0) - 2.0));
	bInjected = Fuel && Fuel->Flag("injected", false);
	TripKm = TripFuel = TopKmh = 0.0;
	bTripFrom = false;
	TopSpeedMs = 0.0;   // worked out from the spec on the ride's first step
}

double FTorqueDash::FuelRate(const TorqueSim::BikeSpec& Spec, double Kw, double AtRpm) const
{
	// engine_load: how far open the throttle is to make Kw at that speed.
	const double RpmOn = FMath::Max(AtRpm, Spec.IdleRpm);
	const double PowerKw = Spec.EngineTorque(RpmOn) * RpmOn * TDI::Tau / 60.0 / 1000.0;
	const double Load = FMath::Clamp(FMath::Max(Kw, 0.0) / FMath::Max(PowerKw, 0.1), 0.0, 1.0);
	// friction_kw: friction and pumping, a mean pressure on the pistons over one power stroke every two turns.
	const double Bar = (TDI::FrictionBar + TDI::FrictionBarPerKrpm * RpmOn / 1000.0) * FrictionMult + TDI::PumpingBar * (1.0 - Load);
	const double FrictionKw = Bar * 100000.0 * Displacement / 1000000.0 * RpmOn / 120.0 / 1000.0;
	double Litres = (FMath::Max(Kw, 0.0) + FrictionKw) * TDI::FuelPerKwh * (bTwoStroke ? TDI::TwoStrokeThirst : 1.0);
	Litres *= 1.0 + TDI::FullThrottleRich * FMath::Clamp((Load - 0.8) / 0.2, 0.0, 1.0);
	// (economy_mult: the petrol, plugs, filter and the engine's health; as new on fresh unleaded.)
	return Litres;
}

double FTorqueDash::CruiseEconomy(const TorqueSim::BikeSpec& Spec) const
{
	const double V = 25.0;
	const double Kw = (0.5 * TDI::AirDensity * Spec.CdA * V * V * V + Spec.Rolling * Spec.TotalMass * 9.81 * V) / Spec.DrivetrainEfficiency / 1000.0;
	const double CruiseRpm = V / Spec.RearRadius * Spec.OverallRatio(Spec.GearCount()) * 60.0 / TDI::Tau;
	return 90.0 / FMath::Max(FuelRate(Spec, Kw, CruiseRpm), 0.01);
}

void FTorqueDash::Step(const TorqueSim::BikeSim& Sim, double Throttle, bool bRunning, double Dt)
{
	Speed = Sim.Speed;
	Rpm = Sim.Rpm;
	Redline = Sim.Build.Redline;
	Gear = Sim.Gear;
	if (TopSpeedMs <= 0.0) TopSpeedMs = TopSpeedOf(Sim.Build);
	bEngineOn = bRunning;
	bCrashed = Sim.IsCrashed;
	TopKmh = FMath::Max(TopKmh, FMath::Abs(Speed) * 3.6);
	// main.gd's hud.trip_km: the distance covered. Measured from where the bike actually goes over
	// the ground (the sim's metres), not speed x frame time, so it can't run ahead of the road.
	const FVector Here(Sim.Position.x, Sim.Position.y, Sim.Position.z);
	if (bTripFrom && Dt > 0.0)
	{
		const double Moved = FVector::Dist(Here, TripFrom);
		if (Moved <= FMath::Max(1.0, 120.0 * Dt)) TripKm += Moved / 1000.0;   // (120 m/s: faster is a jump)
	}
	TripFrom = Here;
	bTripFrom = true;
	if (!bRunning || bCrashed || Dt <= 0.0) return;
	// bike.gd: the petrol the engine burns making what it makes (the crank's share of the back
	// wheel's drive), or on the overrun, the throttle shut in gear above tickover, what an injected
	// engine cuts off and carburettors still let through the idle jets.
	const TorqueSim::BikeSpec& B = Sim.Build;
	const double Ratio = B.OverallRatio(FMath::Clamp(Sim.Gear, 1, B.GearCount()));
	const double Made = FMath::Max(Sim.Wheels[1].DriveTorque / FMath::Max(Ratio * B.DrivetrainEfficiency, 0.01), 0.0);
	const double Kw = Made * Rpm * TDI::Tau / 60.0 / 1000.0;
	const bool bOverrun = Throttle < 0.02 && Rpm > B.IdleRpm * 1.3;
	const double PerHour = bOverrun ? (bInjected ? 0.0 : FuelRate(B, 0.0, FMath::Min(Rpm, B.IdleRpm * 1.5))) : FuelRate(B, Kw, Rpm);
	const double Burnt = FMath::Min(PerHour / 3600.0 * Dt, FuelLitres);
	FuelLitres -= Burnt;
	TripFuel += Burnt;
}

double FTorqueDash::TripEconomy() const
{
	return TripFuel > 0.01 && TripKm > 0.05 ? TripKm / TripFuel : 0.0;
}

double FTorqueDash::RangeKm(const TorqueSim::BikeSpec& Spec) const
{
	const double Econ = TripEconomy() > 0.0 ? TripEconomy() : CruiseEconomy(Spec);
	return FMath::Max(FuelLitres, 0.0) * Econ;
}

void FTorqueDash::HookClocks(AActor* Owner, const TArray<UStaticMeshComponent*>& Meshes, const TArray<TorqueShedCatalogue::FDashGauge>& Gauges,
	UStaticMesh* TickMesh)
{
	Needles.Reset();
	Leds.Reset();
	BakedTicks.Reset();
	for (const TWeakObjectPtr<USceneComponent>& Part : ScaleParts)
		if (USceneComponent* Old = Part.Get()) Old->DestroyComponent();
	ScaleParts.Reset();
	ScaleUnits = -1;
	ClockOwner = Owner;
	ScaleTickMesh = TickMesh;
	bDials = Style == TEXT("analog") || Style == TEXT("twin");
	// bike_model.gd _add_leds: green, green, amber, amber, red, red from the left.
	static const FLinearColor LedColours[6] = {
		FLinearColor(0.1f, 1.0f, 0.2f), FLinearColor(0.1f, 1.0f, 0.2f), FLinearColor(1.0f, 0.85f, 0.1f),
		FLinearColor(1.0f, 0.85f, 0.1f), FLinearColor(1.0f, 0.1f, 0.05f), FLinearColor(1.0f, 0.1f, 0.05f)};
	for (const TorqueShedCatalogue::FDashGauge& G : Gauges)
	{
		UStaticMeshComponent* M = Meshes.IsValidIndex(G.Mesh) ? Meshes[G.Mesh] : nullptr;
		if (!M) continue;
		const FString What = G.What.ToString();
		if (What == TEXT("speed") || What == TEXT("rpm"))
		{
			FNeedle N;
			N.Mesh = M;
			N.bRpm = What == TEXT("rpm");
			N.At = G.At;
			N.Rest = G.Up;
			N.Axis = G.Axis;
			// A quarter turn clockwise, as the rider sees the dial, from straight up it is the left.
			N.Side = FVector::CrossProduct(G.Axis, G.Up).GetSafeNormal();
			if (N.Side.Y > 0.0) N.Side = -N.Side;
			N.Base = M->GetRelativeTransform();
			// bike_model.gd: the needle glows red with the ignition on.
			if (UMaterialInstanceDynamic* Glow = TorqueToolModels::Finish(Owner, FLinearColor(1.0f, 0.25f, 0.1f), 0.0f, 0.4f, 2.5f))
				M->SetMaterial(0, Glow);
			Needles.Add(N);
		}
		else if (What == TEXT("tick"))
		{
			BakedTicks.Add(M);
		}
		else if (What.StartsWith(TEXT("led")))
		{
			FLed L;
			L.Mesh = M;
			L.Colour = LedColours[FMath::Clamp(FCString::Atoi(*What.Mid(3)), 0, 5)];
			L.Mat = TorqueToolModels::Finish(Owner, FLinearColor(0.08f, 0.08f, 0.08f), 0.0f, 0.3f, 0.0f);
			if (L.Mat) M->SetMaterial(0, L.Mat);
			Leds.Add(L);
		}
		else if (What == TEXT("readout") && Owner)
		{
			// bike_model.gd dash_readout: a label flat on the face, 28 px (dials) or 40 px (screens) at
			// 0.4 mm a pixel. A text render faces its +x and reads along its -y.
			UTextRenderComponent* T = NewObject<UTextRenderComponent>(Owner);
			T->SetMobility(EComponentMobility::Movable);
			T->SetupAttachment(M);
			T->RegisterComponent();
			T->SetRelativeLocationAndRotation(G.At + G.Axis * 0.05, FRotationMatrix::MakeFromXZ(G.Axis, G.Up).Rotator());
			T->SetHorizontalAlignment(EHTA_Center);
			T->SetVerticalAlignment(EVRTA_TextCenter);
			T->SetWorldSize(bDials ? 1.12f : 1.6f);
			T->SetCastShadow(false);
			T->SetText(FText::GetEmpty());
			Readout = T;
		}
	}
	ReadoutShown.Reset();
	UE_LOG(LogTemp, Log, TEXT("Torque: the %s dash's clocks work: %d needles, %d shift lights, %s readout (shows %s)"),
		*Style, Needles.Num(), Leds.Num(), Readout.IsValid() ? TEXT("a") : TEXT("no"), *FString::Join(Features, TEXT(", ")));
}

void FTorqueDash::UpdateClocks(int32 Units)
{
	// main.gd _update_dash and bike_model.gd set_gauges had the needles' full scale at 200 km/h or
	// 120 mph and the revs' a tenth past the redline. Ours is the bike's own numbered scale, once
	// the ride's spec is known (and again if the units change).
	const bool bMetric = Units == 0;
	if (bDials && Needles.Num() > 0 && TopSpeedMs > 0.0 && (ScaleUnits != Units || ScaleRedline != Redline))
		BuildScale(Units);
	const bool bScaled = ScaleUnits == Units && SpeedMax > 0.0 && RevMax > 0.0;
	const double SpeedShown = FMath::Abs(Speed) * (bMetric ? 3.6 : 2.23694);
	const double SpeedFrac = FMath::Clamp(SpeedShown / (bScaled ? SpeedMax : (bMetric ? 200.0 : 120.0)), 0.0, 1.0);
	const double RpmFrac = Rpm / FMath::Max(Redline * 1.1, 1000.0);   // (the shift lights)
	const double NeedleRpmFrac = bScaled ? Rpm / RevMax : RpmFrac;
	for (const FNeedle& N : Needles)
	{
		UStaticMeshComponent* M = N.Mesh.Get();
		if (!M) continue;
		// From bottom left (empty) round through the top to bottom right (full scale): 2.35 rad each way.
		const double A = FMath::Lerp(2.35, -2.35, FMath::Clamp(N.bRpm ? NeedleRpmFrac : SpeedFrac, 0.0, 1.0));
		const FVector Dir = N.Rest * FMath::Cos(A) + N.Side * FMath::Sin(A);
		const FQuat Turn = FQuat::FindBetweenNormals(N.Rest, Dir.GetSafeNormal());
		M->SetRelativeTransform(FTransform(-N.At) * FTransform(Turn) * FTransform(N.At) * N.Base);
	}
	for (int32 I = 0; I < Leds.Num(); ++I)
	{
		FLed& L = Leds[I];
		const int32 On = RpmFrac > 0.55 + I * 0.07 ? 1 : 0;
		if (On == L.Lit || !L.Mat) continue;
		L.Lit = On;
		const FLinearColor C = On ? L.Colour : FLinearColor(0.08f, 0.08f, 0.08f);
		L.Mat->SetVectorParameterValue(TEXT("Color"), C);
		L.Mat->SetVectorParameterValue(TEXT("Color2"), C);
		L.Mat->SetScalarParameterValue(TEXT("Emissive"), On ? 3.0f : 0.0f);
	}
	UTextRenderComponent* T = Readout.Get();
	if (!T) return;
	const double Fuel = FMath::Clamp(FuelLitres / FMath::Max(FuelCapacity, 0.1), 0.0, 1.0);
	const int32 Bars = FMath::RoundToInt(Fuel * 8.0);
	const FString BarsText = FString::ChrN(Bars, TEXT('|')) + FString::ChrN(8 - Bars, TEXT('.'));
	const FString GearText = Has(TEXT("gear")) ? (Gear == 0 ? FString(TEXT("N")) : FString::Printf(TEXT("G%d"), Gear)) : FString();
	const FString Unit = bMetric ? TEXT("km/h") : TEXT("mph");
	const bool bLow = Fuel < TDI::LowFuel;
	FString Text;
	FColor Colour;
	// (the sat nav's next turn, on a ride with one)
	const FString Nav = bNav ? (bNavOff ? FString(bNavRerouting ? TEXT("REROUTING") : TEXT("OFF ROUTE")) : NavMark(NavTurn) + TEXT(" ") + NavDistance(NavTo, bMetric)) : FString();
	if (bDials)
	{
		Text = (bEngineOn ? GearText : FString(TEXT("OFF"))) + TEXT("\n") + BarsText;
		if (bNav) Text += TEXT("\n") + Nav;
		// (Godot colours are sRGB, as an FColor is.)
		Colour = bLow ? FLinearColor(0.8f, 0.1f, 0.05f).ToFColor(false) : FLinearColor(0.1f, 0.1f, 0.1f).ToFColor(false);
	}
	else
	{
		if (!bEngineOn)
		{
			Text = FString::Printf(TEXT("%d %s\nSTARTING"), FMath::RoundToInt(SpeedShown), *Unit);
		}
		else
		{
			FString Revs;
			if (Has(TEXT("rpm")))
			{
				const int32 Seg = FMath::RoundToInt(FMath::Clamp(Rpm / FMath::Max(Redline, 1.0), 0.0, 1.0) * 10.0);
				Revs = TEXT("\n") + FString::ChrN(Seg, TEXT('#')) + FString::ChrN(10 - Seg, TEXT('-')) +
					FString::Printf(TEXT(" %d rpm"), FMath::RoundToInt(Rpm / 100.0) * 100);
			}
			Text = FString::Printf(TEXT("%d %s   %s%s\nF %s E"), FMath::RoundToInt(SpeedShown), *Unit, *GearText, *Revs, *BarsText);
			if (bNav) Text += TEXT("\n") + Nav + (NavOnto.IsEmpty() || bNavOff ? FString() : TEXT(" ") + NavOnto.Left(14));
		}
		Colour = bLow ? FLinearColor(1.0f, 0.45f, 0.3f).ToFColor(false) : FLinearColor(0.75f, 1.0f, 0.85f).ToFColor(false);
	}
	if (Text != ReadoutShown)
	{
		ReadoutShown = Text;
		T->SetText(FText::FromString(Text));
	}
	if (Colour != ReadoutColour)
	{
		ReadoutColour = Colour;
		T->SetTextRenderColor(Colour);
	}
}

FString FTorqueDash::NavDistance(double Metres, bool bMetric)
{
	if (bMetric) return Metres < 1000.0 ? FString::Printf(TEXT("%d m"), FMath::RoundToInt(Metres / 10.0) * 10) : FString::Printf(TEXT("%.1f km"), Metres / 1000.0);
	const double Yards = Metres / 0.9144, Miles = Metres / 1609.344;
	return Miles < 0.25 ? FString::Printf(TEXT("%d yd"), FMath::RoundToInt(Yards / 10.0) * 10) : FString::Printf(TEXT("%.1f mi"), Miles);
}

FString FTorqueDash::NavWords(int32 Turn)
{
	switch (Turn)
	{
	case -3: return TEXT("Sharp left");
	case -2: return TEXT("Turn left");
	case -1: return TEXT("Bear left");
	case 0: return TEXT("Straight on");
	case 1: return TEXT("Bear right");
	case 2: return TEXT("Turn right");
	case 3: return TEXT("Sharp right");
	default: return TEXT("Finish");
	}
}

FString FTorqueDash::NavMark(int32 Turn)
{
	return Turn == 9 ? FString(TEXT("F")) : Turn == 0 ? FString(TEXT("^")) : Turn < 0 ? FString::ChrN(FMath::Min(-Turn, 2), TEXT('<')) : FString::ChrN(FMath::Min(Turn, 2), TEXT('>'));
}

double FTorqueDash::TopSpeedOf(const TorqueSim::BikeSpec& Spec)
{
	const int32 Top = Spec.GearCount();
	if (Top <= 0) return 30.0;
	// Top gear at the redline.
	const double Geared = Spec.Redline * TDI::Tau / 60.0 / FMath::Max(Spec.OverallRatio(Top), 0.1) * Spec.RearRadius;
	// The engine's peak power at the back wheel...
	double PeakW = 0.0;
	for (double R = FMath::Max(Spec.IdleRpm, 500.0); R <= Spec.Redline; R += 100.0)
		PeakW = FMath::Max(PeakW, Spec.EngineTorque(R) * R * TDI::Tau / 60.0);
	PeakW *= Spec.DrivetrainEfficiency;
	// ... against the wind and the tyres rolling.
	double Lo = 1.0, Hi = 150.0;
	for (int32 I = 0; I < 50; ++I)
	{
		const double V = 0.5 * (Lo + Hi);
		const double Need = 0.5 * TDI::AirDensity * Spec.CdA * V * V * V + Spec.Rolling * Spec.TotalMass * 9.81 * V;
		(Need > PeakW ? Hi : Lo) = V;
	}
	return FMath::Max(FMath::Min(Geared, Lo), 5.0);
}

void FTorqueDash::SpeedScale(double TopMs, bool bMetric, double& OutMax, double& OutStep)
{
	// A little past the top speed (as real clocks are), never under 100 km/h or 60 mph, numbered
	// in round steps with at most ten gaps so the numbers fit the dial.
	const double Want = FMath::Max(TopMs * (bMetric ? 3.6 : 2.23694) * 1.08, bMetric ? 100.0 : 60.0);
	static const double Steps[] = {10.0, 20.0, 40.0, 50.0, 60.0, 80.0, 100.0};
	OutMax = 0.0;
	OutStep = 10.0;
	for (const double Each : Steps)
	{
		const double Gaps = FMath::CeilToDouble(Want / Each - 1e-6);
		if (Gaps > 10.0) continue;
		const double Full = Gaps * Each;
		if (OutMax <= 0.0 || Full < OutMax - 1e-6)
		{
			OutMax = Full;
			OutStep = Each;
		}
	}
	if (OutMax <= 0.0)
	{
		OutStep = 100.0;
		OutMax = FMath::CeilToDouble(Want / OutStep) * OutStep;
	}
}

void FTorqueDash::RevScale(double AtRedline, double& OutMax, double& OutStep)
{
	// To the next thousand past a tenth over the redline, numbered every thousand (every two on a
	// screamer whose dial would run past 12).
	const double Thousands = FMath::Max(FMath::CeilToDouble(AtRedline * 1.1 / 1000.0 - 1e-6), 4.0);
	if (Thousands > 12.0)
	{
		OutStep = 2000.0;
		OutMax = FMath::CeilToDouble(Thousands / 2.0) * 2000.0;
	}
	else
	{
		OutStep = 1000.0;
		OutMax = Thousands * 1000.0;
	}
}

void FTorqueDash::BuildScale(int32 Units)
{
	for (const TWeakObjectPtr<USceneComponent>& Part : ScaleParts)
		if (USceneComponent* Old = Part.Get()) Old->DestroyComponent();
	ScaleParts.Reset();
	const bool bMetric = Units == 0;
	SpeedScale(TopSpeedMs, bMetric, SpeedMax, SpeedStep);
	RevScale(Redline, RevMax, RevStep);
	ScaleUnits = Units;
	ScaleRedline = Redline;
	AActor* Owner = ClockOwner.Get();
	UStaticMesh* Cube = ScaleTickMesh.Get();
	if (!Owner || !Cube) return;   // (the plain ticks stay; the needles still read the bike's scale)

	// The dial is 4.3 cm across the face's radius: the ticks run in from 4 cm, the numbers inside
	// them, all a hair proud of the face and under the needle. (Cm, the needle mesh's own space.)
	const FColor Ink(22, 22, 22), Red(200, 24, 12);
	UMaterialInstanceDynamic* InkMat = TorqueToolModels::Finish(Owner, FLinearColor(0.02f, 0.02f, 0.02f), 0.0f, 0.5f);
	UMaterialInstanceDynamic* RedMat = TorqueToolModels::Finish(Owner, FLinearColor(0.75f, 0.03f, 0.02f), 0.0f, 0.5f, 0.4f);
	int32 Numbers = 0;
	for (const FNeedle& N : Needles)
	{
		UStaticMeshComponent* NeedleMesh = N.Mesh.Get();
		USceneComponent* Parent = NeedleMesh ? NeedleMesh->GetAttachParent() : nullptr;
		if (!Parent) continue;
		const double Full = N.bRpm ? RevMax : SpeedMax;
		const double Every = N.bRpm ? RevStep : SpeedStep;
		if (Full <= 0.0 || Every <= 0.0) continue;
		auto DirAt = [&N](double Frac) {
			const double A = FMath::Lerp(2.35, -2.35, FMath::Clamp(Frac, 0.0, 1.0));
			return (N.Rest * FMath::Cos(A) + N.Side * FMath::Sin(A)).GetSafeNormal();
		};
		const int32 Halves = FMath::RoundToInt(Full / Every) * 2;
		for (int32 I = 0; I <= Halves; ++I)
		{
			const double Value = Every * 0.5 * I;
			const double Frac = Value / Full;
			const bool bMajor = I % 2 == 0;
			const bool bRedZone = N.bRpm && Value >= Redline - 1.0;
			const FVector Out = DirAt(Frac);
			// A tick: a thin block along the radius, its outer end at 4 cm.
			const double Len = bMajor ? 0.7 : 0.4, Wide = bMajor ? 0.22 : 0.14;
			UStaticMeshComponent* T = NewObject<UStaticMeshComponent>(Owner);
			T->SetStaticMesh(Cube);
			T->SetMobility(EComponentMobility::Movable);
			T->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			T->SetCastShadow(false);
			T->SetupAttachment(Parent);
			T->RegisterComponent();
			if (UMaterialInstanceDynamic* Mat = bRedZone ? RedMat : InkMat) T->SetMaterial(0, Mat);
			const FVector TickAt = N.At + Out * (4.0 - Len * 0.5) - N.Axis * 0.15;
			const FTransform TickLocal(FRotationMatrix::MakeFromXZ(N.Axis, Out).ToQuat(), TickAt, FVector(0.1, Wide, Len) / 100.0);
			T->SetRelativeTransform(TickLocal * N.Base);
			ScaleParts.Add(T);
			if (!bMajor) continue;
			// Its number, upright as the rider reads it: thousands on the rev counter.
			const int32 Shown = FMath::RoundToInt(N.bRpm ? Value / 1000.0 : Value);
			const FString Label = FString::FromInt(Shown);
			UTextRenderComponent* L = NewObject<UTextRenderComponent>(Owner);
			L->SetMobility(EComponentMobility::Movable);
			L->SetupAttachment(Parent);
			L->RegisterComponent();
			L->SetHorizontalAlignment(EHTA_Center);
			L->SetVerticalAlignment(EVRTA_TextCenter);
			L->SetWorldSize(Label.Len() >= 3 ? 0.5f : 0.62f);
			L->SetCastShadow(false);
			L->SetTextRenderColor(bRedZone ? Red : Ink);
			L->SetText(FText::FromString(Label));
			const FTransform LabelLocal(FRotationMatrix::MakeFromXZ(N.Axis, N.Rest).ToQuat(), N.At + Out * (Label.Len() >= 3 ? 2.45 : 2.6) - N.Axis * 0.1);
			L->SetRelativeTransform(LabelLocal * N.Base);
			ScaleParts.Add(L);
			++Numbers;
		}
		// What the numbers are in, small, above the pivot.
		UTextRenderComponent* Unit = NewObject<UTextRenderComponent>(Owner);
		Unit->SetMobility(EComponentMobility::Movable);
		Unit->SetupAttachment(Parent);
		Unit->RegisterComponent();
		Unit->SetHorizontalAlignment(EHTA_Center);
		Unit->SetVerticalAlignment(EVRTA_TextCenter);
		Unit->SetWorldSize(0.36f);
		Unit->SetCastShadow(false);
		Unit->SetTextRenderColor(Ink);
		Unit->SetText(FText::FromString(N.bRpm ? FString(TEXT("x1000 r/min")) : FString(bMetric ? TEXT("km/h") : TEXT("mph"))));
		const FTransform UnitLocal(FRotationMatrix::MakeFromXZ(N.Axis, N.Rest).ToQuat(), N.At + N.Rest * 1.25 - N.Axis * 0.1);
		Unit->SetRelativeTransform(UnitLocal * N.Base);
		ScaleParts.Add(Unit);
	}
	// The plain ticks give way to the numbered scale.
	for (const TWeakObjectPtr<UStaticMeshComponent>& Baked : BakedTicks)
		if (UStaticMeshComponent* M = Baked.Get()) M->SetVisibility(false);
	UE_LOG(LogTemp, Log, TEXT("Torque: the dials' scales: speedo 0-%.0f %s in %.0fs (top speed %.0f), revs 0-%.0f in %.0fs (redline %.0f), %d numbers"),
		SpeedMax, bMetric ? TEXT("km/h") : TEXT("mph"), SpeedStep, TopSpeedMs * (bMetric ? 3.6 : 2.23694), RevMax, RevStep, Redline, Numbers);
}
