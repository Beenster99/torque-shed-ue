#include "Shed/TorqueShedRadio.h"

#include "Shed/TorqueBuildSubsystem.h"
#include "Shed/TorqueToolModels.h"
#include "Coop/TorqueSettings.h"
#include "Components/AudioComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/PlatformTime.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/ScopeLock.h"
#include "TorqueAudio/RadioStream.h"
#include "Sound/SoundWaveProcedural.h"
#include "UObject/ConstructorHelpers.h"

namespace TorqueShedRadioImpl
{
constexpr double Timeout = 12.0;      // seconds to tune in before it's no signal (radio_stream.gd TIMEOUT)
constexpr double Retry = 15.0;        // before trying a dead station again (RETRY)
constexpr double Settle = 0.35;       // the dial left alone this long before it connects (clicking through doesn't open a stream per click)
constexpr double Starved = 6.0;       // on air but nothing to play for this long: the stream's stopped
constexpr double Lead = 2.0;          // seconds of sound kept queued ahead (rides out a slow frame)
constexpr double StartWith = 0.5;     // queued before it starts playing
constexpr int32 PipeMax = 4 * 1024 * 1024;   // undecoded bytes held at most (a stuck game, not a fast stream)
constexpr int32 HissRate = 22050;
const TCHAR* const Agent = TEXT("TorqueShed/1.0");
// How it carries (shed_radio.gd: unit_size 7 m, max_distance 70 m).
void Carry(FSoundAttenuationSettings& A)
{
	A.bAttenuate = true;
	A.bSpatialize = true;
	A.AttenuationShape = EAttenuationShape::Sphere;
	A.AttenuationShapeExtents = FVector(700.0, 0.0, 0.0);
	A.FalloffDistance = 6300.0f;
}
}

namespace TSRI = TorqueShedRadioImpl;

const TArray<TorqueShedRadio::FStation>& TorqueShedRadio::Stations()
{
	static TArray<FStation> L;
	if (L.Num() > 0) return L;
	auto Add = [](const TCHAR* Name, const TCHAR* Genre, const TCHAR* Id)
	{
		FStation S;
		S.Name = Name;
		S.Genre = Genre;
		S.Url = FString::Printf(TEXT("https://ice1.somafm.com/%s-128-mp3"), Id);
		L.Add(S);
	};
	Add(TEXT("Groove Salad"), TEXT("Ambient and downtempo"), TEXT("groovesalad"));
	Add(TEXT("Left Coast 70s"), TEXT("Mellow album rock"), TEXT("seventies"));
	Add(TEXT("Underground 80s"), TEXT("Synthpop and new wave"), TEXT("u80s"));
	Add(TEXT("Indie Pop Rocks!"), TEXT("Indie pop and rock"), TEXT("indiepop"));
	Add(TEXT("Boot Liquor"), TEXT("Americana roots"), TEXT("bootliquor"));
	Add(TEXT("Seven Inch Soul"), TEXT("Vintage soul"), TEXT("7soul"));
	Add(TEXT("Secret Agent"), TEXT("Spy lounge"), TEXT("secretagent"));
	Add(TEXT("Metal Detector"), TEXT("Heavy metal"), TEXT("metal"));
	Add(TEXT("Folk Forward"), TEXT("Indie folk"), TEXT("folkfwd"));
	Add(TEXT("Sonic Universe"), TEXT("Jazz"), TEXT("sonicuniverse"));
	Add(TEXT("Beat Blender"), TEXT("Deep house"), TEXT("beatblender"));
	Add(TEXT("Drone Zone"), TEXT("Atmospheric ambient"), TEXT("dronezone"));
	return L;
}

ATorqueShedRadio::ATorqueShedRadio()
{
	PrimaryActorTick.bCanEverTick = true;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	CubeMesh = Cube.Object;
	CylinderMesh = Cylinder.Object;
}

