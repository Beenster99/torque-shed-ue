#include "Shed/TorqueShedPower.h"

#include "Shed/TorqueBuildSubsystem.h"
#include "Shed/TorqueShedParts.h"
#include "Fit/BikeTune.h"

namespace TSPI
{
constexpr float TorqueFade = 1.5f;          // BikeBuild.TORQUE_FADE
constexpr float Drivetrain = 0.92f;         // DRIVETRAIN_EFFICIENCY
constexpr float FixedMass = 36.0f;          // FIXED_MASS: the bits no slot holds
constexpr float FluidMass = 0.74f * 13.0f + 0.87f * 3.5f;   // a full tank and sump (the shed has no fluids yet)
constexpr float HeatPower = 0.07f;          // shed_state DYNO_HEAT_POWER
// dyno_room.gd's run phases, seconds.
constexpr float IdleT = 0.8f, QuickT = 0.6f, SettleT = 0.5f, SweepT = 4.0f, CoastT = 1.2f;

// bike_build.gd part_health.
float PartHealth(const FTorqueItem& It)
{
	return FMath::Clamp(1.0f - 0.6f * It.Wear - 0.35f * It.Rust - 0.12f * It.Dirt, 0.15f, 1.0f);
}

// A slot's part as Torque Shed reads it: the catalogue id, its condition and whether it's there.
struct FSlotPart
{
	FName Part;
	bool bThere = false, bStock = false;
	float Health = 1.0f, Rust = 0.0f, Dirt = 0.0f;
};

FSlotPart PartIn(const UTorqueBuildSubsystem& B, int32 Bay, const TCHAR* Slot)
{
	FSlotPart Out;
	const FTorqueItem* It = B.Item(B.ItemOn(Bay, FName(Slot)));
	if (!It) return Out;
	Out.bThere = true;
	Out.Part = FName(*It->Part.ToString().ToLower());
	Out.bStock = TorqueShedParts::IsStock(It->Part);
	Out.Health = PartHealth(*It);
	Out.Rust = It->Rust;
	Out.Dirt = It->Dirt;
	return Out;
}

// A field of the part, or Default for a Speed Twin part or one that isn't there (as stock).
float Field(const FSlotPart& P, const TCHAR* Name, float Default)
{
	if (!P.bThere || P.bStock) return Default;
	return (float)TorqueShedParts::PartNumber(P.Part, FName(Name), Default);
}

float Lerp(float A, float B, float T) { return A + (B - A) * T; }

// The run's measured gear: the one nearest 1:1, below top.
int32 MeasureGear(const FTorqueDynoRun& Run) { return FMath::Clamp(Run.Gears - 1, 1, FMath::Max(Run.Gears, 1)); }
}

