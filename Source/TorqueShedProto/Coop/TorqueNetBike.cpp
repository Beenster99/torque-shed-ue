#include "TorqueNetBike.h"

#include "TorqueSettings.h"
#include "TorqueBikePawn.h"
#include "Components/AudioComponent.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"
#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Net/UnrealNetwork.h"
#include "Sound/SoundAttenuation.h"

using namespace TorqueSim;

namespace
{
// Where each blended number sits in a TorqueNet::Snap.
enum ENetVal
{
	NV_Steer, NV_Speed, NV_Lean, NV_Pitch, NV_Rpm, NV_Throttle, NV_FrontBrake, NV_RearBrake, NV_LeanIn, NV_ShiftIn,
	NV_FrontOmega, NV_RearOmega, NV_FrontX, NV_FrontY, NV_FrontZ, NV_RearX, NV_RearY, NV_RearZ,
	NV_FrontKappa, NV_RearKappa, NV_FrontSlip, NV_RearSlip, NV_Count
};
static_assert(NV_Count <= TorqueNet::MaxVals, "more values than a snapshot holds");
enum ENetInt { NI_Gear, NI_Flags, NI_FrontSurf, NI_RearSurf };

constexpr uint8 FlagCrashed = 1, FlagFrontDown = 2, FlagRearDown = 4, FlagGhost = 8, FlagParked = 16;

bool NetLogEverySecond()
{
	static const bool bEvery = FParse::Param(FCommandLine::Get(), TEXT("TorqueNetLog"));
	return bEvery;
}

FString RiderName(const APawn* P)
{
	const APlayerState* PS = P ? P->GetPlayerState() : nullptr;
	return PS ? PS->GetPlayerName() : FString(TEXT("rider"));
}
} // namespace

UTorqueNetBikeComponent::UTorqueNetBikeComponent()
{
	PrimaryComponentTick.bCanEverTick = false;   // the pawn calls it from its own tick
	SetIsReplicatedByDefault(true);
}

void UTorqueNetBikeComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UTorqueNetBikeComponent, Slot);
	DOREPLIFETIME_CONDITION(UTorqueNetBikeComponent, Recent, COND_SkipOwner);
}

void UTorqueNetBikeComponent::BeginPlay()
{
	Super::BeginPlay();
	float Hz = 30.f;
	if (FParse::Value(FCommandLine::Get(), TEXT("TorqueNetHz="), Hz)) SendHz = FMath::Clamp(Hz, 5.f, 120.f);
	float DelayMs = 100.f;
	if (FParse::Value(FCommandLine::Get(), TEXT("TorqueNetDelayMs="), DelayMs)) Smooth.Delay = FMath::Clamp(DelayMs, 20.f, 400.f) / 1000.0;
	Smooth.MaxDelay = FMath::Max(Smooth.MaxDelay, Smooth.Delay);

	// The host gives each new bike the lowest start slot nobody else has.
	AActor* MyOwner = GetOwner();
	if (MyOwner && MyOwner->HasAuthority() && GetWorld())
	{
		TSet<int32> Used;
		for (TActorIterator<ATorqueBikePawn> It(GetWorld()); It; ++It)
		{
			if (*It == MyOwner || !It->NetBike) continue;
			Used.Add(It->NetBike->Slot);
		}
		int32 Free = 0;
		while (Used.Contains(Free)) ++Free;
		Slot = Free;
	}
}

ATorqueBikePawn* UTorqueNetBikeComponent::Bike() const
{
	return Cast<ATorqueBikePawn>(GetOwner());
}

bool UTorqueNetBikeComponent::IsRemote() const
{
	const APawn* P = Cast<APawn>(GetOwner());
	if (!P || P->GetNetMode() == NM_Standalone) return false;
	return !P->IsLocallyControlled();
}

