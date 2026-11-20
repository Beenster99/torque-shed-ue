#include "Weather/TorqueTyreFx.h"

#include "Coop/TorqueGameState.h"
#include "Coop/TorqueSettings.h"
#include "TorqueBikePawn.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/DecalComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/CommandLine.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"

namespace TorqueTyreFxImpl
{
TAutoConsoleVariable<int32> CVarSmoke(TEXT("torque.RideTyreSmoke"), 1,
	TEXT("Tyre smoke from burnouts, wheelspin and skids (Weather/TorqueTyreFx): 1 on, 0 off."));
TAutoConsoleVariable<float> CVarSmokeTest(TEXT("torque.RideTyreSmokeTest"), 0.0f,
	TEXT("[test] For this many seconds, the rider's own back tyre smokes as in a burnout wherever the bike is (also -TorqueTyreSmokeTest=<s>, from 5 s into the ride)."));
TAutoConsoleVariable<int32> CVarMarks(TEXT("torque.RideTyreMarks"), 1,
	TEXT("Darkies, the rubber a spinning or skidding tyre lays on the road (Weather/TorqueTyreFx): 1 on, 0 off."));

constexpr int32 DataFloats = 6;            // per sprite: colour (3), alpha, softness (2 the veil's flat fill), seed
const FLinearColor SmokeColour(0.80f, 0.80f, 0.82f);
constexpr double NearBikesCm = 40000.0;    // smoke and darkies only from bikes within 400 m of the camera
constexpr double FarMarkCm = 150000.0;     // darkies left 1.5 km behind go
constexpr double MarkLifeS = 600.0;        // and any after ten minutes (fading away)
constexpr double MarkFadeS = 4.0;
constexpr double MarkDepthCm = 6.0;        // half its box's depth (from 4 cm above the road to 8 cm in)
constexpr double SegmentCm = 120.0;        // a darkie's strip is laid in pieces this long at most
const FTransform Hidden(FQuat::Identity, FVector(0.0, 0.0, -100000.0), FVector(1e-4));
constexpr int32 MaxFlames = 12;            // flames at once (all the bikes)
constexpr int32 FlameBits = 4;             // sprites in each, from the pipe out
TAutoConsoleVariable<int32> CVarFlames(TEXT("torque.RideExhaustFlames"), 1,
	TEXT("Flames out of a sports engine's pipe with its pops and bangs (Weather/TorqueTyreFx): 1 on, 0 off."));

// Settings > Graphics > Effects (or the quality preset's): how many puffs, how many darkies.
int32 Quality()
{
	const FTorqueSettings& S = FTorqueSettings::Get();
	return FMath::Clamp(S.Effects >= 0 ? S.Effects : S.Quality, 0, 4);
}
int32 PuffCap()
{
	static const int32 Cap[5] = {90, 140, 200, 260, 300};
	return Cap[Quality()];
}
int32 MarkCap()
{
	static const int32 Cap[5] = {60, 100, 140, 180, 220};
	return Cap[Quality()];
}
} // namespace TorqueTyreFxImpl

namespace TTF = TorqueTyreFxImpl;

ATorqueTyreFx::ATorqueTyreFx()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;   // after the bikes and the camera have moved
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	SetReplicates(false);
}

void ATorqueTyreFx::BeginPlay()
{
	Super::BeginPlay();
	Make();
}

