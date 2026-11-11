#include "TorquePing.h"

#include "TorquePlayerController.h"
#include "TorqueMenuPawn.h"
#include "TorqueUi.h"
#include "TorqueBikePawn.h"

#include "CollisionQueryParams.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/Canvas.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"

namespace TorquePing
{
namespace
{
constexpr double Grow = 2.0;   // cm round the thing (ping_marker.gd box.grow(0.02))

void SetBox(const FVector& Centre, const FVector& Extent, const FQuat& Rot, FTorquePingNet& P)
{
	P.bBox = true;
	P.BoxCentre = Centre;
	// No thing in the shed or on the road is bigger than a bus: a wild box (an actor with something
	// far off attached) is kept to a size that still reads.
	P.BoxExtent = (Extent + FVector(Grow)).ComponentMin(FVector(600.0));
	P.BoxRot = Rot;
}
} // namespace

void BoxOf(const UPrimitiveComponent* C, FTorquePingNet& P)
{
	if (!C) return;
	const FBox Local = C->CalcBounds(FTransform::Identity).GetBox();
	if (!Local.IsValid) return;
	const FTransform& T = C->GetComponentTransform();
	SetBox(T.TransformPosition(Local.GetCenter()), Local.GetExtent() * T.GetScale3D().GetAbs(), T.GetRotation(), P);
}

void BoxOf(const FBox& Box, FTorquePingNet& P)
{
	if (!Box.IsValid) return;
	SetBox(Box.GetCenter(), Box.GetExtent(), FQuat::Identity, P);
}

void BoxOfActor(const AActor* A, FTorquePingNet& P)
{
	if (!A) return;
	const FBox Local = A->CalculateComponentsBoundingBoxInLocalSpace(true, false);
	const FTransform T = A->GetActorTransform();
	if (Local.IsValid) SetBox(T.TransformPosition(Local.GetCenter()), Local.GetExtent() * T.GetScale3D().GetAbs(), T.GetRotation(), P);
	else SetBox(A->GetActorLocation(), FVector(100.0, 45.0, 80.0), T.GetRotation(), P);
}

bool PlayerOnLine(const APawn* Me, const FVector& Start, const FVector& Dir, double MaxCm, FTorquePingNet& Out, double& OutCm)
{
	UWorld* World = Me ? Me->GetWorld() : nullptr;
	if (!World) return false;
	const APawn* Best = nullptr;
	double BestAlong = MaxCm;
	bool bBestBike = false;
	for (TActorIterator<APawn> It(World); It; ++It)
	{
		const APawn* Other = *It;
		if (Other == Me || Other->IsHidden() || Other->IsA<ATorqueMenuPawn>() || !Other->GetPlayerState()) continue;
		const bool bBike = Other->IsA<ATorqueBikePawn>();
		// A rider and bike stand about as high as a person: aim at the middle of them.
		const FVector Centre = Other->GetActorLocation() + FVector(0.0, 0.0, bBike ? 70.0 : 0.0);
		const double Along = FVector::DotProduct(Centre - Start, Dir);
		if (Along <= 0.0 || Along > BestAlong) continue;
		if (FVector::Dist(Centre, Start + Dir * Along) > (bBike ? 110.0 : 60.0)) continue;
		Best = Other;
		BestAlong = Along;
		bBestBike = bBike;
	}
	if (!Best) return false;
	const FString Name = Best->GetPlayerState()->GetPlayerName();
	Out = FTorquePingNet();
	Out.What = bBestBike ? FString::Printf(TEXT("%s's bike"), *Name) : Name;
	Out.At = Start + Dir * BestAlong;
	BoxOfActor(Best, Out);
	OutCm = BestAlong;
	return true;
}

FString FromView(APlayerController* PC, const AActor* Ignore)
{
	UWorld* World = PC ? PC->GetWorld() : nullptr;
	if (!World) return FString();
	FVector Eye;
	FRotator View;
	PC->GetPlayerViewPoint(Eye, View);
	const FVector Dir = View.Vector();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(TorquePing), true, Ignore);
	if (PC->GetPawn()) Params.AddIgnoredActor(PC->GetPawn());
	FHitResult Hit;
	const bool bHit = World->LineTraceSingleByChannel(Hit, Eye, Eye + Dir * ReachCm, ECC_Visibility, Params);
	// Another player in front of what the line meets: them.
	FTorquePingNet P;
	double Cm = 0.0;
	if (PlayerOnLine(PC->GetPawn(), Eye, Dir, bHit ? Hit.Distance + 50.0 : ReachCm, P, Cm))
	{
		Send(PC, P);
		return P.What;
	}
	if (!bHit) return FString();
	P = FTorquePingNet();
	P.At = Hit.ImpactPoint;
	P.What = TEXT("Over here");
	Send(PC, P);
	return P.What;
}

void Send(APlayerController* PC, const FTorquePingNet& P)
{
	// Through the host to everyone (alone, the call runs here at once).
	if (ATorquePlayerController* TPC = Cast<ATorquePlayerController>(PC)) TPC->ServerPing(P);
}