void UTorqueNetBikeComponent::Capture(FTorqueBikeNetState& S) const
{
	const ATorqueBikePawn* B = Bike();
	const BikeSim& Sim = *B->Sim;
	S.Time = FPlatformTime::Seconds();
	S.Pos = B->GetActorLocation();
	// Sim metres (x east, y up, z south) to Unreal cm (x east, y south, z up).
	S.Vel = FVector(Sim.LinearVelocity.x, Sim.LinearVelocity.z, Sim.LinearVelocity.y) * 100.0;
	S.Rot = B->GetActorQuat();
	S.Steer = (float)Sim.Steer;
	S.Speed = (float)Sim.Speed;
	S.LeanAngle = (float)Sim.LeanAngle;
	S.PitchAngle = (float)Sim.PitchAngle;
	S.Rpm = (float)Sim.Rpm;
	S.Throttle = (float)B->Throttle;
	S.FrontBrake = (float)B->FrontBrake;
	S.RearBrake = (float)B->RearBrake;
	S.LeanInput = (float)B->Lean;
	S.ShiftInput = (float)B->Shift;
	const Wheel& F = Sim.Wheels[0];
	const Wheel& R = Sim.Wheels[1];
	S.FrontCentre = FVector3f((float)F.Centre.x, (float)F.Centre.y, (float)F.Centre.z);
	S.RearCentre = FVector3f((float)R.Centre.x, (float)R.Centre.y, (float)R.Centre.z);
	S.FrontOmega = (float)F.Omega;
	S.RearOmega = (float)R.Omega;
	S.FrontKappa = (float)F.Kappa;
	S.RearKappa = (float)R.Kappa;
	S.FrontSlip = (float)F.Slip;
	S.RearSlip = (float)R.Slip;
	S.Gear = (uint8)FMath::Clamp(Sim.Gear, 0, 255);
	S.Flags = (Sim.IsCrashed ? FlagCrashed : 0) | (F.Contact ? FlagFrontDown : 0) | (R.Contact ? FlagRearDown : 0) | (Sim.IsGhost() ? FlagGhost : 0) | (B->IsOnFoot() ? FlagParked : 0);
	S.FrontSurf = (uint8)FMath::Clamp(F.Surf, 0, 255);
	S.RearSurf = (uint8)FMath::Clamp(R.Surf, 0, 255);
}

void UTorqueNetBikeComponent::Receive(const FTorqueBikeNetState& S)
{
	if (S.Seq <= LastPushedSeq) return;   // already have it (the property carries the last few)
	LastPushedSeq = S.Seq;
	TorqueNet::Snap N;
	N.Time = S.Time;
	N.Pos[0] = S.Pos.X; N.Pos[1] = S.Pos.Y; N.Pos[2] = S.Pos.Z;
	N.Vel[0] = S.Vel.X; N.Vel[1] = S.Vel.Y; N.Vel[2] = S.Vel.Z;
	const FQuat Q = S.Rot.GetNormalized();
	N.Rot[0] = Q.X; N.Rot[1] = Q.Y; N.Rot[2] = Q.Z; N.Rot[3] = Q.W;
	N.Val[NV_Steer] = S.Steer;
	N.Val[NV_Speed] = S.Speed;
	N.Val[NV_Lean] = S.LeanAngle;
	N.Val[NV_Pitch] = S.PitchAngle;
	N.Val[NV_Rpm] = S.Rpm;
	N.Val[NV_Throttle] = S.Throttle;
	N.Val[NV_FrontBrake] = S.FrontBrake;
	N.Val[NV_RearBrake] = S.RearBrake;
	N.Val[NV_LeanIn] = S.LeanInput;
	N.Val[NV_ShiftIn] = S.ShiftInput;
	N.Val[NV_FrontOmega] = S.FrontOmega;
	N.Val[NV_RearOmega] = S.RearOmega;
	N.Val[NV_FrontX] = S.FrontCentre.X; N.Val[NV_FrontY] = S.FrontCentre.Y; N.Val[NV_FrontZ] = S.FrontCentre.Z;
	N.Val[NV_RearX] = S.RearCentre.X; N.Val[NV_RearY] = S.RearCentre.Y; N.Val[NV_RearZ] = S.RearCentre.Z;
	N.Val[NV_FrontKappa] = S.FrontKappa;
	N.Val[NV_RearKappa] = S.RearKappa;
	N.Val[NV_FrontSlip] = S.FrontSlip;
	N.Val[NV_RearSlip] = S.RearSlip;
	N.Ints[NI_Gear] = S.Gear;
	N.Ints[NI_Flags] = S.Flags;
	N.Ints[NI_FrontSurf] = S.FrontSurf;
	N.Ints[NI_RearSurf] = S.RearSurf;
	Smooth.Push(N, FPlatformTime::Seconds());
}