UStaticMeshComponent* ATorqueShedRadio::Piece(UStaticMesh* Mesh, const FVector& Where, const FVector& SizeCm, const FRotator& Rot,
	const FLinearColor& Colour, float Metallic, float Roughness, float Emissive)
{
	if (!Mesh) return nullptr;
	UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
	C->SetStaticMesh(Mesh);
	C->SetMaterial(0, TorqueToolModels::Finish(this, Colour, Metallic, Roughness, Emissive));
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetGenerateOverlapEvents(false);
	C->SetupAttachment(GetRootComponent());
	C->SetRelativeLocationAndRotation(Where, Rot);
	C->SetRelativeScale3D(SizeCm / 100.0);   // the basic shapes are a metre across
	C->RegisterComponent();
	return C;
}

void ATorqueShedRadio::BeginPlay()
{
	Super::BeginPlay();
	SetActorLocationAndRotation(TorqueShedRadio::At, FRotator::ZeroRotator);
	// The garage props' radio model (Tools/ue_import_props.py dresses the level with it, over the
	// shelf, when it's been made): then it's that, and this only plays it and shows what's on.
	auto Has = [](const TCHAR* Path) { return LoadObject<UStaticMesh>(nullptr, Path, nullptr, LOAD_NoWarn | LOAD_Quiet) != nullptr; };
	// beenster's Fab radio on our shelf, when both are in (Data/Props/swaps.json 'prefer', as dress() does).
	const bool bFab = Has(TEXT("/Game/Shed/Props/fab_radio/SM_fab_radio.SM_fab_radio")) && Has(TEXT("/Game/Shed/Props/radio_shelf/SM_radio_shelf.SM_radio_shelf"));
	const bool bModel = bFab || Has(TEXT("/Game/Shed/Props/shed_radio/SM_shed_radio.SM_shed_radio"));
	// Else shed_radio.gd _ready, in cm: its front faces +x, its width along y (Torque Shed's x is our -y).
	const FRotator None = FRotator::ZeroRotator;
	if (!bModel)
	{
		Piece(CubeMesh, FVector(0.0, 0.0, 13.0), FVector(16.0, 46.0, 26.0), None, FLinearColor(0.42f, 0.12f, 0.08f), 0.1f, 0.45f);   // the cabinet
		Piece(CubeMesh, FVector(8.1, 10.0, 13.0), FVector(1.0, 20.0, 18.0), None, FLinearColor(0.75f, 0.68f, 0.52f), 0.0f, 0.9f);    // the grille cloth
		for (int32 I = 0; I < 5; ++I)
			Piece(CubeMesh, FVector(8.7, 10.0, 6.0 + 3.5 * I), FVector(0.4, 20.0, 0.6), None, FLinearColor(0.8f, 0.78f, 0.72f), 1.0f, 0.25f);
		Piece(CubeMesh, FVector(8.2, -12.0, 17.0), FVector(0.6, 17.0, 7.0), None, FLinearColor(0.95f, 0.85f, 0.55f), 0.0f, 0.4f);    // the dial
		for (double Y : {-7.0, -17.0})   // the knobs
			Piece(CylinderMesh, FVector(9.0, Y, 8.0), FVector(4.8, 4.8, 2.5), FRotator(90.0, 0.0, 0.0), FLinearColor(0.1f, 0.08f, 0.07f), 0.2f, 0.4f);
		Piece(CylinderMesh, FVector(-4.0, -20.0, 45.0), FVector(0.7, 0.7, 45.0), FRotator(0.0, 0.0, 20.0), FLinearColor(0.85f, 0.86f, 0.88f), 1.0f, 0.15f);   // the aerial
	}
	// The dial's glowing needle, along the dial window (the model's: just in front of its glass, as the
	// props thread gave it; the drawn one's: on its dial).
	NeedleX = bModel ? 15.7 : 8.6;
	if (bFab)
	{
		// Its dial window (Data/Props/fab.json): face 8.4 cm in front of the shelf's middle (so 13.4
		// from here), 15.8 .. 24.2 cm up; the needle sweeps its scale's 0 .. 90, Torque Shed x -0.125 .. 0.03 (short of the tuning box).
		NeedleX = 13.7;
		NeedleY0 = -12.5;
		NeedleSpan = 15.5;
		NeedleZ = 20.0;
		NeedleH = 7.5;
	}
	UE_LOG(LogTemp, Log, TEXT("Torque radio: on its shelf (%s), %d stations"), bFab ? TEXT("the Fab model") : bModel ? TEXT("the props' model") : TEXT("drawn"), TorqueShedRadio::Stations().Num());
	Needle = Piece(CubeMesh, FVector(NeedleX, -NeedleY0, NeedleZ), FVector(0.4, 0.5, NeedleH), None, FLinearColor(1.0f, 0.6f, 0.2f), 0.0f, 0.5f, 4.0f);
	// The station and the song over it.
	Display = NewObject<UTextRenderComponent>(this);
	Display->SetupAttachment(GetRootComponent());
	Display->RegisterComponent();
	Display->SetRelativeLocationAndRotation(FVector(5.0, 0.0, 42.0), FRotator::ZeroRotator);
	Display->SetHorizontalAlignment(EHTA_Center);
	Display->SetVerticalAlignment(EVRTA_TextBottom);
	Display->SetWorldSize(4.5f);
	Display->SetTextRenderColor(FColor(255, 217, 140));
	Display->SetCastShadow(false);
	Display->SetText(FText::GetEmpty());

	// The speaker: the station's sound (MusicWave, made when a station's first sound decodes), from the grille.
	Speaker = NewObject<UAudioComponent>(this);
	Speaker->bAutoActivate = false;
	Speaker->SetupAttachment(GetRootComponent());
	Speaker->SetRelativeLocation(FVector(8.0, 10.0, 13.0));
	Speaker->bAllowSpatialization = true;
	Speaker->bOverrideAttenuation = true;
	TSRI::Carry(Speaker->AttenuationOverrides);
	Speaker->RegisterComponent();
	Speaker->SetVolumeMultiplier(0.7f);
	// The static between stations (or with no signal), made as it plays.
	HissWave = NewObject<USoundWaveProcedural>(this);
	HissWave->SetSampleRate(TSRI::HissRate);
	HissWave->NumChannels = 1;
	HissWave->Duration = INDEFINITELY_LOOPING_DURATION;
	HissWave->SoundGroup = SOUNDGROUP_Default;
	HissWave->bLooping = false;
	Hiss = NewObject<UAudioComponent>(this);
	Hiss->bAutoActivate = false;
	Hiss->SetupAttachment(GetRootComponent());
	Hiss->SetRelativeLocation(FVector(8.0, 10.0, 13.0));
	Hiss->bAllowSpatialization = true;
	Hiss->bOverrideAttenuation = true;
	TSRI::Carry(Hiss->AttenuationOverrides);
	Hiss->RegisterComponent();
	Hiss->SetSound(HissWave);
	Hiss->SetVolumeMultiplier(0.12f);
	Sync();
}