void Draw(UCanvas* Canvas, const TArray<FShown>& Pings, double Now, const FVector& Eye, const FVector& Ahead)
{
	if (!Canvas || Pings.Num() == 0) return;
	namespace Ui = TorqueUi;
	const float S = Ui::Scale(Canvas);
	const double Near = 15.0;   // cm in front of the eye: what's nearer is cut off
	auto Depth = [&Eye, &Ahead, Near](const FVector& P) { return FVector::DotProduct(P - Eye, Ahead) - Near; };
	auto Screen = [Canvas](const FVector& P)
	{
		const FVector V = Canvas->Project(P, false);
		return FVector2D(V.X, V.Y);
	};
	// A line on the screen, T pixels thick (a thin quad, see-through).
	auto Line2 = [Canvas](const FVector2D& A, const FVector2D& B, float T, const FLinearColor& C)
	{
		const FVector2D D = B - A;
		const double L = D.Size();
		if (L < 0.5) return;
		const FVector2D N(-D.Y / L * T * 0.5, D.X / L * T * 0.5);
		Ui::Polygon(Canvas, TArray<FVector2D>{A + N, B + N, B - N, A - N}, C);
	};
	// A line in the world, the part behind the eye cut off.
	auto Line3 = [&](FVector A, FVector B, float T, const FLinearColor& C)
	{
		const double Da = Depth(A), Db = Depth(B);
		if (Da <= 0.0 && Db <= 0.0) return;
		if (Da <= 0.0) A = A + (B - A) * (Da / (Da - Db) + 0.001);
		else if (Db <= 0.0) B = A + (B - A) * (Da / (Da - Db) - 0.001);
		Line2(Screen(A), Screen(B), T, C);
	};

	for (const FShown& Shown : Pings)
	{
		const FTorquePingNet& P = Shown.P;
		const double Age = Now - Shown.Born;
		if (Age < 0.0 || Age >= Life) continue;
		const float Pulse = 0.65f + 0.35f * (float)FMath::Sin(Age * 7.0);
		const float Fade = (float)FMath::Clamp(Life - Age, 0.0, 1.0);
		// The pinger's colour, lightened (Color.lightened(0.35)).
		const FLinearColor Base(P.Colour.R / 255.0f, P.Colour.G / 255.0f, P.Colour.B / 255.0f, 1.0f);
		const FLinearColor Bright = Base + (FLinearColor::White - Base) * 0.35f;
		auto Glow = [&Bright, Pulse, Fade](float A) { return FLinearColor(Bright.R, Bright.G, Bright.B, A * Pulse * Fade); };

		FVector Top = P.At;
		if (P.bBox)
		{
			FVector Corner[8];
			double TopZ = -TNumericLimits<double>::Max();
			for (int32 I = 0; I < 8; ++I)
			{
				const FVector Local((I & 1) ? P.BoxExtent.X : -P.BoxExtent.X, (I & 2) ? P.BoxExtent.Y : -P.BoxExtent.Y,
					(I & 4) ? P.BoxExtent.Z : -P.BoxExtent.Z);
				Corner[I] = P.BoxCentre + P.BoxRot.RotateVector(Local);
				TopZ = FMath::Max(TopZ, Corner[I].Z);
			}
			// A faint fill so it reads from across the shed: the faces turned to the eye.
			static const int32 Faces[6][4] = {{0, 2, 6, 4}, {1, 3, 7, 5}, {0, 1, 5, 4}, {2, 3, 7, 6}, {0, 1, 3, 2}, {4, 5, 7, 6}};
			for (const auto& F : Faces)
			{
				const FVector Mid = 0.25 * (Corner[F[0]] + Corner[F[1]] + Corner[F[2]] + Corner[F[3]]);
				const FVector Normal = Mid - P.BoxCentre;
				if (FVector::DotProduct(Normal, Mid - Eye) >= 0.0) continue;   // facing away
				bool bInFront = true;
				for (int32 K = 0; K < 4; ++K) bInFront &= Depth(Corner[F[K]]) > 0.0;
				if (!bInFront) continue;
				Ui::Polygon(Canvas, TArray<FVector2D>{Screen(Corner[F[0]]), Screen(Corner[F[1]]), Screen(Corner[F[2]]), Screen(Corner[F[3]])},
					Glow(0.18f));
			}
			// The wire box: twelve edges.
			static const int32 Edges[12][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3}, {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
			for (const auto& E : Edges) Line3(Corner[E[0]], Corner[E[1]], 2.5f * S, Glow(0.95f));
			Top = FVector(P.BoxCentre.X, P.BoxCentre.Y, TopZ);
		}
		else
		{
			// A ring on the spot (a 22-28 cm torus lying flat).
			const FVector C = P.At + FVector(0.0, 0.0, 2.0);
			constexpr int32 Segs = 32;
			for (int32 I = 0; I < Segs; ++I)
			{
				const double A0 = 2.0 * UE_DOUBLE_PI * I / Segs, A1 = 2.0 * UE_DOUBLE_PI * (I + 1) / Segs;
				Line3(C + 25.0 * FVector(FMath::Cos(A0), FMath::Sin(A0), 0.0), C + 25.0 * FVector(FMath::Cos(A1), FMath::Sin(A1), 0.0), 3.0f * S,
					Glow(0.85f));
			}
		}
		// A beam up from it, and the label on top.
		Line3(Top, Top + FVector(0.0, 0.0, 90.0), 2.0f * S, Glow(0.6f));
		const FVector LabelAt = Top + FVector(0.0, 0.0, 100.0);
		if (Depth(LabelAt) > 0.0)
		{
			const FVector2D At = Screen(LabelAt);
			const float Px = 20.0f * S;
			const FString Label = P.Name.IsEmpty() ? P.What : FString::Printf(TEXT("%s: %s"), *P.Name, *P.What);
			Ui::Text(Canvas, Label, (float)At.X, (float)At.Y - Px, Px, FLinearColor(Bright.R, Bright.G, Bright.B, Fade), true, Ui::EAlign::Centre);
		}
	}
}
} // namespace TorquePing