void ATorqueTyreFx::Make()
{
	bMade = true;
	UWorld* World = GetWorld();
	auto Usable = [World](const TCHAR* Package, const TCHAR* Object) -> UMaterialInterface* {
		if (!FPackageName::DoesPackageExist(Package)) return nullptr;
		UMaterialInterface* M = LoadObject<UMaterialInterface>(nullptr, Object);
		if (M && World)
			if (UMaterial* Base = M->GetMaterial(); Base && Base->IsCompilingOrHadCompileError(World->GetFeatureLevel())) return nullptr;
		return M;
	};
	// The smoke: its own soft billows (Tools/ue_build_spray.py), or the rain's soft puff before it's built.
	UMaterialInterface* SmokeMat = Usable(TEXT("/Game/Shed/Materials/M_TyreSmoke"), TEXT("/Game/Shed/Materials/M_TyreSmoke.M_TyreSmoke"));
	bOwnSmoke = SmokeMat != nullptr;
	if (!SmokeMat) SmokeMat = Usable(TEXT("/Game/Shed/Materials/M_RainSprite"), TEXT("/Game/Shed/Materials/M_RainSprite.M_RainSprite"));
	if (!SmokeMat) SmokeMat = Usable(TEXT("/Game/Shed/Materials/M_SprayMist"), TEXT("/Game/Shed/Materials/M_SprayMist.M_SprayMist"));
	MarkMaterial = Usable(TEXT("/Game/Shed/Materials/M_TyreMark"), TEXT("/Game/Shed/Materials/M_TyreMark.M_TyreMark"));
	UE_LOG(LogTemp, Log, TEXT("Torque tyre fx: smoke drawn with %s, darkies with %s (run Tools/ue_build_shed.py for M_TyreSmoke and M_TyreMark)"),
		SmokeMat ? *SmokeMat->GetName() : TEXT("nothing"), MarkMaterial ? *MarkMaterial->GetName() : TEXT("nothing: none laid"));

	UStaticMesh* Plane = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
	if (Plane && SmokeMat)
	{
		PuffPool = 1 + TTF::PuffCap();   // (slot 0 the veil)
		UInstancedStaticMeshComponent* C = NewObject<UInstancedStaticMeshComponent>(this, TEXT("TyreSmoke"));
		C->SetStaticMesh(Plane);
		C->SetMaterial(0, SmokeMat);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetGenerateOverlapEvents(false);
		C->SetCastShadow(false);
		C->SetCanEverAffectNavigation(false);
		C->SetUsingAbsoluteLocation(true);
		C->SetUsingAbsoluteRotation(true);
		C->SetUsingAbsoluteScale(true);
		C->SetNumCustomDataFloats(TTF::DataFloats);
		C->SetupAttachment(GetRootComponent());
		C->RegisterComponent();
		C->SetWorldTransform(FTransform::Identity);
		C->SetBoundsScale(1000.0f);   // (never culled as a whole: its instances move about)
		TArray<FTransform> All;
		All.Init(TTF::Hidden, PuffPool);
		C->AddInstances(All, false, true);
		C->SetVisibility(false);
		Smoke = C;
	}
	MarkPool = MarkMaterial ? TTF::MarkCap() : 0;

	// The flames: their own glowing tongue (Tools/ue_build_spray.py); none drawn before it's built.
	UMaterialInterface* FlameMat = Usable(TEXT("/Game/Shed/Materials/M_ExhaustFlame"), TEXT("/Game/Shed/Materials/M_ExhaustFlame.M_ExhaustFlame"));
	UE_LOG(LogTemp, Log, TEXT("Torque tyre fx: exhaust flames %s"), FlameMat ? TEXT("drawn with M_ExhaustFlame") : TEXT("off (no M_ExhaustFlame: run Tools/ue_build_shed.py)"));
	if (Plane && FlameMat)
	{
		UInstancedStaticMeshComponent* C = NewObject<UInstancedStaticMeshComponent>(this, TEXT("ExhaustFlames"));
		C->SetStaticMesh(Plane);
		C->SetMaterial(0, FlameMat);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetGenerateOverlapEvents(false);
		C->SetCastShadow(false);
		C->SetCanEverAffectNavigation(false);
		C->SetUsingAbsoluteLocation(true);
		C->SetUsingAbsoluteRotation(true);
		C->SetUsingAbsoluteScale(true);
		C->SetNumCustomDataFloats(TTF::DataFloats);
		C->SetupAttachment(GetRootComponent());
		C->RegisterComponent();
		C->SetWorldTransform(FTransform::Identity);
		C->SetBoundsScale(1000.0f);
		TArray<FTransform> All;
		All.Init(TTF::Hidden, TTF::MaxFlames * TTF::FlameBits);
		C->AddInstances(All, false, true);
		C->SetVisibility(false);
		FlameSprites = C;
		// One quick flash for the brightest flame now (no shadows: a tenth of a second of orange light).
		UPointLightComponent* L = NewObject<UPointLightComponent>(this, TEXT("ExhaustFlash"));
		L->SetUsingAbsoluteLocation(true);
		L->SetupAttachment(GetRootComponent());
		L->SetCastShadows(false);
		L->SetAttenuationRadius(450.0f);
		L->SetLightColor(FLinearColor(1.0f, 0.55f, 0.2f));
		L->SetIntensityUnits(ELightUnits::Candelas);
		L->SetIntensity(0.0f);
		L->SetVisibility(false);
		L->RegisterComponent();
		FlameLight = L;
	}
}

int32 ATorqueTyreFx::MarksShown() const
{
	int32 N = 0;
	for (const FMark& M : Marks) N += M.bLive ? 1 : 0;
	return N;
}

void ATorqueTyreFx::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UWorld* World = GetWorld();
	const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	if (!bMade || !PC || !PC->PlayerCameraManager) return;
	const float Dt = FMath::Min(DeltaSeconds, 0.1f);
	Eye = PC->PlayerCameraManager->GetCameraLocation();
	EyeRot = PC->PlayerCameraManager->GetCameraRotation().Quaternion();
	EyeFwd = EyeRot.GetForwardVector();

	// The weather: how wet the road is (a wet tyre barely smokes or marks) and the wind the smoke drifts on.
	double Wet = 0.0;
	FVector Wind = FVector::ZeroVector;
	if (const ATorqueGameState* GS = World->GetGameState<ATorqueGameState>())
	{
		const TorqueWeather::State& W = GS->GetWeatherState();
		Wet = FMath::Clamp(W.RoadWet, 0.0, 1.0);
		const double To = FMath::DegreesToRadians(W.WindFrom + 180.0);
		// (x east, y south, cm/s; near the ground about half the wind at 10 m)
		Wind = FVector(FMath::Sin(To) * W.Wind * 100.0 * 0.5, -FMath::Cos(To) * W.Wind * 100.0 * 0.5, 0.0);
	}
	TickBikes(Dt, Wet, Wind);
	TickMarks(Dt);
	SimulatePuffs(Dt, Wind);
	DrawPuffs();
	TickFlames(Dt);

	// [r158] The log says how much smoke there's been, not only what's in the air at that moment.
	PeakPuffs = FMath::Max(PeakPuffs, Puffs.Num());
	if ((LogClock += Dt) > 30.0 && (Puffs.Num() > 0 || MarksShown() > 0 || Emitted > EmittedLogged))
	{
		LogClock = 0.0;
		UE_LOG(LogTemp, Log, TEXT("Torque tyre fx: %d smoke puffs now (of %d; %d made so far, most at once %d, hottest tyre %.2f), %d darkies (of %d; %d laid so far), smoke round the camera %.2f"),
			Puffs.Num(), FMath::Max(PuffPool - 1, 0), Emitted, PeakPuffs, HottestTyre, MarksShown(), MarkPool, MarksLaid, Veil);
		EmittedLogged = Emitted;
		HottestTyre = 0.0;
	}
}

