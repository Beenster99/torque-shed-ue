#include "TorqueAudio/TorqueSfx.h"

#include "TorqueAudio/RideSound.h"
#include "TorqueAudio/SfxRules.h"
#include "Async/Async.h"
#include "Camera/PlayerCameraManager.h"
#include "Coop/TorqueSettings.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Shed/TorqueBuildSubsystem.h"
#include "Shed/TorqueDynoRoom.h"
#include "Shed/TorquePaintBay.h"
#include "Shed/TorqueShedLayout.h"
#include "Shed/TorqueShedParts.h"
#include "Shed/TorqueShedRadio.h"
#include "Shed/TorqueWalker.h"

#include <mutex>

namespace TorqueSfxImpl
{
using namespace TorqueSfxMix;
using namespace TorqueSfxRules;

// The bank is shared by every world (the shed, the ride): read once, its takes kept.
std::shared_ptr<TorqueSfxBank::FBank> TheBank()
{
	static std::shared_ptr<TorqueSfxBank::FBank> Bank;
	static std::once_flag Once;
	std::call_once(Once, [] {
		Bank = std::make_shared<TorqueSfxBank::FBank>();
		const FString Dir = FPaths::Combine(FPaths::ProjectDir(), TEXT("Data/Audio/TS"));
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *FPaths::Combine(Dir, TEXT("manifest.json"))))
		{
			UE_LOG(LogTemp, Warning, TEXT("Torque: no Data/Audio/TS/manifest.json: the shed is silent (Tools/import_ts_sounds.py makes it)"));
			return;
		}
		const bool bOk = Bank->Load(TCHAR_TO_UTF8(*Text), [Dir](const std::string& Rel, std::vector<uint8_t>& Out) {
			TArray<uint8> Bytes;
			if (!FFileHelper::LoadFileToArray(Bytes, *FPaths::Combine(Dir, UTF8_TO_TCHAR(Rel.c_str())), FILEREAD_Silent)) return false;
			Out.assign(Bytes.GetData(), Bytes.GetData() + Bytes.Num());
			return true;
		});
		UE_LOG(LogTemp, Log, TEXT("Torque: Torque Shed's sounds: %d (%s)"), (int32)Bank->Count(), bOk ? TEXT("ok") : TEXT("manifest unreadable"));
		// Every take but the place beds read in the background, so the first of each doesn't stall.
		std::shared_ptr<TorqueSfxBank::FBank> Keep = Bank;
		Async(EAsyncExecution::ThreadPool, [Keep] {
			const double T0 = FPlatformTime::Seconds();
			const int32 N = Keep->Preload();
			UE_LOG(LogTemp, Log, TEXT("Torque: %d sound takes read in %.0f ms"), N, (FPlatformTime::Seconds() - T0) * 1000.0);
		});
	});
	return Bank;
}

std::string S(const FString& F) { return std::string(TCHAR_TO_UTF8(*F)); }

// Torque Shed's action names (shed_state's "a"), for the work loops and the done sounds.
FString ActName(ETorqueShedAct K)
{
	switch (K)
	{
	case ETorqueShedAct::Unbolt: return TEXT("unbolt");
	case ETorqueShedAct::Bolt: return TEXT("bolt");
	case ETorqueShedAct::Remove: return TEXT("remove");
	case ETorqueShedAct::Install: return TEXT("install");
	case ETorqueShedAct::Take: return TEXT("take");
	case ETorqueShedAct::Drop: return TEXT("drop");
	case ETorqueShedAct::Place: return TEXT("place");
	case ETorqueShedAct::Bin: return TEXT("bin");
	case ETorqueShedAct::Clean: return TEXT("clean");
	case ETorqueShedAct::Repair: return TEXT("repair");
	case ETorqueShedAct::Fabricate: return TEXT("repair");
	case ETorqueShedAct::Paint:
	case ETorqueShedAct::Prime:
	case ETorqueShedAct::Coat: return TEXT("paint");
	case ETorqueShedAct::Wash: return TEXT("wash");
	case ETorqueShedAct::OrderPart: return TEXT("order_part");
	case ETorqueShedAct::TyreFit: return TEXT("tyre_fit");
	case ETorqueShedAct::TyreRemove: return TEXT("tyre_remove");
	case ETorqueShedAct::StartBuild:
	case ETorqueShedAct::ReleaseBike: return TEXT("set_stand");
	default: return FString();
	}
}

const TCHAR* ToolName(ETorqueTool T)
{
	switch (T)
	{
	case ETorqueTool::Wrench: return TEXT("wrench");
	case ETorqueTool::Brush: return TEXT("brush");
	case ETorqueTool::Repair: return TEXT("repair");
	case ETorqueTool::Spray: return TEXT("spray");
	case ETorqueTool::Lance: return TEXT("lance");
	case ETorqueTool::Gun: return TEXT("gun");
	default: return TEXT("");
	}
}

// What a part is made of, as the parts list says ("Carbon fibre" -> "carbon").
FString MaterialId(FName Part)
{
	FString M = TorqueShedParts::MaterialName(Part).ToLower();
	FString Left, Right;
	if (M.Split(TEXT(" "), &Left, &Right)) M = Left;
	return M;
}

