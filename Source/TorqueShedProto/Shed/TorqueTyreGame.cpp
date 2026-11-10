#include "Shed/TorqueTyreGame.h"

FTorqueTyreGame::FTorqueTyreGame()
{
	SegScore.Init(0.0, Segments);
	SegTime.Init(0.0, Segments);
}

double FTorqueTyreGame::MarkerAngle() const
{
	return UE_DOUBLE_TWO_PI * FMath::Clamp((T - Grace) / Duration, 0.0, 1.0);
}

void FTorqueTyreGame::Move(const FVector2D& Pixels)
{
	Cursor += Pixels * MouseScale;
	if (Cursor.Size() > 1.4) Cursor = Cursor.GetSafeNormal() * 1.4;
}

void FTorqueTyreGame::Aim(const FVector2D& Stick, double Dt)
{
	// tyre_game.gd eases 35% of the way each physics frame (60 a second): the same at any frame rate.
	if (Stick.Size() <= 0.35) return;
	const double K = 1.0 - FMath::Pow(0.65, FMath::Max(Dt, 0.0) * 60.0);
	Cursor = FMath::Lerp(Cursor, Stick.GetSafeNormal(), K);
}

double FTorqueTyreGame::OnTarget() const
{
	const double Len = Cursor.Size();
	if (Len < 0.05) return 0.0;
	const double Err = FMath::Abs(FMath::FindDeltaAngleRadians(FMath::Atan2(Cursor.X, -Cursor.Y), MarkerAngle()));
	const double Radial = FMath::Clamp(1.0 - FMath::Abs(Len - 1.0) / 0.6, 0.0, 1.0);
	return FMath::Clamp(1.0 - Err / Tolerance, 0.0, 1.0) * Radial;
}

bool FTorqueTyreGame::Step(double Dt)
{
	T += Dt;
	if (T > Grace && !Done())
	{
		const double S = OnTarget();
		Score += S * Dt;
		Time += Dt;
		const int32 Seg = FMath::Clamp((int32)(MarkerAngle() / UE_DOUBLE_TWO_PI * Segments), 0, Segments - 1);
		SegScore[Seg] += S * Dt;
		SegTime[Seg] += Dt;
	}
	return Done();
}

double FTorqueTyreGame::Quality() const
{
	if (Time <= 0.0) return 0.0;
	return FMath::Clamp(Score / Time * 1.1, 0.0, 1.0);
}

double FTorqueTyreGame::Segment(int32 I) const
{
	if (!SegTime.IsValidIndex(I) || SegTime[I] <= 0.0) return -1.0;
	return SegScore[I] / SegTime[I];
}
