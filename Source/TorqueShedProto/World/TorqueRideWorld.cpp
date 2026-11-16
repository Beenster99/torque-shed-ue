#include "World/TorqueRideWorld.h"
#include "World/RideCosts.h"
#include "World/TorqueRidePart.h"
#include "World/WorldRoam.h"
#include <algorithm>
#include "TorqueDash.h"
#include "Coop/TorqueSettings.h"
#include "Coop/TorqueGameState.h"
#include "Shed/TorqueProfile.h"

#include "Async/Async.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/CriticalSection.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/ScopeLock.h"
#include "ProceduralMeshComponent.h"

#include "World/WorldRide.h"
#include "World/WorldFiles.h"
#include "World/WorldRideDiary.h"
#include "World/WorldRoute.h"
#include "World/TorqueWorldSubsystem.h"
#include "Engine/GameInstance.h"
#include "HAL/FileManager.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Scalability.h"
#include "UObject/UObjectGlobals.h"
#include "RenderCore.h"
#include "RHI.h"

#include <atomic>
#include <cmath>

// What the workers and the actor share: the squares made and waiting to be put in place.
// [perf, build 149] A square's meshes as procedural mesh sections, readied on its worker (see PreparedNow).
using FRidePrepared = TMap<const TorqueWorld::RideMesh*, FProcMeshSection>;

struct FTorqueRideJobs
{
	FCriticalSection Lock;
	TArray<TPair<FIntVector, TSharedPtr<TorqueWorld::RideMesh, ESPMode::ThreadSafe>>> Made;
	TMap<FIntVector, TSharedPtr<FRidePrepared, ESPMode::ThreadSafe>> Prepared;   // by square, beside Made
	// [perf, build 149] Pieces of road made on workers (RidePlan::RoadPiece, then readied), by piece key.
	// (with the plan it was made from: a piece from a plan since grown is dropped, its road may be redrawn)
	TArray<TTuple<int64, TSharedPtr<TorqueWorld::RideMesh, ESPMode::ThreadSafe>, TSharedPtr<FRidePrepared, ESPMode::ThreadSafe>, std::shared_ptr<const TorqueWorld::RidePlan>>> RoadMade;
	std::atomic<int32> RoadBusy{0};
	std::atomic<int32> Busy{0};
	std::atomic<bool> bStop{false};
};

namespace TRWI
{
// [perf, build 150] What of the ride's world goes in the ray-tracing scene (Lumen's hardware ray
// tracing): procedural meshes are gathered for it again every frame, so only what lights the view
// near the rider. Squares' standing parts (walls, buildings) up to this level of the quadtree (0 the
// nearest 256 m squares), water only on the nearest, roads only if torque.RideRoadsRayTraced.
TAutoConsoleVariable<int32> CVarRayTraceLevel(TEXT("torque.RideRayTraceLevel"), 1,
	TEXT("Ride UK: squares' walls and buildings are in the ray-tracing scene up to this quadtree level (0 nearest .. 3 all; -1 none). For squares made after the change."));
TAutoConsoleVariable<float> CVarPlantPatch(TEXT("torque.RidePlantPatch"), 64.0f,
	TEXT("Ride UK: the patches (m) grass with a near limit is grouped in (32 until build 149; bigger = fewer groups). For squares made after the change."));
TAutoConsoleVariable<int32> CVarPool(TEXT("torque.RidePool"), 1,
	TEXT("Ride UK: pieces of the world let go are kept to be used again (1, build 150) or destroyed (0)."));
TAutoConsoleVariable<float> CVarRegisterMs(TEXT("torque.RideRegisterMs"), 2.0f,
	TEXT("Ride UK: ms a frame for putting waiting walls, buildings and water in the scene (0: each at once, as until 149)."));
TAutoConsoleVariable<float> CVarRoadLookMs(TEXT("torque.RideRoadLookMs"), 1.5f,
	TEXT("Ride UK: ms each look over the roads near the rider may take (every 0.2 s), carrying on next time (0: all at once, as until 150)."));
TAutoConsoleVariable<int32> CVarRoadsRayTraced(TEXT("torque.RideRoadsRayTraced"), 0,
	TEXT("Ride UK: pieces of road in the ray-tracing scene (1) or not (0, build 150). For pieces made after the change."));
constexpr int32 Levels = 4;
constexpr double Finest = 256.0;         // metres across a square of level 0; each level's are 4 times the last's
constexpr int32 CellsOf[Levels] = {64, 128, 64, 64};
constexpr double SplitWithin[Levels] = {0.0, 400.0, 1500.0, 6000.0};   // a square of this level is split when the rider is this near it
// The ground is laid out this far: the far hills, to the skyline heights' own reach from the road
// (Terrain::LiveReach, 18 km). Beyond 6 km in 16 km squares, a point every 256 m.
constexpr double Reach = 16000.0;
constexpr int32 ShadowLevels = 2;        // the squares of these levels cast shadows (the far hills' are past the shadows' reach)
constexpr int32 PiecePoints = 64;        // a piece of road: 256 m
constexpr double RoadReach = 2500.0;
constexpr int32 AtATime = 6;             // workers (6 since build 149: the PC has 16 cores; was 3)
constexpr int32 RoadsAtATime = 2;        // [perf, build 149] workers making pieces of road

double SizeOf(int32 Level)
{
	double S = Finest;
	for (int32 I = 0; I < Level; ++I) S *= 4.0;
	return S;
}

// How far (At) is from a square, 0 inside it.
double Away(const FVector2D& At, const FIntVector& Key)
{
	const double S = SizeOf(Key.X);
	const double X0 = Key.Y * S, Z0 = Key.Z * S;
	const double Dx = FMath::Max3(X0 - At.X, 0.0, At.X - (X0 + S)), Dz = FMath::Max3(Z0 - At.Y, 0.0, At.Y - (Z0 + S));
	return FMath::Sqrt(Dx * Dx + Dz * Dz);
}

// [perf, build 149] One mesh as MakePart's CreateMeshSection_LinearColor would make its section (the same
// vertices, white where it has no colours, the colours' bytes unconverted, indices clamped to the
// points), on a worker; meshes MakePart leaves out are left out here too.
void Prepare(const TorqueWorld::RideMesh* M, FRidePrepared& Out)
{
	const int32 Count = M ? int32(M->Vertices()) : 0;
	if (Count < 3 || M->Idx.size() < 3) return;
	FProcMeshSection& Sec = Out.Add(M);
	const bool bColoured = M->Col.size() == M->Pos.size();
	const bool bAlpha = M->Alpha.size() == size_t(Count);
	Sec.ProcVertexBuffer.SetNum(Count);
	for (int32 I = 0; I < Count; ++I)
	{
		const size_t V = size_t(I) * 3;
		FProcMeshVertex& Pt = Sec.ProcVertexBuffer[I];
		// The ride's frame is metres, x east, y up, z south; Unreal's centimetres, x east, y south, z up.
		Pt.Position = FVector(double(M->Pos[V]) * 100.0, double(M->Pos[V + 2]) * 100.0, double(M->Pos[V + 1]) * 100.0);
		Pt.Normal = FVector(double(M->Nrm[V]), double(M->Nrm[V + 2]), double(M->Nrm[V + 1]));
		Pt.UV0 = FVector2D(double(M->Uv[size_t(I) * 2]), double(M->Uv[size_t(I) * 2 + 1]));
		Pt.Color = bColoured ? FLinearColor(M->Col[V], M->Col[V + 1], M->Col[V + 2], bAlpha ? M->Alpha[size_t(I)] : 1.0f).ToFColor(false) : FColor(255, 255, 255);
		Pt.Tangent = FProcMeshTangent();
		Sec.SectionLocalBox += Pt.Position;
	}
	const int32 Indices = int32(M->Idx.size() / 3) * 3;
	Sec.ProcIndexBuffer.SetNumUninitialized(Indices);
	for (int32 I = 0; I < Indices; ++I) Sec.ProcIndexBuffer[I] = FMath::Min(M->Idx[size_t(I)], uint32(Count - 1));
	Sec.bSectionVisible = true;
}

void PrepareSquare(const TorqueWorld::RideMesh& Mesh, FRidePrepared& Out)
{
	Prepare(&Mesh, Out);
	Prepare(Mesh.Water.get(), Out);
	if (const TorqueWorld::RideMesh* E = Mesh.Extra.get())
	{
		Prepare(E, Out);
		Prepare(E->Stone.get(), Out);
		Prepare(E->Masonry.get(), Out);
		Prepare(E->Render.get(), Out);
		Prepare(E->Roofing.get(), Out);
		Prepare(E->Brickwork.get(), Out);
		Prepare(E->Night.get(), Out);
		Prepare(E->Signals.get(), Out);
		Prepare(E->Signs.get(), Out);
	}
}

// [perf, build 149] Instances culled nearer than a square of its level can ever be (a square of level
// 1 and up is split before the rider comes within SplitWithin of it): never drawn there, so never
// made. Build 148 had 5,700-11,700 plant groups in the scene, most of them on squares past every
// grass's cull distance.
bool Unseen(const FIntVector& Key, float CullCm)
{
	return Key.X > 0 && double(CullCm) < SplitWithin[Key.X] * 100.0 * 0.97;
}

bool Overlap(const FIntVector& A, const FIntVector& B)
{
	const double Sa = SizeOf(A.X), Sb = SizeOf(B.X);
	const double Ax = A.Y * Sa, Az = A.Z * Sa, Bx = B.Y * Sb, Bz = B.Z * Sb;
	return Ax < Bx + Sb - 0.5 && Bx < Ax + Sa - 0.5 && Az < Bz + Sb - 0.5 && Bz < Az + Sa - 0.5;
}
} // namespace TRWI

ATorqueRideWorld::ATorqueRideWorld()
{
	PrimaryActorTick.bCanEverTick = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Tags.Add(TEXT("Land"));
}

TWeakObjectPtr<ATorqueRideWorld> ATorqueRideWorld::Current;

bool ATorqueRideWorld::PlaceHeard(double& Sea, double& Water, double& Town, double& Sheep, double& Cattle)
{
	const ATorqueRideWorld* Ride = Current.Get();
	if (!Ride || !Ride->Plan) return false;
	Sea = Ride->Heard[0];
	Water = Ride->Heard[1];
	Town = Ride->Heard[2];
	Sheep = Ride->Heard[3];
	Cattle = Ride->Heard[4];
	return true;
}

void ATorqueRideWorld::DropStreetLights(const FIntVector& Key)
{
	if (TArray<TWeakObjectPtr<UPointLightComponent>>* Lamps = StreetLights.Find(Key))
	{
		for (const TWeakObjectPtr<UPointLightComponent>& Lamp : *Lamps)
			if (Lamp.IsValid()) Lamp->DestroyComponent();
		StreetLights.Remove(Key);
	}
}

FString ATorqueRideWorld::DiaryFolder()
{
	return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("RideDiary"));
}