// dyno_room.gd and TorqueDynoRoom.cpp: the rig, the drum, the fan, the switches (keep in step).
const FVector Rig(0.0, -940.0, 0.0);
const FVector DrumAt = Rig + FVector(0.0, 71.0, 10.0);
const FVector FanAt = Rig + FVector(0.0, -183.0, 95.0);
const FVector SwitchesAt(-147.0, -586.0, 135.0);
constexpr double Primary = 1.9, FinalDrive = 41.0 / 18.0, RearRadiusM = 0.312;
const double Gears[] = {2.6, 1.95, 1.6, 1.36, 1.19, 1.06};
constexpr float FanDb = -6.0f;
// sfx_ambience.gd: the compressor in the shed's back left corner, its rests and runs.
const FVector CompressorAt(-590.0, -490.0, 50.0);
constexpr double CompRest[2] = {50.0, 120.0}, CompRun[2] = {10.0, 20.0}, AirDrain = 8.0;
// The Unreal shed's roller door stands open (there's no door to work yet).
constexpr bool bDoorOpen = true;

// The engine on the dyno: the ride's own engine sound (RideSound), fed the run's revs.
struct FRideGen : public TorqueSfxMix::FGenerator
{
	TorqueSound::RideSound Sound;
	std::mutex Lock;
	TorqueSound::RideInputs In;
	int Shifts = 0;   // gear changes to play: + up, - down
	bool Render(float* Out, int N) override
	{
		{
			std::lock_guard<std::mutex> G(Lock);
			Sound.Set(In);
			for (; Shifts > 0; --Shifts) Sound.GearChange(true);
			for (; Shifts < 0; ++Shifts) Sound.GearChange(false);
		}
		Sound.Render(Out, N);
		return true;
	}
};
}

using namespace TorqueSfxImpl;

// --- The stream --------------------------------------------------------------------------------------

bool UTorqueSfxSynth::Init(int32& SampleRate)
{
	NumChannels = 2;
	SampleRate = TorqueSfxMix::Rate;
	return true;
}

int32 UTorqueSfxSynth::OnGenerateAudio(float* OutAudio, int32 NumSamples)
{
	if (Mixer) Mixer->Render(OutAudio, NumSamples / 2);
	else FMemory::Memzero(OutAudio, NumSamples * sizeof(float));
	return NumSamples;
}

// --- The subsystem ----------------------------------------------------------------------------------

UTorqueSfx* UTorqueSfx::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	return World ? World->GetSubsystem<UTorqueSfx>() : nullptr;
}

bool UTorqueSfx::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return World && (World->WorldType == EWorldType::Game || World->WorldType == EWorldType::PIE);
}

void UTorqueSfx::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Mixer = std::make_shared<TorqueSfxMix::FMixer>();
	Rng.seed((uint32)FPlatformTime::Cycles());
	TheBank();
}

void UTorqueSfx::Deinitialize()
{
	if (UTorqueBuildSubsystem* B = UTorqueBuildSubsystem::Of(this)) B->OnRequestDone.Remove(RequestHandle);
	if (Synth) Synth->Stop();   // (it keeps its mixer: the audio thread may be in it)
	Super::Deinitialize();
}

void UTorqueSfx::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	bShed = InWorld.GetMapName().Contains(TEXT("Shed"));
	EnsureSpeaker();
	if (UTorqueBuildSubsystem* B = UTorqueBuildSubsystem::Of(this))
		RequestHandle = B->OnRequestDone.AddUObject(this, &UTorqueSfx::Requested);
	CompTimer = Uni(15.0, 45.0);
	CreakTimer = Uni(10.0, 30.0);
	ListenerRoomName = bShed ? TEXT("shed") : FString();
	Shown = RoomPreset(RoomKind(S(ListenerRoomName)));
	Mixer->SetRoom({Shown.Size, Shown.Damp, Shown.Wet, Shown.Pre, Shown.Hp, Shown.Spread});
	UE_LOG(LogTemp, Log, TEXT("Torque: shed sounds on (%s)"), bShed ? TEXT("the shed's rooms") : TEXT("outdoors"));
}

TStatId UTorqueSfx::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UTorqueSfx, STATGROUP_Tickables);
}

void UTorqueSfx::EnsureSpeaker()
{
	if (Synth || !GetWorld()) return;
	FActorSpawnParameters P;
	P.ObjectFlags |= RF_Transient;
	Speaker = GetWorld()->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, P);
	if (!Speaker) return;
	USceneComponent* Root = NewObject<USceneComponent>(Speaker, TEXT("Root"));
	Speaker->SetRootComponent(Root);
	Root->RegisterComponent();
	Synth = NewObject<UTorqueSfxSynth>(Speaker, TEXT("TorqueSfx"));
	Synth->Mixer = Mixer;
	Synth->bAllowSpatialization = false;   // the mixer places every sound itself (sfx.gd's 3D players)
	Synth->SetupAttachment(Root);
	Synth->RegisterComponent();
	Synth->Start();
}

int32 UTorqueSfx::SoundsKnown() const { return (int32)TheBank()->Count(); }

FString UTorqueSfx::RoomAt(const FVector& P) const
{
	return bShed ? FString(TorqueSfxRules::RoomAt(P.X, P.Y, P.Z).c_str()) : FString();
}

TorqueSfxMix::EBus UTorqueSfx::BusFor(const FVector& P) const
{
	return RoomAt(P) == ListenerRoomName ? EBus::Room : EBus::Far;
}

double UTorqueSfx::Occlusion(const FVector& P) const
{
	return OcclusionDb(S(RoomAt(P)), S(ListenerRoomName), bDoorOpen);
}

