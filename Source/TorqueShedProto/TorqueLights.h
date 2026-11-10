// The bike's lamps while riding, as Torque Shed has them (bike.gd's beam and _step_lamps, bike_lamps.gd,
// bike_effects.gd's beam, bike_model.gd's set_lights and set_signals), each on its own switch:
//
//  - the headlight (dipped beam), on with the ignition as a modern bike's is, L to switch it off and on;
//  - main beam, K: further and narrower, aimed level (it puts the headlight on if it was off);
//  - the fog lights, N, on a bike that has them fitted (they need the headlight on, as on the road);
//  - the indicators, , and . (the < and > keys): the same side again cancels, the other side swaps,
//    and like real self-cancelling ones they go off by themselves once the turn's done (the bike has
//    leant or turned that way and come back upright); / for the hazard lights, both sides at once;
//  - the rear light, dim with the headlight on and bright while either brake is on, with a small red
//    lamp shining back onto the road behind.
//
// Everything runs off the ignition: a bike that isn't running is dark. The lamps are the bike's own
// lenses as TorqueShedCatalogue::MakeBike drew them (its "Surf:lens_white / lens_amber / lens_red"
// surfaces), lit through their material's Emissive, with Unreal lights at the headlight, the fog
// lamps, each indicator and the rear light. Other riders' bikes show the same, from their snapshots
// (NetBits / FromNet), the flash keeping its own time.
#pragma once

#include "CoreMinimal.h"

#include <vector>

class AActor;
class UStaticMeshComponent;
class USceneComponent;
class USpotLightComponent;
class UPointLightComponent;

struct FTorqueLights
{
	// --- The switches.
	bool bHeadlight = true;   // dipped beam (Torque Shed's beam 1: a bike leaves the shed with it on)
	bool bMainBeam = false;   // main (high) beam
	bool bFog = false;        // the fog lights
	int32 Side = 0;           // the indicators: -1 left, 1 right, 0 off
	bool bHazard = false;     // both sides flashing (no self-cancel)
	bool bBraking = false;    // the stop lamp
	bool bIgnition = true;    // all of it needs the engine running

	// --- From the parts (Setup) and the drawn bike (Hook).
	double HeadLumens = 0.0, FogLumens = 0.0;   // parts.json's (0: none fitted)
	FLinearColor FogColour = FLinearColor(1.0f, 0.92f, 0.78f);
	FLinearColor BeamColour = FLinearColor(1.0f, 0.92f, 0.78f);   // warm halogen, or cool LED from 2000 lm

	bool HasHeadlight() const { return HeadLumens > 0.0 || HeadLenses.Num() > 0; }
	bool HasFog() const { return FogLumens > 0.0 && FogLenses.Num() > 0; }
	bool HasIndicators() const { return Indicators.Num() > 0; }

	// The headlight, fog light and horn-side parts the bike has (Godot slot -> catalogue id).
	void Setup(const TMap<FString, FString>& Parts);
	// Finds the lenses among the bike's meshes and puts the lights on them. Call once the meshes are on
	// their groups (the headlight turns with the bars). bShadows: the headlight casts shadows (our own
	// bike; others' bikes' lamps don't, to keep co-op cheap).
	void Hook(AActor* Owner, const TArray<UStaticMeshComponent*>& Meshes, bool bShadows);

	// The switches as the keys work them. Each returns what to say on the HUD ("Main beam on",
	// "No fog lights on this bike"); bOk false when it couldn't.
	FString ToggleHeadlight(bool& bOk);
	FString ToggleMainBeam(bool& bOk);
	FString ToggleFog(bool& bOk);
	FString Indicate(int32 Which, bool& bOk);   // -1 left, 1 right
	FString ToggleHazard(bool& bOk);
	// The pad's one lights button (beenster): round dipped, main beam, fog lights (with dipped beam,
	// on a bike that has them), off, and dipped again.
	FString CycleBeam(bool& bOk);

	// One frame: the ignition, the brakes (0..1 each), the lean (radians, + right) and the turn
	// (rad/s, + turning right) for the self-cancel, then the lamps shown. bSelfCancel false for someone
	// else's bike (its own machine cancels it and sends the switch). DayLight 0 at night .. 1 by day:
	// the lenses glow less into a dark-adapted eye. Returns 1 when a flash came on (the relay's tick),
	// -1 when one went off (its tock), else 0.
	int32 Step(double Dt, bool bEngineRunning, double FrontBrake, double RearBrake, double Lean, double TurnRate,
		bool bSelfCancel, float DayLight);

	// Whether the indicators flashing now are in the lit half of the flash.
	bool FlashLit() const;
	bool LeftLit() const { return bIgnition && (bHazard || Side < 0) && FlashLit(); }
	bool RightLit() const { return bIgnition && (bHazard || Side > 0) && FlashLit(); }
	bool Indicating() const { return bIgnition && (bHazard || Side != 0); }
	bool HeadOn() const { return bIgnition && bHeadlight && HasHeadlight(); }
	bool MainOn() const { return HeadOn() && bMainBeam; }
	bool FogOn() const { return HeadOn() && bFog && HasFog(); }

	// For TorqueShedTest's lights verb: the lamps shown now (not waiting for the next frame), and one
	// line on everything: the switches, what's lit, each lens's glow, the lights and their strength,
	// and the HUD's tell-tales as ATorqueHUD::DrawTellTales would draw them.
	void ShowNow(float DayLight) { Shown = -1; Show(DayLight); }
	FString Describe() const;
	bool bTestBrake = false;   // the stop lamp held on for the test

	// The switches for a snapshot (a byte), and someone's snapshot taken in.
	uint8 NetBits() const;
	void FromNet(uint8 Bits);

	// The relay's tick (or the lower tock), 35 ms at 22050 Hz (bike_lamps.gd click).
	static const std::vector<float>& Click(bool bTick);
	static constexpr int32 ClickRate = 22050;

private:
	void SetSide(int32 Which);
	void Show(float DayLight);
	double FlashT = 0.0;      // seconds since this side started flashing
	bool bTurning = false;    // the bike has made a start on the turn it's indicating for
	double Turned = 0.0;      // how far it has turned that way since (radians)

	struct FLens
	{
		TWeakObjectPtr<UStaticMeshComponent> Mesh;
		float Lit = -1.0f;    // the Emissive last set
	};
	TArray<FLens> HeadLenses, FogLenses, TailLenses, Indicators;
	TArray<int8> IndicatorSide;   // -1 left, 1 right, per Indicators
	TWeakObjectPtr<USpotLightComponent> Beam, Flood, StopGlow;
	TArray<TWeakObjectPtr<USpotLightComponent>> FogBeams;
	TArray<TWeakObjectPtr<UPointLightComponent>> IndicatorGlow;   // per Indicators
	int32 Shown = -1;         // the lamps' state last shown (a bit each), so a steady frame does nothing
	float ShownDay = -1.0f;
	void SetLens(FLens& L, float Emissive);
};
