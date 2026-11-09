// Hands-off tests of the shed, for runs on the PC with nobody at the controls:
//   -TorqueShedAction=loop
// runs Torque Shed's loop end to end on bay 1's bike (see LoopSteps in the .cpp): unbolt the
// exhaust with the wrench, take it off, restore it on the repair bench, spray it, pressure wash it
// in the wash bay, put it on the workbench, order a slip-on from the parts computer, unpack the
// box, fit it and bolt it up, ride out (Buttertubs, showing the slip-on) and come back. Or give the
// steps yourself, separated by commas. Each runs about two seconds after the last (four after a
// map has loaded), through the walker's own targeting, plan and request, as a player's would, and
// logs "Torque test: ..." lines:
//   loop                  the steps above
//   fab                   a scratch build that needs fabrication: a steel backbone frame set up in bay 3,
//                         finished as an mx with a 660 triple and no fork, a USD sport fork fitted (a
//                         job owed), the grinder and welder making it (fabricate), and a ride out on it.
//                         loop and fab can sit anywhere in a list; alone, they quit at the end
//   finish[:<style>][:<slot>=<part>|none]...  (a test shortcut, not a player's move) the bay's catalogue
//                         bike's empty slots filled from a style's reference bike (its own style if none
//                         is given) with those slots changed, and everything bolted up
//   fabricate             the repair tool on the frame once for each job the bike owes
//   dyno                  the dyno room on bay 1's bike: wheel it onto the rig, strap it at the four posts,
//                         a run with the fan on and one with it off, the big screen mid-run and after,
//                         the straps undone and the bike wheeled home (alone, it quits at the end)
//   booth                 the paint shop's spray bay on bay 1's bike: its exhaust restored, hung on a hook and
//                         sprayed with the gun (primer, gloss red, clear lacquer), the gun refused on the
//                         tank (off the hooks), the can on it, and the exhaust back on (alone, it quits at the end)
//   bay:<n>               the steps after work on bay n's bike (1-4; 1 to start with)
//   wheel:rig|home        take hold of the bay's bike, walk it onto the dyno's rig or back to its bay, let go
//   strap:<0-3>           the work button at that strap post on the rig (does it up, or undoes it)
//   fan:on|off, lights:on|off, switch:fan|lights   the dyno room's switches by the door (F)
//   dynorun[:mid]         F at the dyno desk and wait out the run (mid: about 60% of it, for a shot)
//   dynolog               log every run on the rig's bike (peak hp and Nm, limiter, temperature, fan)
//   tool:<tool>           hands, wrench, brush, repair, spray, lance or gun (the spray gun)
//   crouch:<0|1>          crouch (C) or stand
//   spray:<colour>[:<load>]  the spray can's or gun's paint: 0-15 or a name (Red, Teal...); gloss, matte, metallic, chrome,
//                         patina, primer, or a coat: clear (the can too), satin_clear, enamel_black, enamel_silver (the gun)
//   part:<slot>           stand by the bay's bike and look at that slot's part (exhaust, tank, front_wheel...)
//   item:<what>           look at the newest part off the bikes that is <what>: a part id, a kind or a slot
//   place:<name>          look at a place: bench_0-7, repair_0-2, washer_0-1, changer, stand, general,
//                         plastic, metal (the bins), computer, bay_0-3, hook_0-9 (the spray bay), dyno_strap_0-3, dyno_desk
//   floor:<x>:<y>         look at the floor there (world cm; the wash bay is round -900:100)
//   door                  stand in the roller door
//   look:<what>           stand back and look, for a screenshot: bench, washer, repair, computer,
//                         changer, stand, bins, bike, screen, board, monitor, dyno (the big
//                         screen), dynoroom (the whole room), booth (the spray bay), deliveries, washbay,
//                         or any place name; player: 3 m back from the nearest other player, facing them
//   act[:alt]             what the work button (or the second button) does to what's looked at, at once
//   hold:<seconds>        hold the work button that long (the pressure washer's passes)
//   f                     press F at what's looked at (unpack a box, the parts computer, ride out)
//   order:<part>|bike     order from the parts computer (delivered outside the roller door)
//   newbike[:<style>][:<slot>=<part>]...[:order]  the parts computer's New bike aisle (the configurator):
//                         that style's bike with those parts on, a colour of its own, ordered built into a free bay
//   rideout               stand in the roller door and press F (loads Buttertubs with the bike as built)
//   back                  on the road: back to the shed (as Backspace)
//   log                   log every bay's bike and the loose parts
//   shot                  a screenshot (HighResShot 1, into Saved/Screenshots)
//   shot:ui               a screenshot of the viewport with the HUD and menus (FScreenshotRequest)
//   wait:<seconds>        wait longer
//   geo                   geometry mode on a catalogue bike (a steel backbone set up in bay 3, finished as a
//                         cafe racer): a rig handle slid, the bars up 2 cm and the seat back 1 cm, saved,
//                         shots of the panel and the bike, the shed saved, forgotten and loaded back,
//                         the map opened again, and whether the fit survived (alone, it quits at the end)
//   geotwin               the same on bay 1's Speed Twin (no rig: the bars and seat moved), then ridden out
//                         to Buttertubs for a shot with the fit on, and back (alone, it quits at the end)
//   geo:open              F2 on the bay's bike (geometry mode)
//   geo:rig:<handle>:<v>  a rig handle (tank_up, pegs, hoop_up in mm; hoop_deg)
//   geo:move:<slot>:<x>:<y>:<z>  that part moved, cm in Godot's frame (x right, y up, z back)
//   geo:select:<slot>     select that part (its gizmo shows)
//   geo:save, geo:exit    "Save to this bike"; leave geometry mode (discarding what isn't saved)
//   geo:check             whether the bay's saved fit is the one geo:save sent, and how far its
//                         moved parts are drawn from where they'd be without it
//   ride                  a scratch-built bike ridden: a steel backbone set up in bay 3 and finished as a cafe
//                         racer, ridden out on its own physics, its spec logged, the robot rider taking it
//                         10 s down the road, logged again (speed, gear, wheel spin, bars, suspension), and
//                         back (alone, it quits at the end)
//   rides                 the rider sat on four styles in turn: the ride test's scratch bike remade as a cafe
//                         racer, chopper, sport bike and MX bike, each ridden out and logged with a shot
//                         at the start and after 10 s of the robot, plus a side-on shot (alone, it quits at the end)
//   restyle:<style>       (a test shortcut) every part of the bay's catalogue bike that isn't that style's
//                         reference part (the frame too) thrown away, and the bike finished as that style
//   ridelog               on the road: our bike's riding spec, what it's doing now, its drawn groups and
//                         where the rider sits (seat, pegs, grips and the pose's hips, wrists and ankles)
//   camera:<mode>[:<yaw>[:<cm>[:<fov>]]]  on the road: 0 chase, 1 first person, 2 chin, 3 the showroom orbit
//                         held at yaw degrees round from ahead (90 side on from the right), cm out (270)
//                         with that field of view (40); rides uses 3:90:450:60, the whole rider and bike
//   lever:clutch|brake[:<pull>]  on the road: hold that lever, the rider's fingers over it, pulled 0..1 (1);
//                         lever:off lets both go back to the riding
//   robot[:off]           on the road: the hands-off robot rider (-TorqueAutoRide's) rides our bike, or stops
//   georeload             (a test shortcut) the shed saved to its slot, forgotten and loaded back from
//                         it, then the shed map opened again
//   quit
// Every bay's bike is logged at every map load, so the log shows it survive riding out and coming
// back. It lives on the game instance, so it carries on across the map changes.
#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "TorqueShedTest.generated.h"

class ATorqueWalker;

UCLASS()
class UTorqueShedTest : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

private:
	bool Step(float Dt);
	bool RunStep(const FString& S);
	ATorqueWalker* Walker() const;
	void LogBuild(const TCHAR* Where) const;

	TArray<FString> Steps;
	FString GeoSent;          // the fit geo:save sent, for geo:check
	int32 Next = 0;
	int32 TestBay = 0;
	int32 Tries = 0;
	bool bAimLost = false;   // the last look missed what it was after: the next act is skipped
	double Wait = 0.0;
	TWeakObjectPtr<UWorld> SeenWorld;
	FTSTicker::FDelegateHandle Ticker;
};
