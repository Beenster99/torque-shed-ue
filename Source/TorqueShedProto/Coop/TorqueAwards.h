// [ride awards] The end-of-ride awards on screen (beenster, Nov 19 2026): Settings > Gameplay > Co-op >
// Ride awards (the host's choice, off unless turned on).
//
// While riding, each rider's game counts their numbers (Coop/RideAwards.h: top speed, wheelies, offs,
// air time...) and sends them to the host every second (ATorquePlayerState::AwardStats). A rider who
// stops at a planned ride's finish says so; once everyone has (or the host picks "Ride awards now" in
// the pause menu), the host starts the awards (ATorqueGameState::StartAwards):
//  1. The vote, before any numbers are shown: each rider picks the three things they care about most,
//     in order (1st, 2nd, 3rd). Nobody sees anyone's picks, only a tick once they've voted. When everyone
//     has, a three second countdown; a minute at most. Alone, there's no vote.
//  2. The dashboard: a tile for each stat, each rider's bar in their colour, the leader's number big,
//     and how many picked it. The route, distance, time and weather along the top.
//  3. The podium: the top three riders (two with two), scored by the votes (RideAwards.h).
// Nothing is kept afterwards: no history, no stats page. Plain C++ owned by the player controller,
// drawn with TorqueUi like the Route planner.
#pragma once

#include "CoreMinimal.h"

class APlayerController;
class UCanvas;
class UObject;
struct FTorqueDash;
namespace TorqueSim
{
class BikeSim;
}

namespace TorqueAwardsUe
{
// A new ride (ATorqueRideWorld::Begin): this machine's rider's numbers start again.
void NewRide();
// The rider's own bike, every frame (ATorqueBikePawn::Tick): counts the numbers, sends them to the host
// every second while the awards are on, and says when the rider has stopped at the planned finish.
void StepBike(UObject* Bike, const TorqueSim::BikeSim& Sim, const FTorqueDash& Dash, bool bOnFoot, float Dt);
// The results of awards Seq are in: the numbers start again for the rest of the ride (once per Seq).
void ResultsIn(int32 Seq);
}

class FTorqueAwardsScreen
{
public:
	// A new awards: its vote (or straight to the results when there's no vote).
	void Begin(int32 InSeq);
	// The keys, pad and mouse, each frame it is up; false once it's closed (after the podium).
	bool Tick(APlayerController* PC, uint32 SonyHit);
	void Draw(UCanvas* Canvas, APlayerController* PC);

private:
	enum class EView : uint8 { Vote, Dashboard, Podium };
	EView View = EView::Vote;
	int32 Seq = -1;
	TArray<int32> Picks;   // stats, in pick order (up to three)
	int32 Hot = 0;         // the tile lit for the pad and the keys
	int32 SentFor = -1;    // the picks last sent (a hash), so a change is sent once
	double ViewAt = 0.0;   // when the view was opened (the dashboard's bars, the podium's reveal)
	FVector2D Mouse = FVector2D(-1.0, -1.0), LastMouse = FVector2D(-1.0, -1.0);
	TArray<FBox2D> TileBoxes;   // where the last drawing put each stat's tile
	int32 StickX = 0, StickY = 0;
	void Pick(int32 Stat);
	void SendPicks(APlayerController* PC);
	void SetView(EView V);
	void DrawVote(UCanvas* Canvas, APlayerController* PC);
	void DrawDashboard(UCanvas* Canvas, APlayerController* PC);
	void DrawPodium(UCanvas* Canvas, APlayerController* PC);
	// [podium finale] The reveal's clock (seconds since the podium opened): each place's moment, when it all
	// ends (Enter before then skips to the end), and the chimes already played this showing.
	static double RevealAt(int32 Place, int32 Steps);
	static double PodiumEnd(int32 Steps);
	int32 PodiumChimes = 0;
	void PodiumSounds(APlayerController* PC, int32 Steps);
};
