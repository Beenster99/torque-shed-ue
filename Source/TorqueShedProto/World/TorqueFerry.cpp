#include "World/TorqueFerry.h"

#include "TorqueBikePawn.h"
#include "World/TorqueRideWorld.h"
#include "World/WorldFerries.h"
#include "World/WorldRide.h"
#include "Sim/BikeContacts.h"

#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerState.h"
#include "Materials/MaterialInterface.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "ProceduralMeshComponent.h"

#include <array>
#include <cmath>
#include <string>

namespace TorqueFerryImpl
{
namespace TFer = TorqueWorld::Ferries;

constexpr double NearM = 1500.0, FarM = 2500.0;   // stood up within, taken down past (m, from either end)
constexpr double RampSeconds = 5.0, ArmSeconds = 3.0, ReadySeconds = 4.0, CallSeconds = 2.0;
constexpr double RaisedDeg = 72.0;                // a ramp raised for sailing
constexpr double CallReach = 90.0, HintReach = 260.0;   // a rider this near a slipway's head calls the boat; the HUD's line (m)
// The boat in its own frame (cm: x across, y along with +y the bow, z up from the waterline), as make_ferry.py draws it.
constexpr double Deck = 100.0, HalfDeck = 1260.0, SideIn = 430.0, Beam = 450.0, Bulwark = 205.0;
constexpr double TowerX0 = 245.0, TowerX1 = 450.0, TowerY0 = -460.0, TowerY1 = 460.0, TowerTop = 345.0;
constexpr double HouseX0 = 265.0, HouseX1 = 435.0, HouseY0 = -200.0, HouseY1 = 200.0, HouseTop = 575.0;
constexpr double StairX0 = 260.0, StairX1 = 350.0, StairY0 = -840.0, StairY1 = -460.0;
constexpr double LaneCars = -295.0, BayX0 = 75.0, BayX1 = 230.0, BayY0 = 490.0, BayY1 = 1060.0;
constexpr double RampHalf = 330.0, RampLong = 460.0;

struct FData
{
	bool bRead = false;
	std::vector<TFer::Listed> List;
	const TorqueWorld::RidePlan* Plan = nullptr;
	size_t Lines = 0;
	std::vector<TFer::Crossing> Here;
	std::vector<double> TriedAt;      // per crossing: when it was last laid and failed (world seconds)
};
FData& Data()
{
	static FData D;
	return D;
}
TArray<TWeakObjectPtr<ATorqueFerry>>& Live()
{
	static TArray<TWeakObjectPtr<ATorqueFerry>> Out;   // (weak pointers: nothing held from GC)
	return Out;
}
int32& Version()
{
	static int32 V = 0;
	return V;
}
FVector Ue(const TorqueWorld::P2& P, double Y) { return FVector(P.X * 100.0, P.Z * 100.0, Y * 100.0); }
// A yaw (degrees) that turns a model's front (Unreal's +y) onto a way in the ride's frame.
double YawOf(const TorqueWorld::P2& Way) { return FMath::RadiansToDegrees(std::atan2(-Way.X, Way.Z)); }
TorqueWorld::P2 UnitOf(const TorqueWorld::P2& V)
{
	const double L = V.Length();
	return L > 1e-9 ? V * (1.0 / L) : TorqueWorld::P2(1.0, 0.0);
}
double Smooth(double T)
{
	T = FMath::Clamp(T, 0.0, 1.0);
	return T * T * (3.0 - 2.0 * T);
}
} // namespace TorqueFerryImpl
namespace TFerI = TorqueFerryImpl;

ATorqueFerry::ATorqueFerry()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;
	USceneComponent* Origin = CreateDefaultSubobject<USceneComponent>(TEXT("Origin"));
	RootComponent = Origin;
	// (only the deck, ramps, slipways and stairs are ground: a wheel's or foot's trace that meets a wall steps past it)
	Tags.Add(TEXT("RoadParts"));
}

void ATorqueFerry::EndPlay(const EEndPlayReason::Type Reason)
{
	TFerI::Live().RemoveAll([this](const TWeakObjectPtr<ATorqueFerry>& F) { return !F.IsValid() || F.Get() == this; });
	++TFerI::Version();
	Super::EndPlay(Reason);
}

UBoxComponent* ATorqueFerry::AddBox(USceneComponent* Parent, const FName& Name, const FVector& Lo, const FVector& Hi, bool bGround, bool bSolid, bool bSteel)
{
	UBoxComponent* Box = NewObject<UBoxComponent>(this, Name);
	Box->SetupAttachment(Parent);
	Box->SetRelativeLocation((Lo + Hi) * 0.5);
	Box->SetBoxExtent((Hi - Lo) * 0.5);
	Box->SetMobility(EComponentMobility::Movable);
	Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Box->SetCollisionResponseToAllChannels(ECR_Ignore);
	Box->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	Box->SetCollisionResponseToChannel(ECC_Camera, ECR_Block);
	Box->SetHiddenInGame(true);
	if (bGround) Box->ComponentTags.Add(TEXT("Road"));
	if (bSteel) Box->ComponentTags.Add(TEXT("Steel"));   // (the wheels: SURF_STEEL, TorqueBikePawn's Ray)
	Box->RegisterComponent();
	Boxes.Add(Box);
	BoxSolid.Add(bSolid);
	return Box;
}

UStaticMeshComponent* ATorqueFerry::AddProp(USceneComponent* Parent, const TCHAR* Id, const FVector& At, const FRotator& Rot)
{
	const FString Path = FString::Printf(TEXT("/Game/Shed/Props/%s/SM_%s.SM_%s"), Id, Id, Id);
	UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *Path);
	if (!Mesh)
	{
		UE_LOG(LogTemp, Warning, TEXT("Torque ferry: no model at %s (run Tools/ue_import_props.py %s)"), *Path, Id);
		return nullptr;
	}
	UStaticMeshComponent* Part = NewObject<UStaticMeshComponent>(this);
	Part->SetupAttachment(Parent);
	Part->SetMobility(EComponentMobility::Movable);
	Part->SetStaticMesh(Mesh);
	Part->SetRelativeLocationAndRotation(At, Rot);
	Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);   // (the boxes are its collision)
	Part->RegisterComponent();
	return Part;
}