void ATorqueShedRadio::EndPlay(const EEndPlayReason::Type Why)
{
	StopStream();
	if (Speaker) Speaker->Stop();
	if (Hiss) Hiss->Stop();
	Super::EndPlay(Why);
}

FString ATorqueShedRadio::NowPlaying(const UObject* WorldContext)
{
	const UTorqueBuildSubsystem* B = UTorqueBuildSubsystem::Of(WorldContext);
	const TArray<TorqueShedRadio::FStation>& L = TorqueShedRadio::Stations();
	if (!B || !B->RadioOn() || !L.IsValidIndex(B->RadioStation())) return TEXT("Off");
	const TorqueShedRadio::FStation& S = L[B->RadioStation()];
	FString Line = FString::Printf(TEXT("%s  ·  %s"), *S.Name, *S.Genre);
	UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	if (World)
		for (TActorIterator<ATorqueShedRadio> It(World); It; ++It)
		{
			if (!It->Title.IsEmpty() && It->bLive) Line += TEXT("\n") + It->Title;
			else if (!It->Status.IsEmpty() && It->Status != TEXT("On air")) Line += TEXT("\n") + It->Status;
			break;
		}
	return Line;
}

void ATorqueShedRadio::Sync()
{
	// Matches the shed's radio: switches on or off and retunes when the station changes.
	const UTorqueBuildSubsystem* B = UTorqueBuildSubsystem::Of(this);
	const bool bWantOn = B && B->RadioOn() && TorqueShedRadio::Stations().IsValidIndex(B->RadioStation());
	const int32 Want = B ? B->RadioStation() : 0;
	const FString NewSig = FString::Printf(TEXT("%d|%d"), bWantOn ? 1 : 0, Want);
	if (NewSig == Sig) return;
	UE_LOG(LogTemp, Log, TEXT("Torque radio: the shed says %s, station %d"), bWantOn ? TEXT("on") : TEXT("off"), Want);
	Sig = NewSig;
	bOn = bWantOn;
	Station = Want;
	if (bOn) Tune();
	else StopAll();
	UpdateLooks();
}