void ATorqueRideWorld::SaveDiary()
{
	// (a ride of a few hundred metres, a look round the start, isn't kept)
	if (!Diary || (Diary->Metres < 300.0 && Diary->Photos.empty())) return;
	const FString Path = FPaths::Combine(DiaryFolder(), UTF8_TO_TCHAR((Diary->Id + ".json").c_str()));
	if (!FFileHelper::SaveStringToFile(UTF8_TO_TCHAR(Diary->ToJson().c_str()), *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		UE_LOG(LogTemp, Warning, TEXT("Torque ride diary: couldn't write %s"), *Path);
	SinceDiarySaved = 0.0;
}

FString ATorqueRideWorld::TakePhoto()
{
	ATorqueRideWorld* Ride = Current.Get();
	if (!Ride || !Ride->Diary) return TEXT("Photos are for the ride diary: take them on a Ride UK ride");
	IFileManager::Get().MakeDirectory(*DiaryFolder(), true);
	const std::string Name = Ride->Diary->Id + "_photo_" + std::to_string(Ride->Diary->Photos.size() + 1) + ".png";
	const FString Path = FPaths::Combine(DiaryFolder(), UTF8_TO_TCHAR(Name.c_str()));
	FScreenshotRequest::RequestScreenshot(Path, false, false);
	Ride->Diary->AddPhoto(Name, FDateTime::UtcNow().ToUnixTimestamp());
	Ride->SaveDiary();
	UE_LOG(LogTemp, Display, TEXT("Torque ride diary: photo %d at %.1f km (%s)"), int32(Ride->Diary->Photos.size()), Ride->Diary->Metres / 1000.0, *Path);
	return FString::Printf(TEXT("Photo %d taken: it's in the ride diary (Plan a ride > Diary)"), int32(Ride->Diary->Photos.size()));
}

bool ATorqueRideWorld::SurfaceUnder(const FVector& At, int32& Kind, double& Lift)
{
	const ATorqueRideWorld* Ride = Current.Get();
	if (!Ride || !Ride->Plan) return false;
	const TorqueWorld::RidePlan::RideSurface S = Ride->Plan->SurfaceAt(At.X / 100.0, At.Y / 100.0);
	Kind = S.Kind;
	Lift = S.Lift - S.Drawn;   // (the ray hit the mark as drawn, a hair over the road)
	return true;
}

bool ATorqueRideWorld::CattleGrids(TArray<TPair<FVector, FVector>>& Out)
{
	Out.Reset();
	const ATorqueRideWorld* Ride = Current.Get();
	if (!Ride || !Ride->Plan) return false;
	for (const TorqueWorld::RidePlan::Furnish& F : Ride->Plan->Furniture)
		if (F.Kind == TorqueWorld::RidePlan::CattleGrid)
			Out.Emplace(FVector(F.At.X, F.Height, F.At.Z), FVector(F.Face.X, F.Size, F.Face.Z));
	return true;
}

bool ATorqueRideWorld::RoadBack(const FVector& At, double Heading, FVector& OutPos, double& OutHeading, int32& Which)
{
	const ATorqueRideWorld* Ride = Current.Get();
	if (!Ride || !Ride->Plan || !Ride->Plan->Ok) return false;
	using TorqueWorld::RidePlan;
	const RidePlan& P = *Ride->Plan;
	RidePlan::NearOpts Opts;
	RidePlan::NearHit Hit;
	if (!P.NearestOf(At.X, At.Z, Opts, Hit) || Hit.Dist > 80.0 || Hit.Index < 0) return false;
	const std::vector<TorqueWorld::RoadPoint>& Pts = P.RoadOf(Hit.Which);
	if (Pts.size() < 2) return false;
	const size_t I = FMath::Min(size_t(Hit.Index), Pts.size() - 2);
	double Dx = Pts[I + 1].At.X - Pts[I].At.X, Dz = Pts[I + 1].At.Z - Pts[I].At.Z;
	const double Len = FMath::Sqrt(Dx * Dx + Dz * Dz);
	if (Len < 1e-3) return false;
	Dx /= Len;
	Dz /= Len;
	// (the way along the road nearer the way the bike was going)
	const double Fx = -FMath::Sin(Heading), Fz = -FMath::Cos(Heading);
	if (Dx * Fx + Dz * Fz < 0.0)
	{
		Dx = -Dx;
		Dz = -Dz;
	}
	// (left of (dx, dz) with x east and z south is (dz, -dx)); a third of the way out from the middle
	const double Aside = FMath::Min(1.5, Hit.Half * 0.35);
	OutPos = FVector(Hit.Foot.X + Dz * Aside, Hit.Height, Hit.Foot.Z - Dx * Aside);
	OutHeading = FMath::Atan2(-Dx, -Dz);
	Which = Hit.Which;
	return true;
}

int32 ATorqueRideWorld::TestLimit = INDEX_NONE;
TArray<FString> ATorqueRideWorld::PropCredits;

void ATorqueRideWorld::NavFor(const FVector& At, FTorqueDash& Dash, double& Was)
{
	ATorqueRideWorld* Ride = Current.Get();
	Dash.bNav = Ride && Ride->Plan && Ride->Plan->Ok && !Ride->Plan->Turns.empty();
	Dash.bAtFuel = false;
	Dash.bNavRerouting = false;
	Dash.bNavVia = false;
	if (!Dash.bNav) return;
	using TorqueWorld::RidePlan;
	const double X = At.X / 100.0, Z = At.Y / 100.0;
	RidePlan::RideNav N = Ride->Plan->NavAt(X, Z, Was);
	Was = N.Along;
	// [ride anywhere] Off the ride's road a few seconds: the way back by the map's roads, from the
	// junction ahead of the rider, found again whenever they leave it too. The guide (the maps' route)
	// follows it; back on the ride's road, the ride's own again.
	FRideNavState& S = Ride->NavState;
	const double Now = FPlatformTime::Seconds();
	const FVector2D Here(X, Z);
	if (S.Prev.X < 1e11 && FVector2D::Distance(Here, S.Prev) > 2.0) S.Heading = FMath::Atan2(-(Here.X - S.Prev.X), -(Here.Y - S.Prev.Y));
	if (S.Prev.X > 1e11 || FVector2D::Distance(Here, S.Prev) > 2.0) S.Prev = Here;
	// [waypoint] Going by a place picked in the info panel: its way, on or off the ride's road, until
	// it's reached (then the way on is the way back to the ride's road, as after any detour).
	bool bViaNav = false;
	if (S.bVia)
	{
		RidePlan::RideNav Via;
		bool bOnIt = false;
		if (S.Detour && S.bDetourVia)
		{
			Via = Ride->Plan->NavDetour(*S.Detour, X, Z);
			bOnIt = !Via.OffRoute;
			const bool bThere = FVector2D::Distance(Here, FVector2D(S.ViaX, S.ViaZ)) < 150.0 ||
				FVector2D::Distance(Here, FVector2D(S.Detour->Via.X, S.Detour->Via.Z)) < 40.0 || (bOnIt && Via.Along > S.Detour->ViaAlong + 10.0);
			if (bThere)
			{
				S.bVia = S.bDetourVia = false;
				++S.ViaReached;
				UE_LOG(LogTemp, Log, TEXT("Torque sat nav: waypoint %s reached; on to the finish (%.2f km)"), *FString(UTF8_TO_TCHAR(S.ViaName.c_str())), Via.ToFinish / 1000.0);
			}
		}
		if (S.bVia)
		{
			if (!bOnIt && Now - S.ViaTry > 4.0)
			{
				S.ViaTry = Now;
				const std::shared_ptr<const TorqueWorld::RideDetour> Way = Ride->Plan->DetourVia(X, Z, S.Heading, Was, S.ViaX, S.ViaZ, S.ViaName);
				if (Way)
				{
					S.Detour = Way;
					S.bDetourVia = true;
					Via = Ride->Plan->NavDetour(*Way, X, Z);
					bOnIt = !Via.OffRoute;
					Ride->Guide = TorqueWorld::GuideOfDetour(*Ride->Plan, *Way, Ride->Guide ? Ride->Guide->Version + 1 : 1);
					OnRoamChanged.Broadcast();
					UE_LOG(LogTemp, Log, TEXT("Torque sat nav: the way by %s: %.2f km, reached at %.2f km, %d turns, back on the ride's road at %.2f km"),
						*FString(UTF8_TO_TCHAR(S.ViaName.c_str())), Way->Length / 1000.0, Way->ViaAlong / 1000.0, int32(Way->Turns.size()), Way->RejoinAlong / 1000.0);
				}
				else
					UE_LOG(LogTemp, Log, TEXT("Torque sat nav: no way by %s yet (no road near it in the map loaded so far, or none there and back)"), *FString(UTF8_TO_TCHAR(S.ViaName.c_str())));
			}
			if (bOnIt)
			{
				bViaNav = true;
				S.OffSince = -1.0;
				N.OffRoute = false;
				N.Turn = Via.Turn;
				N.ToTurn = Via.ToTurn;
				N.Onto = Via.Onto;
				N.ToFinish = Via.ToFinish;
				N.Road = Via.Road;
				N.Limit = 0;
				for (double& To : N.ToStop) To = -1.0;
			}
			else if (S.bDetourVia)
			{
				bViaNav = true;   // off the way by it: found again in a moment
				N.OffRoute = true;
				Dash.bNavRerouting = true;
			}
		}
	}
	if (bViaNav) {}
	else if (!N.OffRoute)
	{
		S.OffSince = -1.0;
		if (S.Detour) { S.Detour.reset(); Ride->Guide = TorqueWorld::GuideOfPlan(*Ride->Plan, Ride->Guide ? Ride->Guide->Version + 1 : 1); OnRoamChanged.Broadcast(); }
	}
	else
	{
		if (S.OffSince < 0.0) S.OffSince = Now;
		RidePlan::RideNav Back;
		bool bOnIt = false;
		if (S.Detour)
		{
			Back = Ride->Plan->NavDetour(*S.Detour, X, Z);
			bOnIt = !Back.OffRoute;
		}
		if (!bOnIt && Now - S.OffSince > 3.0 && Now - S.LastTry > 4.0)
		{
			S.LastTry = Now;
			S.Detour = Ride->Plan->DetourFrom(X, Z, S.Heading, Was);
			if (S.Detour)
			{
				Back = Ride->Plan->NavDetour(*S.Detour, X, Z);
				bOnIt = !Back.OffRoute;
				Ride->Guide = TorqueWorld::GuideOfDetour(*Ride->Plan, *S.Detour, Ride->Guide ? Ride->Guide->Version + 1 : 1);
				OnRoamChanged.Broadcast();
			}
		}
		if (bOnIt)
		{
			// (the way back's turns and distance; the limits and stops stay the ride road's, but for the road's name)
			N.OffRoute = false;
			N.Turn = Back.Turn;
			N.ToTurn = Back.ToTurn;
			N.Onto = Back.Onto;
			N.Exit = Back.Exit;
			N.ToFinish = Back.ToFinish;
			N.Road = Back.Road;
			N.Limit = 0;
			for (double& To : N.ToStop) To = -1.0;
		}
		else Dash.bNavRerouting = true;
	}
	Dash.bNavOff = N.OffRoute;
	Dash.bNavVia = bViaNav && !N.OffRoute && N.Turn == RidePlan::Finish;
	Dash.NavTurn = N.Turn;
	Dash.NavTo = N.ToTurn;
	Dash.NavOnto = UTF8_TO_TCHAR(N.Onto.c_str());
	Dash.NavExit = N.Exit;   // [OSM stage 5B]
	// [build 148] (the hub's robot rides look for it in the log: the HUD's words, once a roundabout)
	if (N.Exit > 0 && N.Exit != Ride->LastNavExit)
	{
		UE_LOG(LogTemp, Log, TEXT("Torque sat nav: %s%s%s in %.0f m"), *FTorqueDash::ExitWords(N.Exit), N.Onto.empty() ? TEXT("") : TEXT(", "), UTF8_TO_TCHAR(N.Onto.c_str()), N.ToTurn);
	}
	Ride->LastNavExit = N.Exit;
	Dash.NavLeft = N.ToFinish;
	Dash.bAtFuel = N.ToStop[RidePlan::StopFuel] >= 0.0 && N.ToStop[RidePlan::StopFuel] < 25.0;
	// [OSM stage 1] The road's facts.
	Dash.NavLimit = N.Limit;
	Dash.bNavLimitMph = N.LimitMph;
	Dash.bNavLimitNational = N.LimitNational;
	Dash.bNavLimitAssumed = N.LimitAssumed;
	if (TestLimit != INDEX_NONE)
	{
		Dash.NavLimit = TestLimit < 0 ? 60 : TestLimit;
		Dash.bNavLimitNational = TestLimit < 0;
		Dash.bNavLimitAssumed = false;
	}
	Dash.NavRoad = UTF8_TO_TCHAR(N.Road.c_str());
	Dash.NavPlace = UTF8_TO_TCHAR(N.Place.c_str());
	// (the next fuel and the next cafe, whichever's nearer; a viewpoint if it's close)
	Dash.NavStop.Reset();
	double Best = 1e18;
	const bool bMetric = FTorqueSettings::Get().Units == 0;
	for (int32 K : {int32(RidePlan::StopFuel), int32(RidePlan::StopCafe), int32(RidePlan::StopView)})
	{
		const double To = N.ToStop[K];
		if (To < 0.0 || To > (K == RidePlan::StopView ? 3000.0 : 30000.0) || To >= Best) continue;
		Best = To;
		const FString Called = UTF8_TO_TCHAR(N.StopCalled[K].c_str());
		Dash.NavStop = FString::Printf(TEXT("%s %s%s%s"), UTF8_TO_TCHAR(RidePlan::StopName(K)), *FTorqueDash::NavDistance(To, bMetric), Called.IsEmpty() ? TEXT("") : TEXT(": "), *Called);
	}
	// [places stage 2] At a place's stop (its pull-off, car park, lay-by or board): the sat nav says so.
	const int32 StopHere = Ride->Plan->PlaceStopAt(X, Z, 30.0);
	if (StopHere >= 0)
	{
		const std::string Name = Ride->Plan->PlaceStopName(StopHere);
		// [places stage 5] (and its site note's words for a famous one: "38 stones, about 5,000 years old")
		const std::string Note = Ride->Plan->PlaceStopNote(StopHere);
		if (!Name.empty()) Dash.NavStop = Note.empty() ? FString::Printf(TEXT("Arrived: %s"), UTF8_TO_TCHAR(Name.c_str())) : FString::Printf(TEXT("Arrived: %s. %s"), UTF8_TO_TCHAR(Name.c_str()), UTF8_TO_TCHAR(Note.c_str()));
		if (Ride->LastPlaceStop != StopHere)
		{
			UE_LOG(LogTemp, Log, TEXT("Torque places: arrived at '%s' (stop %d)%s%s"), UTF8_TO_TCHAR(Name.c_str()), StopHere, Note.empty() ? TEXT("") : TEXT(": "), UTF8_TO_TCHAR(Note.c_str()));
			Ride->bFirstVisitHere = VisitPlace(*Ride->Plan, StopHere);
		}
		if (Ride->bFirstVisitHere && !Name.empty()) Dash.NavStop += TEXT(" First visit.");
	}
	Ride->LastPlaceStop = StopHere;
}

bool ATorqueRideWorld::VisitPlace(const TorqueWorld::RidePlan& InPlan, int32 Stop)
{
	// [places stage 7] Onto this profile's visited list (once), saved at once.
	const FString Key = UTF8_TO_TCHAR(InPlan.PlaceStopKey(Stop).c_str());
	const FString Name = UTF8_TO_TCHAR(InPlan.PlaceStopName(Stop).c_str());
	if (Name.IsEmpty()) return false;
	FTorqueProfile& Me = FTorqueProfile::Mine();
	if (!Me.Visit(Key, Name)) return false;
	Me.Save();
	UE_LOG(LogTemp, Log, TEXT("Torque places: first visit to '%s' (%s); %d places visited"), *Name, *Key, Me.Visited.Num());
	return true;
}

void ATorqueRideWorld::ViewSpotFor(const FVector& At, FTorqueDash& Dash)
{
	// [places stage 7] On foot at a place's view spot (the end of its path in): the line says what you're
	// looking at, and you've visited it.
	ATorqueRideWorld* Ride = Current.Get();
	if (!Ride || !Ride->Plan || !Ride->Plan->Ok) return;
	const int32 Spot = Ride->Plan->PlaceViewAt(At.X / 100.0, At.Y / 100.0, 8.0);
	if (Spot >= 0)
	{
		const std::string Name = Ride->Plan->PlaceStopName(Spot);
		if (!Name.empty()) Dash.NavStop = FString::Printf(TEXT("View of %s"), UTF8_TO_TCHAR(Name.c_str()));
		if (Ride->LastViewSpot != Spot)
		{
			UE_LOG(LogTemp, Log, TEXT("Torque places: at the view spot of '%s' (stop %d)"), UTF8_TO_TCHAR(Name.c_str()), Spot);
			VisitPlace(*Ride->Plan, Spot);
		}
	}
	Ride->LastViewSpot = Spot;
}

ATorqueRideWorld::FOnRoamChanged ATorqueRideWorld::OnRoamChanged;


std::shared_ptr<const TorqueWorld::RoamArea> ATorqueRideWorld::RoamNow()
{
	const ATorqueRideWorld* Ride = Current.Get();
	return Ride ? Ride->Roam : nullptr;
}

std::shared_ptr<const TorqueWorld::RideGuide> ATorqueRideWorld::GuideNow()
{
	const ATorqueRideWorld* Ride = Current.Get();
	return Ride ? Ride->Guide : nullptr;
}

bool ATorqueRideWorld::SetWaypoint(double Lat, double Lon, const FString& Name)
{
	ATorqueRideWorld* Ride = Current.Get();
	if (!Ride || !Ride->Plan || !Ride->Plan->Ok) return false;
	const TorqueWorld::P2 At = Ride->Plan->Area.Roads.FrameOf().FromGeo(Lat, Lon);
	FRideNavState& S = Ride->NavState;
	S.bVia = true;
	S.bDetourVia = false;   // (the next look finds the way by it, from wherever the rider is then)
	S.ViaX = At.X;
	S.ViaZ = At.Z;
	S.ViaName = TCHAR_TO_UTF8(*Name.Left(40));
	S.ViaTry = -1e9;
	UE_LOG(LogTemp, Log, TEXT("Torque sat nav: waypoint %s at (%.0f, %.0f) in the ride's frame"), *Name, At.X, At.Z);
	return true;
}

void ATorqueRideWorld::ClearWaypoint()
{
	ATorqueRideWorld* Ride = Current.Get();
	if (!Ride) return;
	FRideNavState& S = Ride->NavState;
	if (!S.bVia) return;
	S.bVia = false;
	if (S.bDetourVia && Ride->Plan)
	{
		// (the ride's own way again, or the way back to it from off it, found as after any detour)
		S.Detour.reset();
		S.bDetourVia = false;
		S.LastTry = -1e9;
		Ride->Guide = TorqueWorld::GuideOfPlan(*Ride->Plan, Ride->Guide ? Ride->Guide->Version + 1 : 1);
		OnRoamChanged.Broadcast();
	}
}

bool ATorqueRideWorld::HasWaypoint()
{
	const ATorqueRideWorld* Ride = Current.Get();
	return Ride && Ride->NavState.bVia;
}

int32 ATorqueRideWorld::WaypointsReached()
{
	const ATorqueRideWorld* Ride = Current.Get();
	return Ride ? Ride->NavState.ViaReached : 0;
}

void ATorqueRideWorld::Begin(std::shared_ptr<TorqueWorld::RidePlan> InPlan)
{
	Plan = InPlan;
	Current = this;
	if (Plan && Plan->Ok)
	{
		// [stage 7] This ride's page in the diary.
		Diary = std::make_shared<TorqueWorld::DiaryRide>();
		Diary->Id = TorqueWorld::DiaryRide::IdFor(FDateTime::UtcNow().ToUnixTimestamp());
		Diary->Title = !Plan->Title.empty() ? Plan->Title : Plan->Names.empty() ? std::string("Ride UK") : "Ride UK: " + Plan->Names.front() + " to " + Plan->Names.back();
		if (Plan->Request.Pins.size() >= 2) Diary->Code = TorqueWorld::Route::CodeOf(Plan->Request);
	}
	if (!Plan || !Plan->Ok || Plan->Road.size() < 2) return;
	// [ride anywhere] The world and the way as planned, for the maps.
	Roam = TorqueWorld::RoamOfPlan(Plan);
	Guide = TorqueWorld::GuideOfPlan(*Plan);
	NavState = FRideNavState();
	OnRoamChanged.Broadcast();
	Jobs = std::make_shared<FTorqueRideJobs>();
	// (the ground in the hand-built ride's scanned grass, moor and rock when Tools/ue_build_rideuk.py
	// could make it from Buttertubs' textures; the plain mottled one otherwise)
	LandMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/RideUK/Materials/M_RideLandScan.M_RideLandScan"));
	const bool bScannedLand = LandMaterial != nullptr;
	if (!LandMaterial) LandMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/RideUK/Materials/M_RideLand.M_RideLand"));
	RoadMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/RideUK/Materials/M_RideRoad.M_RideRoad"));
	StoneMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/RideUK/Materials/M_RideStone.M_RideStone"));
	MasonryMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/RideUK/Materials/M_RideMasonry.M_RideMasonry"));
	LampMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/RideUK/Materials/M_RideLamp.M_RideLamp"));
	RenderMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/RideUK/Materials/M_RideRender.M_RideRender"));
	WaterMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/RideUK/Materials/M_RideWater.M_RideWater"));
	NightMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/RideUK/Materials/M_RideNight.M_RideNight"));
	SignalMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/RideUK/Materials/M_RideSignal.M_RideSignal"));
	SignMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/RideUK/Materials/M_RideSign.M_RideSign"));
	RoofMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/RideUK/Materials/M_RideRoof.M_RideRoof"));
	BrickMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/RideUK/Materials/M_RideBrick.M_RideBrick"));
	if (!LandMaterial || !RoadMaterial)
	{
		UE_LOG(LogTemp, Warning, TEXT("Torque ride world: no materials in /Game/RideUK/Materials (run Tools/ue_build_rideuk.py); the ground is drawn plain"));
	}
	UE_LOG(LogTemp, Display, TEXT("Torque ride world: materials: ground %s, walls %s, houses %s"), bScannedLand ? TEXT("scanned") : TEXT("plain"),
		StoneMaterial ? TEXT("scanned stone") : TEXT("plain"), MasonryMaterial ? TEXT("scanned masonry") : TEXT("plain"));
	if (!StoneMaterial) StoneMaterial = RoadMaterial;
	if (!MasonryMaterial) MasonryMaterial = StoneMaterial;
	if (!RenderMaterial) RenderMaterial = RoadMaterial;
	if (!WaterMaterial) WaterMaterial = RoadMaterial;
	if (!NightMaterial) NightMaterial = RoadMaterial;   // (pale studs and glass, never lit)
	// [OSM stage 3] (roofs and brick in their vertex colours, plain, until the materials are made)
	UE_LOG(LogTemp, Display, TEXT("Torque ride world: roofs %s, brick %s"), RoofMaterial ? TEXT("coursed") : TEXT("plain"), BrickMaterial ? TEXT("coursed") : TEXT("plain"));
	if (!RoofMaterial) RoofMaterial = RoadMaterial;
	if (!BrickMaterial) BrickMaterial = RoadMaterial;
	// (before any square is made: whether its trees come as real ones, and how much grass)
	const bool bKit = FindKit();
	// [OSM stage 2] Which kinds of prop come from models (before any square is made).
	FindProps();
	for (int32 K = 0; K < TorqueWorld::RidePlan::PropKinds; ++K) Plan->PropModelled[K] = PropModels.IsValidIndex(K) && PropModels[K].Num() > 0;
	{
		// [fps plan step 2] The houses' windows and doors as the facade kit's instances when every kind of it
		// has its models in M_RideFacade (Tools/ue_build_rideuk.py prop_kit), unless -TorqueRideFacadeKit=0
		// (the hub's A/B: then drawn into the houses' own mesh, as 149).
		bool bFacade = true;
		for (int32 K = TorqueWorld::RidePlan::FirstFacade; K < TorqueWorld::RidePlan::PropKinds; ++K)
		{
			bFacade = bFacade && PropModels.IsValidIndex(K) && PropModels[K].Num() > 0;
			if (bFacade)
				for (const int32 Mi : PropModels[K])
				{
					const UMaterialInterface* Mat = PropKit[Mi].Material.Get();
					bFacade = bFacade && Mat && Mat->GetName().Contains(TEXT("RideFacade"));
				}
		}
		int32 FacadeAsked = 1;
		const bool bAsked = FParse::Value(FCommandLine::Get(), TEXT("TorqueRideFacadeKit="), FacadeAsked);
		Plan->FacadeKit = bFacade && FacadeAsked != 0;
		UE_LOG(LogTemp, Display, TEXT("Torque ride world: facade kit %s"), Plan->FacadeKit ? TEXT("on (windows and doors instanced)")
			: bAsked && FacadeAsked == 0 ? TEXT("off (-TorqueRideFacadeKit=0)") : TEXT("off (its models or M_RideFacade missing: Tools/ue_build_rideuk.py prop_kit)"));
	}
	bool bTreeModels = false;
	for (int32 K = TorqueWorld::RidePlan::FirstTree; K < TorqueWorld::RidePlan::PlantKinds && bKit; ++K) bTreeModels = bTreeModels || KindNear[K].Num() > 0;
	Plan->RealTrees = bTreeModels;
	Plan->RealHedges = bTreeModels && KindNear.IsValidIndex(TorqueWorld::RidePlan::HedgeKind) && KindNear[TorqueWorld::RidePlan::HedgeKind].Num() > 0;
	float Density = 1.0f;
	if (FParse::Value(FCommandLine::Get(), TEXT("TorqueFoliage="), Density)) Plan->GrassDensity = FMath::Clamp(Density, 0.0f, 1.0f);
	if (FParse::Value(FCommandLine::Get(), TEXT("TorqueFoliageCull="), CullScale)) CullScale = FMath::Clamp(CullScale, 0.05f, 4.0f);
	if (!bKit || KitTrees == 0) Plan->GrassDensity = 0.0;   // (nothing to draw it with: don't lay it out)
	const FVector2D Start(Plan->Road[0].At.X, Plan->Road[0].At.Z);
	const double T0 = FPlatformTime::Seconds();
	Update(Start, true);
	UpdateRoad(Start, true);
	UE_LOG(LogTemp, Display, TEXT("Torque ride world: %d squares of ground and %d pieces of road round the start in %.2f s; the road starts at %.0f m above the sea"),
		Tiles.Num(), RoadParts.Num(), FPlatformTime::Seconds() - T0, Plan->Road[0].Height);
	// [stage 8] The landmark bridges the ride's roads cross, made whole now and kept all ride: seen
	// from far off, as the real ones are.
	{
		std::string Text;
		std::vector<TorqueWorld::RidePlan::Landmark> Marks;
		const std::string Path = TCHAR_TO_UTF8(*FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("Data/RideUK/landmark_bridges.json")));
		if (TorqueWorld::Files::Read(Path, Text) && TorqueWorld::RidePlan::ReadLandmarks(Text, Marks))
		{
			std::vector<TorqueWorld::RidePlan::HeroPiece> Heroes;
			Plan->HeroBridges(Marks, Heroes);
			for (const TorqueWorld::RidePlan::HeroPiece& Hero : Heroes)
				if (UProceduralMeshComponent* Part = MakePart(*Hero.Mesh, RoadMaterial.Get(), false, true, true))
				{
					Part->SetCullDistance(0.0f);
					HeroParts.Add(Part);
					UE_LOG(LogTemp, Display, TEXT("Torque ride world: landmark bridge %s (%d points)"), UTF8_TO_TCHAR(Hero.Name.c_str()), int32(Hero.Mesh->Vertices()));
				}
		}
		else
			UE_LOG(LogTemp, Warning, TEXT("Torque ride world: no landmark bridges (%s)"), UTF8_TO_TCHAR(Path.c_str()));
	}
}

void ATorqueRideWorld::EndPlay(const EEndPlayReason::Type Reason)
{
	if (Current.Get() == this) Current.Reset();
	SaveDiary();   // [stage 7]
	if (Jobs)
	{
		// (a worker part way through a square reads the world's stores: let it finish before they can go)
		Jobs->bStop = true;
		const double GiveUpAt = FPlatformTime::Seconds() + 5.0;
		while (Jobs->Busy.load() > 0 && FPlatformTime::Seconds() < GiveUpAt) FPlatformProcess::Sleep(0.002f);
	}
	Super::EndPlay(Reason);
}

FVector2D ATorqueRideWorld::RiderAt() const
{
	if (const UWorld* World = GetWorld())
	{
		if (const APlayerController* PC = World->GetFirstPlayerController())
		{
			if (const APawn* Pawn = PC->GetPawn())
			{
				const FVector L = Pawn->GetActorLocation();
				return FVector2D(L.X / 100.0, L.Y / 100.0);
			}
		}
	}
	return LookedAt.X < 1e11 ? LookedAt : FVector2D(Plan->Road[0].At.X, Plan->Road[0].At.Z);
}

void ATorqueRideWorld::Wanted(const FVector2D& At, TArray<FIntVector>& Out) const
{
	Out.Reset();
	const int32 Top = TRWI::Levels - 1;
	const double S = TRWI::SizeOf(Top);
	const int32 X0 = FMath::FloorToInt32((At.X - TRWI::Reach) / S), X1 = FMath::FloorToInt32((At.X + TRWI::Reach) / S);
	const int32 Z0 = FMath::FloorToInt32((At.Y - TRWI::Reach) / S), Z1 = FMath::FloorToInt32((At.Y + TRWI::Reach) / S);
	TArray<FIntVector> Open;
	for (int32 Z = Z0; Z <= Z1; ++Z)
		for (int32 X = X0; X <= X1; ++X)
		{
			const FIntVector Key(Top, X, Z);
			if (TRWI::Away(At, Key) <= TRWI::Reach) Open.Add(Key);
		}
	while (Open.Num() > 0)
	{
		const FIntVector Key = Open.Pop(EAllowShrinking::No);
		if (Key.X > 0 && TRWI::Away(At, Key) < TRWI::SplitWithin[Key.X])
		{
			for (int32 Dz = 0; Dz < 4; ++Dz)
				for (int32 Dx = 0; Dx < 4; ++Dx) Open.Add(FIntVector(Key.X - 1, Key.Y * 4 + Dx, Key.Z * 4 + Dz));
		}
		else Out.Add(Key);
	}
	// Nearest first: they are made in this order.
	Out.Sort([&At](const FIntVector& A, const FIntVector& B) { return TRWI::Away(At, A) < TRWI::Away(At, B); });
}

void ATorqueRideWorld::Update(const FVector2D& At, bool bAtOnce)
{
	LookedAt = At;
	Wanted(At, Want);
	const std::shared_ptr<const TorqueWorld::RidePlan> Held = Plan;
	const std::shared_ptr<FTorqueRideJobs> Share = Jobs;
	for (const FIntVector& Key : Want)
	{
		if ((Tiles.Contains(Key) && !Stale.Contains(Key)) || Asked.Contains(Key)) continue;
		const double Size = TRWI::SizeOf(Key.X);
		const int32 Cells = TRWI::CellsOf[Key.X];
		if (bAtOnce)
		{
			// Before anyone is on the road: the finest squares near the start now, so the bike has
			// ground under it (the rest come from the workers as soon as the ride is under way).
			if (Key.X != 0 || TRWI::Away(At, Key) > 300.0) continue;
			TorqueWorld::RideMesh Mesh;
			Plan->LandTile(Key.Y * Size, Key.Z * Size, Size, Cells, Mesh);
			Place(Key, Mesh, true);
			continue;
		}
		if (Share->Busy.load() >= TRWI::AtATime) break;
		Asked.Add(Key);
		Stale.Remove(Key);
		++Share->Busy;
		AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [Held, Share, Key, Size, Cells]()
		{
			TSharedPtr<TorqueWorld::RideMesh, ESPMode::ThreadSafe> Mesh = MakeShared<TorqueWorld::RideMesh, ESPMode::ThreadSafe>();
			if (!Share->bStop) Held->LandTile(Key.Y * Size, Key.Z * Size, Size, Cells, *Mesh);
			TSharedPtr<FRidePrepared, ESPMode::ThreadSafe> Ready = MakeShared<FRidePrepared, ESPMode::ThreadSafe>();
			if (!Share->bStop) TRWI::PrepareSquare(*Mesh, *Ready);
			{
				FScopeLock Hold(&Share->Lock);
				Share->Made.Emplace(Key, Mesh);
				Share->Prepared.Add(Key, Ready);
			}
			--Share->Busy;
		});
	}
}