void ATorqueFerry::Build(const TorqueWorld::RidePlan& Plan)
{
	using namespace TorqueFerryImpl;
	Ramps.Init(nullptr, 2);
	Arms.Init(nullptr, 2);
	Gates.Init(nullptr, 2);
	Barriers.Init(nullptr, 2);
	for (int32 E = 0; E < 2; ++E) BuildShore(Plan, E);
	BuildBoat();
	// Tied up at the end nearer the rider (Keep sets DockEnd before Build).
	Along = DockEnd == 0 ? 0.0 : 1.0;
	RampAt[DockEnd] = 0.0;
	RampAt[1 - DockEnd] = 1.0;
	ArmAt[DockEnd] = 1.0;
	ArmAt[1 - DockEnd] = 0.0;
	BoatWas = BoatAt(Along);
	Boat->SetWorldTransform(BoatWas);
	bBoatPlaced = true;
	PoseRamps();
	PoseBarriers();
}

void ATorqueFerry::BuildShore(const TorqueWorld::RidePlan& Plan, int32 End)
{
	using namespace TorqueFerryImpl;
	const TFer::End& N = Lay->Ends[End];
	const FVector Base = GetActorLocation();
	UMaterialInterface* Concrete = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Shed/Props/Materials/MI_Prop_slip_concrete.MI_Prop_slip_concrete"));
	UMaterialInterface* Weed = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Shed/Props/Materials/MI_Prop_slip_weed.MI_Prop_slip_weed"));
	UMaterialInterface* Road = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/RideUK/Materials/M_RideRoad.M_RideRoad"));
	if (!Concrete) Concrete = Road;
	if (!Weed) Weed = Concrete;
	UProceduralMeshComponent* Mesh = NewObject<UProceduralMeshComponent>(this);
	Mesh->SetupAttachment(RootComponent);
	Mesh->SetCollisionProfileName(TEXT("BlockAll"));
	Mesh->bUseComplexAsSimpleCollision = true;
	Mesh->ComponentTags.Add(TEXT("Road"));
	Mesh->SetCastShadow(true);
	// A strip down a middle line (x, y, z in the ride's frame) Half wide either side, with skirts down into the
	// ground at its sides (and at its first end, the slipway's head), its UVs in metres.
	struct FStrip { TArray<FVector> Points; TArray<int32> Tris; TArray<FVector> Normals; TArray<FVector2D> Uvs; TArray<FLinearColor> Colours; };
	auto Strip = [&Base](FStrip& S, const std::vector<std::array<double, 3>>& Mid, double Half, const FLinearColor& Colour, double VFrom)
	{
		double V = VFrom;
		for (size_t I = 0; I < Mid.size(); ++I)
		{
			const size_t A = I == 0 ? 0 : I - 1, B = I + 1 < Mid.size() ? I + 1 : I;
			TorqueWorld::P2 Way(Mid[B][0] - Mid[A][0], Mid[B][2] - Mid[A][2]);
			const double L = std::hypot(Way.X, Way.Z);
			if (L > 1e-6) Way = TorqueWorld::P2(Way.X / L, Way.Z / L);
			const TorqueWorld::P2 Across(-Way.Z, Way.X);
			if (I > 0) V += std::hypot(Mid[I][0] - Mid[I - 1][0], Mid[I][2] - Mid[I - 1][2]);
			const int32 First = S.Points.Num();
			for (int32 K = 0; K < 4; ++K)
			{
				// (the skirt's foot, the top's edge, the other edge, the other foot)
				const double Side = K < 2 ? -Half : Half;
				const double Drop = (K == 0 || K == 3) ? 1.6 : 0.0;
				const FVector P((Mid[I][0] + Across.X * Side) * 100.0, (Mid[I][2] + Across.Z * Side) * 100.0, (Mid[I][1] - Drop) * 100.0);
				S.Points.Add(P - Base);
				S.Normals.Add(Drop > 0.0 ? FVector(Across.X * (Side < 0 ? -1.0 : 1.0), Across.Z * (Side < 0 ? -1.0 : 1.0), 0.0) : FVector::UpVector);
				S.Uvs.Add(FVector2D((Side + (K == 0 ? -1.6 : K == 3 ? 1.6 : 0.0)) * 0.5, V * 0.5));
				S.Colours.Add(Colour);
			}
			if (I == 0) continue;
			const int32 Prev = First - 4;
			// (wound as the ride's meshes are, for Unreal's front faces: the top up, each skirt outwards)
			for (int32 K = 0; K < 3; ++K)
			{
				S.Tris.Append({Prev + K, Prev + K + 1, First + K});
				S.Tris.Append({Prev + K + 1, First + K + 1, First + K});
			}
		}
	};
	auto Make = [&Mesh](int32 Section, FStrip& S, UMaterialInterface* Mat)
	{
		if (S.Points.Num() == 0) return;
		Mesh->CreateMeshSection_LinearColor(Section, S.Points, S.Tris, S.Normals, S.Uvs, S.Colours, TArray<FProcMeshTangent>(), true, false);
		if (Mat) Mesh->SetMaterial(Section, Mat);
	};
	// The slipway: dry concrete from its head down to just over the water, weed on the rest down to its foot.
	const double WeedFrom = Lay->Water + 0.35;
	const double Run = N.Head.DistanceTo(N.Foot);
	const double DryRun = N.Grade > 1e-6 ? FMath::Clamp((N.HeadY - WeedFrom) / N.Grade, 0.0, Run) : Run;
	std::vector<std::array<double, 3>> Dry, Wet;
	for (int32 K = 0; K <= 8; ++K)
	{
		const double D = DryRun * K / 8.0;
		Dry.push_back({N.Head.X + N.Axis.X * D, N.HeadY - N.Grade * D, N.Head.Z + N.Axis.Z * D});
	}
	for (int32 K = 0; K <= 4; ++K)
	{
		const double D = DryRun + (Run - DryRun) * K / 4.0;
		Wet.push_back({N.Head.X + N.Axis.X * D, N.HeadY - N.Grade * D, N.Head.Z + N.Axis.Z * D});
	}
	FStrip SDry, SWet, SRoad;
	const FLinearColor Plain(1.0f, 1.0f, 1.0f, 1.0f);
	Strip(SDry, Dry, TFer::SlipHalfW, Plain, 0.0);
	Strip(SWet, Wet, TFer::SlipHalfW, Plain, DryRun);
	// The head's own skirt (across its top end), so the slipway never shows its inside from the land.
	{
		const TorqueWorld::P2 Across(-N.Axis.Z, N.Axis.X);
		const int32 First = SDry.Points.Num();
		for (int32 K = 0; K < 4; ++K)
		{
			const double Side = K < 2 ? -TFer::SlipHalfW : TFer::SlipHalfW;
			const double Y = N.HeadY - ((K == 0 || K == 3) ? 1.6 : 0.0);
			SDry.Points.Add(FVector((N.Head.X + Across.X * Side) * 100.0, (N.Head.Z + Across.Z * Side) * 100.0, Y * 100.0) - Base);
			SDry.Normals.Add(FVector(-N.Axis.X, -N.Axis.Z, 0.0));
			SDry.Uvs.Add(FVector2D(Side * 0.5, Y * 0.5));
			SDry.Colours.Add(Plain);
		}
		SDry.Tris.Append({First, First + 2, First + 1, First, First + 3, First + 2});
	}
	// The approach from the map's road (the ride's tarmac as the car parks lay theirs).
	if (N.Approach.size() >= 2) Strip(SRoad, N.Approach, TFer::ApproachHalfW, FLinearColor(0.035f, 0.035f, 0.038f, 1.0f), 0.0);
	Make(0, SDry, Concrete);
	Make(1, SWet, Weed);
	Make(2, SRoad, Road);
	Mesh->RegisterComponent();
	Slips.Add(Mesh);

	// The shore kit at the head: the barrier across the lane, the ticket hut and the shelter either side, the sign
	// back up the approach facing the traffic coming down.
	const TorqueWorld::P2 Inland = N.Approach.size() >= 4
		? TFerI::UnitOf(TorqueWorld::P2(N.Approach[N.Approach.size() - 4][0], N.Approach[N.Approach.size() - 4][2]) - N.Head)
		: TorqueWorld::P2(-N.Axis.X, -N.Axis.Z);
	const TorqueWorld::P2 Right(Inland.Z, -Inland.X);
	const TorqueWorld::P2 Up(-N.Axis.X, -N.Axis.Z);   // (up the slipway: the slipway's own inland)
	const TorqueWorld::P2 RightS(Up.Z, -Up.X);
	auto GroundAt = [&Plan](const TorqueWorld::P2& P, double AtLeast)
	{
		const double H = Plan.Ground(P.X, P.Z);
		return std::isnan(H) ? AtLeast : FMath::Max(H, AtLeast);
	};
	auto Put = [this, &Base](const TCHAR* Id, const TorqueWorld::P2& P, double Y, const TorqueWorld::P2& Front)
	{
		return AddProp(RootComponent, Id, TFerI::Ue(P, Y) - Base, FRotator(0.0, TFerI::YawOf(Front), 0.0));
	};
	// (2 m down the slipway from its head, the post at its side facing up it, the arm across the lane)
	const TorqueWorld::P2 BarrierAt = N.Head + N.Axis * 2.0 + RightS * (TFer::SlipHalfW + 0.45);
	const double BarrierY = N.HeadY - N.Grade * 2.0;
	const TorqueWorld::P2 Lane = N.Head + N.Axis * 2.0;
	Put(TEXT("ferry_barrier"), BarrierAt, BarrierY, Up);
	USceneComponent* Pivot = NewObject<USceneComponent>(this);
	Pivot->SetupAttachment(RootComponent);
	ArmYaw[End] = TFerI::YawOf(N.Axis);
	Pivot->SetRelativeLocationAndRotation(TFerI::Ue(BarrierAt, BarrierY + 1.0) - Base, FRotator(0.0, ArmYaw[End], 0.0));
	Pivot->RegisterComponent();
	Arms[End] = Pivot;
	AddProp(Pivot, TEXT("ferry_barrier_arm"), FVector::ZeroVector, FRotator::ZeroRotator);
	// (the arm's box: across the lane when it's down, for the walker, the camera and the bike)
	{
		const double Lx = TFer::SlipHalfW * 2.0 + 0.6;
		UBoxComponent* Box = AddBox(RootComponent, *FString::Printf(TEXT("Barrier%d"), End), FVector(-Lx * 50.0, -12.0, -100.0), FVector(Lx * 50.0, 12.0, 25.0), false, true);
		Box->SetRelativeLocationAndRotation(TFerI::Ue(Lane, BarrierY + 1.0) - Base, FRotator(0.0, TFerI::YawOf(Up), 0.0));
		Barriers[End] = Box;
	}
	const TorqueWorld::P2 HutAt = N.Head + Inland * 3.0 + Right * 7.5;
	Put(TEXT("ferry_hut"), HutAt, GroundAt(HutAt, Lay->Water + 0.5), TorqueWorld::P2(-Right.X, -Right.Z));
	AddBox(RootComponent, *FString::Printf(TEXT("Hut%d"), End), FVector(-150.0, -120.0, 0.0), FVector(150.0, 120.0, 280.0), false, false)
		->SetRelativeLocationAndRotation(TFerI::Ue(HutAt, GroundAt(HutAt, Lay->Water + 0.5)) - Base, FRotator(0.0, TFerI::YawOf(TorqueWorld::P2(-Right.X, -Right.Z)), 0.0));
	const TorqueWorld::P2 ShelterAt = N.Head + Inland * 6.0 - Right * 7.0;
	Put(TEXT("ferry_shelter"), ShelterAt, GroundAt(ShelterAt, Lay->Water + 0.5), Right);
	const TorqueWorld::P2 SignAt = N.Head + Inland * 18.0 - Right * 4.8;
	Put(TEXT("ferry_sign"), SignAt, GroundAt(SignAt, Lay->Water + 0.5), Inland);
}