void ATorqueShedRadio::Tune()
{
	StopAll();
	Status = TEXT("Tuning in...");
	TunedAt = FPlatformTime::Seconds();
	RetryAt = 0.0;
	// Static while the dial turns; the station once it's been left on one for a moment.
	ConnectAt = TunedAt + TSRI::Settle;
	if (Hiss) Hiss->Play();
}

void ATorqueShedRadio::Connect()
{
	ConnectAt = 0.0;
	StopStream();
	const TorqueShedRadio::FStation& S = TorqueShedRadio::Stations()[Station];
	UE_LOG(LogTemp, Log, TEXT("Torque radio: tuning to %s (%s)"), *S.Name, *S.Url);
	TunedAt = FPlatformTime::Seconds();
	TSharedPtr<FTorqueRadioPipe, ESPMode::ThreadSafe> Box = MakeShared<FTorqueRadioPipe, ESPMode::ThreadSafe>();
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Req = FHttpModule::Get().CreateRequest();
	Req->SetURL(S.Url);
	Req->SetVerb(TEXT("GET"));
	Req->SetHeader(TEXT("User-Agent"), TSRI::Agent);
	Req->SetHeader(TEXT("Icy-MetaData"), TEXT("1"));
	// Everything on the HTTP thread, so the headers (icy-metaint) are in before the first bytes.
	// Only the mailbox is captured: a request dropped mid-stream can still call these safely.
	Req->SetDelegateThreadPolicy(EHttpRequestDelegateThreadPolicy::CompleteOnHttpThread);
	Req->OnHeaderReceived().BindLambda([Box](FHttpRequestPtr, const FString& Name, const FString& Value)
	{
		if (!Name.Equals(TEXT("icy-metaint"), ESearchCase::IgnoreCase)) return;
		FScopeLock Hold(&Box->Lock);
		Box->MetaInt = FCString::Atoi(*Value);
	});
	Req->SetResponseBodyReceiveStreamDelegateV2(FHttpRequestStreamDelegateV2::CreateLambda([Box](void* Ptr, int64& Length)
	{
		FScopeLock Hold(&Box->Lock);
		if (!Ptr || Length <= 0) return;
		Box->Total += Length;
		if (Box->Bytes.Num() + Length > TSRI::PipeMax) { Box->bOverflow = true; return; }
		Box->Bytes.Append((const uint8*)Ptr, (int32)Length);
	}));
	Req->OnProcessRequestComplete().BindLambda([Box](FHttpRequestPtr, FHttpResponsePtr, bool)
	{
		FScopeLock Hold(&Box->Lock);
		Box->bEnded = true;
	});
	Pipe = Box;
	StreamRequest = Req;
	Decoder = MakeShared<TorqueRadio::FIcyMp3Decoder>();
	if (!Req->ProcessRequest()) Fail(TEXT("the download wouldn't start"));
}