void ATorqueRideWorld::LetGo(UProceduralMeshComponent* Part)
{
	if (!Part) return;
	URidePart* R = Cast<URidePart>(Part);
	if (!R || R->Look || TRWI::CVarPool.GetValueOnGameThread() == 0 || Spare.Num() >= 400 || R->IsBeingDestroyed())
	{
		Part->DestroyComponent();
		return;
	}
	R->ClearAllMeshSections();
	R->EmptyOverrideMaterials();
	R->ComponentTags.Reset();
	if (R->IsRegistered()) R->UnregisterComponent();
	Spare.Add(R);
}

URidePart* ATorqueRideWorld::TakeSpare()
{
	while (Spare.Num() > 0)
	{
		URidePart* R = Spare.Pop(EAllowShrinking::No);
		if (IsValid(R) && !R->IsBeingDestroyed())
		{
			R->SetVisibility(true, false);
			return R;
		}
	}
	return nullptr;
}

void ATorqueRideWorld::RegisterWaiting()
{
	const double Started = FPlatformTime::Seconds();
	const double BudgetMs = TRWI::CVarRegisterMs.GetValueOnGameThread();
	int32 Done = 0;
	while (ToRegister.Num() > 0 && (Done == 0 || (FPlatformTime::Seconds() - Started) * 1000.0 < BudgetMs))
	{
		URidePart* R = ToRegister[0].Get();
		ToRegister.RemoveAt(0, EAllowShrinking::No);
		// (let go while it waited: in the spare pile, or gone)
		if (!R || R->IsBeingDestroyed() || R->IsRegistered() || Spare.Contains(R)) continue;
		R->RegisterComponent();
		++Done;
	}
}

UProceduralMeshComponent* ATorqueRideWorld::MakePart(const TorqueWorld::RideMesh& Mesh, UMaterialInterface* Material, bool bCollide, bool bAtOnce, bool bShadow)
{
	TArray<TPair<const TorqueWorld::RideMesh*, UMaterialInterface*>> One;
	One.Emplace(&Mesh, Material);
	return MakePart(One, bCollide, bAtOnce, bShadow);
}

UProceduralMeshComponent* ATorqueRideWorld::MakePart(const TArray<TPair<const TorqueWorld::RideMesh*, UMaterialInterface*>>& Sections, bool bCollide, bool bAtOnce, bool bShadow)
{
	// [perf, build 148] Drawn as a static mesh built from the same triangles (World/TorqueRidePart.h):
	// the procedural mesh then holds only what collides, hidden.
	UStaticMesh* LookMesh = URidePart::StaticLooks() ? URidePart::BuildLook(this, Sections) : nullptr;
	URidePart* Part = nullptr;
	int32 Section = 0;
	for (const TPair<const TorqueWorld::RideMesh*, UMaterialInterface*>& Each : Sections)
	{
		const TorqueWorld::RideMesh* Mesh = Each.Key;
		const int32 Count = Mesh ? int32(Mesh->Vertices()) : 0;
		if (Count < 3 || Mesh->Idx.size() < 3) continue;
		if (!Part)
		{
			// ([perf, build 150] one let go of earlier if there is one: fewer objects made and collected)
			Part = TakeSpare();
			if (!Part)
			{
				Part = NewObject<URidePart>(this);
				Part->SetupAttachment(RootComponent);
			}
			Part->bUseAsyncCooking = !bAtOnce;
			Part->SetMobility(EComponentMobility::Movable);
			Part->SetRelativeLocation(FVector(Mesh->OriginX * 100.0, Mesh->OriginZ * 100.0, 0.0));
			Part->SetCollisionProfileName(bCollide ? TEXT("BlockAll") : TEXT("NoCollision"));
			Part->SetCastShadow(bShadow && !LookMesh);
			Part->bVisibleInRayTracing = bNextInRayTracing;
			// (drawn by its look: the renderer never sees the procedural mesh)
			if (LookMesh) Part->SetVisibility(false, false);
			// ([perf, build 150] registered once its sections are all in, below: one render state and
			// one physics state made, not one per section)
		}
		// (with a look, only what collides needs its triangles here)
		if (LookMesh && !bCollide)
		{
			++Section;
			continue;
		}
		// [perf, build 149] Readied on the square's worker: only copied in here.
		if (PreparedNow)
			if (FProcMeshSection* Ready = PreparedNow->Find(Mesh))
			{
				Ready->bEnableCollision = bCollide;
				Part->SetProcMeshSection(Section, *Ready);
				if (Each.Value) Part->SetMaterial(Section, Each.Value);
				++Section;
				continue;
			}
		// The ride's frame is metres, x east, y up, z south; Unreal's centimetres, x east, y south, z up.
		TArray<FVector> Points;
		TArray<FVector> Normals;
		TArray<FVector2D> Uvs;
		TArray<int32> Triangles;
		TArray<FLinearColor> Colours;
		const bool bColoured = Mesh->Col.size() == Mesh->Pos.size();
		const bool bAlpha = Mesh->Alpha.size() == size_t(Count);
		if (bColoured) Colours.Reserve(Count);
		Points.Reserve(Count);
		Normals.Reserve(Count);
		Uvs.Reserve(Count);
		Triangles.Reserve(int32(Mesh->Idx.size()));
		for (int32 I = 0; I < Count; ++I)
		{
			const size_t V = size_t(I) * 3;
			Points.Add(FVector(double(Mesh->Pos[V]) * 100.0, double(Mesh->Pos[V + 2]) * 100.0, double(Mesh->Pos[V + 1]) * 100.0));
			Normals.Add(FVector(double(Mesh->Nrm[V]), double(Mesh->Nrm[V + 2]), double(Mesh->Nrm[V + 1])));
			Uvs.Add(FVector2D(double(Mesh->Uv[size_t(I) * 2]), double(Mesh->Uv[size_t(I) * 2 + 1])));
			if (bColoured) Colours.Add(FLinearColor(Mesh->Col[V], Mesh->Col[V + 1], Mesh->Col[V + 2], bAlpha ? Mesh->Alpha[size_t(I)] : 1.0f));
		}
		// (the plan winds its triangles anticlockwise seen from above, which is Unreal's front face too:
		// the two frames name the same points)
		for (uint32 Index : Mesh->Idx) Triangles.Add(int32(Index));
		// (the colours are kept as they are, linear: M_RideRoad reads the vertex colour as the road's own,
		// M_RideStone and M_RideMasonry as their masks)
		Part->CreateMeshSection_LinearColor(Section, Points, Triangles, Normals, Uvs, Colours, TArray<FProcMeshTangent>(), bCollide, false);
		if (Each.Value) Part->SetMaterial(Section, Each.Value);
		++Section;
	}
	bNextInRayTracing = true;
	// ([perf, build 150] a part that nothing collides with and nothing hangs on may wait its turn: Tick
	// registers them within a few ms a frame, so a town's big squares don't all land in one frame)
	const bool bDefer = bDeferNext && !bAtOnce && !bCollide && !LookMesh && TRWI::CVarRegisterMs.GetValueOnGameThread() > 0.0f;
	bDeferNext = false;
	if (Part)
	{
		if (bDefer) ToRegister.Add(Part);
		else if (!Part->IsRegistered()) Part->RegisterComponent();
	}
	if (Part && LookMesh)
	{
		UStaticMeshComponent* Drawn = NewObject<UStaticMeshComponent>(this, NAME_None, RF_Transient);
		Drawn->SetMobility(EComponentMobility::Movable);
		Drawn->SetupAttachment(Part);
		Drawn->SetStaticMesh(LookMesh);
		Drawn->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Drawn->SetCollisionProfileName(TEXT("NoCollision"));
		Drawn->SetCanEverAffectNavigation(false);
		Drawn->SetGenerateOverlapEvents(false);
		Drawn->SetCastShadow(bShadow);
		Drawn->RegisterComponent();
		Part->Look = Drawn;
	}
	return Part;
}

