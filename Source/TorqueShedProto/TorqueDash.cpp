#include "TorqueDash.h"

#include "Sim/BikeSim.h"
#include "Fit/BikeLayout.h"
#include "Fit/FitJson.h"
#include "Shed/TorqueToolModels.h"

#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
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
	bEngineOn = bRunning;
	bCrashed = Sim.IsCrashed;
	TopKmh = FMath::Max(TopKmh, FMath::Abs(Speed) * 3.6);
	TripKm += FMath::Abs(Speed) * Dt / 1000.0;   // main.gd: hud.trip_km
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

void FTorqueDash::HookClocks(AActor* Owner, const TArray<UStaticMeshComponent*>& Meshes, const TArray<TorqueShedCatalogue::FDashGauge>& Gauges)
{
	Needles.Reset();
	Leds.Reset();
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
			// A quarter turn clockwise, as the rider sees the dial, from straight up it is the left.
			N.Side = FVector::CrossProduct(G.Axis, G.Up).GetSafeNormal();
			if (N.Side.Y > 0.0) N.Side = -N.Side;
			N.Base = M->GetRelativeTransform();
			// bike_model.gd: the needle glows red with the ignition on.
			if (UMaterialInstanceDynamic* Glow = TorqueToolModels::Finish(Owner, FLinearColor(1.0f, 0.25f, 0.1f), 0.0f, 0.4f, 2.5f))
				M->SetMaterial(0, Glow);
			Needles.Add(N);
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
	// main.gd _update_dash and bike_model.gd set_gauges: the needles' full scale is 200 km/h or 120
	// mph, the revs' a tenth past the redline.
	const bool bMetric = Units == 0;
	const double SpeedShown = FMath::Abs(Speed) * (bMetric ? 3.6 : 2.23694);
	const double SpeedFrac = FMath::Clamp(SpeedShown / (bMetric ? 200.0 : 120.0), 0.0, 1.0);
	const double RpmFrac = Rpm / FMath::Max(Redline * 1.1, 1000.0);
	for (const FNeedle& N : Needles)
	{
		UStaticMeshComponent* M = N.Mesh.Get();
		if (!M) continue;
		// From bottom left (empty) round through the top to bottom right (full scale): 2.35 rad each way.
		const double A = FMath::Lerp(2.35, -2.35, FMath::Clamp(N.bRpm ? RpmFrac : SpeedFrac, 0.0, 1.0));
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
	if (bDials)
	{
		Text = (bEngineOn ? GearText : FString(TEXT("OFF"))) + TEXT("\n") + BarsText;
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