void ATorqueTyreFx::TickBikes(float Dt, double Wet, const FVector& Wind)
{
	const bool bSmoke = TTF::CVarSmoke.GetValueOnGameThread() != 0 && Smoke != nullptr;
	const bool bMarks = TTF::CVarMarks.GetValueOnGameThread() != 0 && MarkPool > 0;
	// [r158 test] A forced burnout on our own back tyre, to see the smoke without riding one.
	static double TestFrom = -1.0;
	static bool bTestArgRead = false;
	if (!bTestArgRead)
	{
		bTestArgRead = true;
		float Secs = 0.0f;
		if (FParse::Value(FCommandLine::Get(), TEXT("TorqueTyreSmokeTest="), Secs) && Secs > 0.0f) TTF::CVarSmokeTest->Set(Secs);
	}
	const float TestSecs = TTF::CVarSmokeTest.GetValueOnGameThread();
	const double Now = GetWorld()->GetTimeSeconds();
	if (TestSecs > 0.0f && TestFrom < 0.0) TestFrom = Now + 5.0;
	if (TestSecs <= 0.0f) { TestFrom = -1.0; bTestMarksLaid = false; }
	const bool bTestNow = TestSecs > 0.0f && Now >= TestFrom && Now < TestFrom + TestSecs;
	if (TestSecs > 0.0f && Now >= TestFrom + TestSecs)
	{
		UE_LOG(LogTemp, Log, TEXT("Torque tyre fx: smoke test done: %d puffs made, most at once %d, smoke round the camera %.2f"), Emitted, PeakPuffs, Veil);
		TTF::CVarSmokeTest->Set(0.0f);
	}
	for (TActorIterator<ATorqueBikePawn> It(GetWorld()); It; ++It)
	{
		ATorqueBikePawn* Bike = *It;
		if (!Bike) continue;
		FBikeTyres& Tyres = Bikes.FindOrAdd(TWeakObjectPtr<AActor>(Bike));
		const bool bNear = FVector::DistSquared(Bike->GetActorLocation(), Eye) < FMath::Square(TTF::NearBikesCm);
		for (int32 I = 0; I < 2; ++I)
		{
			FTyreState& St = Tyres.T[I];
			FTorqueTyreScrub Got;
			FScrub In;
			if (bNear && Bike->TyreScrub(I, Got))
			{
				In.SlideMs = Got.SlideMs;
				In.RoadMs = Got.RoadMs;
				In.HalfWidthCm = Got.HalfWidthCm;
				In.RadiusCm = Got.RadiusCm;
				In.SpinMs = Got.SpinMs;
				In.bTarmac = Got.bTarmac;
				In.At = Got.At;
				In.Fwd = Got.Fwd;
				In.Up = Got.Up;
			}
			else In.bTarmac = false;   // (in the air, down, parked, far off: nothing laid, the tyre cools)
			if (bTestNow && I == 1 && Bike->IsLocallyControlled())
			{
				In.SlideMs = 20.0;
				In.RoadMs = 0.0;
				In.SpinMs = 20.0;
				In.bTarmac = true;
				if (Bike->TyreScrub(I, Got))
				{
					In.At = Got.At;
					In.Fwd = Got.Fwd;
					In.Up = Got.Up;
					In.HalfWidthCm = Got.HalfWidthCm;
					In.RadiusCm = Got.RadiusCm;
				}
				else
				{
					In.Fwd = Bike->GetActorForwardVector().GetSafeNormal2D(UE_SMALL_NUMBER, FVector::ForwardVector);
					In.Up = FVector::UpVector;
					In.At = Bike->GetActorLocation() - In.Fwd * 70.0 - In.Up * 30.0;
				}
			}
			// ([r161] "marks barely draw": the forced burnout sits on one spot, its one darkie under the
			// tyre. The test lays a strip 4 m behind the back tyre and a burnout's circle beside it too, to
			// see them by.)
			if (bTestNow && I == 1 && Bike->IsLocallyControlled() && !bTestMarksLaid && bMarks)
			{
				bTestMarksLaid = true;
				const int32 Before = MarksLaid;
				FTyreState Test;
				FScrub S = In;
				S.SlideMs = 20.0;
				const FVector Side = FVector::CrossProduct(In.Up, In.Fwd).GetSafeNormal();
				for (int32 K = 0; K <= 40; ++K)
				{
					// ([r162] 1.5 m to the side, from beside the bike back: not under the burnout's bank)
					S.At = In.At - Side * 150.0 + In.Fwd * (100.0 - K * 10.0);
					LayMark(Test, S, 0.1f, 0.0, Bike);
				}
				CloseMark(Test);
				const FVector Centre = In.At + Side * 250.0 + In.Fwd * 150.0;
				for (int32 K = 0; K <= 120; ++K)
				{
					const double A = FMath::DegreesToRadians(K * 3.0);
					S.At = Centre + (In.Fwd * FMath::Cos(A) + Side * FMath::Sin(A)) * 145.0;
					S.Fwd = (Side * FMath::Cos(A) - In.Fwd * FMath::Sin(A)).GetSafeNormal();
					LayMark(Test, S, 0.1f, 0.0, Bike);
				}
				CloseMark(Test);
				UE_LOG(LogTemp, Log, TEXT("Torque tyre fx: smoke test laid %d darkies: a 4 m strip 1.5 m to one side of the bike and a 1.45 m circle 2.5 m to the other, a bit ahead"), MarksLaid - Before);
				// (where the first few went and what they sit on, to check by)
				for (int32 K = 0; K < FMath::Min(Marks.Num(), 4); ++K)
				{
					const FMark& M = Marks[K];
					FHitResult Under;
					FCollisionQueryParams Q(SCENE_QUERY_STAT(TorqueTyreMarkLog), true, Bike);
					const bool bOn = GetWorld()->LineTraceSingleByChannel(Under, M.To + M.Up * 25.0, M.To - M.Up * 40.0, ECC_Visibility, Q);
					UE_LOG(LogTemp, Log, TEXT("Torque tyre fx: darkie %d from %s to %s, up %s, %.0f cm wide, opacity %.2f, visible %d, on %s %.1f cm below its end"),
						K, *M.From.ToCompactString(), *M.To.ToCompactString(), *M.Up.ToCompactString(), M.HalfWidth * 2.0, M.Opacity * M.Fade,
						M.Decal && M.Decal->IsVisible() ? 1 : 0, bOn && Under.GetActor() ? *Under.GetActor()->GetName() : TEXT("nothing"), bOn ? (M.To - Under.ImpactPoint).Size() : -1.0);
				}
			}
			// The tyre heats as it slides (half a second sliding at 12 m/s, hot) and cools when it stops.
			// ([r158] a hard skid or a spin pulling away smokes too now, not only a long burnout: it was
			// a second at 10 m/s before the first wisp, so the robot's slides laid darkies but no smoke)
			const double Sliding = In.bTarmac ? In.SlideMs : 0.0;
			St.Heat = FMath::Clamp(St.Heat + (Sliding > 1.0 ? Sliding / 6.0 : -0.5) * Dt, 0.0, 1.0);
			HottestTyre = FMath::Max(HottestTyre, St.Heat);
			if (bSmoke && Sliding > 1.0) EmitSmoke(St, In, Dt, Wet, Wind);
			else St.Debt = 0.0;
			if (bMarks && Sliding > 1.2) LayMark(St, In, Dt, Wet, Bike);
			else CloseMark(St);
		}
	}
	for (auto It = Bikes.CreateIterator(); It; ++It)
		if (!It.Key().IsValid())
		{
			for (FTyreState& St : It.Value().T) CloseMark(St);
			It.RemoveCurrent();
		}
}