int32 UTorqueSfx::Play(const FString& Sound, const FPlay& Base, bool bRandomise)
{
	if (!Mixer) return 0;
	std::shared_ptr<const FClip> Clip = TheBank()->Pick(S(Sound), Rng);
	if (!Clip) return 0;
	FPlay P = Base;
	P.Clip = Clip;
	P.bLoop = false;   // a one-shot plays once, even a loop's take (sfx.gd once)
	if (bRandomise)
	{
		P.Db += Uni(-1.2, 1.2);
		P.Pitch *= Uni(0.95, 1.05);
	}
	const double Life = FMath::Min(Clip->Seconds() / FMath::Max(P.Pitch, 0.1) + 0.25, OneShotMax);
	P.Life = P.Life > 0.0 ? FMath::Min(P.Life, Life) : Life;
	return Mixer->Play(P);
}

int32 UTorqueSfx::PlayAt(const FString& Sound, const FVector& At, float Db, float Seconds)
{
	FPlay P;
	P.At = V(At);
	P.Db = Db + Occlusion(At);
	P.Bus = BusFor(At);
	P.Life = Seconds;
	return Play(Sound, P, true);
}

int32 UTorqueSfx::PlayOn(const FString& Sound, USceneComponent* Source, const FVector& Offset, float Db, float Seconds)
{
	if (!Source) return 0;
	const int32 Id = PlayAt(Sound, Source->GetComponentTransform().TransformPosition(Offset), Db, Seconds);
	if (Id) Follows.Add({Id, Source, Offset});
	return Id;
}

void UTorqueSfx::PlayUi(const FString& Sound, float Db)
{
	FPlay P;
	P.b3D = false;
	P.Bus = EBus::Sfx;
	P.Db = Db;
	Play(Sound, P, false);
}

int32 UTorqueSfx::Impact(const FString& Material, const FString& Surface, float Strength, const FVector& At)
{
	const std::string M = ImpactMaterial(S(Material.ToLower()));
	const std::string Su = (Surface == TEXT("wood") || Surface == TEXT("steel") || Surface == TEXT("ground")) ? S(Surface) : std::string("concrete");
	const double K = FMath::Clamp((double)Strength, 0.0, 1.0);
	FPlay P;
	P.At = V(At);
	P.Db = FMath::Lerp(-18.0, -1.0, K) + Occlusion(At);
	P.Bus = BusFor(At);
	P.Pitch = FMath::Lerp(0.97, 1.02, K);
	return Play(FString::Printf(TEXT("impact_%s_%s"), UTF8_TO_TCHAR(M.c_str()), UTF8_TO_TCHAR(Su.c_str())), P, true);
}

int32 UTorqueSfx::Footstep(const FVector& Feet, bool bRunning, float Db)
{
	const std::string Floor = StepSurface(S(RoomAt(Feet + FVector(0.0, 0.0, 20.0))), "tarmac");   // the yard and the roads
	return PlayAt(FString(TEXT("step_")) + UTF8_TO_TCHAR(Floor.c_str()), Feet, Db + (bRunning ? 3.0f : 0.0f));
}

void UTorqueSfx::JobDone(const FTorqueShedRequest& R, int32 HeldBefore, const FVector& At)
{
	const UTorqueBuildSubsystem* B = UTorqueBuildSubsystem::Of(this);
	const FString Name = ActName(R.Kind);
	// Putting a part down lands it, by what it's made of and what it lands on (sfx.gd job_done).
	if ((R.Kind == ETorqueShedAct::Drop || R.Kind == ETorqueShedAct::Place) && HeldBefore != 0 && B)
	{
		const FTorqueItem* It = B->Item(HeldBefore);
		const FString Mat = It ? (It->bBoxed ? FString(TEXT("cardboard")) : MaterialId(It->Part)) : FString(TEXT("metal"));
		FVector Land = At;
		FString Surface = TEXT("concrete");
		if (R.Kind == ETorqueShedAct::Drop)
		{
			if (!R.Place.GetLocation().IsNearlyZero()) Land = R.Place.GetLocation();
			if (RoomAt(Land).IsEmpty() && !bShed) Surface = TEXT("ground");
		}
		else
		{
			Surface = R.Spot.ToString().ToLower().StartsWith(TEXT("bench")) ? TEXT("wood") : TEXT("steel");
			if (const FTorquePlace* Pl = TorqueShedLayout::FindPlace(R.Spot)) Land = Pl->Box.GetCenter();
		}
		Impact(Mat, Surface, R.Kind == ETorqueShedAct::Drop ? 0.85f : 0.5f, Land);
		return;
	}
	// The odd ones walker.gd plays itself as it asks.
	switch (R.Kind)
	{
	case ETorqueShedAct::Unpack: PlayAt(TEXT("pickup"), At, -4.0f); return;
	case ETorqueShedAct::PaintCup: PlayAt(TEXT("pickup"), At - FVector(0.0, 0.0, 30.0), -6.0f); return;
	case ETorqueShedAct::PaintTinTake: PlayAt(TEXT("pickup"), At, -6.0f); return;
	case ETorqueShedAct::TyreFit: PlayAt(TEXT("bead_pop"), At, -4.0f); return;
	case ETorqueShedAct::Fabricate: PlayAt(TEXT("ting"), At, -2.0f); return;
	default: break;
	}
	const char* Done = DoneSound(S(Name));
	if (*Done) PlayAt(UTF8_TO_TCHAR(Done), At, -4.0f);
}

