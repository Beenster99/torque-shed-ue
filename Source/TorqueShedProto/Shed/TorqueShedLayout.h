// Where things are in the shed, as Tools/ue_build_shed.py builds it (a port of Torque Shed's
// workshop.gd and the machines shed_state.gd's start_new puts in): world cm, x east, y towards the
// roller door, z up, the floor at 0. Keep the two in step.
//
// The places a part can be put (shed_state.gd's spots and surfaces): along the workbench, on the
// repair bench and in the parts washer, on the tyre changer and the engine stand. Anything else
// goes on the floor where you put it. And the ten hooks in the paint shop's spray bay.
#pragma once

#include "CoreMinimal.h"

enum class ETorquePlace : uint8
{
	Bench,     // the workbench down the west wall
	Repair,    // the repair bench (grinder and welder): restore parts on it
	Washer,    // the parts washer
	Changer,   // the tyre changer: wheels only
	Stand,     // the engine stand: engines only
	Bin,       // the three bins left of the roller door
	Computer,  // the parts computer on the east wall
	Bay,       // a bay's floor (a frame put down here starts a bike)
	DynoStrap, // one of the dyno rig's four strap posts (Index 0 .. 3): hold the work button to strap it
	DynoDesk,  // the dyno desk: run the bike on the rig
	DynoSwitch,// the dyno room's switches by the door (Index 0 the lights, 1 the fan)
	Hook,      // a hook in the paint shop's spray bay (Index 0 .. 9): a part hung here is sprayed with the gun
	PaintBench,// the paint mixing bay's bench (F: step up to it, or put the tin in your hands on the scale)
	PaintShelf,// a place on the mixing bay's tin shelf (Index 0 .. 11): F takes a tin or puts one down
	Radio,     // the shed radio over the workbench: F switches it on or off, the work button retunes it
	Tape,      // the masking tape roll's hook inside the mixing bay door: take it, hang it back
	Lift,      // a bike lift's control post (Index: the lift, a bay or UTorqueBuildSubsystem::WashLift): F raises or lowers it
	// [kit] (ATorqueShedKit)
	ShedSwitch,   // the switch boxes right of the roller door (Index 0 the main lights, 1 the door's up / down)
	Extinguisher, // the fire extinguisher by the workbench: F takes it, F hangs it back
	// [fluids] (ATorqueShedPump)
	Pump,         // the petrol pump on the front wall outside: F lifts the nozzle off it, F hangs it back
	// [tv] (ATorqueShedScreens)
	Tv,           // a big wall TV (Index: TorqueShedTv, 0 .. 3 the back wall's, 4 over the workbench): F changes its channel
};

struct FTorquePlace
{
	ETorquePlace Kind = ETorquePlace::Bench;
	FName Name;          // spot name ("bench_3", "repair_1"), bin ("general") or bay ("bay_2")
	FBox Box;            // what the walker points at
	FVector Put = FVector::ZeroVector;   // where a part put here sits (its bottom, centred)
	int32 Index = 0;     // the bay, for a bay
};