void ATorqueRideWorld::DropWords(const FIntVector& Key)
{
	if (TArray<TWeakObjectPtr<UTextRenderComponent>>* Words = TileWords.Find(Key))
	{
		for (const TWeakObjectPtr<UTextRenderComponent>& W : *Words)
			if (W.IsValid()) W->DestroyComponent();
		TileWords.Remove(Key);
	}
}

void ATorqueRideWorld::DropWater(const FIntVector& Key)
{
	if (TObjectPtr<UProceduralMeshComponent>* Wet = TileWater.Find(Key))
	{
		LetGo(*Wet);
		TileWater.Remove(Key);
	}
}

UMaterialInterface* ATorqueRideWorld::TintedOf(UMaterialInterface* Base, uint32 Which, const TorqueWorld::Tint& By)
{
	// (the stone as it is needs no instance; a material made before Tint was added just ignores it)
	if (!Base || (By.R == 1.0f && By.G == 1.0f && By.B == 1.0f)) return Base;
	const auto Q = [](float V) { return uint32(FMath::Clamp(FMath::RoundToInt32(V * 100.0f), 0, 255)); };
	const uint32 Key = (Which << 24) | (Q(By.R) << 16) | (Q(By.G) << 8) | Q(By.B);
	if (const TObjectPtr<UMaterialInterface>* Have = Tinted.Find(Key)) return Have->Get();
	UMaterialInstanceDynamic* Made = UMaterialInstanceDynamic::Create(Base, this);
	if (!Made) return Base;
	Made->SetVectorParameterValue(TEXT("Tint"), FLinearColor(By.R, By.G, By.B, 1.0f));
	Tinted.Add(Key, Made);
	return Made;
}

void ATorqueRideWorld::Place(const FIntVector& Key, const TorqueWorld::RideMesh& Mesh, bool bAtOnce)
{
	if (TObjectPtr<UProceduralMeshComponent>* Old = Tiles.Find(Key))
	{
		LetGo(*Old);
	}
	{
		RideCosts::FScope Cost(RideCosts::Land);
		Tiles.Add(Key, MakePart(Mesh, LandMaterial, Key.X == 0, bAtOnce, Key.X < TRWI::ShadowLevels));
	}
	{
		RideCosts::FScope Cost(RideCosts::Plants);
		DropPlants(Key);
		PlacePlants(Key, Mesh);
	}
	{
		RideCosts::FScope Cost(RideCosts::Animals);
		PlaceAnimals(Key, Mesh);
	}
	{
		RideCosts::FScope Cost(RideCosts::Props);
		PlaceProps(Key, Mesh);   // [OSM stage 2]
	}
	{
		RideCosts::FScope Cost(RideCosts::Words);
		DropWords(Key);
	}
	{
		RideCosts::FScope Cost(RideCosts::Water);
		DropWater(Key);
		// (its water: seen, not cast as shadow, not ridden on)
		if (Mesh.Water && Mesh.Water->Vertices() > 0)
		{
			bNextInRayTracing = Key.X == 0;
			bDeferNext = true;
			if (UProceduralMeshComponent* Wet = MakePart(*Mesh.Water, WaterMaterial, false, bAtOnce, false)) TileWater.Add(Key, Wet);
		}
	}
	RideCosts::FScope BuildingsCost(RideCosts::Buildings);
	if (TObjectPtr<UProceduralMeshComponent>* OldExtra = TileExtras.Find(Key))
	{
		LetGo(*OldExtra);
		TileExtras.Remove(Key);
	}
	DropStreetLights(Key);
	if (Mesh.Extra)
	{
		// (what stands on it: only the nearest squares' cast shadows)
		TArray<TPair<const TorqueWorld::RideMesh*, UMaterialInterface*>> Kinds;
		Kinds.Emplace(Mesh.Extra.get(), RoadMaterial.Get());
		Kinds.Emplace(Mesh.Extra->Stone.get(), TintedOf(StoneMaterial.Get(), 0, Mesh.Extra->StoneTint));
		Kinds.Emplace(Mesh.Extra->Masonry.get(), TintedOf(MasonryMaterial.Get(), 1, Mesh.Extra->MasonryTint));
		Kinds.Emplace(Mesh.Extra->Render.get(), RenderMaterial.Get());
		Kinds.Emplace(Mesh.Extra->Roofing.get(), RoofMaterial.Get());
		Kinds.Emplace(Mesh.Extra->Brickwork.get(), BrickMaterial.Get());
		Kinds.Emplace(Mesh.Extra->Night.get(), NightMaterial.Get());
		// [OSM stage 5D] traffic lights' and beacons' lamps, cycling by the shared clock
		Kinds.Emplace(Mesh.Extra->Signals.get(), SignalMaterial ? SignalMaterial.Get() : LampMaterial ? LampMaterial.Get() : NightMaterial.Get());
		// [OSM stage 6A] road signs' faces and lettering, shining back the headlight after dark
		Kinds.Emplace(Mesh.Extra->Signs.get(), SignMaterial ? SignMaterial.Get() : RoadMaterial.Get());
		bNextInRayTracing = Key.X <= TRWI::CVarRayTraceLevel.GetValueOnGameThread();
		bDeferNext = !(Key.X == 0 && Mesh.Extra->Lights.size() >= 3);   // (street lamps hang on it: in at once)
		UProceduralMeshComponent* Stands = MakePart(Kinds, false, bAtOnce, Key.X == 0);
		if (Stands) TileExtras.Add(Key, Stands);
		// [stage 7] The street lamps' lights, on the nearest squares only: warm, unshadowed, on after dark.
		if (Stands && Key.X == 0 && Mesh.Extra->Lights.size() >= 3)
		{
			RideCosts::FScope LightsCost(RideCosts::Lights);
			TArray<TWeakObjectPtr<UPointLightComponent>>& Lamps = StreetLights.FindOrAdd(Key);
			for (size_t L = 0; L + 2 < Mesh.Extra->Lights.size(); L += 3)
			{
				UPointLightComponent* Lamp = NewObject<UPointLightComponent>(this);
				Lamp->SetMobility(EComponentMobility::Movable);
				Lamp->SetupAttachment(Stands);
				Lamp->SetRelativeLocation(FVector(double(Mesh.Extra->Lights[L]) * 100.0, double(Mesh.Extra->Lights[L + 2]) * 100.0, double(Mesh.Extra->Lights[L + 1]) * 100.0));
				Lamp->SetIntensityUnits(ELightUnits::Lumens);
				Lamp->SetIntensity(4000.0f);
				Lamp->SetAttenuationRadius(2200.0f);
				Lamp->SetLightColor(FLinearColor(1.0f, 0.8f, 0.55f));
				Lamp->SetCastShadows(false);
				Lamp->SetVisibility(bStreetLightsOn);
				Lamp->RegisterComponent();
				Lamps.Add(Lamp);
			}
		}
		// The words on its signs, as text standing just proud of each board.
		RideCosts::FScope WordsCost(RideCosts::Words);
		TArray<TWeakObjectPtr<UTextRenderComponent>>& Words = TileWords.FindOrAdd(Key);
		for (const TorqueWorld::RideMesh::Label& L : Mesh.Extra->Labels)
		{
			UTextRenderComponent* Text = NewObject<UTextRenderComponent>(this);
			if (!Text) continue;
			Text->SetupAttachment(RootComponent);
			// (the ride's frame: x east, y up, z south; Unreal's: x east, y south, z up)
			Text->SetRelativeLocation(FVector((Mesh.Extra->OriginX + L.X) * 100.0, (Mesh.Extra->OriginZ + L.Z) * 100.0, L.Y * 100.0));
			Text->SetRelativeRotation(FRotator(0.0, FMath::RadiansToDegrees(FMath::Atan2(double(L.Nz), double(L.Nx))), 0.0));
			Text->SetText(FText::FromString(FString(UTF8_TO_TCHAR(L.Text.c_str()))));
			Text->SetHorizontalAlignment(EHTA_Center);
			Text->SetVerticalAlignment(EVRTA_TextCenter);
			Text->SetWorldSize(L.Size * 100.0f);
			Text->SetTextRenderColor(FLinearColor(L.R, L.G, L.B).ToFColor(true));
			Text->SetCastShadow(false);
			Text->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Text->RegisterComponent();
			Words.Add(Text);
		}
	}
}

void ATorqueRideWorld::PlaceRoadPiece(int64 Key, const TorqueWorld::RideMesh& Mesh, TMap<const TorqueWorld::RideMesh*, FProcMeshSection>* Ready, bool bAtOnce)
{
	// (a tunnel's lamps as a second section: they glow, and a few real lights under them)
	TArray<TPair<const TorqueWorld::RideMesh*, UMaterialInterface*>> Sections;
	Sections.Emplace(&Mesh, RoadMaterial.Get());
	if (Mesh.Glow) Sections.Emplace(Mesh.Glow.get(), LampMaterial ? LampMaterial.Get() : RoadMaterial.Get());
	if (Mesh.Night) Sections.Emplace(Mesh.Night.get(), NightMaterial.Get());   // [stage 7] cat's eyes
	PreparedNow = Ready;
	bNextInRayTracing = TRWI::CVarRoadsRayTraced.GetValueOnGameThread() != 0;
	UProceduralMeshComponent* Part = MakePart(Sections, true, bAtOnce, false);
	PreparedNow = nullptr;
	if (Part)
	{
		Part->ComponentTags.Add(TEXT("Road"));
		TArray<TWeakObjectPtr<UPointLightComponent>> Lights;
		for (size_t L = 0; L + 2 < Mesh.Lights.size(); L += 3)
		{
			UPointLightComponent* Lamp = NewObject<UPointLightComponent>(this);
			Lamp->SetMobility(EComponentMobility::Movable);
			Lamp->SetupAttachment(Part);
			Lamp->SetRelativeLocation(FVector(double(Mesh.Lights[L]) * 100.0, double(Mesh.Lights[L + 2]) * 100.0, double(Mesh.Lights[L + 1]) * 100.0));
			Lamp->SetIntensityUnits(ELightUnits::Lumens);
			Lamp->SetIntensity(2500.0f);
			Lamp->SetAttenuationRadius(1800.0f);
			Lamp->SetLightColor(FLinearColor(1.0f, 0.86f, 0.66f));
			Lamp->SetCastShadows(false);
			Lamp->RegisterComponent();
			Lights.Add(Lamp);
		}
		if (Lights.Num() > 0) RoadLights.Add(Key, Lights);
	}
	RoadParts.Add(Key, Part);
}

void ATorqueRideWorld::UpdateRoad(const FVector2D& At, bool bAtOnce)
{
	// Every road (the ride's own, then those that meet it) in pieces of PiecePoints points; a piece's
	// key is its road times 4096 and which piece of it.
	// [ride anywhere] The roads laid so far round the rider (the network's blocks are laid by the near
	// squares of ground as they are made), and those whose pieces are in place (to drop the far ones).
	TorqueWorld::Rect Around;
	Around.X0 = At.X - TRWI::RoadReach - 600.0; Around.Z0 = At.Y - TRWI::RoadReach - 600.0;
	Around.X1 = At.X + TRWI::RoadReach + 600.0; Around.Z1 = At.Y + TRWI::RoadReach + 600.0;
	// [perf, build 151] The roads to look at, found again only when the rider has gone 150 m since (or the
	// plan has grown), and looked at a few at a time within torque.RideRoadLookMs a call (round 150, York:
	// this look over every road and piece near the rider was most of 8 ms a frame, worst 76 ms).
	const bool bSlice = !bAtOnce && TRWI::CVarRoadLookMs.GetValueOnGameThread() > 0.0f;
	if (!bSlice || RoadListPlan != Plan.get() || FVector2D::Distance(At, RoadListAt) > 150.0)
	{
		RoadList = Plan->RoadsNear(Around, false);
		for (const auto& Have : RoadParts) RoadList.push_back(int(Have.Key / 4096));
		std::sort(RoadList.begin(), RoadList.end());
		RoadList.erase(std::unique(RoadList.begin(), RoadList.end()), RoadList.end());
		RoadListPlan = Plan.get();
		RoadListAt = At;
		RoadCursor = 0;
	}
	const double LookStart = FPlatformTime::Seconds();
	const double LookMs = TRWI::CVarRoadLookMs.GetValueOnGameThread();
	const size_t RoadCount = RoadList.size();
	for (size_t Looked = 0; Looked < RoadCount; ++Looked)
	{
		if (bSlice && Looked > 0 && (FPlatformTime::Seconds() - LookStart) * 1000.0 > LookMs) break;
		if (RoadCursor >= RoadCount) RoadCursor = 0;
		const int Which = RoadList[RoadCursor++];
		const std::vector<TorqueWorld::RoadPoint>& Pts = Plan->RoadOf(Which);
		const int32 Points = int32(Pts.size());
		if (Points < 2) continue;
		const int32 Pieces = FMath::Min((Points - 1 + TRWI::PiecePoints - 1) / TRWI::PiecePoints, 4096);
		for (int32 K = 0; K < Pieces; ++K)
		{
			const int32 First = K * TRWI::PiecePoints, Last = FMath::Min(First + TRWI::PiecePoints, Points - 1);
			const TorqueWorld::RoadPoint& Mid = Pts[size_t((First + Last) / 2)];
			const double HowFar = FVector2D::Distance(At, FVector2D(Mid.At.X, Mid.At.Z));
			const int64 Key = int64(Which) * 4096 + K;
			const bool bHave = RoadParts.Contains(Key);
			// (a piece with an end of its road waits for the block that end is in: its junction is drawn with it)
			const bool bEndsLaid = (First != 0 || Plan->BlockLaid(Pts.front().At.X, Pts.front().At.Z)) && (Last != Points - 1 || Plan->BlockLaid(Pts.back().At.X, Pts.back().At.Z));
			if (!bHave && HowFar < TRWI::RoadReach && bEndsLaid && bAtOnce)
			{
				TorqueWorld::RideMesh Mesh;
				Plan->RoadPiece(Which, First, Last, Mesh);
				PlaceRoadPiece(Key, Mesh, nullptr, true);
			}
			else if (!bHave && HowFar < TRWI::RoadReach && bEndsLaid && !RoadAsked.Contains(Key) && Jobs->RoadBusy.load() < TRWI::RoadsAtATime)
			{
				// [perf, build 149] Made on a worker (it was here, up to 125 ms a look in a town), put in
				// place by Tick as it comes.
				RoadAsked.Add(Key);
				++Jobs->RoadBusy;
				const std::shared_ptr<const TorqueWorld::RidePlan> Held = Plan;
				const std::shared_ptr<FTorqueRideJobs> Share = Jobs;
				AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [Held, Share, Key, Which, First, Last]()
				{
					TSharedPtr<TorqueWorld::RideMesh, ESPMode::ThreadSafe> Made = MakeShared<TorqueWorld::RideMesh, ESPMode::ThreadSafe>();
					TSharedPtr<FRidePrepared, ESPMode::ThreadSafe> Ready = MakeShared<FRidePrepared, ESPMode::ThreadSafe>();
					if (!Share->bStop)
					{
						Held->RoadPiece(Which, First, Last, *Made);
						TRWI::Prepare(Made.Get(), *Ready);
						TRWI::Prepare(Made->Glow.get(), *Ready);
						TRWI::Prepare(Made->Night.get(), *Ready);
					}
					{
						FScopeLock Hold(&Share->Lock);
						Share->RoadMade.Emplace(Key, Made, Ready, Held);
					}
					--Share->RoadBusy;
				});
			}
			else if (bHave && HowFar > TRWI::RoadReach + 500.0)
			{
				LetGo(RoadParts[Key]);
				RoadParts.Remove(Key);
				if (TArray<TWeakObjectPtr<UPointLightComponent>>* Lights = RoadLights.Find(Key))
				{
					for (const TWeakObjectPtr<UPointLightComponent>& Lamp : *Lights) if (Lamp.IsValid()) Lamp->DestroyComponent();
					RoadLights.Remove(Key);
				}
			}
		}
	}
}