void ATorqueFerry::BuildBoat()
{
	using namespace TorqueFerryImpl;
	Boat = NewObject<USceneComponent>(this, TEXT("Boat"));
	Boat->SetupAttachment(RootComponent);
	Boat->SetMobility(EComponentMobility::Movable);
	Boat->RegisterComponent();
	Look = NewObject<USceneComponent>(this, TEXT("BoatLook"));
	Look->SetupAttachment(Boat);
	Look->SetMobility(EComponentMobility::Movable);
	Look->RegisterComponent();
	AddProp(Look, TEXT("ferry_small"), FVector::ZeroVector, FRotator::ZeroRotator);
	// The ramps: end 0's is the stern's (-y), end 1's the bow's (+y), each hinged at its deck end and lying along +y
	// from it (the stern's turned round).
	for (int32 E = 0; E < 2; ++E)
	{
		const double S = E == 0 ? -1.0 : 1.0;
		USceneComponent* Hinge = NewObject<USceneComponent>(this, *FString::Printf(TEXT("Ramp%d"), E));
		Hinge->SetupAttachment(Boat);
		Hinge->SetMobility(EComponentMobility::Movable);
		Hinge->SetRelativeLocation(FVector(0.0, S * HalfDeck, Deck));
		Hinge->RegisterComponent();
		Ramps[E] = Hinge;
		AddProp(Hinge, TEXT("ferry_ramp"), FVector::ZeroVector, FRotator::ZeroRotator);
		AddBox(Hinge, *FString::Printf(TEXT("RampPlate%d"), E), FVector(-RampHalf, -10.0, -28.0), FVector(RampHalf, RampLong, 0.0), true, false, true);
		// The deck's end, shut (a wall) while its ramp isn't down.
		Gates[E] = AddBox(Boat, *FString::Printf(TEXT("Gate%d"), E), FVector(-SideIn, S > 0 ? HalfDeck : -HalfDeck - 20.0, Deck), FVector(SideIn, S > 0 ? HalfDeck + 20.0 : -HalfDeck, Deck + 150.0), false, true);
	}
	// The steel deck (the wheels and the feet), its sides' bulwarks, the tower, the stair up its end to the upper
	// deck, the wheelhouse on that and the rail round it.
	AddBox(Boat, TEXT("Deck"), FVector(-SideIn, -HalfDeck, Deck - 40.0), FVector(SideIn, HalfDeck, Deck), true, false, true);
	AddBox(Boat, TEXT("BulwarkPort"), FVector(-Beam, -HalfDeck, Deck), FVector(-SideIn, HalfDeck, Bulwark), false, true);
	AddBox(Boat, TEXT("BulwarkStarboard"), FVector(SideIn, -HalfDeck, Deck), FVector(Beam, HalfDeck, Bulwark), false, true);
	AddBox(Boat, TEXT("Tower"), FVector(TowerX0, TowerY0, Deck), FVector(TowerX1, TowerY1, TowerTop - 8.0), false, true);
	{
		const double Rise = TowerTop - Deck, Len = StairY1 - StairY0;
		const double Pitch = std::atan2(Rise, Len);
		const double Slope = std::sqrt(Rise * Rise + Len * Len);
		UBoxComponent* Stair = AddBox(Boat, TEXT("Stair"), FVector(-(StairX1 - StairX0) * 0.5, -Slope * 0.5, -12.0), FVector((StairX1 - StairX0) * 0.5, Slope * 0.5, 0.0), true, false);
		const FQuat Tilt(FVector::XAxisVector, Pitch);   // (+y, up towards the tower, tipped up)
		Stair->SetRelativeRotation(Tilt);
		Stair->SetRelativeLocation(FVector((StairX0 + StairX1) * 0.5, (StairY0 + StairY1) * 0.5, (Deck + TowerTop) * 0.5) + Tilt.RotateVector(FVector(0.0, 0.0, -6.0)));
	}
	AddBox(Boat, TEXT("UpperDeck"), FVector(TowerX0 - 15.0, TowerY0 - 30.0, TowerTop - 10.0), FVector(TowerX1 + 5.0, TowerY1 + 30.0, TowerTop + 8.0), true, false);
	AddBox(Boat, TEXT("Wheelhouse"), FVector(HouseX0, HouseY0, TowerTop + 8.0), FVector(HouseX1, HouseY1, HouseTop), false, false);
	const double RailTop = TowerTop + 8.0 + 105.0;
	AddBox(Boat, TEXT("RailIn"), FVector(TowerX0 - 18.0, StairY1, TowerTop + 8.0), FVector(TowerX0 - 8.0, TowerY1 + 30.0, RailTop), false, false);
	AddBox(Boat, TEXT("RailOut"), FVector(TowerX1, TowerY0 - 30.0, TowerTop + 8.0), FVector(TowerX1 + 10.0, TowerY1 + 30.0, RailTop), false, false);
	AddBox(Boat, TEXT("RailBow"), FVector(TowerX0 - 18.0, TowerY1 + 25.0, TowerTop + 8.0), FVector(TowerX1 + 10.0, TowerY1 + 35.0, RailTop), false, false);
	AddBox(Boat, TEXT("RailStern"), FVector(StairX1 + 5.0, TowerY0 - 35.0, TowerTop + 8.0), FVector(TowerX1 + 10.0, TowerY0 - 25.0, RailTop), false, false);
	// Two cars parked in the cars' lane (the ride's own parked-car models), their boxes the bike and the walker meet.
	for (int32 K = 0; K < 2; ++K)
	{
		const double Y = K == 0 ? -620.0 : 380.0;
		UStaticMesh* CarMesh = nullptr;
		FTransform CarAt;
		const int32 Variant = (ListRow * 7 + K * 3) & 1023;
		if (ATorqueRideWorld::PropModel(TorqueWorld::RidePlan::PropParkedCar, Variant, FVector::ZeroVector, 0.0, CarMesh, CarAt) && CarMesh)
		{
			// (the kit's car faces the ride's +x at yaw 0: turned to face the bow, +y here)
			UStaticMeshComponent* Car = NewObject<UStaticMeshComponent>(this);
			Car->SetupAttachment(Look);
			Car->SetMobility(EComponentMobility::Movable);
			Car->SetStaticMesh(CarMesh);
			const FQuat Turn(FVector::UpVector, FMath::DegreesToRadians(90.0));
			Car->SetRelativeTransform(FTransform(Turn * CarAt.GetRotation(), Turn.RotateVector(CarAt.GetLocation()) + FVector(LaneCars, Y, Deck), CarAt.GetScale3D()));
			Car->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Car->RegisterComponent();
		}
		AddBox(Boat, *FString::Printf(TEXT("Car%d"), K), FVector(LaneCars - 92.0, Y - 225.0, Deck), FVector(LaneCars + 92.0, Y + 225.0, Deck + 145.0), false, true);
	}
}