void ATorqueTyreFx::EmitSmoke(FTyreState& St, const FScrub& In, float Dt, double Wet, const FVector& Wind)
{
	// Hot rubber smokes: from a little once it's warm to a torrent once it's hot, the more the faster it
	// slides (a burnout's whole rim speed pours it out); a wet road keeps the tyre cool.
	const double Hot = FMath::SmoothStep(0.03, 0.5, St.Heat);
	// (A spinning tyre's half again: the ball round the wheel is short-lived and doesn't thin the bank.)
	const double Rate = 45.0 * Hot * FMath::Clamp(In.SlideMs / 12.0, 0.0, 1.6) * (1.0 - 0.85 * Wet) * (In.SpinMs - In.RoadMs > 3.0 ? 1.6 : 1.0);
	St.Debt = FMath::Min(St.Debt + Rate * Dt, 6.0);
	if (PuffPool <= 1) return;
	const FVector Side = FVector::CrossProduct(In.Up, In.Fwd).GetSafeNormal();
	// ([beenster, build 160] "the burnout should gather smoke around the spinning wheel and then flare out
	// from the back (like the water spray)".) A tyre spinning faster than the road (a burnout, wheelspin
	// pulling away): the smoke is carried round with the tread, a churning ball wrapped round the back of
	// the wheel, and flung off the back of it in a fan that widens and rolls out into the bank behind. A
	// locked or sideways slide's smoke is left hanging where it was made.
	const double Tread = In.SpinMs - In.RoadMs;   // m/s the tread runs back over the road
	const bool bSpin = Tread > 3.0;
	const double R = FMath::Max(In.RadiusCm, 20.0);
	const FVector Axle = In.At + In.Up * R;
	while (St.Debt >= 1.0)
	{
		St.Debt -= 1.0;
		FPuff P;
		P.Seed = FMath::FRand();
		const double Across = FMath::FRandRange(-1.0, 1.0) * In.HalfWidthCm;
		if (bSpin && FMath::FRand() < 0.45f)
		{
			// Round the wheel: from the contact patch, round the back and over the top (Theta from the
			// bottom), moving with the tread (back at the bottom, up the back, forward over the top) and
			// held close to it by the air the tyre drags round; a metre or two across, gone in a second or two.
			const double Theta = FMath::DegreesToRadians(FMath::FRandRange(0.0, 200.0));
			const double Out = R + FMath::FRandRange(2.0, 14.0);
			P.Pos = Axle - In.Up * (Out * FMath::Cos(Theta)) - In.Fwd * (Out * FMath::Sin(Theta)) + Side * Across * 1.4;
			const FVector Round = -In.Fwd * FMath::Cos(Theta) + In.Up * FMath::Sin(Theta);
			P.Vel = Round * (Tread * 100.0 * FMath::FRandRange(0.08, 0.16)) + Side * FMath::FRandRange(-40.0, 40.0);
			P.Life = FMath::FRandRange(1.2f, 2.5f);
			P.Size0 = FMath::FRandRange(25.0f, 40.0f);
			P.Size1 = FMath::FRandRange(120.0f, 220.0f);
			P.Alpha = FMath::FRandRange(0.40f, 0.60f) * (float)FMath::Lerp(0.6, 1.0, Hot);
			P.Drag = 3.5f;
		}
		else if (bSpin)
		{
			// Flung off the back: where the tread leaves the road and comes up round the back of the tyre,
			// thrown back (a fraction of the tread's speed: the air slows it fast) and a little up, in a fan
			// that spreads to the sides; it slows and swells into the bank behind.
			const double Theta = FMath::DegreesToRadians(FMath::FRandRange(10.0, 70.0));
			P.Pos = Axle - In.Up * (R * FMath::Cos(Theta)) - In.Fwd * (R * FMath::Sin(Theta)) + Side * Across;
			const double Fling = FMath::Min(Tread, 30.0) * 100.0 * FMath::FRandRange(0.14, 0.32);
			P.Vel = -In.Fwd * Fling + In.Up * (Fling * FMath::FRandRange(0.0, 0.22)) + Side * (Fling * FMath::FRandRange(-0.38, 0.38)) + Wind * 0.3;
			P.Life = FMath::FRandRange(9.0f, 15.0f);
			P.Size0 = FMath::FRandRange(30.0f, 50.0f);
			P.Size1 = FMath::FRandRange(400.0f, 800.0f);
			P.Alpha = FMath::FRandRange(0.30f, 0.50f) * (float)FMath::Lerp(0.6, 1.0, Hot);
			P.Drag = 1.4f;
		}
		else
		{
			// From the back of the contact patch, across the tread, left hanging where the slide made it.
			P.Pos = In.At + In.Up * FMath::FRandRange(4.0, 14.0) - In.Fwd * FMath::FRandRange(5.0, 30.0) + Side * Across;
			P.Vel = -In.Fwd * FMath::FRandRange(20.0, 90.0) + In.Up * FMath::FRandRange(30.0, 120.0) + Side * FMath::FRandRange(-120.0, 120.0) + Wind * 0.3;
			P.Life = FMath::FRandRange(9.0f, 15.0f);
			P.Size0 = FMath::FRandRange(40.0f, 70.0f);
			P.Size1 = FMath::FRandRange(400.0f, 800.0f);
			P.Alpha = FMath::FRandRange(0.30f, 0.50f) * (float)FMath::Lerp(0.6, 1.0, Hot);
			P.Drag = 0.9f;
		}
		AddPuff(P);
	}
}