void UTorqueSfx::Requested(const FTorqueShedRequest& R, int32 HeldBefore)
{
	FVector At = Ear;
	if (const APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
		if (PC->PlayerCameraManager) At = PC->PlayerCameraManager->GetCameraLocation() + PC->PlayerCameraManager->GetCameraRotation().Vector() * 120.0;
	JobDone(R, HeldBefore, At);
}

// --- Loops ------------------------------------------------------------------------------------------

int32 UTorqueSfx::MakeLoop(USceneComponent* Parent, const FVector& Offset, float Db, float UnitCm)
{
	FLoop L;
	L.Parent = Parent;
	L.bParented = Parent != nullptr;
	L.Offset = Offset;
	L.BaseDb = Db;
	L.UnitCm = UnitCm;
	const int32 Id = NextLoop++;
	Loops.Add(Id, L);
	return Id;
}

int32 UTorqueSfx::MakeLoopAt(const FVector& At, float Db, float UnitCm) { return MakeLoop(nullptr, At, Db, UnitCm); }

FVector UTorqueSfx::LoopPos(const FLoop& L) const
{
	if (!L.bParented) return L.Offset;
	const USceneComponent* P = L.Parent.Get();
	return P ? P->GetComponentTransform().TransformPosition(L.Offset) : L.At;
}

void UTorqueSfx::SetLoop(int32 Id, const FString& Sound)
{
	FLoop* L = Loops.Find(Id);
	if (!L || !Mixer) return;
	const bool bPlaying = L->Voice && Mixer->Alive(L->Voice);
	const FVector At = LoopPos(*L);
	std::string Up, Down;
	if (Sound.IsEmpty())
	{
		if (bPlaying)
		{
			Mixer->Stop(L->Voice);
			if (LoopTails(S(L->Sound), Up, Down)) PlayAt(UTF8_TO_TCHAR(Down.c_str()), At, L->BaseDb);
		}
		L->Voice = 0;
		L->Sound.Reset();
		return;
	}
	if (bPlaying && L->Sound == Sound) return;
	std::shared_ptr<const FClip> Clip = TheBank()->Pick(S(Sound), Rng);
	if (!Clip) return;
	if (bPlaying) Mixer->Stop(L->Voice, 0.03);
	L->Detune = LoopDetunes(S(Sound)) ? (float)Uni(0.97, 1.03) : 1.0f;
	L->Sound = Sound;
	L->At = At;
	FPlay P;
	P.Clip = Clip;
	P.bLoop = true;
	P.StartAt = Uni(0.0, 1.0) * Clip->Seconds() * 0.9;
	P.Pitch = L->Pitch * L->Detune;
	P.FadeIn = bPlaying ? 0.0 : LoopFadeIn(S(Sound));
	P.At = V(At);
	P.Unit = L->UnitCm;
	P.Db = L->BaseDb + Occlusion(At);
	P.Bus = BusFor(At);
	L->Voice = Mixer->Play(P);
	if (!bPlaying && LoopTails(S(Sound), Up, Down)) PlayAt(UTF8_TO_TCHAR(Up.c_str()), At, L->BaseDb);
}

void UTorqueSfx::SetLoopPitch(int32 Id, float Pitch)
{
	if (FLoop* L = Loops.Find(Id))
	{
		L->Pitch = Pitch;
		if (L->Voice) Mixer->SetPitch(L->Voice, L->Pitch * L->Detune);
	}
}

void UTorqueSfx::SetLoopDb(int32 Id, float Db)
{
	if (FLoop* L = Loops.Find(Id)) L->BaseDb = Db;
}

FString UTorqueSfx::LoopSound(int32 Id) const
{
	const FLoop* L = Loops.Find(Id);
	return L && L->Voice && Mixer->Alive(L->Voice) ? L->Sound : FString();
}

void UTorqueSfx::FreeLoop(int32 Id)
{
	if (const FLoop* L = Loops.Find(Id))
		if (L->Voice) Mixer->Stop(L->Voice);
	Loops.Remove(Id);
}

void UTorqueSfx::Route(FLoop& L)
{
	if (!L.Voice) return;
	const FVector At = LoopPos(L);
	L.At = At;
	Mixer->SetAt(L.Voice, V(At));
	Mixer->SetBus(L.Voice, BusFor(At));
	Mixer->SetDb(L.Voice, L.BaseDb + Occlusion(At));
}

void UTorqueSfx::SetMuffled(bool bOn)
{
	bMuffled = bOn;
	if (Mixer) Mixer->SetMuffled(bOn);
}

// --- Horns ------------------------------------------------------------------------------------------

int32 UTorqueSfx::StartHorn(USceneComponent* Source, const FString& Sound, const FString& Style, const TArray<double>& Tones, float Loudness)
{
	FHorn H;
	H.Source = Source;
	H.Sound = Sound;
	H.Style = Style.IsEmpty() ? FString(TEXT("disc")) : Style;
	H.Tones = Tones.Num() ? Tones : TArray<double>{420.0};
	H.Loudness = Loudness;
	const TorqueSfxBank::FSoundDef* D = Sound.IsEmpty() ? nullptr : TheBank()->Def(S(Sound));
	if (D && D->From == "recording" && !TheBank()->Takes(S(Sound)).empty()) H.Mode = UTF8_TO_TCHAR(D->Horn.c_str());
	const int32 Id = NextHorn++;
	Horns.Add(Id, H);
	return Id;
}

int32 UTorqueSfx::StartBikeHorn(USceneComponent* Source, int32 Bay)
{
	FName Part(TEXT("disc_horn"));
	float Health = 1.0f;
	if (const UTorqueBuildSubsystem* B = UTorqueBuildSubsystem::Of(this))
		if (B->Exists(Bay))
		{
			const FTorqueItem* It = B->Item(B->ItemOn(Bay, FName(TEXT("horn"))));
			if (!It) return 0;   // no horn fitted
			Part = It->Part;
			Health = UTorqueBuildSubsystem::Health(*It);
		}
	// The horns in the parts list (bike_build.gd: style, tones, loudness, and the recordings' sound).
	static TSharedPtr<FJsonObject> HornParts;
	if (!HornParts.IsValid())
	{
		FString Text;
		TSharedPtr<FJsonObject> Root;
		if (FFileHelper::LoadFileToString(Text, *FPaths::Combine(FPaths::ProjectDir(), TEXT("Data/Parts/parts.json"))) &&
			FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) && Root.IsValid())
		{
			const TSharedPtr<FJsonObject>* H = nullptr;
			if (Root->TryGetObjectField(TEXT("horn"), H)) HornParts = *H;
		}
		if (!HornParts.IsValid()) HornParts = MakeShared<FJsonObject>();
	}
	const TSharedPtr<FJsonObject>* Entry = nullptr;
	if (!HornParts->TryGetObjectField(Part.ToString(), Entry)) return 0;
	FString Sound, Style;
	(*Entry)->TryGetStringField(TEXT("sound"), Sound);
	(*Entry)->TryGetStringField(TEXT("style"), Style);
	TArray<double> Tones;
	const TArray<TSharedPtr<FJsonValue>>* List = nullptr;
	if ((*Entry)->TryGetArrayField(TEXT("tones"), List))
		for (const TSharedPtr<FJsonValue>& T : *List) Tones.Add(T->AsNumber());
	if (Tones.Num() == 0) return 0;
	double Loud = 0.6;
	(*Entry)->TryGetNumberField(TEXT("loudness"), Loud);
	return StartHorn(Source, Sound, Style, Tones, (float)(Loud * FMath::Lerp(0.5, 1.0, (double)Health)));
}

