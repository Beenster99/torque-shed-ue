// The dyno room's desk, as Torque Shed draws it (tuning_computer.gd, "DYNO & TUNING"): tune the bike
// on the rig, or any bike close by, and run it on the dyno. A page over the screen worked with the
// mouse: the bike picked along the top; down the left the tabs (Engine: fuel map, ignition, rev
// limiter; Suspension: the clickers the fork, shock and steering damper have; Tyres & chain:
// pressures and slack; Gearing: sprockets and, with a cassette gearbox, the ratio spacing; Rider
// aids: those the ECU has, off or a level each; Weight: parts drilled and trimmed for lightness),
// each setting a slider with its reading, why it can't move, and Reset; on the right the dyno's
// graph (the last run dashed behind the latest) and Run the dyno; under the tabs what the bike makes
// now. Back to stock puts every setting back.
//
// Every change is a shed request (ETorqueShedAct::Tune, Lighten, DynoRun), so the whole crew sees
// the same tune and riding uses it (the riding spec reads it: Fit/BikeRideSpec, TorqueShedPower).
// Drawn on the HUD's canvas with the game's UI kit (Coop/TorqueUi); the walker hands it the mouse,
// keys and pad. The pad: L1 / R1 the tabs, up / down a setting, left / right moves it, Square puts it
// back, Cross runs the dyno.
//
// Not ported: the Last ride tab (the ride's telemetry: the UE game doesn't record it yet), the
// dropdown of bikes (the arrows step through those in reach), the Dyno Reader and Fine Tuning skills.
#pragma once

#include "CoreMinimal.h"
#include "Shed/TorqueBuildSubsystem.h"

class AHUD;
class UCanvas;

// What the page asks for: the shed requests to send, and whether to walk away.
struct FTorqueDeskOut
{
	bool bClose = false;
	TArray<FTorqueShedRequest> Requests;
};

class FTorqueDynoDesk
{
public:
	static constexpr float ReachCm = 1000.0f;   // tuning_computer.gd REACH: bikes this close can be tuned
	static constexpr double SendEvery = 0.12;   // seconds between a dragged slider's updates

	// Opened at the desk (From: where you stand): the bike on the rig, else the one picked before if
	// it's still in reach, else the nearest.
	void Open(const UTorqueBuildSubsystem* Shed, const FVector& From);
	// What's held back sent (a slider let go as it closes).
	FTorqueDeskOut Close();
	void Draw(AHUD* Hud, UCanvas* Canvas, const UTorqueBuildSubsystem* Shed, const FVector2D& Mouse, const FString& Status, bool bStatusOk);
	// The mouse: a click, the left button held (a slider dragged) and the wheel over the settings.
	FTorqueDeskOut Click(const FVector2D& Mouse);
	FTorqueDeskOut Hold(const FVector2D& Mouse, bool bLeftDown);
	void Scroll(int32 Dir);
	// The pad (and the keys): Row -1 / +1 the setting, Move -1 / +1 the value a step, Tab -1 / +1.
	FTorqueDeskOut Pad(int32 RowStep, int32 MoveStep, int32 TabStep, bool bReset, bool bRun);

	int32 BikeBay() const { return Bay; }
	// For the test steps: a tab by name ("Engine", "Weight"), a setting moved or put back, Run.
	bool PickTab(const FString& Name);
	FTorqueDeskOut TestSet(const FString& Key, float Value, bool bReset);
	FString Describe(const UTorqueBuildSubsystem* Shed) const;   // the page in a line, for the test log

private:
	enum class EHit : uint8 { Close, Stock, BikePrev, BikeNext, Tab, Slider, Reset, Run, Lighten };
	struct FHit { FBox2D Box; EHit What; int32 Arg; };
	struct FRow { FString Key, Name; double Step = 0.1; };

	const UTorqueBuildSubsystem* Shed = nullptr;   // the last drawn, for the clicks' readings
	FVector From = FVector::ZeroVector;
	int32 Bay = INDEX_NONE;
	int32 Tab = 0;
	int32 Focus = 0;                 // the setting the pad has
	int32 RowScroll = 0;
	TArray<FRow> Rows;               // the tab's settings shown (aids the ECU hasn't got left out)
	TArray<int32> LightenItems;      // the Weight tab's parts, by item id
	TArray<FHit> Hits;
	FString Dragging;                // the slider held
	FBox2D DragTrack = FBox2D(ForceInit);
	double DragLo = 0.0, DragHi = 1.0, DragStep = 0.1;
	TMap<FString, float> Pending;    // moved, waiting to go
	TMap<FString, float> Shown;      // sent: shown as set till the shed has it (a client waits on the host)
	TMap<FString, double> ShownUntil;
	double LastSend = 0.0;
	// What the bike makes now (a riding spec worked out: kept a moment rather than every frame).
	FString SummaryText;
	TArray<FString> SummaryNotes;
	double SummaryAt = -1.0;
	int32 SummaryBay = INDEX_NONE;

	TArray<int32> InReach(const UTorqueBuildSubsystem* B) const;
	void Relist(const UTorqueBuildSubsystem* B);
	void PickBike(int32 Step);
	FTorqueShedRequest TuneRequest(const FString& Key, float Value, bool bReset) const;
	void Flush(FTorqueDeskOut& Out);
	// A setting's value as the page shows it: being dragged or sent, else the shed's.
	bool ValueOf(const FString& Key, double& Out) const;
	const FHit* HitAt(const FVector2D& Mouse) const;
};