void ATorqueTyreFx::AddPuff(const FPuff& P)
{
	++Emitted;
	const int32 Cap = PuffPool - 1;
	if (Puffs.Num() < Cap)
	{
		Puffs.Add(P);
		return;
	}
	// Full: the puff nearest its end makes way (the cloud by the bike stays thick; the far edge thins).
	int32 Oldest = 0;
	float Most = -1.0f;
	for (int32 K = 0; K < Puffs.Num(); ++K)
		if (Puffs[K].Age / Puffs[K].Life > Most) { Most = Puffs[K].Age / Puffs[K].Life; Oldest = K; }
	Puffs[Oldest] = P;
}

void ATorqueTyreFx::LayMark(FTyreState& St, const FScrub& In, float Dt, double Wet, AActor* Bike)
{
	// Where the tread meets the road, and the road's way up there.
	FVector At = In.At, Up = In.Up;
	// ([beenster, build 160] "the line seems to hover above the surface": the road's drawn surface, not its
	// simpler collision, and nothing riding on the bike in the way.)
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(TorqueTyreMark), true, Bike);
	if (Bike)
	{
		TArray<AActor*> Riding;
		Bike->GetAttachedActors(Riding, true, true);
		Params.AddIgnoredActors(Riding);
	}
	if (GetWorld()->LineTraceSingleByChannel(Hit, In.At + In.Up * 25.0, In.At - In.Up * 40.0, ECC_Visibility, Params))
	{
		At = Hit.ImpactPoint;
		Up = Hit.ImpactNormal;
	}
	FMark* M = St.Mark != INDEX_NONE && Marks.IsValidIndex(St.Mark) && Marks[St.Mark].bOpen ? &Marks[St.Mark] : nullptr;
	if (M)
	{
		// Too long, or the slide's swung round: the strip goes on as a new piece from where this one ends.
		// ([beenster, build 160] a burnout's circle "feels octagonal not round": it took 20 deg of the
		// chord, some 40 deg of the circle, a piece. Now a piece every 10 deg or so of a curve.)
		const FVector Seg = At - M->From;
		const double Len = Seg.Size();
		// ([r162] and the piece's way along is now set once, from its first 8 cm: it was re-aimed along the
		// chord every frame, so a curve never looked "turned" and only the 120 cm limit cut it: the
		// octagon was still there.)
		const bool bTurned = M->bAimed && Len > 8.0 && FVector::DotProduct(Seg / Len, M->Along) < 0.996;
		if (Len > TTF::SegmentCm || bTurned)
		{
			const FVector Joint = M->To;
			CloseMark(St);
			M = nullptr;
			const int32 Index = TakeMark();
			if (Index == INDEX_NONE) return;
			St.Mark = Index;
			M = &Marks[Index];
			M->From = Joint;
			M->Along = (At - Joint).GetSafeNormal2D(UE_SMALL_NUMBER, In.Fwd);
		}
		else if (!M->bAimed && Len > 8.0)
		{
			M->Along = Seg / Len;
			M->bAimed = true;
		}
	}
	else
	{
		const int32 Index = TakeMark();
		if (Index == INDEX_NONE) return;
		St.Mark = Index;
		M = &Marks[Index];
		M->From = At;
		M->Along = In.Fwd;
	}
	M->To = At;
	M->Up = Up;
	M->HalfWidth = In.HalfWidthCm;
	// Rubber laid: metres of tread slid over the strip. A locked wheel's skid (as much slide as road) a
	// solid grey-black stripe; wheelspin pulling away a fainter one; a burnout's spot black in a second.
	M->Laid += In.SlideMs * Dt;
	const double LenM = FMath::Max(FVector::Dist(M->From, M->To) / 100.0, 0.25);
	M->Opacity = (float)(FMath::Clamp(0.6 * M->Laid / LenM, 0.0, 0.9) * (1.0 - 0.7 * Wet));
	PlaceMark(*M);
}

void ATorqueTyreFx::CloseMark(FTyreState& St)
{
	if (St.Mark != INDEX_NONE && Marks.IsValidIndex(St.Mark)) Marks[St.Mark].bOpen = false;
	St.Mark = INDEX_NONE;
}