void ATorqueShedRadio::Pump()
{
	// The stream's new bytes into the decoder, and enough decoded sound queued to play.
	if (!Pipe.IsValid() || !Decoder.IsValid()) return;
	TArray<uint8> Got;
	int32 MetaInt = -1;
	bool bEnded = false, bOverflow = false;
	int64 Total = 0;
	{
		FScopeLock Hold(&Pipe->Lock);
		MetaInt = Pipe->MetaInt;
		Total = Pipe->Total;
		bEnded = Pipe->bEnded;
		bOverflow = Pipe->bOverflow;
		// Held until the headers say whether there's metadata in it (they come first; 64 KB in, there's none).
		if (MetaInt >= 0 || Total >= 65536 || bEnded) Got = MoveTemp(Pipe->Bytes);
	}
	if (bOverflow) { Fail(TEXT("the stream came faster than it played")); return; }
	if (Got.Num() > 0)
	{
		if (!Decoder->Started()) Decoder->SetMetaInt(FMath::Max(0, MetaInt));
		Decoder->Push(Got.GetData(), (size_t)Got.Num());
	}
	std::string Said;
	if (Decoder->TakeTitle(Said))
	{
		Title = UTF8_TO_TCHAR(Said.c_str());
		UE_LOG(LogTemp, Log, TEXT("Torque radio: now playing %s"), *Title);
	}
	// Decode up to the lead: a few frames per tick, never a wait.
	const int32 Rate = MusicWave ? Decoder->SampleRate() : 0;
	const int32 Queued = (MusicWave && Rate > 0) ? MusicWave->GetAvailableAudioByteCount() / (int32)(sizeof(int16) * Decoder->Channels()) : 0;
	const int32 Room = Rate > 0 ? (int32)(TSRI::Lead * Rate) - Queued : 4608;
	if (Room > 0)
	{
		std::vector<int16_t> Pcm;
		const int32 Made = Decoder->Decode(Pcm, Room);
		if (Made > 0)
		{
			if (!MusicWave)
			{
				MusicWave = NewObject<USoundWaveProcedural>(this);
				MusicWave->SetSampleRate(Decoder->SampleRate());
				MusicWave->NumChannels = Decoder->Channels();
				MusicWave->Duration = INDEFINITELY_LOOPING_DURATION;
				MusicWave->SoundGroup = SOUNDGROUP_Music;
				MusicWave->bLooping = false;
				UE_LOG(LogTemp, Log, TEXT("Torque radio: decoding %s at %d Hz, %d channel(s)"), *TorqueShedRadio::Stations()[Station].Name,
					Decoder->SampleRate(), Decoder->Channels());
			}
			MusicWave->QueueAudio((const uint8*)Pcm.data(), (int32)(Pcm.size() * sizeof(int16)));
		}
	}
	const double Now = FPlatformTime::Seconds();
	const int32 Ahead = (MusicWave && Decoder->SampleRate() > 0) ? MusicWave->GetAvailableAudioByteCount() / (int32)(sizeof(int16) * Decoder->Channels()) : 0;
	if (!bLive && MusicWave && Ahead >= (int32)(TSRI::StartWith * Decoder->SampleRate()))
	{
		bLive = true;
		Status = TEXT("On air");
		StarvedAt = 0.0;
		if (Hiss) Hiss->Stop();
		if (Speaker)
		{
			Speaker->SetSound(MusicWave);
			Speaker->Play();
		}
		UE_LOG(LogTemp, Log, TEXT("Torque radio: on air: %s (%.1f s after tuning, %lld bytes in)"), *TorqueShedRadio::Stations()[Station].Name, Now - TunedAt, Total);
	}
	if (bLive)
	{
		// Running dry: fine for a moment (a slow frame), not for long.
		if (Ahead > 0) StarvedAt = 0.0;
		else if (StarvedAt == 0.0) StarvedAt = Now;
		else if (bEnded && Now - TunedAt > 30.0)
		{
			// The server (or a timeout of the download's) ended a stream that had been playing a while:
			// pick it straight up again, no waiting out a retry.
			UE_LOG(LogTemp, Log, TEXT("Torque radio: %s's stream ended after %.0f s; reconnecting"), *TorqueShedRadio::Stations()[Station].Name, Now - TunedAt);
			StopStream();
			bLive = false;
			Status = TEXT("Tuning in...");
			StarvedAt = 0.0;
			ConnectAt = Now;
			if (Hiss) Hiss->Play();
		}
		else if (Now - StarvedAt > TSRI::Starved || bEnded) Fail(bEnded ? TEXT("the stream ended") : TEXT("the stream stopped"));
	}
	else if (bEnded && Decoder->Pending() < 1441) Fail(TEXT("the station hung up"));
	else if (Now - TunedAt > TSRI::Timeout) Fail(Total > 0 ? TEXT("it couldn't make sense of the stream") : TEXT("nothing came"));
}

void ATorqueShedRadio::StopStream()
{
	// Drops the download without waiting for it (the HTTP thread finishes cancelling it in its own
	// time). Its callbacks stay bound: they run on the HTTP thread and only touch the old mailbox.
	if (StreamRequest.IsValid()) StreamRequest->CancelRequest();
	StreamRequest.Reset();
	Pipe.Reset();
	Decoder.Reset();
	if (Speaker) Speaker->Stop();
	MusicWave = nullptr;
}