void UTorqueNetBikeComponent::OnRep_State()
{
	// Oldest first, skipping any already taken.
	int32 Order[RecentCount];
	for (int32 I = 0; I < RecentCount; ++I) Order[I] = I;
	for (int32 I = 1; I < RecentCount; ++I)
		for (int32 J = I; J > 0 && Recent[Order[J]].Seq < Recent[Order[J - 1]].Seq; --J) Swap(Order[J], Order[J - 1]);
	for (int32 I = 0; I < RecentCount; ++I)
		if (Recent[Order[I]].Seq > 0) Receive(Recent[Order[I]]);
}

void UTorqueNetBikeComponent::Keep(const FTorqueBikeNetState& S)
{
	Recent[RecentNext] = S;
	RecentNext = (RecentNext + 1) % RecentCount;
	// Out in this frame's replication, not whenever the actor's turn next comes round.
	if (AActor* MyOwner = GetOwner()) MyOwner->ForceNetUpdate();
}

void UTorqueNetBikeComponent::ServerState_Implementation(const FTorqueBikeNetState& S)
{
	Keep(S);        // on to the other riders
	Receive(S);     // and drawn on the host
}

void UTorqueNetBikeComponent::ApplyRemote(float DeltaSeconds)
{
	ATorqueBikePawn* B = Bike();
	if (!B || !B->Sim) return;
	TorqueNet::Snap Out;
	if (!Smooth.Sample(FPlatformTime::Seconds(), DeltaSeconds, Out)) return;   // nothing heard yet: stay put

	B->CurPos = B->PrevPos = FVector(Out.Pos[0], Out.Pos[1], Out.Pos[2]);
	B->CurRot = B->PrevRot = FQuat(Out.Rot[0], Out.Rot[1], Out.Rot[2], Out.Rot[3]).GetNormalized();
	B->Accum = 0.0;

	BikeSim& Sim = *B->Sim;
	Sim.Position = V3(Out.Pos[0] / 100.0, Out.Pos[2] / 100.0, Out.Pos[1] / 100.0);
	Sim.LinearVelocity = V3(Out.Vel[0] / 100.0, Out.Vel[2] / 100.0, Out.Vel[1] / 100.0);
	{
		// Its attitude too, so the others' bikes meet it where it's drawn (BikeSim::AsCapsules).
		const FQuat Q = B->CurRot;
		const FVector Fx = Q.GetAxisX(), Fy = Q.GetAxisY(), Fz = Q.GetAxisZ();
		Sim.Basis = M3(V3(Fy.X, Fy.Z, Fy.Y), V3(Fz.X, Fz.Z, Fz.Y), V3(-Fx.X, -Fx.Z, -Fx.Y));
	}
	Sim.Steer = Out.Val[NV_Steer];
	Sim.Speed = Out.Val[NV_Speed];
	Sim.LeanAngle = Out.Val[NV_Lean];
	Sim.PitchAngle = Out.Val[NV_Pitch];
	Sim.Rpm = Out.Val[NV_Rpm];
	Sim.Gear = Out.Ints[NI_Gear];
	Sim.IsCrashed = (Out.Ints[NI_Flags] & FlagCrashed) != 0;
	// A ghost (respawned): the others pass through it. The time left isn't sent: drawn at its faintest.
	Sim.GhostLeft = (Out.Ints[NI_Flags] & FlagGhost) != 0 ? 2.0 : 0.0;
	B->SetParkedLook((Out.Ints[NI_Flags] & FlagParked) != 0);   // its rider's off it, the stand down
	B->Throttle = Out.Val[NV_Throttle];
	B->FrontBrake = Out.Val[NV_FrontBrake];
	B->RearBrake = Out.Val[NV_RearBrake];
	B->Lean = Out.Val[NV_LeanIn];
	B->Shift = Out.Val[NV_ShiftIn];

	Wheel& F = Sim.Wheels[0];
	Wheel& R = Sim.Wheels[1];
	F.Centre = V3(Out.Val[NV_FrontX], Out.Val[NV_FrontY], Out.Val[NV_FrontZ]);
	R.Centre = V3(Out.Val[NV_RearX], Out.Val[NV_RearY], Out.Val[NV_RearZ]);
	// The wheels turn at the speed they were turning; their angle isn't sent (at 30 a second it
	// would alias), only how fast.
	F.Omega = Out.Val[NV_FrontOmega];
	R.Omega = Out.Val[NV_RearOmega];
	FrontSpin = FMath::Fmod(FrontSpin + F.Omega * DeltaSeconds, 2.0 * UE_DOUBLE_PI);
	RearSpin = FMath::Fmod(RearSpin + R.Omega * DeltaSeconds, 2.0 * UE_DOUBLE_PI);
	F.Spin = FrontSpin;
	R.Spin = RearSpin;
	F.Kappa = Out.Val[NV_FrontKappa];
	R.Kappa = Out.Val[NV_RearKappa];
	F.Slip = Out.Val[NV_FrontSlip];
	R.Slip = Out.Val[NV_RearSlip];
	F.Contact = (Out.Ints[NI_Flags] & FlagFrontDown) != 0;
	R.Contact = (Out.Ints[NI_Flags] & FlagRearDown) != 0;
	F.Surf = Out.Ints[NI_FrontSurf];
	R.Surf = Out.Ints[NI_RearSurf];
}