void UTorqueSfx::SetHorn(int32 Id, bool bPressed)
{
	FHorn* H = Horns.Find(Id);
	if (!H || !Mixer || H->bPressed == bPressed) return;
	H->bPressed = bPressed;
	const USceneComponent* Src = H->Source.Get();
	const FVector At = Src ? Src->GetComponentLocation() : Ear;
	FPlay P;
	P.At = V(At);
	P.Unit = 2500.0;        // horn_audio.gd: unit_size 25, max_db 6, heard a long way
	P.MaxDb = 6.0;
	P.MaxDistance = 0.0;
	P.Bus = BusFor(At);
	if (H->Mode.IsEmpty())
	{
		// Synthesised: one voice per press, it dies away by itself when let go.
		if (bPressed)
		{
			if (!H->Synth || !Mixer->Alive(H->Voice))
			{
				H->Synth = std::make_shared<TorqueSfxBank::FHornSynth>(S(H->Style), std::vector<double>(H->Tones.GetData(), H->Tones.GetData() + H->Tones.Num()),
					H->Loudness, (uint32)Rng());
				P.Gen = H->Synth;
				H->Voice = Mixer->Play(P);
			}
			H->Synth->SetPressed(true);
		}
		else if (H->Synth)
			H->Synth->SetPressed(false);
		return;
	}
	if (bPressed)
	{
		std::shared_ptr<const FClip> Clip = TheBank()->Pick(S(H->Sound), Rng);
		if (!Clip) return;
		if (H->Voice) Mixer->Stop(H->Voice, 0.02);
		const TorqueSfxBank::FSoundDef* D = TheBank()->Def(S(H->Sound));
		P.Clip = Clip;
		P.bLoop = H->Mode == TEXT("hold");
		P.LoopFrom = D ? D->LoopFrom : 0.25;
		P.Db = LinToDb(FMath::Clamp((double)H->Loudness, 0.2, 1.3));
		P.Pitch = Uni(0.985, 1.015);
		H->Voice = Mixer->Play(P);
	}
	else if (H->Mode == TEXT("hold") && H->Voice)
		Mixer->Stop(H->Voice, 0.07);   // horn_audio.gd RELEASE
}

void UTorqueSfx::TickHorns()
{
	for (auto& KV : Horns)
	{
		FHorn& H = KV.Value;
		const USceneComponent* Src = H.Source.Get();
		if (!Src || !H.Voice) continue;
		Mixer->SetAt(H.Voice, V(Src->GetComponentLocation()));
		// The police horn keeps yelping while held, a take after another.
		if (H.Mode == TEXT("repeat") && H.bPressed && !Mixer->Alive(H.Voice))
		{
			H.bPressed = false;
			SetHorn(KV.Key, true);
		}
	}
}

// --- Every frame ------------------------------------------------------------------------------------

void UTorqueSfx::Tick(float Dt)
{
	Super::Tick(Dt);
	if (!Mixer || !GetWorld() || !GetWorld()->HasBegunPlay()) return;
	EnsureSpeaker();
	Mixer->SetVolume(FMath::Clamp(FTorqueSettings::Get().Sfx, 0.0f, 1.0f));   // Settings > Sound > Sound effects
	TickListener(Dt);
	// One-shots riding along on something, and the loops' places, walls and rooms.
	for (int32 I = Follows.Num() - 1; I >= 0; --I)
	{
		const USceneComponent* Src = Follows[I].Source.Get();
		if (!Src || !Mixer->Alive(Follows[I].Voice)) { Follows.RemoveAtSwap(I); continue; }
		Mixer->SetAt(Follows[I].Voice, V(Src->GetComponentTransform().TransformPosition(Follows[I].Offset)));
	}
	RouteIn -= Dt;
	const bool bRoute = RouteIn <= 0.0;
	if (bRoute) RouteIn = 0.15;
	for (auto& KV : Loops)
	{
		FLoop& L = KV.Value;
		if (!L.Voice) continue;
		if (bRoute) Route(L);
		else Mixer->SetAt(L.Voice, V(LoopPos(L)));
	}
	TickWalkers(Dt);
	TickHorns();
	if (bShed)
	{
		TickAmbience(Dt);
		TickKit(Dt);
	}
}