namespace TRProbe
{
// [perf, build 150] Garbage collection's time (game thread), summed until the ride's Tick takes it, so
// a hitch says whether it was the collector.
double GcStartedAt = 0.0, GcSumMs = 0.0;
void WatchGc()
{
	static bool bWatching = false;
	if (bWatching) return;
	bWatching = true;
	FCoreUObjectDelegates::GetPreGarbageCollectDelegate().AddLambda([]() { GcStartedAt = FPlatformTime::Seconds(); });
	FCoreUObjectDelegates::GetPostGarbageCollect().AddLambda([]()
	{
		if (GcStartedAt > 0.0) GcSumMs += (FPlatformTime::Seconds() - GcStartedAt) * 1000.0;
		GcStartedAt = 0.0;
	});
}
double TakeGcMs()
{
	const double Taken = GcSumMs;
	GcSumMs = 0.0;
	return Taken;
}
FString CVarText(const TCHAR* Name)
{
	const IConsoleVariable* V = IConsoleManager::Get().FindConsoleVariable(Name);
	return V ? V->GetString() : FString(TEXT("-"));
}
// The A/B run's steps: what each turns off (console variables, "name=value;..."), and which of the
// ride's things it hides (1 trees, 2 grass and small plants, 4 ground, roads, walls and buildings).
struct FABStep
{
	const TCHAR* Label;
	const TCHAR* Vars;
	int32 Hide;
};
const FABStep ABSteps[] = {
	{TEXT("as set"), TEXT(""), 0},
	{TEXT("drawn at half size (r.ScreenPercentage 50)"), TEXT("r.ScreenPercentage=50"), 0},
	{TEXT("no trees"), TEXT(""), 1},
	{TEXT("no grass or small plants"), TEXT(""), 2},
	{TEXT("no ground, roads, walls or buildings drawn"), TEXT(""), 4},
	{TEXT("no virtual shadow maps"), TEXT("r.Shadow.Virtual.Enable=0"), 0},
	{TEXT("no shadows"), TEXT("r.ShadowQuality=0"), 0},
	{TEXT("no volumetric clouds"), TEXT("r.VolumetricCloud=0"), 0},
	{TEXT("no volumetric fog"), TEXT("r.VolumetricFog=0"), 0},
	{TEXT("no Lumen"), TEXT("r.DynamicGlobalIlluminationMethod=0;r.ReflectionMethod=2"), 0},
	{TEXT("Lumen without hardware ray tracing"), TEXT("r.Lumen.HardwareRayTracing=0"), 0},
	{TEXT("no Nanite"), TEXT("r.Nanite=0"), 0},
	{TEXT("FXAA instead of TSR/TAA"), TEXT("r.AntiAliasingMethod=1"), 0},
	{TEXT("as set again"), TEXT(""), 0},
};
constexpr int32 ABCount = UE_ARRAY_COUNT(ABSteps);
// ([perf, build 151] 2 s to settle and 5 measured by default, -TorqueRideABSecs=<measured s>: round 150's
// 90 s runs fitted only 4 of 14 steps at 3 + 10 s)
double ABMeasureSecs()
{
	static const double Secs = []() { float Given = 5.0f; FParse::Value(FCommandLine::Get(), TEXT("TorqueRideABSecs="), Given); return double(FMath::Clamp(Given, 2.0f, 30.0f)); }();
	return Secs;
}
constexpr double ABSettle = 2.0;
} // namespace TRProbe

float ATorqueRideWorld::PresetReach(bool bTree) const
{
	// (so Medium really does less: until build 150 the ride's own plants took no notice of the preset,
	// and beenster's Epic -> Medium changed nothing)
	static const float GrassReach[5] = {0.45f, 0.6f, 0.8f, 1.0f, 1.0f};
	static const float TreeReach[5] = {0.6f, 0.75f, 0.9f, 1.0f, 1.0f};
	const int32 Level = FMath::Clamp(Scalability::GetQualityLevels().FoliageQuality, 0, 4);
	return bTree ? TreeReach[Level] : GrassReach[Level];
}

void ATorqueRideWorld::HideForAB(int32 What, bool bHide)
{
	if (What & 3)
		for (const auto& Square : TilePlants)
			for (const TWeakObjectPtr<UInstancedStaticMeshComponent>& Kind : Square.Value)
				if (UInstancedStaticMeshComponent* C = Kind.Get())
				{
					const bool bIsTree = C->ComponentHasTag(TEXT("RideTree"));
					if ((bIsTree && (What & 1)) || (!bIsTree && (What & 2))) C->SetVisibility(!bHide);
				}
	if (What & 4)
	{
		auto Show = [bHide](UProceduralMeshComponent* P)
		{
			if (!P) return;
			URidePart* R = Cast<URidePart>(P);
			if (R && R->Look) R->Look->SetVisibility(!bHide);
			else P->SetVisibility(!bHide);
		};
		for (const auto& Have : Tiles) Show(Have.Value);
		for (const auto& Have : TileExtras) Show(Have.Value);
		for (const auto& Have : TileWater) Show(Have.Value);
		for (const auto& Have : RoadParts) Show(Have.Value);
		for (UProceduralMeshComponent* Hero : HeroParts) Show(Hero);
	}
}

void ATorqueRideWorld::TickAB(float DeltaSeconds)
{
	// -TorqueRideAB=1 (and -TorqueRideABAfter=<s>, 12 by default: the squares round the rider made
	// first). Best parked or standing still, so each step sees the same view. Each step: 2 s to settle,
	// 5 s measured (-TorqueRideABSecs=), about 100 s for all 14. Its console variables are set as the console sets them, and put back after.
	static const bool bAB = []() { int32 On = 0; return FParse::Value(FCommandLine::Get(), TEXT("TorqueRideAB="), On) && On != 0; }();
	static const double ABAfter = []() { float Secs = 12.0f; FParse::Value(FCommandLine::Get(), TEXT("TorqueRideABAfter="), Secs); return double(FMath::Max(Secs, 0.0f)); }();
	if (!bAB || ABStep >= TRProbe::ABCount) return;
	ABSince += DeltaSeconds;
	auto StartStep = [this](int32 Which)
	{
		ABStep = Which;
		ABSince = 0.0;
		ABGame = ABRender = ABGpu = ABWall = 0.0;
		ABFrames = 0;
		ABWas.Reset();
		if (Which >= TRProbe::ABCount) return;
		const TRProbe::FABStep& Doing = TRProbe::ABSteps[Which];
		TArray<FString> Pairs;
		FString(Doing.Vars).ParseIntoArray(Pairs, TEXT(";"));
		for (const FString& Pair : Pairs)
		{
			FString Name, Value;
			if (!Pair.Split(TEXT("="), &Name, &Value)) continue;
			if (IConsoleVariable* V = IConsoleManager::Get().FindConsoleVariable(*Name))
			{
				ABWas.Emplace(Name, V->GetString());
				V->Set(*Value, ECVF_SetByConsole);
			}
		}
		if (Doing.Hide) HideForAB(Doing.Hide, true);
		UE_LOG(LogTemp, Display, TEXT("Ride A/B: step %d of %d, %s"), Which + 1, TRProbe::ABCount, Doing.Label);
	};
	if (ABStep < 0)
	{
		if (ABSince < ABAfter) return;
		StartStep(0);
		return;
	}
	if (ABSince < TRProbe::ABSettle) return;
	ABGame += FPlatformTime::ToMilliseconds(GGameThreadTime);
	ABRender += FPlatformTime::ToMilliseconds(GRenderThreadTime);
	ABGpu += FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles(0));
	ABWall += double(DeltaSeconds);
	++ABFrames;
	// (one frame's GPU passes, with their sizes, logged by the engine: says whether the screen
	// percentage and upscaler are really in use, and what the GPU's ms go on)
	if (ABStep == 0 && !bABProfiled && ABSince > TRProbe::ABSettle + 1.0 && GEngine)
	{
		bABProfiled = true;
		GEngine->Exec(GetWorld(), TEXT("ProfileGPU"));
	}
	if (ABSince < TRProbe::ABSettle + TRProbe::ABMeasureSecs()) return;
	const TRProbe::FABStep& Done = TRProbe::ABSteps[ABStep];
	const double Frames = double(FMath::Max(ABFrames, 1));
	UE_LOG(LogTemp, Display, TEXT("Ride A/B: %-45s %5.1f fps; game %5.1f ms, render %5.1f ms, GPU %5.1f ms (%d frames)"),
		Done.Label, ABWall > 0.0 ? ABFrames / ABWall : 0.0, ABGame / Frames, ABRender / Frames, ABGpu / Frames, ABFrames);
	for (const TPair<FString, FString>& Was : ABWas)
		if (IConsoleVariable* V = IConsoleManager::Get().FindConsoleVariable(*Was.Key)) V->Set(*Was.Value, ECVF_SetByConsole);
	if (Done.Hide) HideForAB(Done.Hide, false);
	StartStep(ABStep + 1);
	if (ABStep >= TRProbe::ABCount) UE_LOG(LogTemp, Display, TEXT("Ride A/B: done"));
}