void UTorqueNetBikeComponent::SetUpSound(bool bRemote)
{
	ATorqueBikePawn* B = Bike();
	if (!B || !B->SoundAudio || !B->Sound) return;
	SoundRemote = bRemote ? 1 : 0;
	UAudioComponent* A = B->SoundAudio;
	A->Stop();
	// Our own bike is heard as now (in the helmet or from just behind). Someone else's comes from
	// where their bike is, fading over a few hundred metres, without the birds and the wind in
	// their helmet (ours already has those).
	A->bAllowSpatialization = bRemote;
	A->bOverrideAttenuation = bRemote;
	if (bRemote)
	{
		FSoundAttenuationSettings& At = A->AttenuationOverrides;
		At.bAttenuate = true;
		At.bSpatialize = true;
		At.AttenuationShape = EAttenuationShape::Sphere;
		At.AttenuationShapeExtents = FVector(300.0, 0.0, 0.0);
		At.FalloffDistance = 40000.f;
		At.DistanceAlgorithm = EAttenuationDistanceModel::NaturalSound;
		At.dBAttenuationAtMax = -50.f;
	}
	B->Sound->Ambience = !bRemote;
	B->Sound->WindLevel = bRemote ? 0.15 : 1.0;
	A->Play();
}

void UTorqueNetBikeComponent::AfterTick(float DeltaSeconds)
{
	ATorqueBikePawn* B = Bike();
	if (!B || !B->Sim) return;
	const bool bRemote = IsRemote();
	if (SoundRemote != (bRemote ? 1 : 0))
	{
		// Riding alone the sound stays exactly as the pawn made it.
		if (SoundRemote < 0 && !bRemote) SoundRemote = 0;
		else SetUpSound(bRemote);
	}
	// Settings > Sound.
	if (B->Sound)
	{
		const FTorqueSettings& Set = FTorqueSettings::Get();
		B->Sound->EngineLevel = Set.Engine;
		B->Sound->AmbienceLevel = Set.Ambience;
	}
	const bool bNetworked = B->GetNetMode() != NM_Standalone;
	if (!bNetworked) return;

	if (!bRemote && B->IsLocallyControlled())
	{
		// Our own bike, once we know it's ours: to our place on the start, then send it.
		if (!bPlaced)
		{
			bPlaced = true;
			if (Slot > 0) B->PlaceForSlot(Slot);
		}
		const double Interval = 1.0 / SendHz;
		SendClock += DeltaSeconds;
		if (SendClock >= Interval)
		{
			SendClock = SendClock - Interval > Interval ? 0.0 : SendClock - Interval;
			FTorqueBikeNetState S;
			Capture(S);
			S.Seq = ++SendSeq;
			++SentInSecond;
			if (B->HasAuthority()) Keep(S);   // the host's own bike: straight to the others
			else ServerState(S);
		}
	}

	StatClock += DeltaSeconds;
	++FramesInSecond;
	if (StatClock < 1.0) return;
	Fps = (float)(FramesInSecond / StatClock);
	FramesInSecond = 0;
	const TorqueNet::SmootherStats& St = Smooth.Stats;
	TxHz = (float)(SentInSecond / StatClock);
	RxHz = (float)((St.Received - RxAtLast) / StatClock);
	const int64 Frames = St.Frames - FramesAtLast;
	ExtrapPct = Frames > 0 ? (float)(100.0 * (St.Extrapolated - ExtrapAtLast) / Frames) : 0.f;
	const float HeldPct = Frames > 0 ? (float)(100.0 * (St.Held - HeldAtLast) / Frames) : 0.f;
	RxAtLast = St.Received;
	FramesAtLast = St.Frames;
	ExtrapAtLast = St.Extrapolated;
	HeldAtLast = St.Held;
	SentInSecond = 0;
	StatClock = 0.0;
	if (bRemote)
	{
		const double Mean = St.Intervals ? St.IntervalSum / St.Intervals : 0.0;
		const double Sd = St.Intervals ? FMath::Sqrt(FMath::Max(0.0, St.IntervalSq / St.Intervals - Mean * Mean)) : 0.0;
		// step err: this second's worst frame-to-frame kink in the drawn path, cm (a few mm is smooth).
		Summary = FString::Printf(TEXT("%s: rx %.1f/s (interval %.1f +- %.1f ms), delay %.0f ms, extrapolated %.1f%%, held %.1f%%, step err max %.2f cm, late or repeated %lld, jumps %lld, behind newest %.0f ms"),
			*RiderName(B), RxHz, Mean * 1000.0, Sd * 1000.0, Smooth.WantDelay() * 1000.0, ExtrapPct, HeldPct, St.StepErrMax,
			(long long)St.Dropped, (long long)St.Snaps, -Smooth.Behind(FPlatformTime::Seconds()) * 1000.0);
		Smooth.Stats.StepErrMax = 0.0;
	}
	else
	{
		const APlayerState* PS = B->GetPlayerState();
		const AGameStateBase* GS = GetWorld() ? GetWorld()->GetGameState() : nullptr;
		Summary = FString::Printf(TEXT("%s (this machine, %s): %.0f fps, tx %.1f/s, ping %.0f ms, riders %d"),
			*RiderName(B), B->HasAuthority() ? TEXT("host") : TEXT("client"), Fps, TxHz, PS ? PS->GetPingInMilliseconds() : 0.f,
			GS ? GS->PlayerArray.Num() : 0);
	}
	// Into the log every second with -TorqueNetLog, else every ten.
	if (NetLogEverySecond() || ++LogCount >= 10)
	{
		LogCount = 0;
		UE_LOG(LogTemp, Log, TEXT("Torque net: %s"), *Summary);
	}
}

FString UTorqueNetBikeComponent::StatusLine() const
{
	const ATorqueBikePawn* B = Bike();
	if (!B || B->GetNetMode() == NM_Standalone || !GetWorld()) return FString();
	const APlayerState* PS = B->GetPlayerState();
	const AGameStateBase* GS = GetWorld()->GetGameState();
	FString Line = FString::Printf(TEXT("Co-op: %s, %d riding"), B->HasAuthority() ? TEXT("hosting") : TEXT("joined"),
		GS ? GS->PlayerArray.Num() : 1);
	if (!B->HasAuthority() && PS) Line += FString::Printf(TEXT(", ping %.0f ms"), PS->GetPingInMilliseconds());
	for (TActorIterator<ATorqueBikePawn> It(GetWorld()); It; ++It)
	{
		if (*It == B || !It->NetBike) continue;
		Line += FString::Printf(TEXT("   %s %.0f/s"), *RiderName(*It), It->NetBike->RxHz);
		if (It->NetBike->ExtrapPct > 1.f) Line += FString::Printf(TEXT(" (gaps %.0f%%)"), It->NetBike->ExtrapPct);
	}
	return Line;
}