void UTorqueSfx::TickListener(float Dt)
{
	const APlayerController* PC = GetWorld()->GetFirstPlayerController();
	if (!PC || !PC->PlayerCameraManager) return;
	Ear = PC->PlayerCameraManager->GetCameraLocation();
	const FRotator Rot = PC->PlayerCameraManager->GetCameraRotation();
	const FVector Right = FRotationMatrix(Rot).GetUnitAxis(EAxis::Y);
	Mixer->SetListener(V(Ear), V(Right));
	const FString Room = RoomAt(Ear);
	ListenerRoomName = Room;
	// The reverb eases toward this room's (sfx.gd _process: 0.6 a second, the pre-delay 30 ms a second).
	const FPreset T = RoomPreset(RoomKind(S(Room)));
	auto Move = [Dt](double& V0, double To, double Speed) { V0 = FMath::FInterpConstantTo(V0, To, (double)Dt, Speed); };
	const FPreset Was = Shown;
	Move(Shown.Size, T.Size, 0.6); Move(Shown.Damp, T.Damp, 0.6); Move(Shown.Wet, T.Wet, 0.6);
	Move(Shown.Pre, T.Pre, 30.0); Move(Shown.Hp, T.Hp, 0.6); Move(Shown.Spread, T.Spread, 0.6);
	if (Was.Size != Shown.Size || Was.Damp != Shown.Damp || Was.Wet != Shown.Wet || Was.Pre != Shown.Pre || Was.Hp != Shown.Hp || Was.Spread != Shown.Spread)
		Mixer->SetRoom({Shown.Size, Shown.Damp, Shown.Wet, Shown.Pre, Shown.Hp, Shown.Spread});
	// Outside, a sound comes back off the shed's wall a moment later (sfx.gd _slapback).
	bool bSlap = bShed && Room.IsEmpty();
	double Dist = 0.0;
	if (bSlap)
	{
		const double Dx = FMath::Max3(ShedX0 - Ear.X, Ear.X - ShedX1, 0.0), Dy = FMath::Max3(ShedY0 - Ear.Y, Ear.Y - ShedY1, 0.0);
		Dist = FMath::Sqrt(Dx * Dx + Dy * Dy) / 100.0;
		bSlap = Dist > 2.0 && Dist < 45.0;
	}
	Mixer->SetSlap(bSlap, FMath::Clamp(Dist * 2.0 / 343.0 * 1000.0, 15.0, 260.0), -13.0 - Dist * 0.3);
}

void UTorqueSfx::TickWalkers(float Dt)
{
	TSet<uint32> Seen;
	for (TActorIterator<ATorqueWalker> It(GetWorld()); It; ++It)
	{
		ATorqueWalker* W = *It;
		Seen.Add(W->GetUniqueID());
		FWalkerSound& Ws = Walkers.FindOrAdd(W->GetUniqueID());
		const bool bLocal = W->IsLocallyControlled();
		const double Half = W->GetCapsuleComponent() ? W->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 90.0;
		const FVector Feet = W->GetActorLocation() - FVector(0.0, 0.0, Half);
		// A step every 0.7 m walking, 0.85 m running (walker.gd), 0.75 m for another player (remote_player.gd).
		const double Moved = FVector::Dist2D(Feet, Ws.Last) / 100.0;
		const bool bGround = !W->GetCharacterMovement() || W->GetCharacterMovement()->IsMovingOnGround();
		if (Ws.bSeen && bGround && Moved < 2.0 && !W->IsRidingOut()) Ws.Stride += Moved;
		Ws.Last = Feet;
		Ws.bSeen = true;
		const bool bRunning = bLocal && W->GetVelocity().Size2D() > 430.0;
		if (Ws.Stride > (bLocal ? (bRunning ? 0.85 : 0.7) : 0.75))
		{
			Ws.Stride = 0.0;
			Footstep(Feet, bRunning, bLocal ? -14.0f : -12.0f);
		}
		// The loop in their hands: yours by the job you're at (WORK_LOOPS), theirs by the tool (TOOL_LOOPS).
		if (!Ws.Loop) Ws.Loop = MakeLoop(W->GetRootComponent(), FVector(40.0, 15.0, Half + 40.0));
		FString Want;
		if (bLocal)
		{
			FTorqueShedRequest R;
			if (W->SoundJob(R))
			{
				const bool bGun = R.bGun || W->SoundTool() == ETorqueTool::Gun;
				const bool bPaint = R.Kind == ETorqueShedAct::Paint || R.Kind == ETorqueShedAct::Prime || R.Kind == ETorqueShedAct::Coat;
				Want = bPaint && bGun ? FString(TEXT("spray_gun")) : FString(UTF8_TO_TCHAR(WorkLoop(S(ActName(R.Kind)))));
			}
		}
		else if (W->SoundWorking())
			Want = UTF8_TO_TCHAR(ToolLoop(S(ToolName(W->SoundTool()))));
		SetLoop(Ws.Loop, Want);
	}
	for (auto It = Walkers.CreateIterator(); It; ++It)
		if (!Seen.Contains(It.Key()))
		{
			FreeLoop(It.Value().Loop);
			It.RemoveCurrent();
		}
}