namespace TorqueShedPower
{
using namespace TSPI;

FTorqueEngineNumbers Numbers(const UTorqueBuildSubsystem& B, int32 Bay)
{
	FTorqueEngineNumbers E;
	// The bike's mass: the fixed bits, every part's own (a Speed Twin part as its kind's average), fluids.
	E.Mass = FixedMass;
	for (const FName& Slot : TorqueShedParts::Slots())
	{
		const FTorqueItem* It = B.Item(B.ItemOn(Bay, Slot));
		if (!It) continue;
		const FName Kind = TorqueShedParts::KindOf(Slot);
		float M = TorqueShedParts::IsStock(It->Part) ? (float)TorqueShedParts::KindAverage(Kind, TEXT("mass"), 3.0)
			: (float)TorqueShedParts::PartNumber(FName(*It->Part.ToString().ToLower()), TEXT("mass"), TorqueShedParts::KindAverage(Kind, TEXT("mass"), 3.0));
		if (It->bLightened && !TorqueShedParts::IsStock(It->Part))
			B.WithTune(Bay, [&M, It](const TorqueFit::BikeTune& T) { M *= 1.0f - (float)T.LightenCut(TCHAR_TO_UTF8(*It->Part.ToString().ToLower())); });
		E.Mass += M;
	}
	const FSlotPart Block = PartIn(B, Bay, TEXT("engine"));
	if (Block.bThere) E.Mass += FluidMass;
	if (!Block.bThere) return E;
	E.bValid = true;
	const FSlotPart Head = PartIn(B, Bay, TEXT("head")), Fuel = PartIn(B, Bay, TEXT("fuel")), Air = PartIn(B, Bay, TEXT("air_filter"));
	const FSlotPart Oil = PartIn(B, Bay, TEXT("oil_filter")), Plugs = PartIn(B, Bay, TEXT("spark_plugs")), Exhaust = PartIn(B, Bay, TEXT("exhaust"));
	const FSlotPart Ecu = PartIn(B, Bay, TEXT("ecu")), Gearbox = PartIn(B, Bay, TEXT("gearbox")), Final = PartIn(B, Bay, TEXT("final_drive"));
	// The block: a Speed Twin's own is a 1200 twin (112 N m, revving to 8500).
	const float Displacement = Field(Block, TEXT("displacement"), 1200.0f);
	E.PeakTorque = Displacement * Field(Block, TEXT("torque_per_cc"), 112.0f / 1200.0f);
	E.PeakTorqueRpm = Field(Block, TEXT("peak_torque_rpm"), 6000.0f) * Field(Head, TEXT("peak_shift"), 1.0f) * Field(Fuel, TEXT("peak_shift"), 1.0f);
	E.Redline = Field(Block, TEXT("redline"), 8500.0f) + Field(Head, TEXT("redline_add"), 0.0f) + (Ecu.bThere ? Field(Ecu, TEXT("rev_add"), 0.0f) : 0.0f);
	E.IdleRpm = Field(Block, TEXT("idle_rpm"), 950.0f);
	const float Breathing = Field(Block, TEXT("breathing"), 1.0f);
	// Every engine part's multiplier and how worn it is.
	float Mult = Field(Head, TEXT("torque_mult"), 1.0f) * Lerp(0.75f, 1.0f, Head.Health) * (Ecu.bThere ? Field(Ecu, TEXT("power"), 1.0f) : 1.0f);
	const float Demand = Displacement * Field(Head, TEXT("peak_shift"), 1.0f) * Breathing;
	const float Flow = Field(Fuel, TEXT("flow"), 1300.0f) * Lerp(0.8f, 1.0f, Fuel.Health);
	Mult *= Field(Fuel, TEXT("torque_mult"), 1.0f);
	if (Flow < Demand)
	{
		Mult *= FMath::Max(0.7f, Lerp(1.0f, Flow / Demand, 0.6f));
		E.Notes.Add(FString::Printf(TEXT("Fuel starved: the fuel system can't feed %d cc."), FMath::RoundToInt(Displacement)));
	}
	const float Clog = FMath::Clamp(Air.Dirt * Field(Air, TEXT("dirt_sensitivity"), 0.6f), 0.0f, 1.0f);
	Mult *= Field(Air, TEXT("flow"), 1.0f) * (1.0f - 0.2f * Clog) * Lerp(0.9f, 1.0f, Air.Health);
	if (Clog > 0.5f) E.Notes.Add(TEXT("Air filter is clogged."));
	const float PlugHealth = FMath::Clamp(1.0f - (1.0f - Plugs.Health) / FMath::Max(0.05f, Field(Plugs, TEXT("life"), 1.0f)), 0.0f, 1.0f);
	Mult *= Field(Plugs, TEXT("torque_mult"), 1.0f) * Lerp(0.72f, 1.0f, PlugHealth);
	if (PlugHealth < 0.5f) E.Notes.Add(TEXT("Misfiring: the spark plugs are worn out."));
	const float Protection = Field(Oil, TEXT("protection"), 0.9f) * Lerp(0.5f, 1.0f, Oil.Health);
	const float BlockHealth = 1.0f - (1.0f - Block.Health) * (1.4f - 0.5f * Protection);
	Mult *= Lerp(0.55f, 1.0f, FMath::Clamp(BlockHealth, 0.0f, 1.0f));
	if (BlockHealth < 0.5f) E.Notes.Add(TEXT("The engine's tired: grind the rust and wear off it."));
	if (Exhaust.bThere) Mult *= Field(Exhaust, TEXT("torque_mult"), 1.0f) * (1.0f - 0.15f * Exhaust.Rust) * Lerp(0.9f, 1.0f, Exhaust.Health);
	else
	{
		Mult *= 0.9f;
		E.Notes.Add(TEXT("No exhaust: loud and gutless."));
	}
	// The dyno desk's tune: the fuel map and ignition, the rev limiter; and the engine's heat against
	// its cooling (bike_build.gd _compute_engine).
	E.StockRedline = E.Redline;
	const float CoolingHealth = PartIn(B, Bay, TEXT("cooling")).Health;
	B.WithTune(Bay, [&E, &Mult, CoolingHealth](const TorqueFit::BikeTune& T) {
		const TorqueFit::BikeTune::Engine Tn = T.EngineTune();
		Mult *= (float)Tn.TorqueMult;
		// Hot heads need cooling to match (a lean map runs it hotter).
		std::string Hot;
		Mult *= (float)T.CoolingMult(CoolingHealth, &Hot);
		if (!Hot.empty()) E.Notes.Add(UTF8_TO_TCHAR(Hot.c_str()));
		E.TuneLow = (float)Tn.Low;
		E.TuneHigh = (float)Tn.High;
		E.Stress = (float)Tn.Stress;
		E.Redline += (float)Tn.Limiter;
		for (const std::string& N : Tn.Notes) E.Notes.Add(UTF8_TO_TCHAR(N.c_str()));
	});
	E.TorqueMult = Mult;
	E.PeakTorque *= Mult;
	E.Drivetrain = Drivetrain * Lerp(0.8f, 1.0f, Gearbox.Health) * Lerp(0.88f, 1.0f, Final.Health);
	E.Gears = FMath::Max(1, FMath::RoundToInt(Field(Gearbox, TEXT("ratios_count"), 6.0f)));
	return E;
}

float EngineTorque(const FTorqueEngineNumbers& E, float Rpm)
{
	if (!E.bValid || Rpm > E.Redline * 1.02f) return 0.0f;
	const float X = (Rpm - E.PeakTorqueRpm) / FMath::Max(1.0f, E.PeakTorqueRpm);
	// Past its peak the torque fades faster than it builds, so power peaks a little short of the limiter.
	const float Shape = 1.0f - (X < 0.0f ? 0.7f : TorqueFade) * X * X;
	// The tune: a rich map fattens the torque below the peak, ignition advance sharpens the top end.
	const float Tune = X < 0.0f ? 1.0f + E.TuneLow * FMath::Min(-X * 2.0f, 1.0f) : 1.0f + E.TuneHigh * FMath::Min(X * 2.0f, 1.0f);
	return E.PeakTorque * FMath::Clamp(Shape, 0.3f, 1.0f) * Tune;
}

float PeakHp(const FTorqueEngineNumbers& E, float* OutRpm)
{
	float Best = 0.0f, BestRpm = 0.0f;
	for (float Rpm = E.IdleRpm; Rpm <= E.Redline; Rpm += 50.0f)
	{
		const float Kw = EngineTorque(E, Rpm) * Rpm * UE_TWO_PI / 60.0f / 1000.0f;
		if (Kw > Best) { Best = Kw; BestRpm = Rpm; }
	}
	if (OutRpm) *OutRpm = BestRpm;
	return Best * 1.341f;
}

FTorqueDynoRun DynoRun(const FTorqueEngineNumbers& E, int32 Samples)
{
	FTorqueDynoRun R;
	R.Redline = E.Redline;
	R.IdleRpm = E.IdleRpm;
	R.Gears = E.Gears;
	R.Mass = E.Mass;
	const float Lo = FMath::Max(E.IdleRpm * 1.5f, E.Redline * 0.2f);
	for (int32 I = 0; I <= Samples; ++I)
	{
		const float Rpm = FMath::Lerp(Lo, E.Redline, float(I) / Samples);
		const float Tq = EngineTorque(E, Rpm) * E.Drivetrain;
		R.Rpm.Add(FMath::RoundToFloat(Rpm));
		R.Nm.Add(Tq);
		R.Hp.Add(Tq * Rpm * UE_TWO_PI / 60.0f / 1000.0f * 1.341f);
	}
	for (float Rpm = Lo; Rpm <= E.Redline; Rpm += 25.0f)
	{
		const float Tq = EngineTorque(E, Rpm) * E.Drivetrain;
		const float P = Tq * Rpm * UE_TWO_PI / 60.0f / 1000.0f * 1.341f;
		if (P > R.PeakHp) { R.PeakHp = P; R.PeakHpRpm = Rpm; }
		if (Tq > R.PeakNm) { R.PeakNm = Tq; R.PeakNmRpm = Rpm; }
	}
	return R;
}

float DynoTemp(float Heat) { return 85.0f + Heat * 45.0f; }

void HeatRun(FTorqueDynoRun& Run, float Heat)
{
	Run.Heat = Heat;
	Run.TempC = FMath::RoundToFloat(DynoTemp(Heat));
	if (Heat <= 0.05f) return;
	const int32 N = Run.Hp.Num();
	float BestHp = 0.0f, BestNm = 0.0f;
	for (int32 I = 0; I < N && I < Run.Nm.Num() && I < Run.Rpm.Num(); ++I)
	{
		const float Fade = 1.0f - HeatPower * Heat * (0.3f + 0.7f * float(I) / FMath::Max(N - 1, 1));
		Run.Hp[I] *= Fade;
		Run.Nm[I] *= Fade;
		if (Run.Hp[I] > BestHp) { BestHp = Run.Hp[I]; Run.PeakHpRpm = Run.Rpm[I]; }
		if (Run.Nm[I] > BestNm) { BestNm = Run.Nm[I]; Run.PeakNmRpm = Run.Rpm[I]; }
	}
	Run.PeakHp = BestHp;
	Run.PeakNm = BestNm;
}

float RunTime(const FTorqueDynoRun& Run)
{
	return IdleT + QuickT * (MeasureGear(Run) - 1) + SettleT + SweepT + CoastT;
}

FSweep SweepState(const FTorqueDynoRun& Run, float T)
{
	FSweep S;
	const float Lo = FMath::Max(Run.IdleRpm * 1.5f, Run.Redline * 0.2f);
	if (T < IdleT)
	{
		S.Rpm = Run.IdleRpm * (1.0f + 0.8f * FMath::Sin(T / IdleT * UE_PI));
		S.Throttle = 0.35f;
		return S;
	}
	T -= IdleT;
	const int32 Quick = MeasureGear(Run) - 1;
	if (T < Quick * QuickT)
	{
		const int32 G = int32(T / QuickT);
		const float F = FMath::Fmod(T, QuickT) / QuickT;
		const float From = G > 0 ? Run.Redline * 0.55f : Run.IdleRpm * 1.5f;
		S.Rpm = FMath::Lerp(From, Run.Redline * 0.95f, F);
		S.Gear = G + 1;
		S.Throttle = 1.0f;
		return S;
	}
	T -= Quick * QuickT;
	S.Gear = MeasureGear(Run);
	if (T < SettleT)
	{
		S.Rpm = FMath::Lerp(Run.Redline * (Quick > 0 ? 0.6f : 0.3f), Lo, T / SettleT);
		S.Throttle = 0.15f;
		S.Progress = 0.0f;
		return S;
	}
	T -= SettleT;
	if (T < SweepT)
	{
		const float F = T / SweepT;
		S.Rpm = FMath::Lerp(Lo, Run.Redline, F);
		S.Throttle = 1.0f;
		S.Progress = F;
		return S;
	}
	T -= SweepT;
	const float F = FMath::Clamp(T / CoastT, 0.0f, 1.0f);
	S.Rpm = FMath::Lerp(Run.Redline, Run.IdleRpm, F);
	S.Progress = 1.0f;
	return S;
}
}