FTransform ATorqueFerry::BoatAt(double S) const
{
	TorqueWorld::P2 Pos, Bow;
	TorqueWorld::Ferries::Sail(*Lay, Cross->Long, S, Pos, Bow);
	return FTransform(FRotator(0.0, TFerI::YawOf(Bow), 0.0), TFerI::Ue(Pos, Lay->Water));
}

void ATorqueFerry::PoseRamps()
{
	for (int32 E = 0; E < 2; ++E)
	{
		if (!Ramps.IsValidIndex(E) || !Ramps[E]) continue;
		// Down: its toe on the slipway (the end's RampDown below level); raised: up for sailing.
		const double DownDeg = -FMath::RadiansToDegrees(Lay->Ends[E].RampDown);
		const double Deg = FMath::Lerp(DownDeg, TFerI::RaisedDeg, TFerI::Smooth(RampAt[E]));
		const FQuat Turn(FVector::UpVector, E == 0 ? PI : 0.0);
		Ramps[E]->SetRelativeRotation(Turn * FQuat(FVector::XAxisVector, FMath::DegreesToRadians(Deg)));
		if (Gates.IsValidIndex(E) && Gates[E])
			Gates[E]->SetCollisionEnabled(RampAt[E] > 0.02 ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
	}
}

void ATorqueFerry::PoseBarriers()
{
	for (int32 E = 0; E < 2; ++E)
	{
		if (Arms.IsValidIndex(E) && Arms[E])
		{
			// (the arm lies along its +x across the lane; up, swung about its front, its tip to the sky)
			const FQuat Yaw(FVector::UpVector, FMath::DegreesToRadians(ArmYaw[E]));
			Arms[E]->SetRelativeRotation(Yaw * FQuat(FVector::YAxisVector, -FMath::DegreesToRadians(85.0) * TFerI::Smooth(ArmAt[E])));
		}
		if (Barriers.IsValidIndex(E) && Barriers[E])
		{
			const bool bShut = ArmAt[E] < 0.5;
			if (bShut != (Barriers[E]->GetCollisionEnabled() != ECollisionEnabled::NoCollision)) ++TFerI::Version();   // (the bike's contacts again)
			Barriers[E]->SetCollisionEnabled(bShut ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
		}
	}
}

void ATorqueFerry::Riders(TArray<ATorqueBikePawn*>& Out) const
{
	Out.Reset();
	UWorld* W = GetWorld();
	if (!W) return;
	// (every rider's bike: ours, and in co-op each mate's as their snapshots draw it)
	for (TActorIterator<ATorqueBikePawn> It(W); It; ++It)
		if (It->IsLocallyControlled() || It->GetPlayerState()) Out.Add(*It);
}

bool ATorqueFerry::OnBoard(const FVector& At, double Margin) const
{
	if (!Boat) return false;
	const FVector L = Boat->GetComponentTransform().InverseTransformPosition(At);
	return FMath::Abs(L.X) <= TFerI::Beam - Margin && FMath::Abs(L.Y) <= TFerI::HalfDeck + 30.0 - Margin && L.Z > TFerI::Deck - 80.0 && L.Z < TFerI::HouseTop + 200.0;
}

bool ATorqueFerry::EveryoneAboard() const
{
	TArray<ATorqueBikePawn*> All;
	Riders(All);
	if (All.Num() == 0) return false;
	for (ATorqueBikePawn* P : All)
	{
		// Parked on the deck (a bike nobody's on) and, for ours, our feet aboard too.
		if (!P->IsParkedUp() || !OnBoard(P->GetActorLocation(), 40.0)) return false;
		if (P->IsLocallyControlled() && !OnBoard(P->FeetAt(), 20.0)) return false;
	}
	return true;
}

void ATorqueFerry::Carry(const FTransform& From, const FTransform& To)
{
	TArray<ATorqueBikePawn*> All;
	Riders(All);
	for (ATorqueBikePawn* P : All)
	{
		if (!P->IsLocallyControlled()) continue;   // (a mate's bike is drawn from their snapshots)
		const bool bFeet = P->IsOnFoot() && OnBoard(P->FeetAt());
		const bool bBike = P->IsOnFoot() && OnBoard(P->GetActorLocation());
		if (bFeet || bBike) P->FerryCarry(From, To, bFeet, bBike);
	}
}

void ATorqueFerry::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	using namespace TorqueFerryImpl;
	if (!Lay || !Boat) return;
	const double Dt = FMath::Min(double(DeltaSeconds), 0.1);
	UWorld* Here = GetWorld();
	TArray<ATorqueBikePawn*> All;
	Riders(All);
	// (the riders' pawns tick after this, so the deck has moved before they read it)
	for (ATorqueBikePawn* P : All)
		if (!Tied.ContainsByPredicate([P](const TWeakObjectPtr<ATorqueBikePawn>& W) { return W.Get() == P; }))
		{
			P->AddTickPrerequisiteActor(this);
			Tied.Add(P);
		}
	auto NearHead = [&All, this](int32 E, double Reach)
	{
		const FVector H = TFerI::Ue(Lay->Ends[E].Head, Lay->Ends[E].HeadY);
		for (ATorqueBikePawn* P : All)
		{
			if (OnBoard(P->GetActorLocation())) continue;
			if (FVector::Dist2D(P->GetActorLocation(), H) < Reach * 100.0) return true;
			if (P->IsLocallyControlled() && P->IsOnFoot() && FVector::Dist2D(P->FeetAt(), H) < Reach * 100.0) return true;
		}
		return false;
	};
	auto AnyAboard = [&All, this]()
	{
		for (ATorqueBikePawn* P : All)
			if (OnBoard(P->GetActorLocation()) || (P->IsLocallyControlled() && P->IsOnFoot() && OnBoard(P->FeetAt()))) return true;
		return false;
	};
	Clock += Dt;
	const EState Was = State;
	switch (State)
	{
	case EState::Docked:
	{
		ArmAt[DockEnd] = FMath::Min(1.0, ArmAt[DockEnd] + Dt / ArmSeconds);
		// (just in with riders aboard: not away again until they've got back on or gone ashore, and come aboard again)
		const bool bAll = EveryoneAboard();
		if (!bAll) bJustIn = false;
		ReadyFor = bAll && !bJustIn ? ReadyFor + Dt : 0.0;
		bEmpty = false;
		if (ReadyFor >= ReadySeconds)
		{
			State = EState::Raising;
			UE_LOG(LogTemp, Log, TEXT("Torque ferry: %s sails from %s to %s with everyone aboard"), UTF8_TO_TCHAR(Cross->Name.c_str()), *PlaceName[DockEnd], *PlaceName[1 - DockEnd]);
		}
		// Called over: a rider at the other slipway, nobody aboard or waiting here.
		else if (Clock > CallSeconds && !AnyAboard() && NearHead(1 - DockEnd, CallReach) && !NearHead(DockEnd, CallReach))
		{
			State = EState::Raising;
			bEmpty = true;
			UE_LOG(LogTemp, Log, TEXT("Torque ferry: %s comes over empty from %s to fetch a rider at %s"), UTF8_TO_TCHAR(Cross->Name.c_str()), *PlaceName[DockEnd], *PlaceName[1 - DockEnd]);
		}
		break;
	}
	case EState::Raising:
		RampAt[DockEnd] = FMath::Min(1.0, RampAt[DockEnd] + Dt / RampSeconds);
		ArmAt[DockEnd] = FMath::Max(0.0, ArmAt[DockEnd] - Dt / ArmSeconds);
		if (RampAt[DockEnd] >= 1.0)
		{
			State = EState::Sailing;
			DockEnd = 1 - DockEnd;
			SailFor = Lay->Seconds * (bEmpty ? 0.6 : 1.0);
		}
		break;
	case EState::Sailing:
	{
		const double T = FMath::Clamp(Clock / FMath::Max(SailFor, 1.0), 0.0, 1.0);
		const double P = TorqueWorld::Ferries::Progress(T);
		Along = DockEnd == 1 ? P : 1.0 - P;
		if (T >= 1.0)
		{
			Along = DockEnd == 1 ? 1.0 : 0.0;
			State = EState::Lowering;
		}
		break;
	}
	case EState::Lowering:
		RampAt[DockEnd] = FMath::Max(0.0, RampAt[DockEnd] - Dt / RampSeconds);
		if (RampAt[DockEnd] <= 0.0)
		{
			State = EState::Docked;
			bJustIn = AnyAboard();
			UE_LOG(LogTemp, Log, TEXT("Torque ferry: %s tied up at %s, ramp down"), UTF8_TO_TCHAR(Cross->Name.c_str()), *PlaceName[DockEnd]);
		}
		break;
	}
	if (State != Was)
	{
		Clock = 0.0;
		ReadyFor = 0.0;
		++TFerI::Version();   // (its walls come and go from the bike's contacts as it ties up and casts off)
	}
	// The boat where it is on its way, the riders' feet and parked bikes carried with it.
	const FTransform Now = BoatAt(Along);
	if (bBoatPlaced && !Now.Equals(BoatWas, 0.01)) Carry(BoatWas, Now);
	Boat->SetWorldTransform(Now);
	BoatWas = Now;
	bBoatPlaced = true;
	// Its model rolls and heaves a little (only the look: the deck the wheels and feet stand on stays level), more
	// when under way.
	Bob += Dt;
	const double Sea = State == EState::Sailing ? 1.0 : 0.25;
	Look->SetRelativeLocationAndRotation(FVector(0.0, 0.0, 3.0 * Sea * std::sin(Bob * 0.9)),
		FRotator(0.35 * Sea * std::sin(Bob * 0.6 + 1.0), 0.0, 0.9 * Sea * std::sin(Bob * 0.75)));
	PoseRamps();
	PoseBarriers();
	// Nobody gets back on while it's under way (the bike's sim never meets a moving ground).
	if (State != EState::Docked && Here)
		for (ATorqueBikePawn* P : All)
			if (P->IsLocallyControlled() && OnBoard(P->GetActorLocation())) P->FerryHoldUntil = Here->GetTimeSeconds() + 0.25;
}

void ATorqueFerry::Keep(ATorqueRideWorld* Ride, const TorqueWorld::RidePlan& Plan, const FVector2D& RiderAt)
{
	using namespace TorqueFerryImpl;
	UWorld* World = Ride ? Ride->GetWorld() : nullptr;
	if (!World) return;
	FData& D = TFerI::Data();
	if (!D.bRead)
	{
		D.bRead = true;
		FString Text;
		const FString Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("Data/RideUK/ferries.json"));
		if (!FFileHelper::LoadFileToString(Text, *Path) || !TFer::ReadList(std::string(TCHAR_TO_UTF8(*Text)), D.List))
			UE_LOG(LogTemp, Warning, TEXT("Torque ferry: no crossings (%s unreadable)"), *Path);
		else
			UE_LOG(LogTemp, Display, TEXT("Torque ferry: %d crossings listed"), int32(D.List.size()));
	}
	// The listed crossings in the ride's area, found again when the area grows.
	const size_t Lines = Plan.Area.Roads.Scenery.Lines.size() * 1000003u + Plan.Area.Roads.Edges.size();
	if (D.Plan != &Plan || D.Lines != Lines)
	{
		D.Plan = &Plan;
		D.Lines = Lines;
		D.Here = TFer::InPlan(Plan, D.List);
		D.TriedAt.assign(D.Here.size(), -1e9);
		for (const TFer::Crossing& C : D.Here)
			UE_LOG(LogTemp, Log, TEXT("Torque ferry: %s in the ride's area (%.0f m, %s, its line %s)"), UTF8_TO_TCHAR(C.Name.c_str()), C.Length, UTF8_TO_TCHAR(TFer::KindName(C.Kind)),
				C.FromMap ? TEXT("from the map") : TEXT("straight between its ends"));
	}
	TArray<TWeakObjectPtr<ATorqueFerry>>& Have = TFerI::Live();
	auto EndsFrom = [&RiderAt](const TFer::Crossing& C)
	{
		if (C.Line.empty()) return 1e18;
		return FMath::Min(FVector2D::Distance(RiderAt, FVector2D(C.Line.front().X, C.Line.front().Z)), FVector2D::Distance(RiderAt, FVector2D(C.Line.back().X, C.Line.back().Z)));
	};
	// Gone too far: taken down (never with a rider aboard).
	for (int32 I = Have.Num() - 1; I >= 0; --I)
	{
		ATorqueFerry* F = Have[I].Get();
		if (!F || F->GetWorld() != World)
		{
			Have.RemoveAt(I);
			continue;
		}
		if (F->Cross && EndsFrom(*F->Cross) > FarM && F->State == EState::Docked)
		{
			Have.RemoveAt(I);
			F->Destroy();
			++TFerI::Version();
		}
	}
	const double Now = World->GetTimeSeconds();
	for (size_t K = 0; K < D.Here.size(); ++K)
	{
		const TFer::Crossing& C = D.Here[K];
		if (EndsFrom(C) > NearM || Now - D.TriedAt[K] < 5.0) continue;
		bool bStanding = false;
		for (const TWeakObjectPtr<ATorqueFerry>& F : Have) bStanding |= F.IsValid() && F->ListRow == C.Listed;
		if (bStanding) continue;
		auto L = std::make_shared<TFer::Layout>();
		if (!TFer::Lay(C, TFer::AskOf(Plan), *L))
		{
			D.TriedAt[K] = Now;
			UE_LOG(LogTemp, Log, TEXT("Torque ferry: %s not laid yet: %s"), UTF8_TO_TCHAR(C.Name.c_str()), UTF8_TO_TCHAR(L->Why.c_str()));
			continue;
		}
		const FVector At = Ue((L->Ends[0].Berth + L->Ends[1].Berth) * 0.5, L->Water);
		FActorSpawnParameters P;
		P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		P.ObjectFlags |= RF_Transient;
		ATorqueFerry* F = World->SpawnActor<ATorqueFerry>(ATorqueFerry::StaticClass(), At, FRotator::ZeroRotator, P);
		if (!F) continue;
		F->Cross = std::make_shared<TFer::Crossing>(C);
		F->Lay = L;
		F->ListRow = C.Listed;
		const TFer::Listed* Row = C.Listed >= 0 && C.Listed < int32(D.List.size()) ? &D.List[size_t(C.Listed)] : nullptr;
		for (int32 E = 0; E < 2; ++E) F->PlaceName[E] = Row ? UTF8_TO_TCHAR(Row->Places[E].c_str()) : (E == 0 ? TEXT("one side") : TEXT("the other side"));
		// (tied up at the end nearer the rider, so there's no wait to get on)
		const double D0 = FVector2D::Distance(RiderAt, FVector2D(L->Ends[0].Head.X, L->Ends[0].Head.Z));
		const double D1 = FVector2D::Distance(RiderAt, FVector2D(L->Ends[1].Head.X, L->Ends[1].Head.Z));
		F->DockEnd = D1 < D0 ? 1 : 0;
		F->Build(Plan);
		Have.Add(F);
		++TFerI::Version();
		UE_LOG(LogTemp, Log, TEXT("Torque ferry: %s stood up (%s to %s, water %.2f m, %.0f m berth to berth, a sailing %.0f s), tied up at %s"),
			UTF8_TO_TCHAR(C.Name.c_str()), *F->PlaceName[0], *F->PlaceName[1], L->Water, L->PathLength, L->Seconds, *F->PlaceName[F->DockEnd]);
		for (int32 E = 0; E < 2; ++E)
		{
			const TFer::End& N = L->Ends[E];
			UE_LOG(LogTemp, Log, TEXT("Torque ferry:   %s: slipway head %.2f m, 1 in %.1f, ramp %.1f deg, approach %d points (%.0f m from the map's road)"), *F->PlaceName[E], N.HeadY,
				N.Grade > 0.0 ? 1.0 / N.Grade : 0.0, FMath::RadiansToDegrees(N.RampDown), int32(N.Approach.size()), N.Road.DistanceTo(N.Head));
		}
	}
}