void ATorqueRideWorld::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!Plan || !Jobs) return;
	// [perf, build 150] The whole frame (summed for the 5 s line), hitches, and the A/B run.
	TRProbe::WatchGc();
	{
		const double WallNow = FPlatformTime::Seconds();
		FrameGameMs += FPlatformTime::ToMilliseconds(GGameThreadTime);
		FrameRenderMs += FPlatformTime::ToMilliseconds(GRenderThreadTime);
		FrameGpuMs += FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles(0));
		const double GapMs = LastTickWall > 0.0 ? (WallNow - LastTickWall) * 1000.0 : 0.0;
		const double InGc = TRProbe::TakeGcMs();
		// (Unreal caps a frame's DeltaSeconds at 0.4 s, which is why every town's worst frame read 400 ms:
		// this is the real gap)
		if (GapMs > 150.0)
		{
			UE_LOG(LogTemp, Display, TEXT("Ride hitch: %.0f ms between frames; garbage collection in it %.0f ms; the ride's own last frame %.1f ms; squares being made %d, road pieces %d"),
				GapMs, InGc, LastTickOwnMs, Asked.Num(), RoadAsked.Num());
		}
		LastTickWall = WallNow;
	}
	struct FOwnTime
	{
		double& Out;
		double T0;
		~FOwnTime() { Out = (FPlatformTime::Seconds() - T0) * 1000.0; }
	} OwnTime{LastTickOwnMs, FPlatformTime::Seconds()};
	TickAB(DeltaSeconds);
	// [build 147] Where the game thread's time goes, every 5 seconds: each system's ms a frame (its
	// worst single call), the squares placed, and the frame's own time (game thread and all).
	{
		++CostFrames;
		CostSince += DeltaSeconds;
		CostWorstFrame = FMath::Max(CostWorstFrame, double(DeltaSeconds) * 1000.0);
		if (CostSince >= 5.0)
		{
			RideCosts::FSums& S = RideCosts::Sums();
			int32 Waiting = 0;
			{
				FScopeLock Hold(&Jobs->Lock);
				Waiting = Jobs->Made.Num();
			}
			FString Line;
			double All = 0.0;
			for (int32 W = 0; W < RideCosts::Count; ++W)
			{
				All += S.Ms[W];
				if (S.Calls[W] > 0) Line += FString::Printf(TEXT(" %s %.2f (%d, worst %.1f);"), RideCosts::Name(W), S.Ms[W] / CostFrames, S.Calls[W], S.Worst[W]);
			}
			UE_LOG(LogTemp, Display, TEXT("Ride costs: %d frames, %.1f ms a frame (worst %.0f); ours %.2f ms a frame:%s squares waiting %d"),
				CostFrames, CostSince * 1000.0 / CostFrames, CostWorstFrame, All / CostFrames, *Line, Waiting);
			// [perf, build 148] Where the whole frame goes (the engine's own last-frame figures, as stat unit
			// shows them), and how much of the ride is in the scene: what to cut when a town is slow.
			{
				int32 Looks = 0, Hidden = 0, Ism = 0, IsmShadow = 0, Words = 0, Lamps = 0, TreeGroups = 0;
				int64 Instances = 0, TreeCount = 0;
				auto CountPart = [&Looks, &Hidden](const UProceduralMeshComponent* P)
				{
					if (const URidePart* R = Cast<URidePart>(P))
					{
						if (R->Look) ++Looks;
						if (!R->IsVisible()) ++Hidden;
					}
				};
				for (const auto& Have : Tiles) CountPart(Have.Value);
				for (const auto& Have : TileExtras) CountPart(Have.Value);
				for (const auto& Have : TileWater) CountPart(Have.Value);
				for (const auto& Have : RoadParts) CountPart(Have.Value);
				for (const auto& Square : TilePlants)
					for (const TWeakObjectPtr<UInstancedStaticMeshComponent>& Kind : Square.Value)
						if (const UInstancedStaticMeshComponent* C = Kind.Get())
						{
							++Ism;
							if (C->CastShadow) ++IsmShadow;
							Instances += C->GetInstanceCount();
							if (C->ComponentHasTag(TEXT("RideTree")))
							{
								++TreeGroups;
								TreeCount += C->GetInstanceCount();
							}
						}
				for (const auto& Square : TileWords) Words += Square.Value.Num();
				for (const auto& Square : StreetLights) Lamps += Square.Value.Num();
				for (const auto& Piece : RoadLights) Lamps += Piece.Value.Num();
				UE_LOG(LogTemp, Display, TEXT("Ride frame (5 s average): game %.1f ms, render %.1f ms, GPU %.1f ms; in the scene: %d squares, %d with standing parts, %d water, %d road pieces (%d drawn as static meshes, %d procedural kept hidden for collision), %d plant groups (%d casting shadows, %lld plants; of them %d groups of trees, %lld trees), %d sign words, %d lamps"),
					FrameGameMs / CostFrames, FrameRenderMs / CostFrames, FrameGpuMs / CostFrames,
					Tiles.Num(), TileExtras.Num(), TileWater.Num(), RoadParts.Num(), Looks, Hidden, Ism, IsmShadow, Instances, TreeGroups, TreeCount, Words, Lamps);
			}
			// [perf, build 150] How the picture is drawn, logged when it changes: the output size, the screen
			// percentage and what really draws at it, the anti-aliasing / upscaler, and the expensive parts.
			{
				FIntPoint OutSize(0, 0);
				if (GEngine && GEngine->GameViewport && GEngine->GameViewport->Viewport) OutSize = GEngine->GameViewport->Viewport->GetSizeXY();
				const IConsoleVariable* PctVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.ScreenPercentage"));
				const float Pct = PctVar ? PctVar->GetFloat() : 100.0f;
				const Scalability::FQualityLevels Q = Scalability::GetQualityLevels();
				const FTorqueSettings& Saved = FTorqueSettings::Get();
				const FString SettingsSaid = FString::Printf(TEXT("settings applied %d times (saved: preset %d%s, upscaler %s, render %.1f%%, frame gen %d); now r.NGX.DLSS.Enable %s, r.Streamline.DLSSG.Enable %s, Reflex %s; "),
					FTorqueSettings::Applied, Saved.Quality, Saved.bPerformance ? TEXT(" Performance") : TEXT(""), Saved.Upscaler == 1 ? TEXT("DLSS") : TEXT("TSR"),
					FTorqueSettings::ScreenPercent(Saved.UpscaleMode), Saved.bFrameGen ? 1 : 0, *TRProbe::CVarText(TEXT("r.NGX.DLSS.Enable")),
					*TRProbe::CVarText(TEXT("r.Streamline.DLSSG.Enable")), *TRProbe::CVarText(TEXT("t.Streamline.Reflex.Enable")));
				const FString View = SettingsSaid + FString::Printf(TEXT("output %dx%d, r.ScreenPercentage %s (%dx%d if applied; mode %s, dynamic res %s), anti-aliasing %s (4 TSR, 2 TAA), DLSS %s; presets view %d shadows %d GI %d reflections %d foliage %d effects %d; Nanite %s, VSM %s, Lumen GI %s/reflections %s, HW RT %s, clouds %s (steps %s), fog %s, view distance %s"),
					OutSize.X, OutSize.Y, *TRProbe::CVarText(TEXT("r.ScreenPercentage")), int32(OutSize.X * Pct / 100.0f), int32(OutSize.Y * Pct / 100.0f),
					*TRProbe::CVarText(TEXT("r.ScreenPercentage.Mode")), *TRProbe::CVarText(TEXT("r.DynamicRes.OperationMode")), *TRProbe::CVarText(TEXT("r.AntiAliasingMethod")),
					*TRProbe::CVarText(TEXT("r.NGX.DLSS.Enable")), Q.ViewDistanceQuality, Q.ShadowQuality, Q.GlobalIlluminationQuality, Q.ReflectionQuality, Q.FoliageQuality, Q.EffectsQuality,
					*TRProbe::CVarText(TEXT("r.Nanite")), *TRProbe::CVarText(TEXT("r.Shadow.Virtual.Enable")), *TRProbe::CVarText(TEXT("r.DynamicGlobalIlluminationMethod")),
					*TRProbe::CVarText(TEXT("r.ReflectionMethod")), *TRProbe::CVarText(TEXT("r.Lumen.HardwareRayTracing")), *TRProbe::CVarText(TEXT("r.VolumetricCloud")),
					*TRProbe::CVarText(TEXT("r.VolumetricCloud.ViewRaySampleMaxCount")), *TRProbe::CVarText(TEXT("r.VolumetricFog")), *TRProbe::CVarText(TEXT("r.ViewDistanceScale")));
				static FString LastView;
				if (View != LastView)
				{
					LastView = View;
					UE_LOG(LogTemp, Display, TEXT("Ride view: %s"), *View);
				}
			}
			// [perf, build 150] Against the frame budget (the strategy doc, beenster Nov 15: never under
			// 60 at Epic 4K): GPU 14, render thread 10, game thread 10, our own work 3 ms a frame and no
			// single job of ours over 8. Each OVER is what to cut next.
			{
				const double AvgGame = FrameGameMs / CostFrames, AvgRender = FrameRenderMs / CostFrames, AvgGpu = FrameGpuMs / CostFrames;
				double OursMs = 0.0, OursWorst = 0.0;
				int32 WorstWhich = 0;
				for (int32 W = 0; W < RideCosts::Count; ++W)
				{
					OursMs += S.Ms[W];
					if (S.Worst[W] > OursWorst)
					{
						OursWorst = S.Worst[W];
						WorstWhich = W;
					}
				}
				OursMs /= CostFrames;
				auto Mark = [](double Have, double Most) { return Have <= Most ? TEXT("ok") : TEXT("OVER"); };
				UE_LOG(LogTemp, Display, TEXT("Ride budget: GPU %.1f/14 %s, render %.1f/10 %s, game %.1f/10 %s, ours %.2f/3 %s, our worst job %.0f/8 %s (%s), parts waiting %d, spare parts %d, spare plant groups %d"),
					AvgGpu, Mark(AvgGpu, 14.0), AvgRender, Mark(AvgRender, 10.0), AvgGame, Mark(AvgGame, 10.0), OursMs, Mark(OursMs, 3.0),
					OursWorst, Mark(OursWorst, 8.0), RideCosts::Name(WorstWhich), ToRegister.Num(), Spare.Num(), SparePlants.Num());
			}
			FrameGameMs = FrameRenderMs = FrameGpuMs = 0.0;
			S = RideCosts::FSums();
			CostFrames = 0;
			CostSince = 0.0;
			CostWorstFrame = 0.0;
		}
	}
	// What the workers have made goes in place, a few a frame: [perf, build 148] up to 4, but no more
	// once 3 ms have gone on it (each now builds its static meshes here too: a frame stays a frame).
	const double PlaceStart = FPlatformTime::Seconds();
	for (int32 N = 0; N < 4 && (N == 0 || (FPlatformTime::Seconds() - PlaceStart) * 1000.0 < 3.0); ++N)
	{
		TPair<FIntVector, TSharedPtr<TorqueWorld::RideMesh, ESPMode::ThreadSafe>> One;
		TSharedPtr<FRidePrepared, ESPMode::ThreadSafe> Ready;
		{
			FScopeLock Hold(&Jobs->Lock);
			if (Jobs->Made.Num() == 0) break;
			One = Jobs->Made[0];
			Jobs->Made.RemoveAt(0);
			Jobs->Prepared.RemoveAndCopyValue(One.Key, Ready);
		}
		Asked.Remove(One.Key);
		if (One.Value.IsValid())
		{
			PreparedNow = Ready.Get();
			Place(One.Key, *One.Value, false);
			PreparedNow = nullptr;
		}
	}
	// [perf, build 149] The pieces of road the workers have made, likewise (two a frame, in the same 3 ms).
	{
		RideCosts::FScope Cost(RideCosts::Roads);
		for (int32 N = 0; N < 2 && (N == 0 || (FPlatformTime::Seconds() - PlaceStart) * 1000.0 < 3.0); ++N)
		{
			TTuple<int64, TSharedPtr<TorqueWorld::RideMesh, ESPMode::ThreadSafe>, TSharedPtr<FRidePrepared, ESPMode::ThreadSafe>, std::shared_ptr<const TorqueWorld::RidePlan>> Piece;
			{
				FScopeLock Hold(&Jobs->Lock);
				if (Jobs->RoadMade.Num() == 0) break;
				Piece = Jobs->RoadMade[0];
				Jobs->RoadMade.RemoveAt(0);
			}
			const int64 PieceKey = Piece.Get<0>();
			RoadAsked.Remove(PieceKey);
			// (made from a plan that has grown since: dropped, UpdateRoad asks again from the new one)
			if (Piece.Get<3>() != Plan) continue;
			if (!RoadParts.Contains(PieceKey) && Piece.Get<1>().IsValid())
				PlaceRoadPiece(PieceKey, *Piece.Get<1>(), Piece.Get<2>().Get(), false);
		}
	}
	RegisterWaiting();
	TickBirds(DeltaSeconds);
	// [stage 7] The street lamps come on as the light goes, and off in the morning.
	if (const ATorqueGameState* GS = GetWorld() ? GetWorld()->GetGameState<ATorqueGameState>() : nullptr)
	{
		const bool bOn = GS->GetDayLight() < (bStreetLightsOn ? 0.55f : 0.45f);
		if (bOn != bStreetLightsOn)
		{
			bStreetLightsOn = bOn;
			for (TPair<FIntVector, TArray<TWeakObjectPtr<UPointLightComponent>>>& Square : StreetLights)
				for (const TWeakObjectPtr<UPointLightComponent>& Lamp : Square.Value)
					if (Lamp.IsValid()) Lamp->SetVisibility(bOn);
		}
	}
	// [stage 7] The diary: where the rider is, twice a second; written down every half minute.
	SinceDiary += DeltaSeconds;
	SinceDiarySaved += DeltaSeconds;
	if (Diary && SinceDiary >= 0.5 && BirdsLookedAt.X < 1e11)
	{
		double Speed = 0.0;
		if (const UWorld* World = GetWorld())
			if (const APlayerController* PC = World->GetFirstPlayerController())
				if (const APawn* Pawn = PC->GetPawn()) Speed = Pawn->GetVelocity().Size() / 100.0;
		const TorqueWorld::Geo G = Plan->Area.Roads.FrameOf().ToGeo(BirdsLookedAt.X, BirdsLookedAt.Y);
		Diary->Step(G.Lat, G.Lon, Speed, SinceDiary, FDateTime::UtcNow().ToUnixTimestamp());
		SinceDiary = 0.0;
		if (SinceDiarySaved > 30.0) SaveDiary();
	}
	// (the sound of the place, once a second: a fraction of a millisecond)
	SinceHeard += DeltaSeconds;
	if (SinceHeard >= 1.0 && BirdsLookedAt.X < 1e11)
	{
		SinceHeard = 0.0;
		const TorqueWorld::RidePlace Place = Plan->SoundsAt(BirdsLookedAt.X, BirdsLookedAt.Y);
		Heard[0] = Place.Sea; Heard[1] = Place.Water; Heard[2] = Place.Town; Heard[3] = Place.Sheep; Heard[4] = Place.Cattle;
	}
	SinceLook += DeltaSeconds;
	if (SinceLook < 0.2) return;
	SinceLook = 0.0;
	const FVector2D At = RiderAt();
	if (BirdsLookedAt.X < 1e11 && FVector2D::Distance(At, BirdsLookedAt) > 0.5) BirdsGoing = (At - BirdsLookedAt).GetSafeNormal();
	BirdsLookedAt = At;
	{
		RideCosts::FScope Cost(RideCosts::Update);
		Update(At, false);
	}
	{
		// ([perf, build 151] its own line: "roads" is now only putting the made pieces in place)
		RideCosts::FScope Cost(RideCosts::RoadLook);
		UpdateRoad(At, false);
	}
	// [ride anywhere] The ground round the rider fetched as they roam, on a worker, every 800 m: the
	// fine tiles near, the middle and skyline tiles out to their reach. The far squares made before
	// new tiles came are made again (the near ones fetch their own before they are made).
	if (!bFetchingGround && (GroundAt.X > 1e11 || FVector2D::Distance(At, GroundAt) > 800.0))
	{
		bFetchingGround = true;
		GroundAt = At;
		const std::shared_ptr<const TorqueWorld::RidePlan> Held = Plan;
		const std::shared_ptr<FTorqueRideJobs> Share = Jobs;
		const TWeakObjectPtr<ATorqueRideWorld> Self(this);
		AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [Held, Share, Self, At]()
		{
			const int32 Came = Share->bStop ? 0 : Held->GroundRound(At.X, At.Y);
			AsyncTask(ENamedThreads::GameThread, [Self, Came]()
			{
				ATorqueRideWorld* Ride = Self.Get();
				if (Ride == nullptr) return;
				Ride->bFetchingGround = false;
				if (Came <= 0) return;
				for (const auto& Have : Ride->Tiles)
					if (Have.Key.X >= 2) Ride->Stale.Add(Have.Key);
			});
		});
	}
	// [ride anywhere 3] More of the world read as the rider nears the edge of what was.
	SinceGrowLook += 0.2;
	if (SinceGrowLook >= 2.0)
	{
		SinceGrowLook = 0.0;
		TryGrow(At);
		if (Plan)
			for (const std::string& Note : Plan->TakeOutlineNotes())
				UE_LOG(LogTemp, Log, TEXT("Towns: %s"), UTF8_TO_TCHAR(Note.c_str()));
	}
	// A square no longer wanted goes once every wanted square over the same ground is in place.
	TArray<FIntVector> Going;
	const TSet<FIntVector> WantSet(Want);
	bool bAll = true;
	for (const FIntVector& Key : Want) bAll = bAll && Tiles.Contains(Key);
	for (const TPair<FIntVector, TObjectPtr<UProceduralMeshComponent>>& Have : Tiles)
	{
		if (WantSet.Contains(Have.Key)) continue;
		bool bCovered = true;
		for (const FIntVector& Key : Want)
		{
			if (TRWI::Overlap(Key, Have.Key) && !Tiles.Contains(Key)) { bCovered = false; break; }
		}
		if (bCovered) Going.Add(Have.Key);
	}
	for (const FIntVector& Key : Going)
	{
		LetGo(Tiles[Key]);
		Tiles.Remove(Key);
		DropPlants(Key);
		DropWords(Key);
		DropWater(Key);
		if (TObjectPtr<UProceduralMeshComponent>* Stands = TileExtras.Find(Key))
		{
			LetGo(*Stands);
			TileExtras.Remove(Key);
		}
		DropStreetLights(Key);
	}
	if (bAll && !bSaidReady)
	{
		bSaidReady = true;
		UE_LOG(LogTemp, Display, TEXT("Torque ride world: all the ground round the rider is in place (%d squares, %d pieces of road, %d plants, trees and animals; %d animals placed so far)"), Tiles.Num(), RoadParts.Num(), PlantsShown, AnimalsShown);
	}
}

namespace TRWBirds
{
// A bird with its wings spread, facing +x in the ride's frame, wings along z: 0 a buzzard (broad
// brown wings, a fanned tail), 1 a red kite (long, angled rufous wings, a forked tail), 2 a gull
// (white, grey wings with black tips). Seen from both sides.
TorqueWorld::RideMesh BirdMesh(int32 Kind)
{
	TorqueWorld::RideMesh M;
	const float Span = Kind == 1 ? 0.8f : Kind == 2 ? 0.68f : 0.62f;   // (half the wingspan, m)
	const float Body[3] = {Kind == 2 ? 0.55f : 0.09f, Kind == 2 ? 0.55f : 0.06f, Kind == 2 ? 0.55f : 0.04f};
	const float Wing[3] = {Kind == 2 ? 0.32f : Kind == 1 ? 0.17f : 0.075f, Kind == 2 ? 0.33f : Kind == 1 ? 0.065f : 0.052f, Kind == 2 ? 0.35f : Kind == 1 ? 0.028f : 0.035f};
	const float Tip[3] = {Kind == 2 ? 0.02f : 0.03f, Kind == 2 ? 0.02f : 0.025f, Kind == 2 ? 0.02f : 0.02f};
	const auto Tri = [&M](const float* A, const float* B, const float* C, const float* Colour)
	{
		for (int Side = 0; Side < 2; ++Side)
		{
			const uint32 V = uint32(M.Vertices());
			for (const float* P : {A, B, C})
			{
				M.Pos.insert(M.Pos.end(), {P[0], P[1], P[2]});
				M.Nrm.insert(M.Nrm.end(), {0.0f, Side == 0 ? 1.0f : -1.0f, 0.0f});
				M.Uv.insert(M.Uv.end(), {P[0], P[2]});
				M.Col.insert(M.Col.end(), {Colour[0], Colour[1], Colour[2]});
				M.Alpha.push_back(0.0f);
			}
			if (Side == 0) M.Idx.insert(M.Idx.end(), {V, V + 1, V + 2});
			else M.Idx.insert(M.Idx.end(), {V, V + 2, V + 1});
		}
	};
	// The body: a long diamond, nose to tail.
	const float Nose[3] = {0.28f, 0.0f, 0.0f}, Back[3] = {-0.25f, 0.0f, 0.0f}, L[3] = {0.0f, 0.02f, -0.07f}, R[3] = {0.0f, 0.02f, 0.07f};
	Tri(Nose, L, Back, Body);
	Tri(Nose, Back, R, Body);
	// The wings, raised a little (a buzzard's shallow V), the kite's swept back at the wrist.
	for (float S : {-1.0f, 1.0f})
	{
		const float Root0[3] = {0.12f, 0.0f, S * 0.05f}, Root1[3] = {-0.1f, 0.0f, S * 0.05f};
		const float Wrist0[3] = {Kind == 1 ? 0.08f : 0.1f, Kind == 0 ? 0.06f : 0.03f, S * Span * 0.55f}, Wrist1[3] = {Kind == 1 ? -0.14f : -0.12f, Kind == 0 ? 0.06f : 0.03f, S * Span * 0.55f};
		const float End0[3] = {Kind == 1 ? -0.05f : 0.02f, Kind == 0 ? 0.11f : 0.02f, S * Span}, End1[3] = {Kind == 1 ? -0.2f : -0.1f, Kind == 0 ? 0.11f : 0.02f, S * Span * 0.95f};
		Tri(Root0, Wrist0, Root1, Wing);
		Tri(Root1, Wrist0, Wrist1, Wing);
		Tri(Wrist0, End0, Wrist1, Kind == 2 ? Tip : Wing);
		Tri(Wrist1, End0, End1, Kind == 2 ? Tip : Wing);
	}
	// The tail: fanned, or forked on a kite.
	if (Kind == 1)
	{
		const float A[3] = {-0.22f, 0.0f, -0.03f}, B[3] = {-0.22f, 0.0f, 0.03f}, C[3] = {-0.5f, 0.0f, -0.09f}, D[3] = {-0.5f, 0.0f, 0.09f}, E[3] = {-0.42f, 0.0f, 0.0f};
		Tri(A, C, E, Wing);
		Tri(B, E, D, Wing);
		Tri(A, E, B, Wing);
	}
	else
	{
		const float A[3] = {-0.22f, 0.0f, -0.03f}, B[3] = {-0.22f, 0.0f, 0.03f}, C[3] = {-0.42f, 0.0f, -0.09f}, D[3] = {-0.42f, 0.0f, 0.09f};
		Tri(A, C, D, Body);
		Tri(A, D, B, Body);
	}
	return M;
}

double Unit(uint32 H)
{
	H ^= H >> 16; H *= 0x7feb352du; H ^= H >> 15; H *= 0x846ca68bu; H ^= H >> 16;
	return double(H & 0xffffffu) / double(0x1000000);
}
} // namespace TRWBirds

void ATorqueRideWorld::SendBird(FRideBird& Bird, const FVector2D& At, uint32 Salt)
{
	// Somewhere 150 to 450 m off, mostly ahead (the way the rider was last seen going), circling 35 to
	// 110 m up (gulls lower and quicker), round 40 to 90 m.
	const FVector2D Going = BirdsGoing;
	const double Turn = (TRWBirds::Unit(Salt * 3u + 1u) - 0.5) * 2.4;
	const double Far = 150.0 + 300.0 * TRWBirds::Unit(Salt * 3u + 2u);
	const FVector2D Dir(Going.X * FMath::Cos(Turn) - Going.Y * FMath::Sin(Turn), Going.X * FMath::Sin(Turn) + Going.Y * FMath::Cos(Turn));
	const FVector2D Where = At + Dir * Far;
	// (which kind: gulls if the sea is within 4 km, red kites in a share of the places they live, else buzzards)
	bool bSea = false;
	if (Plan->Land)
		for (int32 K = 0; K < 8 && !bSea; ++K)
			for (double R : {1500.0, 4000.0})
				bSea = bSea || Plan->Land->SeaAt(Where.X + FMath::Cos(K * PI / 4.0) * R, Where.Y + FMath::Sin(K * PI / 4.0) * R);
	const FString Region = UTF8_TO_TCHAR(Plan->LookHere(Where.X, Where.Y).Name);
	const bool bKites = Region == TEXT("wales") || Region == TEXT("lowland") || Region == TEXT("cotswolds") || Region == TEXT("dales") || Region == TEXT("highlands");
	const double Pick = TRWBirds::Unit(Salt * 7u + 5u);
	const int32 Kind = bSea && Pick < 0.7 ? 2 : bKites && Pick < 0.5 ? 1 : 0;
	const double Ground = Plan->Ground(Where.X, Where.Y);
	Bird.Centre = FVector(Where.X, (FMath::IsNaN(Ground) ? 300.0 : Ground) + (Kind == 2 ? 20.0 + 30.0 * Pick : 35.0 + 75.0 * Pick), Where.Y);
	Bird.Radius = 40.0 + 50.0 * TRWBirds::Unit(Salt * 11u + 3u);
	Bird.Speed = (Kind == 2 ? 11.0 : Kind == 1 ? 8.0 : 9.0) * (TRWBirds::Unit(Salt * 13u + 7u) < 0.5 ? -1.0 : 1.0);
	Bird.Angle = TRWBirds::Unit(Salt * 17u + 9u) * 2.0 * PI;
	Bird.Bank = FMath::Atan2(Bird.Speed * Bird.Speed, 9.81 * Bird.Radius);
	Bird.Drift = 0.0;
	if (!Bird.Part.IsValid() || Bird.Kind != Kind)
	{
		if (Bird.Part.IsValid()) Bird.Part->DestroyComponent();
		const TorqueWorld::RideMesh Shape = TRWBirds::BirdMesh(Kind);
		Bird.Part = MakePart(Shape, RenderMaterial, false, true, false);
	}
	Bird.Kind = Kind;
}