namespace TorqueShedLayout
{
// The bays (Workshop.bay_transform): along the back wall, 3.2 m apart, noses to the door.
inline FVector BayAt(int32 Bay)
{
	static const double X[] = {-480.0, -160.0, 160.0, 480.0};
	return FVector(X[FMath::Clamp(Bay, 0, 3)], -350.0, 0.0);
}

// The bike lifts (workshop_upgrades.gd _build_lift, wash.gd LIFT): one under each bay's paddock
// stand, and the wash bay's narrow runway over its drain. A lift's frame: x along the bike, nose
// first, y to its right, its origin on the floor under the bike's middle. Lift ids are the bays
// (0 .. 3) and WashLiftId (UTorqueBuildSubsystem::WashLift).
constexpr int32 WashLiftId = 7;
inline FTransform WashLiftXf() { return FTransform(FRotator(0.0, -90.0, 0.0), FVector(-930.0, 100.0, 0.0)); }   // facing the back wall
inline FTransform LiftXf(int32 Lift)
{
	if (Lift == WashLiftId) return WashLiftXf();
	return FTransform(FRotator(0.0, 90.0, 0.0), BayAt(Lift));   // UTorqueBuildSubsystem::HomeOf
}
// Its control post (in the lift's frame): behind it and to one side, as Torque Shed's. East of
// the bike (-y here), but bay 2's on its west, clear of the dyno room's doorway (bay 1's is clear
// of the storage room's door that way): world (-355, -510), (-285, -510), (285, -510), (605, -510),
// and the wash bay's at (-1055, 260).
inline FVector LiftPostLocal(int32 Lift) { return FVector(-160.0, Lift == 1 ? 125.0 : -125.0, 0.0); }
inline FVector LiftPostAt(int32 Lift) { return LiftXf(Lift).TransformPosition(LiftPostLocal(Lift)); }
// The lifts' platform: long enough for the longest bike the parts make (the long-rake chopper,
// 1.92 m between its axles: its front tyre in the vice and its back tyre still on the table;
// SimTest/spec_test checks every frame), and the ramp hinged on its back.
constexpr double LiftDeckLength = 260.0;
constexpr double LiftRampLength = 40.0;
// Whether a bike let go with its middle at At is going onto lift Lift: anywhere over its platform
// or ramp (or just in front), not off to either side. It's then wheeled square onto it.
inline bool OntoLift(int32 Lift, const FVector& At)
{
	const FVector Local = LiftXf(Lift).InverseTransformPosition(FVector(At.X, At.Y, 0.0));
	const double Half = LiftDeckLength * 0.5;
	return Local.X > -(Half + LiftRampLength + 30.0) && Local.X < Half + 30.0 && FMath::Abs(Local.Y) < 75.0;
}

// The spray bay's hooks: where a part on hook n hangs from (its top, centred), and the lowest a
// hung part's bottom comes (workshop.gd HOOK_TIP, HOOK_FLOOR).
constexpr int32 HookCount = 10;
constexpr double HookTip = 138.0;
constexpr double HookFloor = 12.0;
inline FVector HookAt(int32 N)
{
	const int32 I = FMath::Clamp(N, 0, HookCount - 1);
	return FVector(I < 5 ? 1270.0 : 1050.0, -300.0 + 150.0 * (I % 5), HookTip);
}

// Every place, made once.
inline const TArray<FTorquePlace>& Places()
{
	static TArray<FTorquePlace> L;
	if (L.Num() > 0) return L;
	auto Add = [](ETorquePlace Kind, const FString& Name, const FVector& Min, const FVector& Max, const FVector& Put, int32 Index)
	{
		FTorquePlace P;
		P.Kind = Kind;
		P.Name = FName(*Name);
		P.Box = FBox(Min, Max);
		P.Put = Put;
		P.Index = Index;
		L.Add(P);
	};
	// The workbench: x -647.5 .. -552.5, y -180 .. 300, top 95. Eight places along it.
	for (int32 I = 0; I < 8; ++I)
	{
		const double Y = -150.0 + 60.0 * I;
		Add(ETorquePlace::Bench, FString::Printf(TEXT("bench_%d"), I), FVector(-647.5, Y - 30.0, 89.0), FVector(-552.5, Y + 30.0, 97.0),
			FVector(-595.0, Y, 95.0), I);
	}
	// The repair bench against the front wall: x 250 .. 430, y 445 .. 535, top 94. Three places.
	for (int32 I = 0; I < 3; ++I)
	{
		const double X = 280.0 + 55.0 * I;
		Add(ETorquePlace::Repair, FString::Printf(TEXT("repair_%d"), I), FVector(X - 27.0, 445.0, 88.0), FVector(X + 27.0, 535.0, 96.0),
			FVector(X, 480.0, 94.0), I);
	}
	// The parts washer: x 480 .. 610, y 445 .. 535, the tub's rim at 95. Two places.
	for (int32 I = 0; I < 2; ++I)
	{
		const double X = 515.0 + 60.0 * I;
		Add(ETorquePlace::Washer, FString::Printf(TEXT("washer_%d"), I), FVector(X - 30.0, 445.0, 85.0), FVector(X + 30.0, 515.0, 97.0),
			FVector(X, 475.0, 80.0), I);
	}
	// The tyre changer's table past the bench's end, and the engine stand by the east bay.
	Add(ETorquePlace::Changer, TEXT("changer"), FVector(-634.0, 325.0, 66.0), FVector(-574.0, 385.0, 80.0), FVector(-604.0, 355.0, 77.0), 0);
	Add(ETorquePlace::Stand, TEXT("stand"), FVector(-25.0 + 560.0, -55.0, 55.0), FVector(565.0 + 20.0, -25.0, 90.0), FVector(560.0, -40.0, 82.0), 0);
	// The bins left of the roller door, y 487 .. 547, 92 high.
	static const TCHAR* const BinNames[] = {TEXT("general"), TEXT("plastic"), TEXT("metal")};
	static const double BinX[] = {-277.0, -337.0, -397.0};   // 60 cm apart: the wheel hubs are 59 cm across
	for (int32 I = 0; I < 3; ++I)
		Add(ETorquePlace::Bin, BinNames[I], FVector(BinX[I] - 28.0, 483.0, 0.0), FVector(BinX[I] + 28.0, 549.0, 92.0),
			FVector(BinX[I], 517.0, 92.0), I);
	// The parts computer's desk and monitor on the east wall.
	Add(ETorquePlace::Computer, TEXT("computer"), FVector(565.0, 125.0, 74.0), FVector(625.0, 255.0, 131.0), FVector(605.0, 190.0, 78.0), 0);
	// The dyno room (dyno_room.gd, through the back wall): the rig's strap posts round (0, -940), the
	// desk at (290, -1110), the switches by the door. Keep in step with ue_build_shed.py dyno_room.
	static const double PostX[] = {-95.0, 95.0, -95.0, 95.0}, PostY[] = {-1000.0, -1000.0, -860.0, -860.0};
	for (int32 I = 0; I < 4; ++I)
		Add(ETorquePlace::DynoStrap, FString::Printf(TEXT("dyno_strap_%d"), I), FVector(PostX[I] - 17.0, PostY[I] - 17.0, 0.0),
			FVector(PostX[I] + 17.0, PostY[I] + 17.0, 70.0), FVector(PostX[I], PostY[I], 50.0), I);
	Add(ETorquePlace::DynoDesk, TEXT("dyno_desk"), FVector(215.0, -1145.0, 65.0), FVector(365.0, -1070.0, 145.0), FVector(290.0, -1110.0, 84.0), 0);
	// The radio on its shelf over the workbench's back end (TorqueShedRadio::At). Its box comes out
	// past the front of the props' radio model (which stops the look like a wall), so the look
	// reaches the radio and not the wall behind it.
	Add(ETorquePlace::Radio, TEXT("radio"), FVector(-650.0, -178.0, 158.0), FVector(-606.0, -112.0, 205.0), FVector(-638.0, -145.0, 161.5), 0);
	// The masking tape's hook (paint_bay.gd TAPE_AT, by the door): on the mixing bay's west wall just
	// inside its door (y 418 .. 502), the roll drawn by ATorquePaintBayRoom (TorquePaintBayLayout::TapeAt).
	Add(ETorquePlace::Tape, TEXT("tape"), FVector(1430.0, 380.0, 112.0), FVector(1450.0, 410.0, 148.0), FVector(1438.0, 395.0, 130.0), 0);
	// [kit] The switch boxes right of the roller door, inside (swaps.json light_switches at 285, 548,
	// 128, facing in): the light switch's plate 122 up, the door's yellow box with its buttons above
	// it. And the fire extinguisher standing by the end of the workbench (swaps.json dress at -630, 440).
	Add(ETorquePlace::ShedSwitch, TEXT("shed_lights"), FVector(277.0, 536.0, 114.0), FVector(293.0, 550.0, 129.0), FVector(285.0, 547.0, 122.0), 0);
	Add(ETorquePlace::ShedSwitch, TEXT("door_button"), FVector(277.0, 536.0, 129.0), FVector(293.0, 550.0, 146.0), FVector(285.0, 546.0, 136.0), 1);
	Add(ETorquePlace::Extinguisher, TEXT("extinguisher"), FVector(-652.0, 418.0, 0.0), FVector(-608.0, 462.0, 70.0), FVector(-630.0, 440.0, 0.0), 0);
	// [fluids] The petrol pump's cabinet on the front wall outside, west of the roller door
	// (UTorqueBuildSubsystem::PumpAt: -360, 580; the props' petrol_pump, 46 cm wide, its case 60 .. 150
	// up and 24 deep), the box out past its face and holster so the look finds it before the wall.
	Add(ETorquePlace::Pump, TEXT("petrol_pump"), FVector(-386.0, 578.0, 55.0), FVector(-322.0, 612.0, 158.0), FVector(-332.0, 597.0, 100.0), 0);
	// [tv] The big wall TVs (swaps.json bay_screen / tv_136; ue_build_shed.py BayScreen_n, CrewBoard):
	// over each bay on the back wall, 2.6 m x 1.46 m about 245 up, and over the workbench on the
	// west wall, 3.0 m x 1.69 m about 255 up. Each box runs from the wall to past the glass (9 to
	// 10 cm out), so the look finds the TV before the wall behind it.
	{
		static const double TvX[] = {-504.0, -232.0, 232.0, 504.0};
		for (int32 I = 0; I < 4; ++I)
			Add(ETorquePlace::Tv, FString::Printf(TEXT("tv_%d"), I), FVector(TvX[I] - 130.0, -550.0, 172.0), FVector(TvX[I] + 130.0, -535.0, 318.0),
				FVector(TvX[I], -541.0, 245.0), I);
		Add(ETorquePlace::Tv, TEXT("tv_4"), FVector(-650.0, -90.0, 170.0), FVector(-635.0, 210.0, 340.0), FVector(-640.0, 60.0, 255.0), 4);
	}
	Add(ETorquePlace::DynoSwitch, TEXT("dyno_lights"), FVector(-145.0, -590.0, 122.0), FVector(-125.0, -578.0, 148.0), FVector(-135.0, -585.0, 135.0), 0);
	Add(ETorquePlace::DynoSwitch, TEXT("dyno_fan"), FVector(-169.0, -590.0, 122.0), FVector(-149.0, -578.0, 148.0), FVector(-159.0, -585.0, 135.0), 1);
	// The paint shop's downdraft spray bay (workshop.gd BOOTH_XF, equipment.gd booth_shop): 8 m along
	// y (-400 .. 400), its open front at x 925 to the walkway, two rails of five hooks, at x 1270 (the
	// back) and 1050, every 150 cm along y. A hung part's top touches the hook's tip, 138 cm up unless
	// the hook's been raised or lowered (UTorqueBuildSubsystem::HookTipOf; ATorqueShedItems hangs it
	// from there, swinging with ATorqueBoothHooks). The box reaches the hook at any height. Keep in step with ue_build_shed.py paint_shop.
	for (int32 I = 0; I < HookCount; ++I)
	{
		const FVector At = HookAt(I);
		Add(ETorquePlace::Hook, FString::Printf(TEXT("hook_%d"), I), FVector(At.X - 25.0, At.Y - 67.5, 40.0), FVector(At.X + 25.0, At.Y + 67.5, 215.0),
			At, I);
	}
	// The paint mixing bay through the paint shop's east wall (paint_bay.gd BENCH, SHELF): the bench
	// down the east wall, x 1635 .. 1700, y -220 .. 180, top 92; the tin shelf beside it, four tins a
	// row at 45, 95 and 145 up. Keep in step with TorquePaintBayLayout and ue_build_shed.py paint_bay.
	Add(ETorquePlace::PaintBench, TEXT("paint_bench"), FVector(1635.0, -220.0, 84.0), FVector(1700.0, 180.0, 100.0), FVector(1662.0, -40.0, 92.0), 0);
	for (int32 I = 0; I < 12; ++I)
	{
		static const double ShelfZ[] = {45.0, 95.0, 145.0};
		const FVector At(1672.0, 260.0 + (I % 4 - 1.5) * 30.0, ShelfZ[FMath::Clamp(I / 4, 0, 2)]);
		Add(ETorquePlace::PaintShelf, FString::Printf(TEXT("paint_shelf_%d"), I), At + FVector(-16.0, -14.0, 0.0), At + FVector(16.0, 14.0, 18.0), At, I);
	}
	// The bike lifts' control posts (ATorqueBikeLift draws them): the box takes in the whole post.
	for (int32 I = 0; I < 4; ++I)
	{
		const FVector At = LiftPostAt(I);
		Add(ETorquePlace::Lift, FString::Printf(TEXT("lift_%d"), I), At + FVector(-20.0, -20.0, 40.0), At + FVector(20.0, 20.0, 120.0), At + FVector(0.0, 0.0, 110.0), I);
	}
	{
		const FVector At = LiftPostAt(WashLiftId);
		Add(ETorquePlace::Lift, TEXT("lift_wash"), At + FVector(-20.0, -20.0, 40.0), At + FVector(20.0, 20.0, 120.0), At + FVector(0.0, 0.0, 110.0), WashLiftId);
	}
	// Each bay's floor round the stand (a frame put down here starts a bike), up to its lift's
	// platform when that's down (ATorqueBikeLift::LowTop).
	for (int32 I = 0; I < 4; ++I)
	{
		const FVector B = BayAt(I);
		Add(ETorquePlace::Bay, FString::Printf(TEXT("bay_%d"), I), B + FVector(-140.0, -60.0, 0.0), B + FVector(140.0, 60.0, 20.0), B, I);
	}
	return L;
}

// [kit] The engine stand rolls (UTorqueBuildSubsystem::StandTransform, drawn by ATorqueShedKit):
// where it is now, and its place (what you point at, where an engine on it sits) moved with it.
// The stand's own frame: its origin on the floor under the engine plate, +y its front.
inline FTransform& StandNow()
{
	static FTransform X(FVector(560.0, -40.0, 0.0));
	return X;
}
inline void MoveStand(const FTransform& Xf)
{
	StandNow() = Xf;
	for (const FTorquePlace& C : Places())
		if (C.Kind == ETorquePlace::Stand)
		{
			FTorquePlace& P = const_cast<FTorquePlace&>(C);
			P.Put = Xf.TransformPosition(FVector(0.0, 0.0, 82.0));
			P.Box = FBox(FVector(-35.0, -30.0, 20.0), FVector(35.0, 30.0, 95.0)).TransformBy(Xf);
		}
}

inline const FTorquePlace* FindPlace(FName Name)
{
	for (const FTorquePlace& P : Places())
		if (P.Name == Name) return &P;
	return nullptr;
}

// The deliveries bay on the yard east of the door (Shed.DELIVERY_AREA): x 290 .. 1070, y 750 ..
// 1065 is its floor. Boxes go down on a grid there, the nearest the door first.
inline FVector DeliveryAt(int32 Index)
{
	const int32 Cols = 7;
	const int32 Col = Index % Cols, Row = (Index / Cols) % 3, Layer = Index / (Cols * 3);
	return FVector(345.0 + 110.0 * Col, 810.0 + 100.0 * Row, 60.0 * Layer);
}

inline bool InDeliveries(const FVector& At) { return At.X > 280.0 && At.X < 1080.0 && At.Y > 740.0 && At.Y < 1260.0; }

// The wash bay (Workshop.WASH_CENTER, WASH_HALF and the apron in front): the pressure washer's
// lance reaches anything in it.
inline bool InWashBay(const FVector& At) { return At.X > -1170.0 && At.X < -690.0 && At.Y > -240.0 && At.Y < 540.0; }

// Inside the shed, the yard and the wash bay: where a part can be put down.
// (And out on the riding yard's pad, where a bike ridden out is parked: Tools/shed_yard.py's PAD, 600 m
// across and 1.6 km out from the door.)
inline bool OnSite(const FVector& At)
{
	const bool bShed = FMath::Abs(At.X) < 4000.0 && FMath::Abs(At.Y) < 4000.0;
	const bool bPad = FMath::Abs(At.X) < 30000.0 && At.Y > 0.0 && At.Y < 160580.0;
	return (bShed || bPad) && At.Z > -50.0 && At.Z < 500.0;
}

// Where a player stands when they come in, for parts dropped by someone who's left.
inline FVector DropAt() { return FVector(0.0, 180.0, 0.0); }
}