void ATorqueFerry::ContactBoxes(const UWorld* World, std::vector<TorqueSim::ContactBox>& Out)
{
	for (const TWeakObjectPtr<ATorqueFerry>& Weak : TFerI::Live())
	{
		const ATorqueFerry* F = Weak.Get();
		if (!F || F->GetWorld() != World) continue;
		const bool bTied = F->State == EState::Docked;
		for (int32 I = 0; I < F->Boxes.Num(); ++I)
		{
			const UBoxComponent* Box = F->Boxes[I];
			if (!Box || !F->BoxSolid.IsValidIndex(I) || !F->BoxSolid[I]) continue;
			if (Box->GetCollisionEnabled() == ECollisionEnabled::NoCollision) continue;   // (an open gate or barrier)
			// (the boat's own walls only while it's tied up: under way nobody rides)
			const bool bOnBoat = Box->GetAttachParent() == F->Boat || (F->Ramps.Contains(Box->GetAttachParent()));
			if (bOnBoat && !bTied) continue;
			// Sim frame: metres, x east, z the world's y (Unreal's), y up.
			const FVector C = Box->GetComponentLocation();
			const FVector U = Box->GetComponentQuat().RotateVector(FVector::XAxisVector);
			const FVector E = Box->GetUnscaledBoxExtent();
			TorqueSim::ContactBox B;
			B.Cx = C.X / 100.0;
			B.Cz = C.Y / 100.0;
			B.Ux = U.X;
			B.Uz = U.Y;
			B.Hu = E.X / 100.0;
			B.Hv = E.Y / 100.0;
			B.Y0 = (C.Z - E.Z) / 100.0;
			B.Y1 = (C.Z + E.Z) / 100.0;
			Out.push_back(B);
		}
	}
}