int32 UTorqueSfx::Bed(const FString& Sound, EBus Bus)
{
	std::shared_ptr<const FClip> Clip = TheBank()->Pick(S(Sound), Rng);
	if (!Clip) return 0;
	FPlay P;
	P.Clip = Clip;
	P.bLoop = true;
	P.b3D = false;
	P.Bus = Bus;
	P.Db = -80.0;
	P.StartAt = Uni(0.0, 1.0) * Clip->Seconds() * 0.9;
	return Mixer->Play(P);
}

void UTorqueSfx::Fade(int32 Voice, double& Level, double Target, float Dt, double Db)
{
	Level = FMath::FInterpConstantTo(Level, Target, (double)Dt, 0.8);
	if (Voice) Mixer->SetDb(Voice, Level > 0.01 ? LinToDb(Level) + Db : -80.0);
}

void UTorqueSfx::TickAmbience(float Dt)
{
	const UTorqueBuildSubsystem* B = UTorqueBuildSubsystem::Of(this);
	const bool bIndoors = !ListenerRoomName.IsEmpty();
	const float Amb = FMath::Clamp(FTorqueSettings::Get().Ambience, 0.0f, 1.0f);
	// Room tone, the strip lights' hum in a lit room, the wind outside (sfx_ambience.gd).
	if (!ToneVoice) ToneVoice = Bed(TEXT("tone_shed"), EBus::Sfx);
	if (!HumVoice) HumVoice = Bed(TEXT("hum_fluoro"), EBus::Sfx);
	if (!WindVoice) WindVoice = Bed(TEXT("amb_out"), EBus::AmbOut);
	const bool bDynoLit = !B || B->DynoRoom().bLights;
	const bool bLit = ListenerRoomName != TEXT("dyno") || bDynoLit;
	Fade(ToneVoice, ToneLevel, bIndoors ? Amb : 0.0, Dt, -12.0);
	Fade(HumVoice, HumLevel, bIndoors && bLit ? Amb : 0.0, Dt, -10.0);
	Fade(WindVoice, WindLevel, Amb, Dt, -8.0);
	// A fluorescent ticking on when the dyno room's lights go on while you're in it.
	if (bLitKnown && bDynoLit && !bDynoLitSeen && ListenerRoomName == TEXT("dyno"))
		PlayAt(TEXT("tube_start"), Ear + FVector(0.0, 0.0, 150.0), -10.0f);
	bDynoLitSeen = bDynoLit;
	bLitKnown = true;
	// Indoors the outdoors comes through the walls, dull and quiet; less so with the roller door up.
	const bool bOpen = ListenerRoomName == TEXT("shed") && bDoorOpen;
	const double CutTo = !bIndoors ? 20000.0 : (bOpen ? 2500.0 : 700.0);
	const double DbTo = !bIndoors ? 0.0 : (bOpen ? -6.0 : -12.0);
	const double K = FMath::Min((double)Dt * 3.0, 1.0);
	AmbCut = FMath::Lerp(AmbCut, CutTo, K);
	AmbDb = FMath::Lerp(AmbDb, DbTo, K);
	Mixer->SetAmbOut(AmbCut, AmbDb);
	// The compressor in the back corner: rests, then runs; sooner while air tools draw on it.
	if (!CompLoop) CompLoop = MakeLoopAt(CompressorAt, -9.0f, 300.0f);
	bool bAir = false;
	for (const auto& KV : Loops) bAir |= KV.Value.Voice && (KV.Value.Sound == TEXT("spray_gun") || KV.Value.Sound == TEXT("blast"));
	CompTimer -= Dt * (!bCompOn && bAir ? AirDrain : 1.0);
	if (CompTimer <= 0.0)
	{
		bCompOn = !bCompOn;
		CompTimer = bCompOn ? Uni(CompRun[0], CompRun[1]) : Uni(CompRest[0], CompRest[1]);
		SetLoop(CompLoop, bCompOn ? TEXT("compressor") : TEXT(""));
	}
	// The roof creaking now and then over the shed, the store and the dyno room.
	CreakTimer -= Dt;
	if (CreakTimer <= 0.0)
	{
		CreakTimer = Uni(15.0, 45.0);
		const FString& R = ListenerRoomName;
		FBox Box(ForceInit);
		if (R == TEXT("shed")) Box = FBox(FVector(ShedX0, ShedY0, 430.0), FVector(ShedX1, ShedY1, 430.0));
		else if (R == TEXT("storage")) Box = FBox(FVector(-1500.0, -1100.0, 320.0), FVector(-680.0, -280.0, 320.0));
		else if (R == TEXT("dyno")) Box = FBox(FVector(-450.0, -1300.0, 320.0), FVector(450.0, -580.0, 320.0));
		if (Box.IsValid) PlayAt(TEXT("creak"), FVector(Uni(Box.Min.X, Box.Max.X), Uni(Box.Min.Y, Box.Max.Y), Box.Min.Z), -16.0f);
	}
}

