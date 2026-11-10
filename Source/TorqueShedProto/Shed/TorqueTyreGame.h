// Fitting a tyre on the tyre changer is a little skill game (Torque Shed's tyre_game.gd). The wheel
// turns on the turntable and a marker goes once round the rim; you hold the work button and keep
// the tyre lever (moved with the mouse, or pointed with the right stick) on the marker, out at the
// rim, as it goes. How closely you followed it is how well the bead seats: Quality() 0..1, which
// goes with the tyre (FTorqueItem::Bead) and, below UTorqueBuildSubsystem::BeadOk, costs grip.
//
// The lever is in rim radii from the hub, screen style (+X right, +Y down), as the game draws it.
#pragma once

#include "CoreMinimal.h"

class FTorqueTyreGame
{
public:
	static constexpr double Duration = 5.0;       // seconds for the marker to go once round the rim
	static constexpr double Grace = 0.6;          // it waits at the top this long, so you can get the lever on it
	static constexpr double MouseScale = 0.006;   // lever travel per pixel of mouse movement (1.0 = the rim)
	static constexpr double Tolerance = 0.6;      // radians off the marker before that moment scores nothing
	static constexpr int32 Segments = 48;         // how finely the rim is marked good or bad, for drawing

	FTorqueTyreGame();

	double T = 0.0;
	FVector2D Cursor = FVector2D(0.0, -1.0);   // the lever

	// Where the marker is round the rim, radians clockwise from the top, and that point on the rim.
	double MarkerAngle() const;
	static FVector2D OnRim(double Angle) { return FVector2D(FMath::Sin(Angle), -FMath::Cos(Angle)); }
	FVector2D Marker() const { return OnRim(MarkerAngle()); }
	// Mouse movement (pixels, screen style) nudges the lever.
	void Move(const FVector2D& Pixels);
	// A pad's stick (screen style) points the lever round the rim.
	void Aim(const FVector2D& Stick, double Dt);
	// How well the lever is on the marker right now, 0..1: round the rim and out at its edge.
	double OnTarget() const;
	// Moves the game on; true once the marker has gone all the way round.
	bool Step(double Dt);
	bool Done() const { return T >= Grace + Duration; }
	double Progress() const { return FMath::Clamp(T / (Grace + Duration), 0.0, 1.0); }
	// How well the bead went on: the share of the turn the lever was on the marker, with a little
	// slack so a good, not perfect, job still seats it fully.
	double Quality() const;
	// How well each stretch of the rim went so far, 0..1, or -1 where the marker hasn't been yet.
	double Segment(int32 I) const;

private:
	double Score = 0.0, Time = 0.0;
	TArray<double> SegScore, SegTime;
};