int32 ATorqueFerry::ContactVersion()
{
	return TFerI::Version();
}

FString ATorqueFerry::HintFor(const ATorqueBikePawn* Pawn)
{
	using namespace TorqueFerryImpl;
	if (!Pawn || !Pawn->GetWorld()) return FString();
	const FVector Me = Pawn->IsOnFoot() ? Pawn->FeetAt() : Pawn->GetActorLocation();
	const ATorqueFerry* Best = nullptr;
	int32 BestEnd = 0;
	double BestD = HintReach * 100.0;
	for (const TWeakObjectPtr<ATorqueFerry>& Weak : TFerI::Live())
	{
		const ATorqueFerry* F = Weak.Get();
		if (!F || F->GetWorld() != Pawn->GetWorld() || !F->Lay) continue;
		if (F->OnBoard(Me) || F->OnBoard(Pawn->GetActorLocation()))
		{
			Best = F;
			BestD = -1.0;
			break;
		}
		for (int32 E = 0; E < 2; ++E)
		{
			const double Dd = FVector::Dist2D(Me, Ue(F->Lay->Ends[E].Head, 0.0));
			if (Dd < BestD)
			{
				BestD = Dd;
				Best = F;
				BestEnd = E;
			}
		}
	}
	if (!Best) return FString();
	const ATorqueFerry& F = *Best;
	const FString Name = UTF8_TO_TCHAR(F.Cross->Name.c_str());
	const int32 Left = FMath::Max(0, FMath::RoundToInt(F.State == EState::Sailing ? F.SailFor - F.Clock + RampSeconds : F.State == EState::Raising
		? (1.0 - F.RampAt[F.DockEnd]) * RampSeconds + F.Lay->Seconds + RampSeconds : F.State == EState::Lowering ? F.RampAt[F.DockEnd] * RampSeconds : 0.0));
	const bool bBikeAboard = F.OnBoard(Pawn->GetActorLocation());
	if (BestD < 0.0)
	{
		// Aboard.
		const FString To = F.State == EState::Docked ? F.PlaceName[1 - F.DockEnd] : F.PlaceName[F.DockEnd];
		switch (F.State)
		{
		case EState::Docked:
			if (!bBikeAboard) return FString::Printf(TEXT("%s ferry: bring the bike aboard to sail"), *Name);
			if (!Pawn->IsOnFoot()) return FString::Printf(TEXT("%s ferry to %s: park in the bike bay by the tower and get off; it sails once everyone's aboard"), *Name, *To);
			if (F.ReadyFor > 0.0)
				return FString::Printf(TEXT("%s ferry: sailing for %s in %d"), *Name, *To, FMath::Max(1, FMath::CeilToInt(ReadySeconds - F.ReadyFor)));
			return F.bJustIn
				? FString::Printf(TEXT("%s ferry: tied up at %s, ramp down: get back on and ride off"), *Name, *F.PlaceName[F.DockEnd])
				: FString::Printf(TEXT("%s ferry: waiting for everyone to park aboard"), *Name);
		case EState::Raising:
		case EState::Sailing:
			return FString::Printf(TEXT("%s ferry to %s: %d:%02d to go. Walk about (the stair by the tower goes up top); get back on once the ramp is down"),
				*Name, *To, Left / 60, Left % 60);
		case EState::Lowering:
			return FString::Printf(TEXT("%s ferry: arriving at %s, the ramp's coming down"), *Name, *To);
		}
		return FString();
	}
	// At a slipway.
	const FString Other = F.PlaceName[1 - BestEnd];
	if (F.State == EState::Docked && F.DockEnd == BestEnd)
		return FString::Printf(TEXT("%s ferry to %s: ride slowly down the slipway and up the ramp onto the deck"), *Name, *Other);
	if (F.State == EState::Docked)
		return FString::Printf(TEXT("%s ferry to %s: the boat's at the other side; wait at the barrier and it comes over"), *Name, *Other);
	return FString::Printf(TEXT("%s ferry to %s: the boat's on its way, about %d s"), *Name, *Other, F.DockEnd == BestEnd ? Left : Left + int32(F.Lay->Seconds) + 2 * int32(RampSeconds));
}