int32 ATorqueTyreFx::TakeMark()
{
	int32 Index = INDEX_NONE;
	for (int32 I = 0; I < Marks.Num() && Index == INDEX_NONE; ++I)
		if (!Marks[I].bLive) Index = I;
	if (Index == INDEX_NONE && Marks.Num() < MarkPool)
	{
		UDecalComponent* D = NewObject<UDecalComponent>(this);
		D->SetUsingAbsoluteLocation(true);
		D->SetUsingAbsoluteRotation(true);
		D->SetUsingAbsoluteScale(true);
		D->SetupAttachment(GetRootComponent());
		D->RegisterComponent();
		UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(MarkMaterial, this);
		D->SetDecalMaterial(Mid);
		D->SetFadeScreenSize(0.0008f);
		D->SetSortOrder(1);
		D->SetVisibility(false);
		Keep.Add(D);
		Keep.Add(Mid);
		FMark& M = Marks.AddDefaulted_GetRef();
		M.Decal = D;
		M.Mid = Mid;
		Index = Marks.Num() - 1;
	}
	if (Index == INDEX_NONE)
	{
		// The pool's full: the oldest strip no tyre is laying makes way.
		double Oldest = TNumericLimits<double>::Max();
		for (int32 I = 0; I < Marks.Num(); ++I)
			if (!Marks[I].bOpen && Marks[I].Born < Oldest) { Oldest = Marks[I].Born; Index = I; }
		if (Index == INDEX_NONE) return INDEX_NONE;
	}
	FMark& M = Marks[Index];
	UDecalComponent* D = M.Decal;
	UMaterialInstanceDynamic* Mid = M.Mid;
	M = FMark();
	M.Decal = D;
	M.Mid = Mid;
	M.bLive = M.bOpen = true;
	M.Born = GetWorld()->GetTimeSeconds();
	++MarksLaid;
	return Index;
}

void ATorqueTyreFx::PlaceMark(FMark& M)
{
	if (!M.Decal) return;
	// (Its box reaches just 4 cm above the road and well into it: the tyre resting in it, the rider's
	// boot, a kerb's face aren't painted.)
	const FVector Centre = (M.From + M.To) * 0.5 - M.Up * (TTF::MarkDepthCm - 4.0);
	const double Overlap = M.HalfWidth * 0.6;   // past each end, under the next piece's
	const double HalfLen = FMath::Max(FVector::Dist(M.From, M.To) * 0.5 + Overlap, 12.0);
	// The decal projects along its X (down into the road); its Y across the tread, its Z along the strip.
	const FQuat Q = FRotationMatrix::MakeFromXZ(-M.Up, M.Along).ToQuat();
	M.Decal->SetWorldLocationAndRotation(Centre, Q);
	M.Decal->DecalSize = FVector(TTF::MarkDepthCm, M.HalfWidth, HalfLen);
	M.Decal->MarkRenderStateDirty();
	if (!M.Decal->IsVisible()) M.Decal->SetVisibility(true);
	const float Show = M.Opacity * M.Fade;
	if (FMath::Abs(Show - M.Shown) > 0.01f && M.Mid)
	{
		M.Mid->SetScalarParameterValue(TEXT("Opacity"), Show);
		M.Shown = Show;
	}
	// (The ends soften only over the overlap, so the pieces join into one even strip, not beads.)
	if (M.Mid)
	{
		const float Ends = (float)FMath::Clamp(Overlap * 2.0 / HalfLen, 0.05, 0.9);
		if (FMath::Abs(Ends - M.EndsShown) > 0.02f)
		{
			M.Mid->SetScalarParameterValue(TEXT("EndFade"), Ends);
			M.EndsShown = Ends;
		}
	}
}

void ATorqueTyreFx::TickMarks(float Dt)
{
	if (Marks.Num() == 0) return;
	const double Now = GetWorld()->GetTimeSeconds();
	int32 Live = 0, Fading = 0;
	for (const FMark& M : Marks)
	{
		Live += M.bLive ? 1 : 0;
		Fading += M.bLive && M.bFading ? 1 : 0;
	}
	// Room kept for new ones: past most of the pool, the oldest start to fade away.
	int32 ToFade = Live - Fading - FMath::Max(MarkPool * 85 / 100, 1);
	while (ToFade-- > 0)
	{
		int32 Oldest = INDEX_NONE;
		double When = TNumericLimits<double>::Max();
		for (int32 I = 0; I < Marks.Num(); ++I)
			if (Marks[I].bLive && !Marks[I].bOpen && !Marks[I].bFading && Marks[I].Born < When) { When = Marks[I].Born; Oldest = I; }
		if (Oldest == INDEX_NONE) break;
		Marks[Oldest].bFading = true;
	}
	for (FMark& M : Marks)
	{
		if (!M.bLive || M.bOpen) continue;
		if (FVector::DistSquared(M.From, Eye) > FMath::Square(TTF::FarMarkCm)) M.Fade = 0.0f;   // (left far behind: just gone)
		else if (Now - M.Born > TTF::MarkLifeS) M.bFading = true;
		if (M.bFading) M.Fade = FMath::Max(M.Fade - Dt / (float)TTF::MarkFadeS, 0.0f);
		if (M.Fade <= 0.0f)
		{
			M.bLive = false;
			if (M.Decal) M.Decal->SetVisibility(false);
			continue;
		}
		if (M.bFading) PlaceMark(M);
	}
}