void ATorqueRideWorld::TryGrow(const FVector2D& At)
{
	if (bGrowing || !Plan || !Plan->Ok || Plan->CellsFrom == nullptr || Plan->HeightsFrom == nullptr || !Jobs) return;
	// (a cell near the rider not read and not already found missing)
	TArray<int64> Near;
	for (const TorqueWorld::CellId& C : Plan->CellsToGrow(At.X, At.Y, TorqueWorld::RidePlan::GrowPad))
	{
		const int64 Key = int64(C.Row) * 100000 + C.Col;
		if (!GrowTried.Contains(Key)) Near.Add(Key);
	}
	if (Near.Num() == 0) return;
	// (and every other cell it would read, so a miss isn't asked again)
	TArray<int64> Tried = Near;
	for (const TorqueWorld::CellId& C : Plan->CellsToGrow(At.X, At.Y, TorqueWorld::RidePlan::RoamPad)) Tried.AddUnique(int64(C.Row) * 100000 + C.Col);
	bGrowing = true;
	const std::shared_ptr<TorqueWorld::RidePlan> Held = Plan;
	const std::shared_ptr<FTorqueRideJobs> Share = Jobs;
	const TWeakObjectPtr<ATorqueRideWorld> Self(this);
	UE_LOG(LogTemp, Log, TEXT("Ride anywhere: reading more of the world round (%.0f, %.0f), %d cells near not read"), At.X, At.Y, Near.Num());
	AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [Held, Share, Self, At, Tried]()
	{
		std::string Why;
		std::shared_ptr<TorqueWorld::RidePlan> Grown;
		if (!Share->bStop) Grown = TorqueWorld::RidePlan::Grow(*Held->CellsFrom, *Held->HeightsFrom, *Held, At.X, At.Y, &Why);
		if (!Grown) UE_LOG(LogTemp, Log, TEXT("Ride anywhere: not grown: %s"), UTF8_TO_TCHAR(Why.c_str()));
		AsyncTask(ENamedThreads::GameThread, [Self, Held, Grown, Tried]()
		{
			if (ATorqueRideWorld* Ride = Self.Get()) Ride->Grew(Held, Grown, Tried);
		});
	});
}

void ATorqueRideWorld::Grew(const std::shared_ptr<TorqueWorld::RidePlan>& Old, const std::shared_ptr<TorqueWorld::RidePlan>& Grown, const TArray<int64>& Tried)
{
	RideCosts::FScope Cost(RideCosts::Grow);
	bGrowing = false;
	for (const int64 Key : Tried) GrowTried.Add(Key);
	if (!Grown || Plan != Old) return;
	// (cells read now are no longer misses)
	for (const TorqueWorld::CellId& C : Grown->Area.Cells) GrowTried.Remove(int64(C.Row) * 100000 + C.Col);
	Plan = Grown;
	if (UGameInstance* GI = GetGameInstance())
		if (UTorqueWorldSubsystem* Real = GI->GetSubsystem<UTorqueWorldSubsystem>()) Real->RideGrew(Old, Grown);
	// The roads that met the old edge, drawn again with their new junctions.
	TSet<int64> Again;
	for (const int Which : Grown->RedrawRoads) Again.Add(int64(Which));
	TArray<int64> Drop;
	for (const auto& Have : RoadParts)
		if (Again.Contains(Have.Key / 4096)) Drop.Add(Have.Key);
	for (const int64 Key : Drop)
	{
		LetGo(RoadParts[Key]);
		RoadParts.Remove(Key);
		if (TArray<TWeakObjectPtr<UPointLightComponent>>* Lights = RoadLights.Find(Key))
		{
			for (const TWeakObjectPtr<UPointLightComponent>& Lamp : *Lights) if (Lamp.IsValid()) Lamp->DestroyComponent();
			RoadLights.Remove(Key);
		}
	}
	// The squares that reach within a kilometre of the old edge or past it, made again with what is there now.
	for (const auto& Have : Tiles)
	{
		const double Size = TRWI::SizeOf(Have.Key.X);
		const double X0 = Have.Key.Y * Size, Z0 = Have.Key.Z * Size;
		if (!Grown->WasRead(X0 - 1000.0, Z0 - 1000.0, X0 + Size + 1000.0, Z0 + Size + 1000.0)) Stale.Add(Have.Key);
	}
	// The maps: the bigger area, and the way (unless the sat nav is taking the rider back to the road).
	Roam = TorqueWorld::RoamOfPlan(Plan, Roam ? Roam->Version + 1 : 1);
	if (!NavState.Detour) Guide = TorqueWorld::GuideOfPlan(*Plan, Guide ? Guide->Version + 1 : 1);
	OnRoamChanged.Broadcast();
	UE_LOG(LogTemp, Log, TEXT("Ride anywhere: the ride grew to %d cells, %d roads (%d drawn again)"), int32(Grown->Area.Cells.size()), int32(Grown->Area.Roads.Edges.size()),
		int32(Grown->RedrawRoads.size()));
}

void ATorqueRideWorld::TickBirds(float DeltaSeconds)
{
	if (!Plan || BirdsLookedAt.X > 1e11) return;
	if (Birds.Num() == 0) Birds.SetNum(3);
	static uint32 Sent = 1;
	for (FRideBird& Bird : Birds)
	{
		const FVector2D Off(Bird.Centre.X - BirdsLookedAt.X, Bird.Centre.Z - BirdsLookedAt.Y);
		if (!Bird.Part.IsValid() || Off.Size() > 1600.0) SendBird(Bird, BirdsLookedAt, Sent++);
		if (!Bird.Part.IsValid()) continue;
		// Round its circle, the circle itself drifting slowly east as a thermal on the westerlies does.
		Bird.Angle += double(DeltaSeconds) * Bird.Speed / Bird.Radius;
		Bird.Drift += double(DeltaSeconds) * 1.2;
		const double X = Bird.Centre.X + FMath::Cos(Bird.Angle) * Bird.Radius + Bird.Drift, Z = Bird.Centre.Z + FMath::Sin(Bird.Angle) * Bird.Radius;
		const double Y = Bird.Centre.Y + 6.0 * FMath::Sin(Bird.Angle * 0.5);
		// (its heading along the circle, banked into it; the frames share x and their z is Unreal's y)
		const double Heading = Bird.Angle + (Bird.Speed > 0.0 ? 0.5 * PI : -0.5 * PI);
		const double Roll = FMath::RadiansToDegrees(Bird.Bank) * (Bird.Speed > 0.0 ? 1.0 : -1.0);
		Bird.Part->SetRelativeLocationAndRotation(FVector(X * 100.0, Z * 100.0, Y * 100.0), FRotator(0.0, FMath::RadiansToDegrees(Heading), Roll));
	}
}

void ATorqueRideWorld::PlaceAnimals(const FIntVector& Key, const TorqueWorld::RideMesh& Mesh)
{
	if (Mesh.Animals.empty() || AnimalKit.Num() == 0) return;
	TMap<int32, TArray<FTransform>> Groups;   // by model
	for (const TorqueWorld::RideAnimal& A : Mesh.Animals)
	{
		if (A.Kind >= TorqueWorld::RidePlan::AnimalKinds) continue;
		const TArray<int32>& Choice = AnimalModels[A.Kind];
		if (Choice.Num() == 0) continue;
		const int32 Mi = Choice[FMath::Min(int32(A.Pick * Choice.Num()), Choice.Num() - 1)];
		// (the ride's frame is x east, y up, z south; Unreal's x east, y south, z up: a yaw a there is a here)
		const FQuat Rot(FVector::UpVector, FMath::DegreesToRadians(FMath::RadiansToDegrees(double(A.Yaw)) + AnimalFrame.X));
		const double Size = double(A.Scale) * AnimalKit[Mi].ScaleMultiplier;
		const FVector Scale3(Size * AnimalFrame.Z, Size * AnimalFrame.Z * AnimalFrame.Y, Size * AnimalFrame.Z);
		Groups.FindOrAdd(Mi).Add(FTransform(Rot, FVector(double(A.X) * 100.0, double(A.Z) * 100.0, double(A.Y) * 100.0), Scale3));
	}
	TArray<TWeakObjectPtr<UInstancedStaticMeshComponent>>& Parts = TilePlants.FindOrAdd(Key);
	for (TPair<int32, TArray<FTransform>>& Group : Groups)
	{
		const FTorqueFoliageSpecies& S = AnimalKit[Group.Key];
		if (TRWI::Unseen(Key, FMath::Max(S.CullDistance, 20000.0f))) continue;
		UInstancedStaticMeshComponent* Ism = NewObject<UInstancedStaticMeshComponent>(this, NAME_None, RF_Transient);
		Ism->SetMobility(EComponentMobility::Movable);
		Ism->SetupAttachment(RootComponent);
		Ism->SetRelativeLocation(FVector(Mesh.OriginX * 100.0, Mesh.OriginZ * 100.0, 0.0));
		Ism->SetStaticMesh(S.Mesh);
		if (S.Material) Ism->SetMaterial(0, S.Material);
		Ism->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Ism->SetCollisionProfileName(TEXT("NoCollision"));
		Ism->SetCanEverAffectNavigation(false);
		Ism->SetGenerateOverlapEvents(false);
		Ism->SetCastShadow(true);
		Ism->bReceivesDecals = false;
		Ism->bVisibleInRayTracing = false;
		Ism->WorldPositionOffsetDisableDistance = 6000;
		const float Cull = FMath::Max(S.CullDistance, 20000.0f);
		Ism->SetCullDistances(int32(Cull * 0.85f), int32(Cull));
		Ism->LDMaxDrawDistance = Cull * 1.05f;
		Ism->RegisterComponent();
		Ism->AddInstances(Group.Value, false, false);
		AddInstanceComponent(Ism);
		Parts.Add(Ism);
		PlantsShown += Group.Value.Num();   // (let go with the plants, which count them off)
		AnimalsShown += Group.Value.Num();
	}
}

void ATorqueRideWorld::FindProps()
{
	using TorqueWorld::RidePlan;
	PropKit.Reset();
	PropModels.Reset();
	PropModels.SetNum(RidePlan::PropKinds);
	PropFix.Init(FVector(100.0, 0.0, 0.0), RidePlan::PropKinds);
	// (each kind's height in life, turn and drop, from the kit file)
	std::string Text;
	std::vector<RidePlan::PropKitRow> Rows;
	const std::string Path = TCHAR_TO_UTF8(*FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("Data/RideUK/prop_kit.json")));
	if (TorqueWorld::Files::Read(Path, Text) && RidePlan::ReadPropKit(Text, Rows))
		for (const RidePlan::PropKitRow& R : Rows)
			for (int32 K = 0; K < RidePlan::PropKinds; ++K)
				if (R.Kind == RidePlan::PropName(K)) PropFix[K] = FVector(R.Height * 100.0, R.Turn, R.Drop * 100.0);
	if (UWorld* World = GetWorld())
		for (TActorIterator<ATorqueFoliage> It(World); It; ++It)
		{
			if (!It->ActorHasTag(TEXT("RideProps"))) continue;
			PropFrame = FVector(It->FrameYaw, It->FrameMirror, It->FrameScale);
			for (const FTorqueFoliageSpecies& S : It->Species)
			{
				if (!S.Mesh) continue;
				const FString Name = S.Name.ToString();
				for (int32 K = 0; K < RidePlan::PropKinds; ++K)
					if (Name.StartsWith(FString(TEXT("SM_Prop_")) + UTF8_TO_TCHAR(RidePlan::PropName(K)) + TEXT("_"))) PropModels[K].Add(PropKit.Num());
				PropKit.Add(S);
			}
		}
	// [fps plan step 2] (a facade kind's models in order of size, SM_Prop_<kind>_<size>: a prop's Variant is its size)
	for (int32 K = RidePlan::FirstFacade; K < RidePlan::PropKinds; ++K)
		PropModels[K].Sort([this](const int32 A, const int32 B)
		{
			const FString Na = PropKit[A].Name.ToString(), Nb = PropKit[B].Name.ToString();
			return FCString::Atoi(*Na.RightChop(Na.Find(TEXT("_"), ESearchCase::CaseSensitive, ESearchDir::FromEnd) + 1))
				< FCString::Atoi(*Nb.RightChop(Nb.Find(TEXT("_"), ESearchCase::CaseSensitive, ESearchDir::FromEnd) + 1));
		});
	FString Have, Missing;
	PropCredits.Reset();
	for (int32 K = 0; K < RidePlan::PropKinds; ++K)
	{
		(PropModels[K].Num() ? Have : Missing) += FString(TEXT(" ")) + UTF8_TO_TCHAR(RidePlan::PropName(K));
		// (a kind drawn from others' models credits them, as their licence asks)
		bool Theirs = false;
		for (const int32 Mi : PropModels[K]) Theirs = Theirs || PropKit[Mi].bNativeMesh;
		if (!Theirs) continue;
		for (const RidePlan::PropKitRow& R : Rows)
			if (R.Kind == RidePlan::PropName(K) && !R.Credit.empty())
			{
				const FString Credit = FString(TEXT("Models: ")) + UTF8_TO_TCHAR(R.Credit.c_str()) + TEXT(".");
				PropCredits.AddUnique(Credit);
			}
	}
	UE_LOG(LogTemp, Display, TEXT("Torque ride world: props: %d models; from models:%s; code-made:%s%s"), PropKit.Num(), Have.IsEmpty() ? TEXT(" none") : *Have,
		Missing.IsEmpty() ? TEXT(" none") : *Missing, PropKit.Num() ? TEXT("") : TEXT(" (no RideProps actor in the level: python Tools/make_ride_props.py, then Tools/ue_build_rideuk.py)"));
}

void ATorqueRideWorld::PlaceProps(const FIntVector& Key, const TorqueWorld::RideMesh& Mesh)
{
	using TorqueWorld::RidePlan;
	if (Mesh.Props.empty() || PropKit.Num() == 0) return;
	// (by model: the instances, and for the facade kit's their colours, nine floats each)
	struct FPropGroup { TArray<FTransform> At; TArray<float> Tint; };
	TMap<int32, FPropGroup> Groups;
	for (const TorqueWorld::RideProp& P : Mesh.Props)
	{
		if (P.Kind >= RidePlan::PropKinds) continue;
		const TArray<int32>& Choice = PropModels[P.Kind];
		if (Choice.Num() == 0) continue;
		// (a facade model's Variant is its size: the nearest there is if the kit lacks it)
		const int32 Mi = P.Facade ? Choice[FMath::Min(int32(P.Variant), Choice.Num() - 1)] : Choice[int32(P.Variant) % Choice.Num()];
		const FTorqueFoliageSpecies& S = PropKit[Mi];
		UStaticMesh* Model = S.Mesh.Get();
		if (!Model) continue;
		const FVector& Fix = PropFix[P.Kind];
		// (as tall as the kind in life, whatever the model's own height; a kind of height 0, the facade kit, as modelled)
		const double Own = Model->GetBounds().BoxExtent.Z * 2.0 * (S.bNativeMesh ? 1.0 : FMath::Abs(PropFrame.Z));
		const double Size = (Own > 1.0 && Fix.X > 0.0 ? Fix.X / Own : 1.0) * double(P.Scale) * S.ScaleMultiplier;
		// (the ride's frame is x east, y up, z south; Unreal's x east, y south, z up: a yaw a there is a here)
		const FQuat Rot(FVector::UpVector, FMath::DegreesToRadians(FMath::RadiansToDegrees(double(P.Yaw)) + Fix.Y + (S.bNativeMesh ? 0.0 : PropFrame.X)));
		const FVector Scale3 = S.bNativeMesh ? FVector(Size) : FVector(Size * PropFrame.Z, Size * PropFrame.Z * PropFrame.Y, Size * PropFrame.Z);
		FPropGroup& Group = Groups.FindOrAdd(Mi);
		Group.At.Add(FTransform(Rot, FVector(double(P.X) * 100.0, double(P.Z) * 100.0, double(P.Y) * 100.0 - Fix.Z), Scale3));
		if (P.Facade) Group.Tint.Append(P.Tint, 9);
	}
	TArray<TWeakObjectPtr<UInstancedStaticMeshComponent>>& Parts = TilePlants.FindOrAdd(Key);
	for (TPair<int32, FPropGroup>& Group : Groups)
	{
		const FTorqueFoliageSpecies& S = PropKit[Group.Key];
		if (TRWI::Unseen(Key, FMath::Max(S.CullDistance, 5000.0f))) continue;
		// As the animals: no collision, no navigation; shadows only from the taller kinds (the kit says).
		UInstancedStaticMeshComponent* Ism = NewObject<UInstancedStaticMeshComponent>(this, NAME_None, RF_Transient);
		Ism->SetMobility(EComponentMobility::Movable);
		Ism->SetupAttachment(RootComponent);
		Ism->SetRelativeLocation(FVector(Mesh.OriginX * 100.0, Mesh.OriginZ * 100.0, 0.0));
		Ism->SetStaticMesh(S.Mesh);
		if (S.Material) Ism->SetMaterial(0, S.Material);
		Ism->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Ism->SetCollisionProfileName(TEXT("NoCollision"));
		Ism->SetCanEverAffectNavigation(false);
		Ism->SetGenerateOverlapEvents(false);
		Ism->SetCastShadow(S.bCastShadow);
		Ism->bReceivesDecals = false;
		Ism->bVisibleInRayTracing = false;
		const float Cull = FMath::Max(S.CullDistance, 5000.0f);
		Ism->SetCullDistances(int32(Cull * 0.85f), int32(Cull));
		Ism->LDMaxDrawDistance = Cull * 1.05f;
		// [fps plan step 2] (the facade kit's: paint, trim and third colours per instance, read by M_RideFacade)
		const bool bTinted = Group.Value.Tint.Num() == Group.Value.At.Num() * 9;
		if (bTinted) Ism->SetNumCustomDataFloats(9);
		Ism->RegisterComponent();
		Ism->AddInstances(Group.Value.At, false, false);
		if (bTinted)
		{
			for (int32 I = 0; I < Group.Value.At.Num(); ++I) Ism->SetCustomData(I, TArrayView<const float>(Group.Value.Tint.GetData() + I * 9, 9), false);
			Ism->MarkRenderStateDirty();
		}
		AddInstanceComponent(Ism);
		Parts.Add(Ism);
		PlantsShown += Group.Value.At.Num();   // (let go with the plants, which count them off)
		PropsShown += Group.Value.At.Num();
	}
}