void ATorqueShedRadio::StopAll()
{
	StopStream();
	if (Hiss) Hiss->Stop();
	bLive = false;
	Title.Reset();
	Status.Reset();
	RetryAt = 0.0;
	ConnectAt = 0.0;
	StarvedAt = 0.0;
}

void ATorqueShedRadio::Fail(const TCHAR* Why)
{
	StopStream();
	bLive = false;
	Status = TEXT("No signal");
	RetryAt = FPlatformTime::Seconds() + TSRI::Retry;
	ConnectAt = 0.0;
	if (Hiss && !Hiss->IsPlaying()) Hiss->Play();
	UE_LOG(LogTemp, Warning, TEXT("Torque radio: no signal from %s: %s"), *TorqueShedRadio::Stations()[Station].Name, Why);
}

void ATorqueShedRadio::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Sync();
	// Settings > Sound > Radio volume.
	{
		const float Vol = FMath::Clamp(FTorqueSettings::Get().Radio, 0.0f, 1.0f);
		if (Speaker && !FMath::IsNearlyEqual(SpeakerVolume, 0.7f * Vol)) { SpeakerVolume = 0.7f * Vol; Speaker->SetVolumeMultiplier(SpeakerVolume); }
		if (Hiss && !FMath::IsNearlyEqual(Hiss->VolumeMultiplier, 0.12f * Vol)) Hiss->SetVolumeMultiplier(0.12f * Vol);
	}
	if (!bOn)
	{
		UpdateLooks();
		return;
	}
	const double Now = FPlatformTime::Seconds();
	if (RetryAt > 0.0)
	{
		if (Now >= RetryAt) Tune();
	}
	else if (ConnectAt > 0.0)
	{
		if (Now >= ConnectAt) Connect();
	}
	else Pump();
	FeedHiss(DeltaSeconds);
	UpdateLooks();
}

void ATorqueShedRadio::FeedHiss(float Dt)
{
	// Static under the tuning and with no signal; quiet once it's on air.
	if (!HissWave || !Hiss || !Hiss->IsPlaying()) return;
	if (bLive) return;
	const int32 Have = HissWave->GetAvailableAudioByteCount() / (int32)sizeof(int16);
	const int32 Want = FMath::Clamp((int32)(TSRI::HissRate * FMath::Min(Dt, 0.1f)) + TSRI::HissRate / 10 - Have, 0, TSRI::HissRate / 2);
	if (Want <= 0) return;
	TArray<int16> Samples;
	Samples.SetNumUninitialized(Want);
	for (int32 I = 0; I < Want; ++I)
	{
		HissSeed = HissSeed * 1664525u + 1013904223u;
		const float White = ((HissSeed >> 9) & 0xFFFF) / 32768.0f - 1.0f;
		HissLow += (White - HissLow) * 0.35f;   // softened a little: a radio's hiss, not a jet
		Samples[I] = (int16)FMath::Clamp(HissLow * 9000.0f, -32767.0f, 32767.0f);
	}
	HissWave->QueueAudio((const uint8*)Samples.GetData(), Samples.Num() * (int32)sizeof(int16));
}

void ATorqueShedRadio::UpdateLooks()
{
	const int32 N = TorqueShedRadio::Stations().Num();
	if (Needle)
	{
		Needle->SetVisibility(bOn);
		const double F = N > 1 ? (double)Station / (N - 1) : 0.0;
		Needle->SetRelativeLocation(FVector(NeedleX, -(NeedleY0 + NeedleSpan * F), NeedleZ));
	}
	if (Display)
	{
		FString Text;
		if (bOn && TorqueShedRadio::Stations().IsValidIndex(Station))
		{
			Text = TorqueShedRadio::Stations()[Station].Name;
			if (bLive && !Title.IsEmpty()) Text += TEXT("\n") + Title.Left(48);
			else if (!bLive && !Status.IsEmpty()) Text += TEXT("\n") + Status;
		}
		if (!Shown.Equals(Text, ESearchCase::CaseSensitive))
		{
			Shown = Text;
			Display->SetText(FText::FromString(Text));
		}
	}
}