void ATorqueTyreFx::TickFlames(float Dt)
{
	if (!FlameSprites) return;
	const bool bOn = TTF::CVarFlames.GetValueOnGameThread() != 0;
	// New ones: each bike's pops and bangs due now (taken even when off, so none pile up).
	const double Now = GetWorld()->GetTimeSeconds();
	TArray<bool> Bangs;
	for (TActorIterator<ATorqueBikePawn> It(GetWorld()); It; ++It)
	{
		ATorqueBikePawn* Bike = *It;
		if (!Bike || Bike->TakeExhaustFlames(Now, Bangs) == 0 || !bOn) continue;
		if (FVector::DistSquared(Bike->GetActorLocation(), Eye) > FMath::Square(TTF::NearBikesCm)) continue;
		for (const bool bBang : Bangs)
		{
			if (Flames.Num() >= TTF::MaxFlames) Flames.RemoveAt(0);   // (the oldest goes)
			FFlame F;
			F.Bike = Bike;
			F.bBang = bBang;
			F.Seed = FMath::FRand();
			// A pop: a short spit of flame, a hand's length, gone in a tenth of a second. A bang: a long
			// tongue, half a metre and more, a quarter of a second.
			F.Life = bBang ? FMath::FRandRange(0.18f, 0.28f) : FMath::FRandRange(0.07f, 0.13f);
			F.Len = bBang ? FMath::FRandRange(50.0f, 80.0f) : FMath::FRandRange(18.0f, 35.0f);
			Flames.Add(F);
		}
	}
	for (int32 I = Flames.Num() - 1; I >= 0; --I)
	{
		Flames[I].Age += Dt;
		if (Flames[I].Age >= Flames[I].Life || !Flames[I].Bike.IsValid()) Flames.RemoveAt(I);
	}
	if (Flames.Num() == 0)
	{
		if (FlameDrawn > 0)
		{
			FlameSprites->SetVisibility(false);
			FlameLight->SetVisibility(false);
			FlameDrawn = 0;
		}
		return;
	}
	if (!FlameSprites->IsVisible()) FlameSprites->SetVisibility(true);
	const int32 Count = TTF::MaxFlames * TTF::FlameBits;
	TArray<FTransform> Xfs;
	Xfs.Init(TTF::Hidden, Count);
	TArray<float> Data;
	Data.SetNumZeroed(TTF::DataFloats);
	// From the pipe out: a blue root, a white-yellow heart, orange, a dull red tip.
	static const FLinearColor Colour[TTF::FlameBits] = {
		FLinearColor(0.35f, 0.5f, 1.0f), FLinearColor(1.0f, 0.85f, 0.5f), FLinearColor(1.0f, 0.5f, 0.12f), FLinearColor(0.9f, 0.22f, 0.04f)};
	static const float Glow[TTF::FlameBits] = {0.5f, 1.0f, 0.8f, 0.4f};
	float Brightest = 0.0f;
	FVector FlashAt = FVector::ZeroVector;
	int32 Slot = 0;
	TArray<FVector> Tips;
	FVector Dir;
	for (const FFlame& F : Flames)
	{
		ATorqueBikePawn* Bike = Cast<ATorqueBikePawn>(F.Bike.Get());
		if (!Bike || !Bike->ExhaustTips(Tips, Dir)) continue;
		const float T = F.Age / FMath::Max(F.Life, 0.01f);
		// Out in a flash, then shrinking back into the pipe; flickering.
		const float Reach = FMath::Sin(FMath::Min(T * 2.2f, 1.0f) * UE_HALF_PI) * (1.0f - 0.5f * T);
		const float Flick = 0.75f + 0.25f * FMath::Sin((F.Age * 90.0f) + F.Seed * 20.0f);
		const float Bright = (1.0f - FMath::SmoothStep(0.5f, 1.0f, T)) * Flick;
		const float Wide = F.bBang ? 1.6f : 1.0f;
		for (const FVector& Tip : Tips)
		{
			const FVector Mid = Tip + Dir * (F.Len * Reach * 0.5);
			if (Bright * (F.bBang ? 2.0f : 1.0f) > Brightest)
			{
				Brightest = Bright * (F.bBang ? 2.0f : 1.0f);
				FlashAt = Mid;
			}
			for (int32 K = 0; K < TTF::FlameBits && Slot < Count; ++K, ++Slot)
			{
				const float Along = (K + 0.5f) / TTF::FlameBits;
				const FVector At = Tip + Dir * (F.Len * Reach * Along);
				const float Long = F.Len * Reach * 0.55f + 4.0f;          // each sprite overlaps the next
				const float Thick = (5.0f + 7.0f * Along) * Wide;         // widening as it leaves the pipe
				const FVector ToEye = (Eye - At).GetSafeNormal();
				FVector Across = FVector::CrossProduct(ToEye, Dir);
				if (Across.SizeSquared() < 1e-4) Across = FVector::CrossProduct(ToEye, FVector::UpVector);
				const FVector Lengthwise = FVector::CrossProduct(Across.GetSafeNormal(), ToEye);
				const FMatrix M = FRotationMatrix::MakeFromZX(ToEye, Lengthwise);
				Xfs[Slot] = FTransform(M.ToQuat(), At, FVector(Long / 100.0f, Thick / 100.0f, 1.0f));
				Data[0] = Colour[K].R;
				Data[1] = Colour[K].G;
				Data[2] = Colour[K].B;
				Data[3] = Bright * Glow[K] * (F.bBang ? 1.4f : 1.0f);
				Data[4] = 1.0f;
				Data[5] = F.Seed + K * 0.37f;
				FlameSprites->SetCustomData(Slot, Data, false);
			}
		}
	}
	FlameSprites->BatchUpdateInstancesTransforms(0, Xfs, true, false, true);
	FlameSprites->MarkRenderStateDirty();
	FlameDrawn = Slot;
	if (Brightest > 0.01f)
	{
		FlameLight->SetWorldLocation(FlashAt);
		FlameLight->SetIntensity(60.0f * Brightest);
		if (!FlameLight->IsVisible()) FlameLight->SetVisibility(true);
	}
	else if (FlameLight->IsVisible()) FlameLight->SetVisibility(false);
}