FString ATorqueFerry::Test(const UWorld* World, const FString& What, ATorqueBikePawn* Pawn)
{
	using namespace TorqueFerryImpl;
	if (!Pawn) return TEXT("no bike");
	ATorqueFerry* F = nullptr;
	double Nearest = 1e30;
	for (const TWeakObjectPtr<ATorqueFerry>& Weak : TFerI::Live())
		if (Weak.IsValid() && Weak->GetWorld() == World)
		{
			const double Dd = FVector::Dist2D(Weak->GetActorLocation(), Pawn->GetActorLocation());
			if (Dd < Nearest) { Nearest = Dd; F = Weak.Get(); }
		}
	if (!F || !F->Lay) return TEXT("no ferry stood up near the bike (ride within 1.5 km of a listed crossing)");
	static const TCHAR* States[4] = {TEXT("tied up"), TEXT("raising its ramp"), TEXT("sailing"), TEXT("lowering its ramp")};
	const FString Where = FString::Printf(TEXT("%s: %s %s %s, %.0f%% of the way, %.0f s in this state; the bike %s aboard"), UTF8_TO_TCHAR(F->Cross->Name.c_str()),
		States[int32(F->State)], F->State == EState::Docked ? TEXT("at") : TEXT("for"), *F->PlaceName[F->DockEnd], F->Along * 100.0, F->Clock,
		F->OnBoard(Pawn->GetActorLocation()) ? TEXT("is") : TEXT("isn't"));
	if (What.StartsWith(TEXT("goto")))
	{
		// goto:<end>: the bike at that end's slipway head, facing down it.
		const int32 E = FMath::Clamp(What.Len() > 5 ? FCString::Atoi(*What.Mid(5)) : F->DockEnd, 0, 1);
		const TFer::End& N = F->Lay->Ends[E];
		const TorqueWorld::P2 At = N.Head - N.Axis * 6.0;
		return Pawn->TestPlaceAt(At.X * 100.0, At.Z * 100.0, FMath::RadiansToDegrees(std::atan2(N.Axis.Z, N.Axis.X))) + TEXT("; ") + Where;
	}
	if (What == TEXT("board"))
	{
		// board: the bike put in the bike bay facing the bow (the ferry must be tied up), then get off with foot:off.
		if (F->State != EState::Docked) return TEXT("not tied up: ") + Where;
		const FVector Bay = F->Boat->GetComponentTransform().TransformPosition(FVector((BayX0 + BayX1) * 0.5, (BayY0 + BayY1) * 0.5, Deck));
		const FVector Bow = F->Boat->GetComponentQuat().RotateVector(FVector::YAxisVector);
		return Pawn->TestPlaceAt(Bay.X, Bay.Y, Bow.Rotation().Yaw) + TEXT("; ") + Where;
	}
	return Where;
}