void UTorqueSfx::TickKit(float Dt)
{
	const UTorqueBuildSubsystem* B = UTorqueBuildSubsystem::Of(this);
	if (!B) return;
	const FTorqueDynoRoom& Kit = B->DynoRoom();
	// The switches by the dyno room's door, whoever flips them.
	if (bKitKnown && (Kit.bFan != bFanSeen || Kit.bLights != bLightsSeen)) PlayAt(TEXT("light_switch"), SwitchesAt, -4.0f);
	if (bRadioKnown && B->RadioOn() != bRadioSeen) PlayAt(TEXT("switch"), TorqueShedRadio::At + FVector(0.0, 0.0, 12.0), -6.0f);
	bFanSeen = Kit.bFan;
	bLightsSeen = Kit.bLights;
	bRadioSeen = B->RadioOn();
	bKitKnown = bRadioKnown = true;

	// A run on the rig: the drum's surface speed from the revs through the Speed Twin's gearing.
	float Rpm = 0.0f, Progress = 0.0f;
	int32 Gear = 0;
	const bool bLive = ATorqueDynoRoom::Live(this, Rpm, Gear, Progress) && Gear > 0;
	const double Surface = bLive ? Rpm * UE_TWO_PI / 60.0 / (Primary * FinalDrive * Gears[FMath::Clamp(Gear, 1, 6) - 1]) * RearRadiusM : 0.0;

	// The cooling fan (dyno_room.gd): winds up when switched on, pushed harder as a run builds.
	if (!FanLoop) FanLoop = MakeLoopAt(FanAt, FanDb);
	if (Kit.bFan && FanSpeed <= 0.0f) PlayAt(TEXT("fan_build"), FanAt, FanDb);
	if (bLive && !bRunSeen && Kit.bFan) PlayAt(TEXT("fan_build"), FanAt, FanDb);
	FanSpeed = FMath::FInterpConstantTo(FanSpeed, Kit.bFan ? 25.0f : 0.0f, Dt, Kit.bFan ? 12.0f : 5.0f);
	FanPush = FMath::FInterpConstantTo(FanPush, Kit.bFan ? FMath::Clamp((float)FMath::Abs(Surface) / 45.0f, 0.0f, 1.0f) : 0.0f, Dt, 0.6f);
	SetLoop(FanLoop, FanSpeed > 2.0f ? TEXT("fan") : TEXT(""));
	SetLoopPitch(FanLoop, FMath::Clamp(FanSpeed / 25.0f, 0.4f, 1.0f) * (1.0f + 0.15f * FanPush));
	SetLoopDb(FanLoop, FanDb + 5.0f * FanPush);
	// The drum's whine, pitched with its surface speed (made at 60 km/h).
	if (!RollerLoop) RollerLoop = MakeLoopAt(DrumAt, -6.0f);
	const bool bTurning = FMath::Abs(Surface) > 1.0;
	SetLoop(RollerLoop, bTurning ? TEXT("dyno_roller") : TEXT(""));
	if (bTurning) SetLoopPitch(RollerLoop, FMath::Clamp((float)FMath::Abs(Surface) / 16.7f, 0.3f, 3.0f));
	// The engine on the rig: the ride's engine sound, flat out through the gears as the run goes.
	if (bLive && !DynoVoice)
	{
		auto Gen = std::make_shared<FRideGen>();
		Gen->Sound.Ambience = false;
		Gen->Sound.WindLevel = 0.0;
		Gen->Sound.EngineLevel = FMath::Clamp(FTorqueSettings::Get().Engine, 0.0f, 1.0f);
		TArray<uint8> Bytes;
		auto Wav = [&Bytes](const TCHAR* File, int& OutRate) {
			std::vector<float> Out;
			if (FFileHelper::LoadFileToArray(Bytes, *FPaths::Combine(FPaths::ProjectDir(), TEXT("Data/Audio"), File), FILEREAD_Silent))
				Out = TorqueSound::ReadWav(std::vector<uint8_t>(Bytes.GetData(), Bytes.GetData() + Bytes.Num()), OutRate);
			return Out;
		};
		int WavRate = 0;
		std::vector<float> Idle = Wav(TEXT("idle_parallel270.wav"), WavRate);
		if (!Idle.empty()) Gen->Sound.SetIdle(std::move(Idle), WavRate, 1319.0);
		for (int32 I = 1; I <= 8; ++I)
		{
			std::vector<float> Clunk = Wav(*FString::Printf(TEXT("gearclunk%d.wav"), I), WavRate);
			if (!Clunk.empty()) Gen->Sound.AddClunk(std::move(Clunk), WavRate);
		}
		DynoEngine = Gen;
		DynoGearSeen = Gear;
		FPlay P;
		P.Gen = Gen;
		P.At = V(Rig + FVector(0.0, 40.0, 60.0));
		P.Unit = 600.0;
		P.MaxDistance = 0.0;
		P.Db = Occlusion(Rig);
		P.Bus = BusFor(Rig);
		P.FadeIn = 0.3;
		DynoVoice = Mixer->Play(P);
	}
	if (DynoEngine)
	{
		FRideGen* Gen = static_cast<FRideGen*>(DynoEngine.get());
		std::lock_guard<std::mutex> G(Gen->Lock);
		Gen->In.Rpm = bLive ? Rpm : 1100.0;
		Gen->In.Throttle = bLive ? 1.0 : 0.0;
		Gen->In.Inside = 0.0;
		if (bLive && Gear != DynoGearSeen)
		{
			Gen->Shifts += Gear > DynoGearSeen ? 1 : -1;
			DynoGearSeen = Gear;
		}
	}
	if (DynoVoice)
	{
		Mixer->SetBus(DynoVoice, BusFor(Rig));
		Mixer->SetDb(DynoVoice, Occlusion(Rig));
	}
	if (!bLive && bRunSeen && DynoVoice)
	{
		Mixer->Stop(DynoVoice, 1.2);   // the revs drop away as the run ends
		DynoVoice = 0;
		DynoEngine.reset();
	}
	bRunSeen = bLive;
	// The mixing bay's shaker while a tin's in it.
	if (!ShakerLoop) ShakerLoop = MakeLoopAt(TorquePaintBayLayout::ShakerAt);
	const FTorquePaintBay& Bay = B->PaintBay();
	SetLoop(ShakerLoop, Bay.ShakerTin != 0 && Bay.ShakerLeft > 0.0f ? TEXT("shaker") : TEXT(""));
}