void ATorqueTyreFx::SimulatePuffs(float Dt, const FVector& Wind)
{
	// Smoke drifts with the air and rises a little (it's warm); its first fling dies away quickly.
	const FVector Drift = Wind + FVector(0.0, 0.0, 15.0);
	for (int32 I = Puffs.Num() - 1; I >= 0; --I)
	{
		FPuff& P = Puffs[I];
		P.Age += Dt;
		if (P.Age >= P.Life)
		{
			Puffs.RemoveAtSwap(I);
			continue;
		}
		P.Vel = FMath::Lerp(P.Vel, Drift, 1.0f - FMath::Exp(-P.Drag * Dt));
		P.Pos += P.Vel * Dt;
	}
}

void ATorqueTyreFx::DrawPuffs()
{
	if (!Smoke) return;
	// How thick the smoke is right round the camera: each puff the camera is inside counts by how deep in
	// it is. That's drawn as the veil over the view, and those puffs fade out into it.
	double Thick = 0.0;
	TArray<int32> Order;
	TArray<double> Dist2;
	TArray<float> Strength;
	Order.SetNumUninitialized(Puffs.Num());
	Dist2.SetNumUninitialized(Puffs.Num());
	Strength.SetNumUninitialized(Puffs.Num());
	for (int32 I = 0; I < Puffs.Num(); ++I)
	{
		const FPuff& P = Puffs[I];
		const float F = P.Age / FMath::Max(P.Life, 0.01f);
		const float Size = FMath::Lerp(P.Size0, P.Size1, 1.0f - FMath::Cube(1.0f - F));
		// In over a third of a second, held, out over the last of its life; thinner as it spreads.
		float A = P.Alpha * FMath::Min(P.Age / FMath::Min(0.3f, 0.15f * P.Life), 1.0f) * (1.0f - FMath::SmoothStep(0.55f, 1.0f, F)) * FMath::Lerp(1.0f, 0.55f, F);
		const double D = FVector::Dist(P.Pos, Eye);
		const double R = Size * 0.5;
		if (D < R)
		{
			const double In = 1.0 - FMath::Square(D / R);
			Thick += A * In * In;
		}
		// (The camera inside it: it fades into the veil rather than filling the lens.)
		A *= (float)FMath::SmoothStep(R * 0.35, R * 1.0, D);
		Order[I] = I;
		Dist2[I] = D * D;
		Strength[I] = A;
	}
	const float VeilWant = (float)FMath::Min(1.0 - FMath::Exp(-1.6 * Thick), 0.93);
	const float Dt = FMath::Min(GetWorld()->GetDeltaSeconds(), 0.1f);
	Veil = FMath::FInterpConstantTo(Veil, VeilWant, Dt, VeilWant > Veil ? 2.5f : 1.2f);
	if (Puffs.Num() == 0 && Veil < 0.005f)
	{
		Veil = 0.0f;
		if (Smoke->IsVisible()) Smoke->SetVisibility(false);
		DrawnSlots = 0;
		return;
	}
	if (!Smoke->IsVisible()) Smoke->SetVisibility(true);
	Order.Sort([&Dist2](int32 A, int32 B) { return Dist2[A] > Dist2[B]; });   // back to front

	const int32 Used = 1 + Puffs.Num();
	const int32 Count = FMath::Min(FMath::Max(Used, DrawnSlots), PuffPool);
	TArray<FTransform> Xfs;
	Xfs.SetNumUninitialized(Count);
	TArray<float> Data;
	Data.SetNumZeroed(TTF::DataFloats);
	Data[0] = TTF::SmokeColour.R;
	Data[1] = TTF::SmokeColour.G;
	Data[2] = TTF::SmokeColour.B;
	// Slot 0, the veil: a sheet just in front of the lens, filling the view.
	if (Veil > 0.005f)
	{
		const FMatrix VM = FRotationMatrix::MakeFromZX(-EyeFwd, EyeRot.GetRightVector());
		Xfs[0] = FTransform(VM.ToQuat(), Eye + EyeFwd * 30.0, FVector(4.0, 4.0, 1.0));
		Data[3] = Veil * (bOwnSmoke ? 1.0f : 1.2f);
		Data[4] = bOwnSmoke ? 2.0f : 1.0f;
		Data[5] = 0.5f;
	}
	else
	{
		Xfs[0] = TTF::Hidden;
		Data[3] = 0.0f;
		Data[4] = 1.0f;
		Data[5] = 0.0f;
	}
	Smoke->SetCustomData(0, Data, false);
	for (int32 Slot = 1; Slot < Count; ++Slot)
	{
		const int32 K = Slot - 1;
		if (K >= Order.Num())
		{
			Xfs[Slot] = TTF::Hidden;
			continue;
		}
		const FPuff& P = Puffs[Order[K]];
		const float F = P.Age / FMath::Max(P.Life, 0.01f);
		const float Size = FMath::Lerp(P.Size0, P.Size1, 1.0f - FMath::Cube(1.0f - F));
		const FVector ToEye = (Eye - P.Pos).GetSafeNormal();
		FVector U, V;
		ToEye.FindBestAxisVectors(U, V);
		const float Turn = P.Seed * 6.283f + P.Age * (P.Seed - 0.5f) * 0.3f;   // (a slow roll as it billows)
		const FVector Along = U * FMath::Cos(Turn) + V * FMath::Sin(Turn);
		const FMatrix M = FRotationMatrix::MakeFromZX(ToEye, Along);
		Xfs[Slot] = Strength[Order[K]] > 0.002f ? FTransform(M.ToQuat(), P.Pos, FVector(Size / 100.0f, Size / 100.0f, 1.0f)) : TTF::Hidden;
		Data[3] = FMath::Clamp(Strength[Order[K]], 0.0f, 1.0f);
		Data[4] = 1.0f;
		Data[5] = P.Seed;
		Smoke->SetCustomData(Slot, Data, false);
	}
	Smoke->BatchUpdateInstancesTransforms(0, Xfs, true, false, true);
	Smoke->MarkRenderStateDirty();
	DrawnSlots = Used;
}