bool ATorqueRideWorld::FindKit()
{
	Kit.Reset();
	KindNear.Reset();
	KindFar.Reset();
	KindNear.SetNum(TorqueWorld::RidePlan::PlantKinds);
	KindFar.SetNum(TorqueWorld::RidePlan::PlantKinds);
	KitTrees = 0;
	const ATorqueFoliage* Grass = nullptr;
	const ATorqueFoliage* Trees = nullptr;
	if (UWorld* World = GetWorld())
		for (TActorIterator<ATorqueFoliage> It(World); It; ++It)
		{
			if (It->ActorHasTag(TEXT("RideGrass"))) Grass = *It;
			if (It->ActorHasTag(TEXT("RideTrees"))) Trees = *It;
		}
	if (Grass)
	{
		GrassFrame = FVector(Grass->FrameYaw, Grass->FrameMirror, Grass->FrameScale);
		for (const FTorqueFoliageSpecies& S : Grass->Species)
		{
			if (!S.Mesh && S.Variants.Num() == 0) continue;
			for (int32 K = 0; K < TorqueWorld::RidePlan::FirstTree; ++K)
				if (S.Name == FName(TorqueWorld::RidePlan::PlantName(K))) KindNear[K].Add(Kit.Num());
			Kit.Add(S);
		}
	}
	KitTrees = Kit.Num();
	if (Trees)
	{
		TreeFrame = FVector(Trees->FrameYaw, Trees->FrameMirror, Trees->FrameScale);
		for (const FTorqueFoliageSpecies& S : Trees->Species)
		{
			if (!S.Mesh && S.Variants.Num() == 0) continue;
			const FString Name = S.Name.ToString();
			for (int32 K = TorqueWorld::RidePlan::FirstTree; K < TorqueWorld::RidePlan::PlantKinds; ++K)
			{
				const FString Kind = UTF8_TO_TCHAR(TorqueWorld::RidePlan::PlantName(K));
				if (Name.StartsWith(TEXT("SM_Tree_") + Kind + TEXT("_"))) KindNear[K].Add(Kit.Num());
				if (Name.StartsWith(TEXT("SM_TreeFar_") + Kind + TEXT("_"))) KindFar[K].Add(Kit.Num());
			}
			Kit.Add(S);
		}
		// [perf, build 149] A kind with near models (a scanned tree) but no far ones of its own is drawn far
		// off by the far models of the kind that stands in for it, not by the scans: the far ring's
		// trees are thousands, and full scanned trees there cost about 10 ms of GPU at Whaley Bridge
		// (build 148). -TorqueRideFarScans=1 draws them as before.
		static const bool bFarScans = []() { int32 On = 0; return FParse::Value(FCommandLine::Get(), TEXT("TorqueRideFarScans="), On) && On != 0; }();
		for (int32 K = TorqueWorld::RidePlan::FirstTree; K < TorqueWorld::RidePlan::PlantKinds; ++K)
		{
			const bool bScanned = KindNear[K].ContainsByPredicate([this](int32 Si) { return Kit[Si].Name.ToString().EndsWith(TEXT("_scan")); });
			if (bFarScans && bScanned) KindFar[K] = KindNear[K];
			const int32 Instead = TorqueWorld::RidePlan::PlantStandIn(K);
			if (!bFarScans && KindFar[K].Num() == 0 && KindNear[K].Num() > 0 && Instead != K && KindFar[Instead].Num() > 0) KindFar[K] = KindFar[Instead];
		}
		// (a kind with near models but no far ones uses the near ones far off too, and the other way about;
		// one with neither, the models of the kind that stands in for it: oak for ash and so on)
		for (int32 K = TorqueWorld::RidePlan::FirstTree; K < TorqueWorld::RidePlan::PlantKinds; ++K)
		{
			if (KindFar[K].Num() == 0) KindFar[K] = KindNear[K];
			if (KindNear[K].Num() == 0) KindNear[K] = KindFar[K];
		}
		for (int32 K = TorqueWorld::RidePlan::FirstTree; K < TorqueWorld::RidePlan::PlantKinds; ++K)
		{
			const int32 Instead = TorqueWorld::RidePlan::PlantStandIn(K);
			if (KindNear[K].Num() == 0 && Instead != K) { KindNear[K] = KindNear[Instead]; KindFar[K] = KindFar[Instead]; }
		}
	}
	// [stage 6] The livestock's models, by the kind their name says (SM_Animal_<kind>_<n>).
	AnimalKit.Reset();
	AnimalModels.Reset();
	AnimalModels.SetNum(TorqueWorld::RidePlan::AnimalKinds);
	if (UWorld* World = GetWorld())
		for (TActorIterator<ATorqueFoliage> It(World); It; ++It)
		{
			if (!It->ActorHasTag(TEXT("RideAnimals"))) continue;
			AnimalFrame = FVector(It->FrameYaw, It->FrameMirror, It->FrameScale);
			for (const FTorqueFoliageSpecies& S : It->Species)
			{
				if (!S.Mesh) continue;
				const FString Name = S.Name.ToString();
				for (int32 K = 0; K < TorqueWorld::RidePlan::AnimalKinds; ++K)
					if (Name.StartsWith(FString(TEXT("SM_Animal_")) + UTF8_TO_TCHAR(TorqueWorld::RidePlan::AnimalName(K)) + TEXT("_"))) AnimalModels[K].Add(AnimalKit.Num());
				AnimalKit.Add(S);
			}
		}
	UE_LOG(LogTemp, Display, TEXT("Torque ride world: livestock: %d models%s"), AnimalKit.Num(),
		AnimalKit.Num() ? TEXT("") : TEXT(" (no RideAnimals actor in the level: python Tools/make_ride_animals.py, then Tools/ue_build_rideuk.py)"));
	FString Missing;
	for (int32 K = 0; K < TorqueWorld::RidePlan::PlantKinds; ++K)
		if (KindNear[K].Num() == 0) Missing += FString(TEXT(" ")) + UTF8_TO_TCHAR(TorqueWorld::RidePlan::PlantName(K));
	UE_LOG(LogTemp, Display, TEXT("Torque ride world: plant kit: %d grass species, %d tree models%s%s"), KitTrees, Kit.Num() - KitTrees,
		Grass || Trees ? TEXT("") : TEXT(" (no RideGrass or RideTrees actor in the level: run Tools/ue_build_rideuk.py)"),
		Missing.IsEmpty() ? TEXT("") : *(TEXT("; none for") + Missing));
	return Kit.Num() > 0;
}

void ATorqueRideWorld::DropPlants(const FIntVector& Key)
{
	if (TArray<TWeakObjectPtr<UInstancedStaticMeshComponent>>* Old = TilePlants.Find(Key))
	{
		for (const TWeakObjectPtr<UInstancedStaticMeshComponent>& Part : *Old)
			if (UInstancedStaticMeshComponent* Ism = Part.Get())
			{
				PlantsShown -= Ism->GetInstanceCount();
				// ([perf, build 150] kept to be used again, as the parts are: torque.RidePool)
				if (TRWI::CVarPool.GetValueOnGameThread() != 0 && SparePlants.Num() < 2000 && !Ism->IsBeingDestroyed())
				{
					Ism->ClearInstances();
					Ism->ComponentTags.Reset();
					Ism->EmptyOverrideMaterials();
					if (Ism->IsRegistered()) Ism->UnregisterComponent();
					SparePlants.Add(Ism);
				}
				else
					Ism->DestroyComponent();
			}
		TilePlants.Remove(Key);
	}
}

void ATorqueRideWorld::PlacePlants(const FIntVector& Key, const TorqueWorld::RideMesh& Mesh)
{
	if (Mesh.Plants.empty() || Kit.Num() == 0) return;
	// Instances by the kit's species, its variant, and (for the grass with a near limit, which Unreal
	// applies to a whole component) a 32 m patch of the square.
	TMap<FIntVector, TArray<FTransform>> Groups;
	uint32 Counter = 0;
	for (const TorqueWorld::RidePlant& P : Mesh.Plants)
	{
		++Counter;
		if (P.Kind >= TorqueWorld::RidePlan::PlantKinds) continue;
		const bool bTree = P.Kind >= TorqueWorld::RidePlan::FirstTree;
		const TArray<int32>& Choice = bTree && P.Far ? KindFar[P.Kind] : KindNear[P.Kind];
		if (Choice.Num() == 0) continue;
		// (the grass's kinds have their models as variants in one species; the trees' as species of their own)
		const int32 Si = Choice[FMath::Min(int32(P.Pick * Choice.Num()), Choice.Num() - 1)];
		const FTorqueFoliageSpecies& S = Kit[Si];
		const uint32 H = (uint32(FMath::FloorToInt(P.X * 37.0f)) * 73856093u) ^ (uint32(FMath::FloorToInt(P.Z * 37.0f)) * 19349663u) ^ (Counter * 83492791u);
		const float Roll = float((H * 2654435761u) >> 8) / float(1 << 24);
		if (!bTree && S.Keep < 1.0f && Roll >= S.Keep) continue;   // (bigger scanned plants: fewer of them)
		const int32 Vi = S.Variants.Num() > 1 ? FMath::Min(int32(float((H ^ 0x9e3779b9u) * 2246822519u >> 8) / float(1 << 24) * S.Variants.Num()), S.Variants.Num() - 1) : 0;
		UStaticMesh* Model = S.Variants.Num() ? S.Variants[Vi].Get() : S.Mesh.Get();
		if (!Model) continue;
		const FVector& Frame = bTree ? TreeFrame : GrassFrame;
		double Size = double(P.Scale) * S.ScaleMultiplier;
		if (bTree && P.Height > 0.0f)
		{
			// (as tall as the canopy map says, whatever the model's own height; a hedge's length keeps its Scale)
			const double Own = Model->GetBounds().BoxExtent.Z * 2.0 * (S.bNativeMesh ? 1.0 : FMath::Abs(Frame.Z));
			Size = Own > 1.0 ? FMath::Clamp(double(P.Height) * 100.0 / Own, 0.4, 2.5) : 1.0;
		}
		FQuat Rot(FVector::UpVector, FMath::DegreesToRadians(FMath::RadiansToDegrees(double(P.Yaw)) + (S.bNativeMesh ? 0.0 : Frame.X)));
		if (P.Nx != 0.0f || P.Nz != 0.0f)
		{
			// (the ride's frame is x east, y up, z south; Unreal's x east, y south, z up)
			const FVector N(P.Nx, P.Nz, FMath::Sqrt(FMath::Max(0.0f, 1.0f - P.Nx * P.Nx - P.Nz * P.Nz)));
			Rot = FQuat::FindBetweenNormals(FVector::UpVector, N.GetSafeNormal()) * Rot;
		}
		const FVector Scale3 = S.bNativeMesh ? FVector(Size) : FVector(Size * Frame.Z, Size * Frame.Z * Frame.Y, Size * Frame.Z);
		// ([perf, build 150] patches of torque.RidePlantPatch metres, 64 by default: 32 until 149, which
		// made thousands of plant groups for the render thread to look at each frame)
		const bool bPatch = !bTree && S.MinDrawDistance > 0.0f;
		const float PatchM = FMath::Max(8.0f, TRWI::CVarPlantPatch.GetValueOnGameThread());
		const int32 Patch = bPatch ? FMath::Clamp(int32(P.X / PatchM), 0, 63) + 64 * FMath::Clamp(int32(P.Z / PatchM), 0, 63) : -1;
		Groups.FindOrAdd(FIntVector(Si, Vi, Patch)).Add(FTransform(Rot, FVector(double(P.X) * 100.0, double(P.Z) * 100.0, double(P.Y) * 100.0), Scale3));
	}
	TArray<TWeakObjectPtr<UInstancedStaticMeshComponent>>& Parts = TilePlants.FindOrAdd(Key);
	for (TPair<FIntVector, TArray<FTransform>>& Group : Groups)
	{
		const FTorqueFoliageSpecies& S = Kit[Group.Key.X];
		const int32 Vi = Group.Key.Y;
		UStaticMesh* Model = S.Variants.Num() ? S.Variants[Vi].Get() : S.Mesh.Get();
		UMaterialInterface* Mat = S.Variants.Num() ? (S.VariantMaterials.IsValidIndex(Vi) ? S.VariantMaterials[Vi].Get() : nullptr) : S.Material.Get();
		const bool bTree = Group.Key.X >= KitTrees;
		const float PlantReach = PresetReach(bTree);
		if (TRWI::Unseen(Key, PlantReach * S.CullDistance * (bTree ? 1.0f : CullScale * 0.5f * (1.0f + FMath::Max(1.0f, S.CullSpread))))) continue;
		// [perf, build 148] -TorqueRideTrees=0: no trees, to measure what they cost (A/B in one build).
		static const bool bNoTrees = []() { int32 On = 1; return FParse::Value(FCommandLine::Get(), TEXT("TorqueRideTrees="), On) && On == 0; }();
		if (bTree && bNoTrees) continue;
		// As ATorqueFoliage makes its parts (see TorqueFoliage.cpp): no collision, shadows only from the
		// taller plants and cached while they sway, out of Lumen's and the ray tracer's scenes.
		UInstancedStaticMeshComponent* Ism = nullptr;
		while (!Ism && SparePlants.Num() > 0)
		{
			UInstancedStaticMeshComponent* Kept = SparePlants.Pop(EAllowShrinking::No);
			if (IsValid(Kept) && !Kept->IsBeingDestroyed()) Ism = Kept;
		}
		const bool bReused = Ism != nullptr;
		if (!Ism) Ism = NewObject<UInstancedStaticMeshComponent>(this, NAME_None, RF_Transient);
		Ism->SetMobility(EComponentMobility::Movable);
		if (!bReused) Ism->SetupAttachment(RootComponent);
		Ism->SetVisibility(true);
		Ism->SetRelativeLocation(FVector(Mesh.OriginX * 100.0, Mesh.OriginZ * 100.0, 0.0));
		Ism->SetStaticMesh(Model);
		if (Mat) Ism->SetMaterial(0, Mat);
		Ism->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Ism->SetCollisionProfileName(TEXT("NoCollision"));
		Ism->SetCanEverAffectNavigation(false);
		Ism->SetGenerateOverlapEvents(false);
		Ism->SetCastShadow(S.bCastShadow);
		Ism->bCastContactShadow = S.bCastShadow && !bTree;
		Ism->ShadowCacheInvalidationBehavior = EShadowCacheInvalidationBehavior::Rigid;
		Ism->bReceivesDecals = false;
		Ism->bAffectDistanceFieldLighting = bTree;
		Ism->bAffectDynamicIndirectLighting = bTree;
		Ism->bVisibleInRayTracing = false;
		Ism->WorldPositionOffsetDisableDistance = bTree ? 8000 : 2500;
		const float Cull = PlantReach * S.CullDistance * (bTree ? 1.0f : CullScale * 0.5f * (1.0f + FMath::Max(1.0f, S.CullSpread)));
		Ism->SetCullDistances(int32(Cull * 0.85f), int32(Cull));
		Ism->LDMaxDrawDistance = Cull * 1.05f;
		Ism->MinDrawDistance = S.MinDrawDistance * CullScale;
		if (bTree) Ism->ComponentTags.Add(TEXT("RideTree"));   // (for the 5 s scene count)
		Ism->RegisterComponent();
		Ism->AddInstances(Group.Value, false, false);
		if (!bReused) AddInstanceComponent(Ism);
		Parts.Add(Ism);
		PlantsShown += Group.Value.Num();
	}
}
