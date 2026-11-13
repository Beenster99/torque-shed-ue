#include "Shed/TorqueShedScreens.h"

#include "Shed/TorqueBuildSubsystem.h"
#include "Shed/TorqueShedParts.h"
#include "Shed/TorqueShedPower.h"
#include "Shed/TorqueDynoRoom.h"
#include "Shed/TorqueFurniture.h"
#include "Shed/TorqueWalker.h"
#include "Coop/TorquePlayerState.h"
#include "Coop/TorqueSettings.h"
#include "Coop/TorqueUi.h"
#include "TorqueBikePawn.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Camera/PlayerCameraManager.h"
#include "WebBrowserModule.h"
#include "IWebBrowserSingleton.h"
#include "IWebBrowserWindow.h"
#include "SWebBrowserView.h"
#include "Framework/Application/SlateApplication.h"
#include "Slate/WidgetRenderer.h"
#include "RenderingThread.h"
#include "CanvasItem.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "MeshDescription.h"
#include "RenderUtils.h"
#include "StaticMeshAttributes.h"
#include "UObject/Package.h"

namespace TSSI
{
// Torque Shed's dashboard.gd and tv_channels.gd colours (sRGB, as Godot draws them).
FLinearColor Srgb(float R, float G, float B, float A = 1.0f)
{
	FLinearColor C = FLinearColor::FromSRGBColor(FLinearColor(R, G, B).ToFColor(false));
	C.A = A;
	return C;
}
const FLinearColor Bg() { return Srgb(0.03f, 0.05f, 0.07f); }
const FLinearColor Good() { return Srgb(0.35f, 0.9f, 0.45f); }
const FLinearColor Bad() { return Srgb(1.0f, 0.35f, 0.25f); }
const FLinearColor Dim() { return Srgb(1.0f, 1.0f, 1.0f, 0.72f); }
// The page's glow (M_ShedScreenPage's Brightness): 1.6 read dim against the shed's exposure. The
// dyno room is darker, so the eye opens up and its screen reads bright at 4.5; out in the shed, by
// the daylight from the door, the bay screens and the board want more to look the same.
constexpr float PageBrightness = 4.5f;
constexpr float ShedPageBrightness = 9.0f;
// The title band, dashboard.gd's orange (1, 0.55, 0.15). Given as it is, not through Srgb(): the
// canvas writes into the page's sRGB target as given, so Srgb()'s linear value came out a dark red.
const FLinearColor Orange() { return FLinearColor(1.0f, 0.55f, 0.15f, 1.0f); }
const FLinearColor Amber() { return Srgb(1.0f, 0.78f, 0.3f); }
const FLinearColor Ink() { return Srgb(0.05f, 0.05f, 0.05f); }
const FLinearColor Panel() { return Srgb(1.0f, 1.0f, 1.0f, 0.05f); }

// Drawing on a page with Torque Shed's sizes: Size is the font's pixel size, Y its baseline.
struct FPen
{
	UCanvas* C = nullptr;
	UFont* Font = nullptr;
	float LineH = 20.0f;   // the font's line height at scale 1

	void Rect(float X, float Y, float W, float H, const FLinearColor& Colour) const
	{
		if (W <= 0.0f || H <= 0.0f) return;
		FCanvasTileItem T(FVector2D(X, Y), GWhiteTexture, FVector2D(W, H), Colour);
		T.BlendMode = SE_BLEND_Translucent;
		C->DrawItem(T);
	}
	float Scale(float Size) const { return Size * 1.2f / FMath::Max(1.0f, LineH); }
	float Width(const FString& S, float Size) const
	{
		float W = 0.0f, H = 0.0f;
		C->StrLen(Font, S, W, H);
		return W * Scale(Size);
	}
	// Align: 0 left of X, 1 right-aligned in [X, X + Box], 2 centred in it.
	void Text(float X, float Y, const FString& S, float Size, const FLinearColor& Colour, float Box = 0.0f, int32 Align = 0) const
	{
		if (S.IsEmpty() || !Font) return;
		const float K = Scale(Size);
		float At = X;
		if (Align == 1) At = X + Box - Width(S, Size);
		else if (Align == 2) At = X + 0.5f * (Box - Width(S, Size));
		C->SetDrawColor(Colour.ToFColor(true));
		C->DrawText(Font, S, At, Y - Size * 0.95f, K, K);
	}
	// A bar: the empty track, then the filled part.
	void Bar(float X, float Y, float W, float H, float Fraction, const FLinearColor& Colour) const
	{
		Rect(X, Y, W, H, FLinearColor(1, 1, 1, 0.1f));
		Rect(X, Y, W * FMath::Clamp(Fraction, 0.0f, 1.0f), H, Colour);
	}
};

// Shortens to Max characters (Torque Shed's .left(n)).
FString Left(const FString& S, int32 Max) { return S.Len() <= Max ? S : S.Left(Max); }

// Splits into lines of at most Max characters at spaces.
TArray<FString> Wrap(const FString& S, int32 Max)
{
	TArray<FString> Words, Lines;
	S.ParseIntoArray(Words, TEXT(" "));
	FString Line;
	for (const FString& W : Words)
	{
		if (!Line.IsEmpty() && Line.Len() + 1 + W.Len() > Max)
		{
			Lines.Add(Line);
			Line.Reset();
		}
		Line += Line.IsEmpty() ? W : TEXT(" ") + W;
	}
	if (!Line.IsEmpty()) Lines.Add(Line);
	return Lines;
}

FString Plural(int32 N, const TCHAR* One) { return FString::Printf(TEXT("%d %s%s"), N, One, N == 1 ? TEXT("") : TEXT("S")); }

// One row of a bay's part list (workshop.gd _bay_data "parts").
struct FPartRow
{
	FString Name;
	bool bFitted = false, bBolted = false;
	float Health = 0.0f;
};

// What a bay's screen and the crew board say about its bike.
struct FBay
{
	bool bExists = false;
	FString Name, Status;
	FLinearColor StatusColour = FLinearColor::White;
	TArray<FPartRow> Parts;
	int32 Needed = 0, Fitted = 0, Bolted = 0;
	float Health = 0.0f, Rust = 0.0f, Dirt = 0.0f;
	TArray<FString> Owed, Made;
	float Progress = 0.0f;
	FString Next, NextTool;
	TArray<FName> Missing, Loose;
	bool bHasTankPaint = false, bHasFramePaint = false;
	FLinearColor TankPaint = FLinearColor::White, FramePaint = FLinearColor::White;
};

FString SlotList(const TArray<FName>& Slots, int32 Max)
{
	FString Out;
	for (int32 I = 0; I < Slots.Num() && I < Max; ++I)
	{
		if (I > 0) Out += I == FMath::Min(Slots.Num(), Max) - 1 && Slots.Num() <= Max ? TEXT(" and ") : TEXT(", ");
		Out += TorqueShedParts::SlotName(Slots[I]).ToLower();
	}
	if (Slots.Num() > Max) Out += FString::Printf(TEXT(" and %d more"), Slots.Num() - Max);
	return Out;
}

FBay Read(const UTorqueBuildSubsystem* B, int32 Bay)
{
	FBay Out;
	if (!B || !B->Exists(Bay)) return Out;
	const FTorqueShedBike& Bike = B->Bike(Bay);
	Out.bExists = true;
	Out.Name = Bike.Name;
	Out.Missing = B->MissingSlots(Bay);
	Out.Loose = B->LooseSlots(Bay);
	Out.Owed = B->FabOwed(Bay);
	Out.Made = Bike.FabDone;
	// The status, as workshop.gd's _bay_data puts it.
	Out.Status = TEXT("READY TO RIDE");
	Out.StatusColour = Good();
	if (Out.Missing.Num() > 0) { Out.Status = Plural(Out.Missing.Num(), TEXT("PART")) + TEXT(" MISSING"); Out.StatusColour = Bad(); }
	else if (Out.Loose.Num() > 0) { Out.Status = Plural(Out.Loose.Num(), TEXT("PART")) + TEXT(" LOOSE"); Out.StatusColour = Amber(); }
	else if (Out.Owed.Num() > 0) { Out.Status = TEXT("NEEDS FABRICATION"); Out.StatusColour = Amber(); }
	// Every slot the bike shows (engine parts only with an engine in it; empty optional ones left out).
	const FName EngineSlot(TEXT("engine"));
	const bool bEngine = B->ItemOn(Bay, EngineSlot) != 0;
	float HealthSum = 0.0f, RustSum = 0.0f, DirtSum = 0.0f;
	for (const FName& Slot : TorqueShedParts::Slots())
	{
		if (TorqueShedParts::IsEngineSlot(Slot) && !bEngine) continue;
		const int32 Id = B->ItemOn(Bay, Slot);
		if (Id == 0 && TorqueShedParts::MayBeEmpty(Slot)) continue;
		FPartRow Row;
		Row.Name = TorqueShedParts::SlotName(Slot);
		if (Slot == EngineSlot && !bEngine) Row.Name = TEXT("Engine (out)");
		Row.bFitted = Id != 0;
		Row.bBolted = Row.bFitted && B->IsBolted(Bay, Slot);
		if (const FTorqueItem* It = Row.bFitted ? B->Item(Id) : nullptr)
		{
			Row.Health = UTorqueBuildSubsystem::Health(*It);
			HealthSum += Row.Health;
			RustSum += It->Rust;
			DirtSum += It->Dirt;
		}
		++Out.Needed;
		if (Row.bFitted) ++Out.Fitted;
		if (Row.bBolted) ++Out.Bolted;
		Out.Parts.Add(Row);
	}
	if (Out.Fitted > 0)
	{
		Out.Health = HealthSum / Out.Fitted;
		Out.Rust = RustSum / Out.Fitted;
		Out.Dirt = DirtSum / Out.Fitted;
	}
	// How far along: every part on, every part bolted, every fabrication job made.
	const int32 Steps = 2 * Out.Needed + Out.Owed.Num() + Out.Made.Num();
	Out.Progress = Steps > 0 ? float(Out.Fitted + Out.Bolted + Out.Made.Num()) / Steps : 0.0f;
	Out.bHasTankPaint = B->PaintOf(Bay, FName(TEXT("tank")), Out.TankPaint);
	Out.bHasFramePaint = B->PaintOf(Bay, FName(TEXT("frame")), Out.FramePaint);
	// The next thing to do, and which tool does it (the number key).
	if (Out.Missing.Num() > 0)
	{
		Out.Next = FString::Printf(TEXT("Fit the %s: order parts at the computer, unpack them, then hold one and look at the bike."), *SlotList(Out.Missing, 2));
		Out.NextTool = TEXT("1  Hands");
	}
	else if (Out.Loose.Num() > 0)
	{
		Out.Next = FString::Printf(TEXT("Bolt up the %s."), *SlotList(Out.Loose, 3));
		Out.NextTool = TEXT("2  Wrench");
	}
	else if (Out.Owed.Num() > 0)
	{
		Out.Next = FString::Printf(TEXT("Fabricate: %s. Hold the work button at the bike."), *Out.Owed[0]);
		Out.NextTool = TEXT("4  Grinder");
	}
	else if (Out.Fitted > 0 && Out.Health < 0.6f)
	{
		Out.Next = TEXT("Tired parts: unbolt the worst, take them to the repair machine and grind off the rust.");
		Out.NextTool = TEXT("4  Grinder");
	}
	else if (Out.Dirt > 0.3f)
	{
		Out.Next = TEXT("It's filthy: wash it down in the wash bay, or scrub the parts.");
		Out.NextTool = TEXT("5  Pressure wash");
	}
	else
	{
		Out.Next = TEXT("Ready: press F at the bike, or at the roller door, to ride it out.");
		Out.NextTool = TEXT("F  Ride out");
	}
	return Out;
}

// Who's in the shed: their name, colour and what they're doing.
struct FCrewRow
{
	FString Name, Doing;
	FLinearColor Colour = FLinearColor::White;
	bool bMe = false;
	int32 WorkingBay = -1;   // [co-op] the bay their hands are working on now (ATorquePlayerState::Hands)
};

TArray<FCrewRow> Crew(const UWorld* World, const UTorqueBuildSubsystem* B)
{
	TArray<FCrewRow> Out;
	const AGameStateBase* GS = World ? World->GetGameState() : nullptr;
	if (!GS) return Out;
	const APlayerController* PC = World->GetFirstPlayerController();
	const APlayerState* MyState = PC ? PC->PlayerState : nullptr;
	for (const APlayerState* PS : GS->PlayerArray)
	{
		if (!PS) continue;
		FCrewRow Row;
		Row.Name = PS->GetPlayerName();
		Row.bMe = PS == MyState;
		if (Row.Name.IsEmpty()) Row.Name = Row.bMe ? TEXT("You") : TEXT("Rider");
		if (const ATorquePlayerState* TPS = Cast<ATorquePlayerState>(PS))
		{
			const FColor C = TPS->ColourValue();
			Row.Colour = (C.R + C.G + C.B) < 200 ? FLinearColor(0.85f, 0.85f, 0.85f) : FLinearColor(C);
			Row.Doing = TPS->Activity;
			Row.WorkingBay = TPS->WorkingBay();
		}
		if (Row.bMe && B && !B->Activity().IsEmpty()) Row.Doing = B->Activity();
		Out.Add(Row);
	}
	return Out;
}

// A screen's quad: Width x Height cm, centred on Centre, facing Facing, Right the page's +u.
UStaticMesh* Quad(const FVector& Centre, const FVector& Facing, const FVector& Right, float Width, float Height, UMaterialInterface* Material)
{
	const FVector Up = FVector::CrossProduct(Right, Facing).GetSafeNormal() * (FVector::DotProduct(FVector::CrossProduct(Right, Facing), FVector::UpVector) >= 0.0 ? 1.0 : -1.0);
	const FVector Hx = Right * (Width * 0.5), Hy = Up * (Height * 0.5);
	const FVector P[4] = {Centre - Hx + Hy, Centre + Hx + Hy, Centre - Hx - Hy, Centre + Hx - Hy};
	const FVector2f UV[4] = {FVector2f(0, 0), FVector2f(1, 0), FVector2f(0, 1), FVector2f(1, 1)};
	FMeshDescription MD;
	FStaticMeshAttributes Attr(MD);
	Attr.Register();
	TVertexAttributesRef<FVector3f> Positions = Attr.GetVertexPositions();
	TVertexInstanceAttributesRef<FVector3f> Normals = Attr.GetVertexInstanceNormals();
	TVertexInstanceAttributesRef<FVector3f> Tangents = Attr.GetVertexInstanceTangents();
	TVertexInstanceAttributesRef<float> Signs = Attr.GetVertexInstanceBinormalSigns();
	TVertexInstanceAttributesRef<FVector4f> Colours = Attr.GetVertexInstanceColors();
	TVertexInstanceAttributesRef<FVector2f> UVs = Attr.GetVertexInstanceUVs();
	UVs.SetNumChannels(1);
	TPolygonGroupAttributesRef<FName> SlotNames = Attr.GetPolygonGroupMaterialSlotNames();
	const FPolygonGroupID Group = MD.CreatePolygonGroup();
	const FName SlotName(TEXT("Screen"));
	SlotNames[Group] = SlotName;
	FVertexInstanceID Ids[4];
	for (int32 I = 0; I < 4; ++I)
	{
		const FVertexID V = MD.CreateVertex();
		Positions[V] = FVector3f(P[I]);
		Ids[I] = MD.CreateVertexInstance(V);
		Normals[Ids[I]] = FVector3f(Facing);
		Tangents[Ids[I]] = FVector3f(Right);
		Signs[Ids[I]] = 1.0f;
		Colours[Ids[I]] = FVector4f(1.0f, 1.0f, 1.0f, 1.0f);
		UVs.Set(Ids[I], 0, UV[I]);
	}
	// Each triangle wound to face Facing (Unreal's face: -(P1 - P0) x (P2 - P0)).
	const int32 Tris[2][3] = {{0, 1, 2}, {2, 1, 3}};
	for (const auto& T : Tris)
	{
		int32 A = T[0], Bv = T[1], Cv = T[2];
		if (FVector::DotProduct(-FVector::CrossProduct(P[Bv] - P[A], P[Cv] - P[A]), Facing) < 0.0) Swap(Bv, Cv);
		TArray<FVertexInstanceID, TFixedAllocator<3>> Tri;
		Tri.Add(Ids[A]);
		Tri.Add(Ids[Bv]);
		Tri.Add(Ids[Cv]);
		MD.CreateTriangle(Group, Tri);
	}
	UStaticMesh* Mesh = NewObject<UStaticMesh>(GetTransientPackage());
#if WITH_EDITORONLY_DATA
	Mesh->GetStaticMaterials().Add(FStaticMaterial(Material, SlotName, SlotName));
#else
	Mesh->GetStaticMaterials().Add(FStaticMaterial(Material, SlotName));
#endif
	Mesh->GetStaticMaterials().Last().UVChannelData = FMeshUVChannelInfo(1.0f);
	UStaticMesh::FBuildMeshDescriptionsParams Params;
	Params.bBuildSimpleCollision = false;
	Params.bFastBuild = true;
	Params.bCommitMeshDescription = false;
	TArray<const FMeshDescription*> Descriptions;
	Descriptions.Add(&MD);
	if (!Mesh->BuildFromMeshDescriptions(Descriptions, Params)) return nullptr;
	return Mesh;
}

// Where the level's screens are (Tools/ue_build_shed.py, swaps.json): the TVs over the bays on the
// back wall (y -550, facing into the shed; the bay_screen model's glass 9 cm out), the big one over
// the workbench on the west wall (x -650; tv_136's glass 10 cm out). Each page fills its TV's glass
// inside the frame (make_props.py TV_BEZEL, 1.1 cm).
constexpr double BayScreenX[4] = {-504.0, -232.0, 232.0, 504.0};
constexpr float TvBezel = 1.1f;

// The live pictures: each camera's picture size, and how often they're taken (a TV's 10 a second).
constexpr int32 CamW = 512;
constexpr int32 CamH = 288;
constexpr int32 MaxCams = 4;

// A third-person camera's spot behind and above What (Eye: where it looks from, Aim: which way),
// pulled in where a wall or a shelf is in the way.
FVector ChaseSpot(const UWorld* World, const FVector& Eye, const FVector& Back, double Distance, double Up, const AActor* Ignore)
{
	FVector Want = Eye - Back * Distance + FVector(0.0, 0.0, Up);
	FHitResult Hit;
	FCollisionQueryParams P(SCENE_QUERY_STAT(TorqueTvCam), false, Ignore);
	if (World && World->LineTraceSingleByChannel(Hit, Eye, Want, ECC_Camera, P))
		Want = Eye + (Want - Eye) * FMath::Max(0.0, Hit.Time - 0.08);
	return Want;
}

// Draws a camera's picture (its render target) into X, Y, W x H of a page.
void Picture(UCanvas* C, UTextureRenderTarget2D* RT, float X, float Y, float W, float H)
{
	if (!C || !RT || !RT->GetResource()) return;
	FCanvasTileItem T(FVector2D(X, Y), RT->GetResource(), FVector2D(W, H), FLinearColor::White);
	T.BlendMode = SE_BLEND_Opaque;
	C->DrawItem(T);
}

// The red LIVE tag and the time, top right, as a TV channel's bug.
void LiveBug(UCanvas* C, float W, const FString& Label)
{
	const float Pad = 18.0f;
	const float LW = (float)TorqueUi::Measure(Label, 20.0f, true).X + 26.0f;
	TorqueUi::Rect(C, W - Pad - LW - 92.0f, Pad, LW + 92.0f, 34.0f, FLinearColor(0.0f, 0.0f, 0.0f, 0.55f));
	TorqueUi::Rect(C, W - Pad - LW - 86.0f, Pad + 6.0f, 72.0f, 22.0f, FLinearColor(0.8f, 0.06f, 0.05f, 1.0f));
	TorqueUi::Text(C, TEXT("LIVE"), W - Pad - LW - 50.0f, Pad + 7.0f, 17.0f, FLinearColor::White, true, TorqueUi::EAlign::Centre, false);
	TorqueUi::Text(C, Label, W - Pad - 12.0f, Pad + 6.0f, 20.0f, TorqueUi::Ink, true, TorqueUi::EAlign::Right, false);
}

// A slim lower third over a picture: the colour stripe, a name, and a line under it.
void LowerThird(UCanvas* C, float X, float Y, float W, float H, const FLinearColor& Colour, const FString& Name, const FString& Line, float Px)
{
	const float BH = Px * 2.6f;
	TorqueUi::HGradient(C, X, Y + H - BH, W, BH, FLinearColor(0.0f, 0.0f, 0.0f, 0.78f), [](float T) { return 1.0f - 0.55f * T; });
	TorqueUi::Rect(C, X, Y + H - BH, FMath::Max(4.0f, Px * 0.25f), BH, Colour);
	TorqueUi::TextFit(C, Name, X + Px * 0.8f, Y + H - BH + Px * 0.2f, Px, W - Px * 1.6f, Colour, true);
	TorqueUi::TextFit(C, Line, X + Px * 0.8f, Y + H - BH + Px * 1.35f, Px * 0.8f, W - Px * 1.6f, TorqueUi::Ink);
}
}

const TCHAR* TorqueShedTv::Name(int32 Channel)
{
	switch (Channel)
	{
	case Players: return TEXT("Shed cam");
	case Rider: return TEXT("Rider cam");
	case Bikes: return TEXT("Bikes in the shed");
	case BayPage: return TEXT("This bay's bike");
	case YouTube: return TEXT("YouTube");
	default: return TEXT("Off");
	}
}

FString TorqueShedTv::VideoId(const FString& Pasted)
{
	auto IsIdChar = [](TCHAR Ch) { return FChar::IsAlnum(Ch) || Ch == TEXT('-') || Ch == TEXT('_'); };
	auto IsId = [&IsIdChar](const FString& S)
	{
		if (S.Len() != 11) return false;
		for (TCHAR Ch : S)
			if (!IsIdChar(Ch)) return false;
		return true;
	};
	const FString Text = Pasted.TrimStartAndEnd();
	if (IsId(Text)) return Text;
	// The id after one of these, up to the next character that can't be in one (?, &, /, #).
	static const TCHAR* Marks[] = {TEXT("v="), TEXT("youtu.be/"), TEXT("/shorts/"), TEXT("/embed/"), TEXT("/live/")};
	for (const TCHAR* Mark : Marks)
	{
		const int32 At = Text.Find(Mark, ESearchCase::IgnoreCase);
		if (At == INDEX_NONE) continue;
		FString Id;
		for (int32 K = At + FCString::Strlen(Mark); K < Text.Len() && IsIdChar(Text[K]); ++K) Id.AppendChar(Text[K]);
		if (IsId(Id)) return Id;
	}
	return FString();
}

ATorqueShedScreens::ATorqueShedScreens()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.0f;   // every frame for the YouTube video; the rest 10 times a second (Tick)
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

USceneCaptureComponent2D* ATorqueShedScreens::MakeCam(UTextureRenderTarget2D*& OutRT)
{
	OutRT = UKismetRenderingLibrary::CreateRenderTarget2D(this, TSSI::CamW, TSSI::CamH, RTF_RGBA8, FLinearColor::Black);
	if (!OutRT) return nullptr;
	USceneCaptureComponent2D* Cam = NewObject<USceneCaptureComponent2D>(this);
	Cam->TextureTarget = OutRT;
	Cam->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
	Cam->bCaptureEveryFrame = false;
	Cam->bCaptureOnMovement = false;
	Cam->bAlwaysPersistRenderingState = true;   // Lumen and the eye's exposure carry over between pictures
	Cam->FOVAngle = 70.0f;
	Cam->ShowFlags.SetMotionBlur(false);
	Cam->SetupAttachment(GetRootComponent());
	Cam->RegisterComponent();
	return Cam;
}

void ATorqueShedScreens::BeginPlay()
{
	Super::BeginPlay();
	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Shed/Materials/M_ShedScreenPage.M_ShedScreenPage"));
	if (!Base)
	{
		UE_LOG(LogTemp, Warning, TEXT("Torque: no M_ShedScreenPage (run Tools/ue_build_shed.py): the shed's screens stay blank"));
		return;
	}
	PageBase = Base;   // [tv] the bought TVs' pages too
	struct FScreen { FVector Centre, Facing, Right; float W, H; };
	TArray<FScreen> Screens;
	const float Inset = 2.0f * TSSI::TvBezel;
	for (int32 I = 0; I < 4; ++I)
		Screens.Add({FVector(TSSI::BayScreenX[I], -550.0 + 9.0 + 0.3, 245.0), FVector(0, 1, 0), FVector(1, 0, 0), 260.0f - Inset, 146.25f - Inset});
	Screens.Add({FVector(-650.0 + 10.0 + 0.3, 60.0, 255.0), FVector(1, 0, 0), FVector(0, -1, 0), 301.0f - Inset, 169.0f - Inset});
	// The parts computer's monitor on the east wall (its glass at x 613.5, over the PARTS label).
	Screens.Add({FVector(612.6, 190.0, 110.0), FVector(-1, 0, 0), FVector(0, 1, 0), 62.0f, 33.5f});
	// The dyno room's big screen on its far wall (dyno_room.gd _build_screen: 3.2 m across, 2.25 m up).
	Screens.Add({FVector(0.0, -1290.5, 225.0), FVector(0, 1, 0), FVector(1, 0, 0), 320.0f, 173.0f});
	for (int32 I = 0; I < Screens.Num(); ++I)
	{
		const FScreen& S = Screens[I];
		UTextureRenderTarget2D* RT = UKismetRenderingLibrary::CreateRenderTarget2D(this, PageW, PageH, RTF_RGBA8_SRGB, TSSI::Bg());
		UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(Base, this);
		if (RT && M)
		{
			M->SetTextureParameterValue(TEXT("Page"), RT);
			// Bright enough to read in the lit shed: the wall TVs (0 .. 4) out in the shed.
			M->SetScalarParameterValue(TEXT("Brightness"), I <= 4 ? TSSI::ShedPageBrightness : TSSI::PageBrightness);
		}
		UStaticMesh* Mesh = TSSI::Quad(S.Centre, S.Facing, S.Right, S.W, S.H, M);
		if (!RT || !M || !Mesh) continue;
		UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
		C->SetStaticMesh(Mesh);
		C->SetMaterial(0, M);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetGenerateOverlapEvents(false);
		C->SetCastShadow(false);
		C->SetupAttachment(GetRootComponent());
		C->RegisterComponent();
		Pages.Add(RT);
		Quads.Add(C);
		PageMaterials.Add(M);
	}
	if (UTorqueBuildSubsystem* B = UTorqueBuildSubsystem::Of(this))
	{
		ChangedHandle = B->OnChanged.AddLambda([this](int32) { bDirty = true; });
		LooksHandle = B->OnLooks.AddLambda([this](int32) { bDirty = true; });
		ItemsHandle = B->OnItems.AddLambda([this]() { bDirty = true; });
	}
	{
		UE_LOG(LogTemp, Log, TEXT("Torque: shed screens: %d pages"), Pages.Num());
	}
	Redraw();
}

void ATorqueShedScreens::EndPlay(const EEndPlayReason::Type Reason)
{
	StopVideo();
	if (UTorqueBuildSubsystem* B = UTorqueBuildSubsystem::Of(this))
	{
		B->OnChanged.Remove(ChangedHandle);
		B->OnLooks.Remove(LooksHandle);
		B->OnItems.Remove(ItemsHandle);
	}
	Super::EndPlay(Reason);
}

int32 ATorqueShedScreens::ChannelOf(int32 Tv) const
{
	const UTorqueBuildSubsystem* B = UTorqueBuildSubsystem::Of(this);
	return TorqueShedTv::Shown(B ? B->TvChannel(Tv) : (int32)TorqueShedTv::Default[FMath::Clamp(Tv, 0, TorqueShedTv::Tvs - 1)]);
}

bool ATorqueShedScreens::IsLive(int32 Tv) const
{
	const int32 Ch = ChannelOf(Tv);
	return Ch == TorqueShedTv::Players || Ch == TorqueShedTv::Rider;
}

bool ATorqueShedScreens::BikeInBay(int32 Bay) const
{
	const UTorqueBuildSubsystem* B = UTorqueBuildSubsystem::Of(this);
	return B && B->Exists(Bay) && !B->IsAway(Bay) && B->Bike(Bay).Pusher == 0;
}

void ATorqueShedScreens::ApplyBrightness()
{
	// The wall TVs (0 .. 4) and the bought ones, at the player's brightness (F at a TV steps it).
	BrightShown = FTorqueSettings::Get().ScreenBright;
	const float Times = FTorqueSettings::ScreenBrightTimes(BrightShown);
	for (int32 I = 0; I < PageMaterials.Num() && I <= 4; ++I)
		if (PageMaterials[I]) PageMaterials[I]->SetScalarParameterValue(TEXT("Brightness"), TSSI::ShedPageBrightness * Times);
	for (const TPair<int32, UMaterialInstanceDynamic*>& KV : PieceMaterials)
		if (KV.Value) KV.Value->SetScalarParameterValue(TEXT("Brightness"), TSSI::ShedPageBrightness * Times);
}

void ATorqueShedScreens::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UWorld* World = GetWorld();
	if (!World) return;
	// [tv] The YouTube video every frame; everything else 10 times a second.
	VideoTick(DeltaSeconds);
	SlowDt += DeltaSeconds;
	if (SlowDt < 0.1f) return;
	const float Dt = SlowDt;
	SlowDt = 0.0f;
	// [tv] A TV turned to another channel (by anyone in the shed) redraws straight away.
	for (int32 Tv = 0; Tv < TorqueShedTv::Tvs; ++Tv)
		if (ShownChannel[Tv] != ChannelOf(Tv)) bDirty = true;
	SyncPieces();
	if (UTorqueBuildSubsystem* Bays = UTorqueBuildSubsystem::Of(this))
		for (int32 Bay = 0; Bay < 4 && Bay < TorqueShedTv::Tvs; ++Bay)
		{
			const int32 Present = BikeInBay(Bay) ? 1 : 0;
			if (Present == BayPresent[Bay]) continue;
			// A bike just put on the bay: its TV comes on at the bay's page (the host asks, for everyone).
			if (BayPresent[Bay] == 0 && Present == 1 && !Bays->IsHostCopy() && ChannelOf(Bay) != TorqueShedTv::BayPage
				&& ChannelOf(Bay) != TorqueShedTv::YouTube)
			{
				FTorqueShedRequest R;
				R.Kind = ETorqueShedAct::TvChannel;
				R.Item = Bay;
				R.Colour = TorqueShedTv::BayPage;
				FString WhyNot;
				Bays->Request(R, WhyNot);
			}
			BayPresent[Bay] = Present;
			bDirty = true;
		}
	if (BrightShown != FTorqueSettings::Get().ScreenBright) ApplyBrightness();
	const UTorqueBuildSubsystem* Sub = UTorqueBuildSubsystem::Of(this);
	auto PieceChannel = [Sub](int32 Id) -> int32
	{
		const FTorqueFurniture* F = Sub ? Sub->FurnitureById(Id) : nullptr;
		return F ? TorqueShedTv::Shown(F->Channel) : -1;
	};
	for (const TPair<int32, int32>& KV : PieceShown)
		if (KV.Value != PieceChannel(KV.Key)) bDirty = true;
	Clock -= Dt;
	if (bDirty || Clock <= 0.0f)
	{
		Redraw();
		return;
	}
	const float Now = (float)World->GetRealTimeSeconds();
	// [tv] The live channels: their cameras move and take a picture, then the page is drawn round it,
	// 10 times a second, but only for a TV someone can see.
	// A TV on the rider cam is only on while a mate is out riding (beenster, build 131): it's
	// looked for every tick, and the TV switches on or off as they go out or come back.
	bool bWantPlayers = false, bAnyRider = false, bSeeRider = false;
	for (int32 Tv = 0; Tv < TorqueShedTv::Tvs && Tv < Quads.Num(); ++Tv)
	{
		if (ChannelOf(Tv) == TorqueShedTv::Rider) bAnyRider = true;
		if (!Quads[Tv] || !Quads[Tv]->IsVisible() || !Quads[Tv]->WasRecentlyRendered(0.5f) || !IsLive(Tv)) continue;
		if (ChannelOf(Tv) == TorqueShedTv::Rider) bSeeRider = true;
		else bWantPlayers = true;
	}
	for (const TPair<int32, UStaticMeshComponent*>& KV : PieceQuads)   // [tv] and the bought ones
	{
		const int32 Ch = PieceChannel(KV.Key);
		if (Ch == TorqueShedTv::Rider) bAnyRider = true;
		if (!KV.Value || !KV.Value->WasRecentlyRendered(0.5f) || (Ch != TorqueShedTv::Players && Ch != TorqueShedTv::Rider)) continue;
		if (Ch == TorqueShedTv::Rider) bSeeRider = true;
		else bWantPlayers = true;
	}
	if (bAnyRider) UpdateRiderCam(Dt, bSeeRider);
	else Filmed = nullptr;
	if (Filmed.IsValid() != bRiderShown)
	{
		bRiderShown = Filmed.IsValid();
		Redraw();
		return;
	}
	if (bWantPlayers) UpdatePlayerCams(Dt);
	for (int32 Tv = 0; Tv < TorqueShedTv::Tvs && Tv < Quads.Num(); ++Tv)
		if (Quads[Tv] && Quads[Tv]->WasRecentlyRendered(0.5f) && IsLive(Tv)) DrawPage(Tv, Now);
	for (const TPair<int32, UStaticMeshComponent*>& KV : PieceQuads)
	{
		const int32 Ch = PieceChannel(KV.Key);
		if (KV.Value && KV.Value->WasRecentlyRendered(0.5f) && (Ch == TorqueShedTv::Players || Ch == TorqueShedTv::Rider)) DrawPiece(KV.Key, Ch, Now);
	}
	// Someone here at the parts computer: its monitor follows what they pick.
	for (TActorIterator<ATorqueWalker> It(World); It; ++It)
		if (It->IsLocallyControlled() && It->AtComputer())
		{
			DrawPage(5, Now);
			break;
		}
	// A dyno run playing: its graph grows as the sweep goes.
	float Rpm = 0.0f, Progress = 0.0f;
	int32 Gear = 0;
	if (ATorqueDynoRoom::Live(this, Rpm, Gear, Progress) || bDynoWasLive)
	{
		bDynoWasLive = ATorqueDynoRoom::Live(this, Rpm, Gear, Progress);
		DrawPage(6, Now);
	}
}

void ATorqueShedScreens::Redraw()
{
	bDirty = false;
	Clock = 1.0f;   // the bikes' pages and who's on them
	UWorld* World = GetWorld();
	if (!World) return;
	const float Now = (float)World->GetRealTimeSeconds();
	for (int32 I = 0; I < Pages.Num(); ++I) DrawPage(I, Now);
	// [tv] The bought TVs.
	const UTorqueBuildSubsystem* B = UTorqueBuildSubsystem::Of(this);
	for (const TPair<int32, UStaticMeshComponent*>& KV : PieceQuads)
	{
		const FTorqueFurniture* F = B ? B->FurnitureById(KV.Key) : nullptr;
		if (F) DrawPiece(KV.Key, TorqueShedTv::Shown(F->Channel), Now);
	}
}

// --- [tv] The bought TVs -------------------------------------------------------------------------

void ATorqueShedScreens::SyncPieces()
{
	UWorld* World = GetWorld();
	const UTorqueBuildSubsystem* B = UTorqueBuildSubsystem::Of(this);
	if (!World || !B || !PageBase) return;
	const ATorqueFurnishings* Furn = nullptr;
	for (TActorIterator<ATorqueFurnishings> It(World); It; ++It)
	{
		Furn = *It;
		break;
	}
	TSet<int32> Want;
	if (Furn)
		for (const FTorqueFurniture& F : B->Furniture())
		{
			const TorqueFurniture::FModel* M = TorqueFurniture::Find(F.Model);
			UStaticMeshComponent* const* Mesh = Furn->Pieces().Find(F.Id);
			if (!M || !TorqueFurniture::IsTv(*M) || !Mesh || !*Mesh) continue;
			Want.Add(F.Id);
			UStaticMeshComponent* Q = PieceQuads.FindRef(F.Id);
			if (Q && Q->GetAttachParent() == *Mesh) continue;
			if (Q) Q->DestroyComponent();   // its piece was drawn afresh
			// The page fills the glass inside the frame, just in front of it: the model's origin is
			// its middle on the wall, the glass its depth out, its front +y (make_props.py _tv).
			UStaticMesh*& QM = PieceQuadMeshes.FindOrAdd(F.Model);
			if (!QM)
			{
				const float Inset = 2.0f * TSSI::TvBezel;
				QM = TSSI::Quad(FVector(0.0, M->Size.Z + 0.3, 0.0), FVector(0, 1, 0), FVector(1, 0, 0),
					(float)M->Size.X - Inset, (float)M->Size.Y - Inset, PageBase);
			}
			UTextureRenderTarget2D* RT = PiecePages.FindRef(F.Id);
			if (!RT)
			{
				RT = UKismetRenderingLibrary::CreateRenderTarget2D(this, PageW, PageH, RTF_RGBA8_SRGB, TSSI::Bg());
				if (RT) PiecePages.Add(F.Id, RT);
			}
			UMaterialInstanceDynamic* MID = PieceMaterials.FindRef(F.Id);
			if (!MID && RT)
			{
				MID = UMaterialInstanceDynamic::Create(PageBase, this);
				if (MID)
				{
					MID->SetTextureParameterValue(TEXT("Page"), RT);
					MID->SetScalarParameterValue(TEXT("Brightness"), TSSI::ShedPageBrightness * FTorqueSettings::ScreenBrightTimes(FTorqueSettings::Get().ScreenBright));
					PieceMaterials.Add(F.Id, MID);
				}
			}
			if (!QM || !RT || !MID)
			{
				PieceQuads.Remove(F.Id);
				continue;
			}
			Q = NewObject<UStaticMeshComponent>((*Mesh)->GetOwner());
			Q->SetStaticMesh(QM);
			Q->SetMaterial(0, MID);
			Q->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Q->SetGenerateOverlapEvents(false);
			Q->SetCastShadow(false);
			Q->SetMobility(EComponentMobility::Movable);
			Q->SetupAttachment(*Mesh);
			Q->RegisterComponent();
			PieceQuads.Add(F.Id, Q);
			PieceShown.Add(F.Id, -1);
			bDirty = true;
		}
	for (auto It = PieceQuads.CreateIterator(); It; ++It)
		if (!Want.Contains(It.Key()))
		{
			if (It.Value()) It.Value()->DestroyComponent();
			PiecePages.Remove(It.Key());
			PieceMaterials.Remove(It.Key());
			PieceShown.Remove(It.Key());
			It.RemoveCurrent();
		}
}

void ATorqueShedScreens::DrawPiece(int32 Id, int32 Channel, float Now)
{
	UTextureRenderTarget2D* RT = PiecePages.FindRef(Id);
	if (!RT) return;
	PieceShown.Add(Id, Channel);
	UStaticMeshComponent* Q = PieceQuads.FindRef(Id);
	UMaterialInstanceDynamic* PageM = PieceMaterials.FindRef(Id);
	const bool bVideo = Channel == TorqueShedTv::YouTube && VideoRT && !LoadedVideo.IsEmpty();
	if (PageM) PageM->SetTextureParameterValue(TEXT("Page"), bVideo ? (UTexture*)VideoRT : (UTexture*)RT);
	if (bVideo)
	{
		if (Q) Q->SetVisibility(true);
		return;
	}
	// [tv] A back wall TV with no bike in its bay is off, whatever it was on (bar a video someone's
	// watching): it comes back on, at the bay's page, when a bike's put there (Tick).
	if (Index < 4 && Channel != TorqueShedTv::YouTube && !BikeInBay(Index))
	{
		if (Quads.IsValidIndex(Index) && Quads[Index]) Quads[Index]->SetVisibility(false);
		UKismetRenderingLibrary::ClearRenderTarget2D(this, RT, FLinearColor::Black);
		return;
	}
	if (Channel == TorqueShedTv::Rider && !Filmed.IsValid())
	{
		// No mate out riding: the TV's off, as the wall's.
		if (Q) Q->SetVisibility(false);
		UKismetRenderingLibrary::ClearRenderTarget2D(this, RT, FLinearColor::Black);
		return;
	}
	if (Q) Q->SetVisibility(true);
	const bool bLive = Channel == TorqueShedTv::Players || Channel == TorqueShedTv::Rider;
	UKismetRenderingLibrary::ClearRenderTarget2D(this, RT, bLive ? FLinearColor::Black : TSSI::Bg());
	UCanvas* C = nullptr;
	FVector2D Size;
	FDrawToRenderTargetContext Context;
	UKismetRenderingLibrary::BeginDrawCanvasToRenderTarget(this, RT, C, Size, Context);
	if (C)
	{
		if (Channel == TorqueShedTv::Bikes) DrawBikes(C);
		else if (Channel == TorqueShedTv::Rider) DrawRider(C, Now);
		else if (Channel == TorqueShedTv::YouTube) DrawYouTubeCard(C);
		else DrawPlayers(C, FString());
	}
	UKismetRenderingLibrary::EndDrawCanvasToRenderTarget(this, Context);
}

// --- [tv] The live cameras -----------------------------------------------------------------------

void ATorqueShedScreens::UpdatePlayerCams(float Dt)
{
	UWorld* World = GetWorld();
	if (!World) return;
	// Everyone in the shed, in the order they joined (the game state's list), up to MaxCams.
	TArray<ATorqueWalker*> Walkers;
	if (const AGameStateBase* GS = World->GetGameState())
		for (const APlayerState* PS : GS->PlayerArray)
			if (ATorqueWalker* Wk = PS ? Cast<ATorqueWalker>(PS->GetPawn()) : nullptr)
				if (Walkers.Num() < TSSI::MaxCams) Walkers.Add(Wk);
	if (Walkers.Num() == 0)
		for (TActorIterator<ATorqueWalker> It(World); It && Walkers.Num() < TSSI::MaxCams; ++It) Walkers.Add(*It);
	while (PlayerCams.Num() < Walkers.Num())
	{
		UTextureRenderTarget2D* RT = nullptr;
		USceneCaptureComponent2D* Cam = MakeCam(RT);
		if (!Cam) return;
		PlayerCams.Add(Cam);
		PlayerCamRTs.Add(RT);
		PlayerCamAt.Add(FVector::ZeroVector);
	}
	FilmedWalkers.Reset();
	for (int32 I = 0; I < Walkers.Num(); ++I)
	{
		ATorqueWalker* Wk = Walkers[I];
		FilmedWalkers.Add(Wk);
		// Over their shoulder and a little above: their hands, and what they're looking at.
		const FVector Eye = Wk->GetPawnViewLocation();
		const FRotator Aim = Wk->GetBaseAimRotation();
		const FVector Flat = FRotator(0.0, Aim.Yaw, 0.0).Vector();
		const FVector Side = FRotationMatrix(FRotator(0.0, Aim.Yaw, 0.0)).GetScaledAxis(EAxis::Y);
		const FVector From = Eye + Side * 30.0;
		const FVector Want = TSSI::ChaseSpot(World, From, Flat, 140.0, 40.0, Wk);
		FVector& At = PlayerCamAt[I];
		At = At.IsZero() ? Want : FMath::VInterpTo(At, Want, Dt, 6.0f);
		const FVector LookAt = Eye + Aim.Vector() * 120.0 - FVector(0.0, 0.0, 25.0);
		PlayerCams[I]->SetWorldLocationAndRotation(At, (LookAt - At).Rotation());
		PlayerCams[I]->CaptureScene();
	}
}

void ATorqueShedScreens::UpdateRiderCam(float Dt, bool bCapture)
{
	UWorld* World = GetWorld();
	if (!World) return;
	// Riders out on their bikes in this world that aren't this player: a different one every 20 s.
	TArray<ATorqueBikePawn*> Riders;
	for (TActorIterator<ATorqueBikePawn> It(World); It; ++It)
		if (!It->IsLocallyControlled() && It->GetPlayerState()) Riders.Add(*It);
	if (Riders.Num() == 0)
	{
		Filmed = nullptr;
		return;
	}
	RiderSwap -= Dt;
	ATorqueBikePawn* Pick = Filmed.Get();
	if (!Pick || !Riders.Contains(Pick) || (RiderSwap <= 0.0f && Riders.Num() > 1))
	{
		const int32 Was = Pick ? Riders.IndexOfByKey(Pick) : INDEX_NONE;
		Pick = Riders[(Was + 1) % Riders.Num()];
		RiderSwap = 20.0f;
		RiderCamAt = FVector::ZeroVector;
	}
	Filmed = Pick;
	if (!RiderCam)
	{
		RiderCam = MakeCam(RiderCamRT);
		if (!RiderCam) return;
		RiderCam->FOVAngle = 62.0f;
	}
	// A chase camera: behind the bike the way it's going, up over the rider's head, looking a
	// little ahead of it, as a TV bike race's.
	const FVector Body = Pick->GetActorLocation() + FVector(0.0, 0.0, 90.0);
	const FVector Vel = Pick->GetVelocity();
	FVector Fwd = Vel.Size2D() > 300.0 ? FVector(Vel.X, Vel.Y, 0.0).GetSafeNormal() : FRotator(0.0, Pick->GetActorRotation().Yaw, 0.0).Vector();
	RiderFwd = RiderFwd.IsNearlyZero() ? Fwd : FMath::VInterpNormalRotationTo(RiderFwd, Fwd, Dt, 120.0f);
	const FVector Want = TSSI::ChaseSpot(World, Body, RiderFwd, 450.0, 110.0, Pick);
	RiderCamAt = RiderCamAt.IsZero() ? Want : FMath::VInterpTo(RiderCamAt, Want, Dt, 5.0f);
	const FVector LookAt = Body + RiderFwd * 250.0 - FVector(0.0, 0.0, 30.0);
	RiderCam->SetWorldLocationAndRotation(RiderCamAt, (LookAt - RiderCamAt).Rotation());
	if (bCapture) RiderCam->CaptureScene();
}

// --- The pages -----------------------------------------------------------------------------------

void ATorqueShedScreens::DrawPage(int32 Index, float Now)
{
	UTextureRenderTarget2D* RT = Pages.IsValidIndex(Index) ? Pages[Index] : nullptr;
	if (!RT) return;
	const int32 Channel = Index < TorqueShedTv::Tvs ? ChannelOf(Index) : -1;
	if (Index < TorqueShedTv::Tvs) ShownChannel[Index] = Channel;
	// [tv] YouTube: the TV shows the browser's picture itself while a video's on; else its page.
	UMaterialInstanceDynamic* PageM = PageMaterials.IsValidIndex(Index) ? PageMaterials[Index] : nullptr;
	const bool bVideo = Channel == TorqueShedTv::YouTube && VideoRT && !LoadedVideo.IsEmpty();
	if (PageM) PageM->SetTextureParameterValue(TEXT("Page"), bVideo ? (UTexture*)VideoRT : (UTexture*)RT);
	if (bVideo)
	{
		if (Quads.IsValidIndex(Index) && Quads[Index]) Quads[Index]->SetVisibility(true);
		return;
	}
	if (Channel == TorqueShedTv::Rider && !Filmed.IsValid())
	{
		// No mate out riding: the TV's off.
		if (Quads.IsValidIndex(Index) && Quads[Index]) Quads[Index]->SetVisibility(false);
		UKismetRenderingLibrary::ClearRenderTarget2D(this, RT, FLinearColor::Black);
		return;
	}
	if (Channel == TorqueShedTv::BayPage)
	{
		// The bay's own page is on while its bike stands in the bay, and goes blank, off, when it's
		// wheeled away (to the dyno, the wash bay...), scrapped or stripped to nothing.
		const bool bOn = BikeInBay(Index);
		if (Quads.IsValidIndex(Index) && Quads[Index]) Quads[Index]->SetVisibility(bOn);
		if (!bOn)
		{
			UKismetRenderingLibrary::ClearRenderTarget2D(this, RT, FLinearColor::Black);
			return;
		}
	}
	else if (Index < TorqueShedTv::Tvs && Quads.IsValidIndex(Index) && Quads[Index])
	{
		Quads[Index]->SetVisibility(true);
	}
	UKismetRenderingLibrary::ClearRenderTarget2D(this, RT, Channel == TorqueShedTv::Players || Channel == TorqueShedTv::Rider ? FLinearColor::Black : TSSI::Bg());
	UCanvas* C = nullptr;
	FVector2D Size;
	FDrawToRenderTargetContext Context;
	UKismetRenderingLibrary::BeginDrawCanvasToRenderTarget(this, RT, C, Size, Context);
	if (C)
	{
		if (Index == 5) DrawComputer(C, Now);
		else if (Index == 6) DrawDyno(C);
		else if (Channel == TorqueShedTv::BayPage) DrawBay(C, Index);
		else if (Channel == TorqueShedTv::Bikes) DrawBikes(C);
		else if (Channel == TorqueShedTv::Rider) DrawRider(C, Now);
		else if (Channel == TorqueShedTv::YouTube) DrawYouTubeCard(C);
		else DrawPlayers(C, FString());
	}
	UKismetRenderingLibrary::EndDrawCanvasToRenderTarget(this, Context);
}

void ATorqueShedScreens::DrawPlayers(UCanvas* C, const FString& Note) const
{
	using namespace TSSI;
	const float W = (float)PageW, H = (float)PageH;
	const TArray<FCrewRow> Rows = Crew(GetWorld(), UTorqueBuildSubsystem::Of(this));
	const int32 N = FMath::Min(FilmedWalkers.Num(), PlayerCamRTs.Num());
	if (N == 0)
	{
		TorqueUi::Text(C, TEXT("SHED CAM"), W * 0.5f, H * 0.5f - 24.0f, 40.0f, TorqueUi::Dim, true, TorqueUi::EAlign::Centre);
		return;
	}
	// One picture fills the TV; two side by side; three or four in a 2 x 2 grid.
	const int32 Cols = N == 1 ? 1 : 2;
	const int32 RowsN = N <= 2 ? 1 : 2;
	const float Gap = N == 1 ? 0.0f : 6.0f;
	const float TW = (W - Gap * (Cols - 1)) / Cols;
	const float TH = N == 2 ? TW * 9.0f / 16.0f : (H - Gap * (RowsN - 1)) / RowsN;
	const float Top = N == 2 ? (H - TH) * 0.5f : 0.0f;
	for (int32 I = 0; I < N; ++I)
	{
		const float X = (I % Cols) * (TW + Gap);
		const float Y = Top + (I / Cols) * (TH + Gap);
		Picture(C, PlayerCamRTs[I], X, Y, TW, TH);
		// Who it is and what they're doing, from the crew list (matched by their player state).
		const ATorqueWalker* Wk = FilmedWalkers[I].Get();
		FCrewRow Row;
		Row.Name = TEXT("In the shed");
		if (const APlayerState* PS = Wk ? Wk->GetPlayerState() : nullptr)
			for (const FCrewRow& R : Rows)
				if (R.Name == PS->GetPlayerName()) Row = R;
		const FString Line = Row.Doing.IsEmpty() ? FString(TEXT("in the shed")) : Row.Doing;
		LowerThird(C, X, Y, TW, TH, Row.Colour, Row.Name + (Row.bMe ? TEXT("  (you)") : TEXT("")), Line, N == 1 ? 30.0f : 21.0f);
	}
	LiveBug(C, W, Note.IsEmpty() ? FString(TEXT("SHED CAM")) : Note);
}

void ATorqueShedScreens::DrawRider(UCanvas* C, float Now) const
{
	using namespace TSSI;
	const float W = (float)PageW, H = (float)PageH;
	const ATorqueBikePawn* Pick = Filmed.Get();
	if (!Pick || !RiderCamRT) return;   // (DrawPage switches the TV off then)
	Picture(C, RiderCamRT, 0.0f, 0.0f, W, H);
	const APlayerState* PS = Pick->GetPlayerState();
	FLinearColor Colour = TorqueUi::Accent;
	if (const ATorquePlayerState* TPS = Cast<ATorquePlayerState>(PS))
	{
		const FColor Cv = TPS->ColourValue();
		if (Cv.R + Cv.G + Cv.B >= 200) Colour = FLinearColor(Cv);
	}
	const float Mph = (float)Pick->GetVelocity().Size() * 0.0223694f;   // cm/s to mph
	LowerThird(C, 24.0f, 0.0f, 520.0f, H - 24.0f, Colour, PS ? PS->GetPlayerName() : FString(TEXT("Rider")),
		FString::Printf(TEXT("%.0f mph"), Mph), 30.0f);
	LiveBug(C, W, TEXT("RIDER CAM"));
}

void ATorqueShedScreens::DrawBikes(UCanvas* C) const
{
	using namespace TSSI;
	const float W = (float)PageW, H = (float)PageH;
	const UTorqueBuildSubsystem* B = UTorqueBuildSubsystem::Of(this);
	const TArray<FCrewRow> Rows = Crew(GetWorld(), B);
	int32 Count = 0;
	for (int32 Bay = 0; Bay < UTorqueBuildSubsystem::Bays; ++Bay)
		if (B && B->Exists(Bay)) ++Count;
	// The title: the shed's bikes, and how many are ready to ride.
	int32 Ready = 0;
	TArray<FBay> All;
	for (int32 Bay = 0; Bay < UTorqueBuildSubsystem::Bays; ++Bay)
	{
		All.Add(Read(B, Bay));
		if (All.Last().bExists && All.Last().Status == TEXT("READY TO RIDE")) ++Ready;
	}
	TorqueUi::Rect(C, 0.0f, 0.0f, W, 64.0f, FLinearColor(0.0f, 0.0f, 0.0f, 0.35f));
	TorqueUi::Rect(C, 0.0f, 62.0f, W, 2.0f, TorqueUi::Accent);
	TorqueUi::Text(C, TEXT("BIKES IN THE SHED"), 24.0f, 14.0f, 32.0f, TorqueUi::Ink, true);
	TorqueUi::Text(C, FString::Printf(TEXT("%d of %d bays   -   %d ready to ride"), Count, UTorqueBuildSubsystem::Bays, Ready), W - 24.0f, 22.0f, 22.0f, TorqueUi::Dim, false, TorqueUi::EAlign::Right);
	// A card per bay, 2 x 2.
	const float Pad = 16.0f, CW = (W - 3.0f * Pad) * 0.5f, CH = (H - 64.0f - 3.0f * Pad) * 0.5f;
	for (int32 Bay = 0; Bay < UTorqueBuildSubsystem::Bays; ++Bay)
	{
		const FBay& D = All[Bay];
		const float X = Pad + (Bay % 2) * (CW + Pad);
		const float Y = 64.0f + Pad + (Bay / 2) * (CH + Pad);
		TorqueUi::Rect(C, X, Y, CW, CH, FLinearColor(1.0f, 1.0f, 1.0f, 0.05f));
		TorqueUi::Rect(C, X, Y, 5.0f, CH, D.bExists ? D.StatusColour : TorqueUi::Faint);
		TorqueUi::Text(C, FString::Printf(TEXT("BAY %d"), Bay + 1), X + 20.0f, Y + 12.0f, 17.0f, TorqueUi::Dim, true);
		if (!D.bExists)
		{
			TorqueUi::Text(C, TEXT("Empty"), X + 20.0f, Y + 44.0f, 30.0f, TorqueUi::Faint, true);
			continue;
		}
		TorqueUi::TextFit(C, D.Name, X + 92.0f, Y + 8.0f, 24.0f, CW - 112.0f, TorqueUi::Ink, true);
		// How it stands, and where it is if it's out of its bay.
		FString Where;
		if (B->Bike(Bay).Pusher != 0) Where = TEXT("  -  being wheeled");
		else if (B->OnDyno(Bay)) Where = TEXT("  -  on the dyno");
		else if (B->IsAway(Bay)) Where = TEXT("  -  out of its bay");
		TorqueUi::Text(C, D.Status + Where, X + 20.0f, Y + 46.0f, 20.0f, D.StatusColour, true);
		// The build, then condition and muck, as bars.
		auto BarRow = [C, X, CW](float RowY, const FString& Label, float Frac, const FString& Value, const FLinearColor& Fill)
		{
			TorqueUi::Text(C, Label, X + 20.0f, RowY - 3.0f, 17.0f, TorqueUi::Dim);
			const float BX = X + 130.0f, BW = CW - 130.0f - 84.0f;
			TorqueUi::Rect(C, BX, RowY + 3.0f, BW, 10.0f, FLinearColor(1.0f, 1.0f, 1.0f, 0.1f));
			TorqueUi::Rect(C, BX, RowY + 3.0f, BW * FMath::Clamp(Frac, 0.0f, 1.0f), 10.0f, Fill);
			TorqueUi::Text(C, Value, X + CW - 20.0f, RowY - 3.0f, 17.0f, TorqueUi::Ink, true, TorqueUi::EAlign::Right);
		};
		float RY = Y + 84.0f;
		BarRow(RY, TEXT("Build"), D.Progress, FString::Printf(TEXT("%.0f%%"), D.Progress * 100.0f), D.StatusColour);
		RY += 26.0f;
		BarRow(RY, TEXT("Condition"), D.Health, FString::Printf(TEXT("%.0f%%"), D.Health * 100.0f), D.Health >= 0.95f ? Good() : FMath::Lerp(Bad(), Good(), D.Health));
		RY += 26.0f;
		const float Muck = FMath::Max(D.Rust, D.Dirt);
		BarRow(RY, D.Rust >= D.Dirt ? TEXT("Rust") : TEXT("Dirt"), Muck, FString::Printf(TEXT("%.0f%%"), Muck * 100.0f), Srgb(0.75f, 0.45f, 0.2f));
		// The paint, and who's on it now.
		float PX = X + 20.0f;
		const float PY = Y + CH - 32.0f;
		if (D.bHasTankPaint) { TorqueUi::Rect(C, PX, PY, 22.0f, 18.0f, FLinearColor(D.TankPaint.R, D.TankPaint.G, D.TankPaint.B, 1.0f)); PX += 28.0f; }
		if (D.bHasFramePaint) { TorqueUi::Rect(C, PX, PY, 22.0f, 18.0f, FLinearColor(D.FramePaint.R, D.FramePaint.G, D.FramePaint.B, 1.0f)); PX += 28.0f; }
		const FString Tag = FString::Printf(TEXT("bay %d"), Bay + 1);
		FString On;
		FLinearColor OnColour = TorqueUi::Ink;
		for (const FCrewRow& R : Rows)
			if (R.WorkingBay == Bay || R.Doing.Contains(Tag))
			{
				On = On.IsEmpty() ? R.Name : On + TEXT(", ") + R.Name;
				OnColour = R.Colour;
			}
		if (!On.IsEmpty()) TorqueUi::TextFit(C, TEXT("On it: ") + On, X + CW - 20.0f, PY - 2.0f, 17.0f, CW - (PX - X) - 30.0f, OnColour, true, TorqueUi::EAlign::Right);
	}
}

void ATorqueShedScreens::DrawBay(UCanvas* C, int32 Bay) const
{
	using namespace TSSI;
	FPen Pen;
	Pen.C = C;
	Pen.Font = GEngine ? GEngine->GetLargeFont() : nullptr;
	if (!Pen.Font) return;
	float FW = 0.0f, FH = 0.0f;
	C->StrLen(Pen.Font, TEXT("Hg"), FW, FH);
	Pen.LineH = FH;
	const float W = (float)PageW, H = (float)PageH;
	const FBay D = Read(UTorqueBuildSubsystem::Of(this), Bay);
	// The header: orange, the bike's name and how it stands in dark ink (dashboard.gd _draw_page).
	Pen.Rect(0, 0, W, 70, Orange());
	Pen.Text(W - 110.0f, 30.0f, FString::Printf(TEXT("BAY %d"), Bay + 1), 16, Ink(), 90.0f, 1);
	if (!D.bExists)
	{
		Pen.Text(24, 50, TEXT("EMPTY BAY"), 40, Ink());
		return;
	}
	Pen.Text(24, 50, Left(D.Name.ToUpper(), 26), 40, Ink());
	Pen.Text(560, 56, D.Status, 24, Ink(), W - 584.0f, 1);

	// Left: Torque Shed's numbers (power, torque, weight, power to weight), then the build: how far
	// along it is, the counts, condition, fabrication and paint.
	const UTorqueBuildSubsystem* Shed = UTorqueBuildSubsystem::Of(this);
	const FTorqueEngineNumbers E = Shed ? TorqueShedPower::Numbers(*Shed, Bay) : FTorqueEngineNumbers();
	const float Hp = TorqueShedPower::PeakHp(E);
	// Two pages, turning every 10 s: Torque Shed's numbers, then the build.
	const bool bSpecs = int32((GetWorld() ? GetWorld()->GetRealTimeSeconds() : 0.0) / 10.0) % 2 == 0;
	Pen.Rect(W * 0.5f - 30.0f, H - 16, 12, 8, bSpecs ? Amber() : Dim());
	Pen.Rect(W * 0.5f - 10.0f, H - 16, 12, 8, bSpecs ? Dim() : Amber());
	float Y = 104.0f;
	if (bSpecs)
	{
	auto Spec = [&Pen](float RowY, const FString& Label, const FString& Value, const FString& Extra)
	{
		Pen.Text(24, RowY, Label, 22, Dim());
		Pen.Text(200, RowY, Value, 26, FLinearColor::White, 150.0f, 1);
		Pen.Text(362, RowY, Extra, 16, Dim());
	};
	Spec(Y, TEXT("Power"), E.bValid ? FString::Printf(TEXT("%.0f hp"), Hp) : FString(TEXT("-")), E.bValid ? FString::Printf(TEXT("%.0f rpm limit"), E.Redline) : FString(TEXT("no engine")));
	Y += 34.0f;
	Spec(Y, TEXT("Torque"), E.bValid ? FString::Printf(TEXT("%.0f Nm"), E.PeakTorque) : FString(TEXT("-")), E.bValid ? FString::Printf(TEXT("at %.0f rpm"), E.PeakTorqueRpm) : FString());
	Y += 34.0f;
	Spec(Y, TEXT("Weight"), FString::Printf(TEXT("%.0f kg"), E.Mass), FString());
	Y += 34.0f;
	Spec(Y, TEXT("hp per 100 kg"), E.bValid ? FString::Printf(TEXT("%.1f"), Hp / FMath::Max(E.Mass, 1.0f) * 100.0f) : FString(TEXT("-")), FString());
	Y += 34.0f;
	if (Shed && Shed->HasDynoRun(Bay))
	{
		const FTorqueDynoRun& Last = Shed->DynoRunOf(Bay);
		Spec(Y, TEXT("Dyno"), FString::Printf(TEXT("%.0f hp"), Last.PeakHp), FString::Printf(TEXT("%.0f Nm at the wheel"), Last.PeakNm));
		Y += 34.0f;
	}
	// What's holding the engine back (Torque Shed's notes).
	Y += 10.0f;
	// [fluids] Then what it's low on: petrol, oil, brake fluid, a dry chain.
	TArray<FString> Notes = E.Notes;
	if (Shed)
	{
		const UTorqueBuildSubsystem::FFluidLevels Lv = Shed->FluidsOf(Bay);
		const float FuelFrac = Lv.FuelCapacity > 0.0f ? Lv.FuelLitres / Lv.FuelCapacity : 0.0f;
		if (Lv.bTank && Lv.FuelLitres < 0.05f) Notes.Add(TEXT("No petrol in the tank. Fill it at the pump."));
		else if (Lv.bTank && FuelFrac < 0.25f) Notes.Add(FString::Printf(TEXT("Low on petrol (%.1f L)."), Lv.FuelLitres));
		if (Lv.bEngine && Lv.Oil < 0.6f) Notes.Add(FString::Printf(TEXT("Oil low (%.0f%%): top it up."), Lv.Oil * 100.0f));
		else if (Lv.bEngine && Lv.OilAge > 0.6f) Notes.Add(TEXT("The oil's old: change it."));
		if (Lv.bFrontBrake && Lv.FrontBrake < 0.5f) Notes.Add(FString::Printf(TEXT("Front brake fluid low (%.0f%%)."), Lv.FrontBrake * 100.0f));
		if (Lv.bRearBrake && Lv.RearBrake < 0.5f) Notes.Add(FString::Printf(TEXT("Rear brake fluid low (%.0f%%)."), Lv.RearBrake * 100.0f));
		if (Lv.bChain && Lv.Lube < 0.4f) Notes.Add(TEXT("The chain's dry: lube it."));
	}
	for (int32 I = 0; I < Notes.Num() && I < 6 && Y < H - 120.0f; ++I)
	{
		Pen.Text(24, Y, Left(Notes[I], 46), 17, Srgb(1.0f, 0.75f, 0.3f));
		Y += 26.0f;
	}
	if (Notes.Num() == 0 && E.bValid) Pen.Text(24, Y, TEXT("Running sweet."), 17, Good());
	}
	else
	{
	Pen.Text(24, Y, TEXT("BUILD"), 16, Dim());
	Pen.Text(24, Y, FString::Printf(TEXT("%.0f%%"), D.Progress * 100.0f), 16, D.StatusColour, 496.0f, 1);
	Pen.Bar(24, Y + 8, 496, 14, D.Progress, D.StatusColour);
	Y += 50.0f;
	auto Row = [&Pen](float RowY, const FString& Label, const FString& Value, const FLinearColor& Colour, float Fraction, const FLinearColor& BarColour)
	{
		Pen.Text(24, RowY, Label, 20, Dim());
		Pen.Text(200, RowY, Value, 22, Colour, 120.0f, 1);
		if (Fraction >= 0.0f) Pen.Bar(340, RowY - 15, 180, 12, Fraction, BarColour);
	};
	const float Fit = D.Needed > 0 ? float(D.Fitted) / D.Needed : 0.0f;
	const float Bolt = D.Fitted > 0 ? float(D.Bolted) / D.Fitted : 0.0f;
	Row(Y, TEXT("Parts on"), FString::Printf(TEXT("%d / %d"), D.Fitted, D.Needed), D.Fitted == D.Needed ? Good() : FLinearColor::White, Fit, D.Fitted == D.Needed ? Good() : Bad());
	Y += 30.0f;
	Row(Y, TEXT("Bolted up"), FString::Printf(TEXT("%d / %d"), D.Bolted, D.Fitted), D.Bolted == D.Fitted ? Good() : FLinearColor::White, Bolt, D.Bolted == D.Fitted ? Good() : Amber());
	Y += 30.0f;
	const FLinearColor HealthColour = D.Health >= 0.95f ? Good() : FMath::Lerp(Bad(), Good(), D.Health);
	Row(Y, TEXT("Condition"), FString::Printf(TEXT("%.0f %%"), D.Health * 100.0f), FLinearColor::White, D.Health, HealthColour);
	Y += 30.0f;
	Row(Y, TEXT("Rust / dirt"), FString::Printf(TEXT("%.0f / %.0f %%"), D.Rust * 100.0f, D.Dirt * 100.0f), D.Rust > 0.3f ? Bad() : (D.Dirt > 0.3f ? Amber() : FLinearColor::White),
		FMath::Max(D.Rust, D.Dirt), D.Rust >= D.Dirt ? Srgb(0.75f, 0.4f, 0.2f) : Srgb(0.55f, 0.45f, 0.3f));
	Y += 30.0f;
	if (D.Owed.Num() + D.Made.Num() > 0)
	{
		Row(Y, TEXT("Fabrication"), FString::Printf(TEXT("%d / %d"), D.Made.Num(), D.Owed.Num() + D.Made.Num()),
			D.Owed.Num() == 0 ? Good() : Amber(), float(D.Made.Num()) / (D.Owed.Num() + D.Made.Num()), D.Owed.Num() == 0 ? Good() : Amber());
		Y += 30.0f;
	}
	// Its paint: the tank's and the frame's colours.
	if (D.bHasTankPaint || D.bHasFramePaint)
	{
		Pen.Text(24, Y, TEXT("Paint"), 20, Dim());
		float PX = 200.0f;
		if (D.bHasTankPaint)
		{
			Pen.Rect(PX, Y - 18, 24, 20, FLinearColor(D.TankPaint.R, D.TankPaint.G, D.TankPaint.B, 1.0f));
			Pen.Text(PX + 32, Y, TEXT("tank"), 16, Dim());
			PX += 110.0f;
		}
		if (D.bHasFramePaint)
		{
			Pen.Rect(PX, Y - 18, 24, 20, FLinearColor(D.FramePaint.R, D.FramePaint.G, D.FramePaint.B, 1.0f));
			Pen.Text(PX + 32, Y, TEXT("frame"), 16, Dim());
		}
		Y += 30.0f;
	}
	// Who's on it now (the crew's doings naming this bay).
	const FString Tag = FString::Printf(TEXT("bay %d"), Bay + 1);
	TArray<TSSI::FCrewRow> On;
	for (const TSSI::FCrewRow& R : Crew(GetWorld(), UTorqueBuildSubsystem::Of(this)))
		if (R.Doing.Contains(Tag) || R.WorkingBay == Bay)
		{
			On.Add(R);
			if (!R.Doing.Contains(Tag)) On.Last().Doing = FString::Printf(TEXT("working on %s"), *Tag);
		}
	float CY = H - 100.0f - 26.0f * FMath::Max(0, FMath::Min(On.Num(), 2) - 1);
	if (On.Num() > 0) Pen.Text(24, CY - 26.0f, TEXT("ON IT NOW"), 14, Dim());
	for (int32 I = 0; I < On.Num() && I < 2; ++I)
	{
		Pen.Rect(24, CY - 14, 12, 12, On[I].Colour);
		Pen.Text(44, CY, Left(On[I].Name + TEXT(": ") + On[I].Doing, 46), 17, On[I].Colour);
		CY += 26.0f;
	}

	}

	// [fluids] Along the bottom on both pages (dashboard.gd's fluid bars): the petrol, the oil, each
	// brake's fluid and the chain's lube, green when good, amber when low or old, red when it's
	// nearly out; a part that isn't on the bike shows a dash.
	if (Shed)
	{
		const UTorqueBuildSubsystem::FFluidLevels F = Shed->FluidsOf(Bay);
		const float FY = H - 46.0f;
		Pen.Rect(16, FY - 34, 512, 2, Panel());
		auto Gauge = [&Pen, FY](int32 Col, const FString& Label, bool bHas, float Level, float Age, const FString& Value)
		{
			const float GX = 24.0f + Col * 100.0f;
			Pen.Text(GX, FY - 14, Label, 13, Dim());
			if (!bHas)
			{
				Pen.Text(GX, FY + 12, TEXT("-"), 14, Dim());
				return;
			}
			const FLinearColor Col2 = Level < 0.3f ? Bad() : ((Level < 0.6f || Age > 0.6f) ? Amber() : Good());
			Pen.Bar(GX, FY - 6, 88, 10, Level, Col2);
			Pen.Text(GX, FY + 22, Value, 13, Col2);
		};
		const float FuelFrac = F.FuelCapacity > 0.0f ? F.FuelLitres / F.FuelCapacity : 0.0f;
		Gauge(0, TEXT("FUEL"), F.bTank, FuelFrac, F.FuelAge, FString::Printf(TEXT("%.1f / %.0f L"), F.FuelLitres, F.FuelCapacity));
		Gauge(1, TEXT("OIL"), F.bEngine, F.Oil, F.OilAge, FString::Printf(TEXT("%.0f%%"), F.Oil * 100.0f));
		Gauge(2, TEXT("BRAKE F"), F.bFrontBrake, F.FrontBrake, F.BrakeAge, FString::Printf(TEXT("%.0f%%"), F.FrontBrake * 100.0f));
		Gauge(3, TEXT("BRAKE R"), F.bRearBrake, F.RearBrake, F.BrakeAge, FString::Printf(TEXT("%.0f%%"), F.RearBrake * 100.0f));
		Gauge(4, TEXT("CHAIN LUBE"), F.bChain, F.Lube, 0.0f, FString::Printf(TEXT("%.0f%%"), F.Lube * 100.0f));
	}

	// Right: every part, all of it on the one page (beenster: to see what's loose), in two columns
	// (three for a long list), the rows closing up to fit: each with its condition, LOOSE in amber
	// where it isn't bolted up, MISSING in red (dashboard.gd's part list).
	const int32 NumParts = D.Parts.Num();
	if (NumParts > 0)
	{
		const int32 Cols = NumParts > 48 ? 3 : 2;
		const int32 RowsPerCol = FMath::Max(1, (NumParts + Cols - 1) / Cols);
		const float ListTop = 80.0f, ListH = H - 22.0f - ListTop;
		const float RowH = FMath::Min(30.0f, ListH / RowsPerCol);
		const float ColW = (W - 560.0f - 8.0f) / Cols;
		const float NameSize = FMath::Clamp(RowH * 0.52f, 9.0f, 14.0f);
		const int32 NameChars = FMath::Max(8, FMath::FloorToInt(ColW * 0.6f / (NameSize * 0.56f)));
		const float BarX = ColW * 0.63f, BarW = ColW * 0.32f;
		int32 LooseCount = 0;
		for (int32 I = 0; I < NumParts; ++I)
		{
			const TSSI::FPartRow& P = D.Parts[I];
			const float PX = 560.0f + (I / RowsPerCol) * ColW;
			const float PY = ListTop + (I % RowsPerCol) * RowH;
			const bool bLoose = P.bFitted && !P.bBolted;
			if (bLoose) ++LooseCount;
			const FLinearColor NameColour = !P.bFitted ? Bad() : (bLoose ? Amber() : FLinearColor::White);
			Pen.Text(PX, PY + NameSize, Left(P.Name, NameChars), NameSize, NameColour);
			if (!P.bFitted)
			{
				Pen.Text(PX + BarX, PY + NameSize, TEXT("MISSING"), FMath::Min(NameSize, 12.0f), Bad());
			}
			else if (bLoose)
			{
				Pen.Text(PX + BarX, PY + NameSize, TEXT("LOOSE"), FMath::Min(NameSize, 12.0f), Srgb(1.0f, 0.85f, 0.3f));
			}
			else
			{
				const FLinearColor Bc = P.Health < 0.95f ? FMath::Lerp(Bad(), Good(), P.Health) : Good();
				Pen.Bar(PX + BarX, PY + FMath::Max(2.0f, RowH * 0.2f), BarW, FMath::Clamp(RowH * 0.4f, 6.0f, 12.0f), P.Health, Bc);
			}
		}
		// Underneath: how many are loose (or that it's all bolted up).
		const int32 Absent = D.Needed - D.Fitted;
		FString Sum = LooseCount > 0 ? FString::Printf(TEXT("%d LOOSE"), LooseCount) : FString(TEXT("ALL BOLTED UP"));
		if (Absent > 0) Sum += FString::Printf(TEXT("   %d MISSING"), Absent);
		Pen.Text(560, H - 8, Sum, 12, LooseCount > 0 || Absent > 0 ? Amber() : Good());
	}
}

void ATorqueShedScreens::DrawComputer(UCanvas* C, float Now) const
{
	using namespace TSSI;
	FPen Pen;
	Pen.C = C;
	Pen.Font = GEngine ? GEngine->GetLargeFont() : nullptr;
	if (!Pen.Font) return;
	float FW = 0.0f, FH = 0.0f;
	C->StrLen(Pen.Font, TEXT("Hg"), FW, FH);
	Pen.LineH = FH;
	const float W = (float)PageW, H = (float)PageH;
	const UTorqueBuildSubsystem* B = UTorqueBuildSubsystem::Of(this);
	// The kinds, as the walker lists them: project bikes first, then every kind with parts and a slot.
	TArray<FName> Kinds;
	Kinds.Add(NAME_None);
	for (const FName& K : TorqueShedParts::Kinds())
		if (TorqueShedParts::PartsOfKind(K).Num() > 0 && TorqueShedParts::SlotsForKind(K).Num() > 0) Kinds.Add(K);
	// Whoever's at it here, or the shop turning over on its own (a kind every 6 s, a part every 2 s).
	const ATorqueWalker* User = nullptr;
	for (TActorIterator<ATorqueWalker> It(GetWorld()); It; ++It)
		if (It->IsLocallyControlled() && It->AtComputer()) { User = *It; break; }
	FName Kind;
	TArray<FName> Rows;
	int32 Pick = 0;
	if (User)
	{
		Kind = User->ComputerKind();
		Rows = User->ComputerRows();
		Pick = User->ComputerRow();
	}
	else
	{
		const int32 Turn = int32(Now / 6.0f);
		Kind = Kinds[Turn % Kinds.Num()];
		if (Kind.IsNone()) Rows.Add(FName(TEXT("bike")));
		else Rows = TorqueShedParts::PartsOfKind(Kind);
		Pick = Rows.Num() > 0 ? int32(Now / 2.0f) % Rows.Num() : 0;
	}
	auto KindTitle = [](FName K) { return K.IsNone() ? FString(TEXT("Project bikes")) : TorqueShedParts::KindName(K); };
	auto RowTitle = [](FName R) { return R == FName(TEXT("bike")) ? FString(TEXT("Order a project bike")) : TorqueShedParts::PartName(R); };

	Pen.Rect(0, 0, W, 70, Orange());
	Pen.Text(24, 50, TEXT("PARTS COMPUTER"), 40, Ink());
	Pen.Text(560, 56, User ? FString(TEXT("ORDERING")) : FString(TEXT("F AT THE DESK TO ORDER")), 22, Ink(), W - 584.0f, 1);
	// Left: the kinds, the one showing lit (as many as fit round it).
	const int32 KindRows = 11;
	int32 KindAt = FMath::Max(0, Kinds.IndexOfByKey(Kind));
	const int32 KFrom = FMath::Clamp(KindAt - KindRows / 2, 0, FMath::Max(0, Kinds.Num() - KindRows));
	for (int32 I = KFrom; I < Kinds.Num() && I < KFrom + KindRows; ++I)
	{
		const float Y = 104.0f + (I - KFrom) * 38.0f;
		const bool bOn = I == KindAt;
		if (bOn) Pen.Rect(12, Y - 26, 236, 36, Srgb(1.0f, 0.62f, 0.2f, 0.25f));
		Pen.Text(24, Y, Left(KindTitle(Kinds[I]), 18), 22, bOn ? FLinearColor::White : Dim());
	}
	// Middle: the parts of that kind, the picked one lit.
	const int32 PartRows = 11;
	const int32 PFrom = FMath::Clamp(Pick - PartRows / 2, 0, FMath::Max(0, Rows.Num() - PartRows));
	Pen.Text(272, 104, KindTitle(Kind).ToUpper(), 20, Amber());
	for (int32 I = PFrom; I < Rows.Num() && I < PFrom + PartRows; ++I)
	{
		const float Y = 142.0f + (I - PFrom) * 38.0f;
		const bool bOn = I == Pick;
		Pen.Rect(264, Y - 26, 420, 36, bOn ? Srgb(1.0f, 0.62f, 0.2f, 0.3f) : Panel());
		Pen.Text(276, Y, Left(RowTitle(Rows[I]), 30), 22, bOn ? FLinearColor::White : Srgb(1.0f, 1.0f, 1.0f, 0.75f));
	}
	if (Rows.Num() > PFrom + PartRows) Pen.Text(276, 142.0f + PartRows * 38.0f, FString::Printf(TEXT("and %d more"), Rows.Num() - PFrom - PartRows), 16, Dim());
	// Right: the picked part, what it is and where it goes.
	if (Rows.IsValidIndex(Pick))
	{
		const FName R = Rows[Pick];
		float Y = 104.0f;
		for (const FString& L : Wrap(RowTitle(R), 22))
		{
			Pen.Text(704, Y, L, 26, FLinearColor::White);
			Y += 32.0f;
		}
		Y += 8.0f;
		const FString Desc = R == FName(TEXT("bike")) ? FString(TEXT("Tired, rusty and complete: strip it, restore every part and build it back up."))
			: TorqueShedParts::PartDescription(R);
		for (const FString& L : Wrap(Desc, 30))
		{
			if (Y > H - 130.0f) break;
			Pen.Text(704, Y, L, 18, Srgb(1.0f, 1.0f, 1.0f, 0.75f));
			Y += 24.0f;
		}
		if (R != FName(TEXT("bike")))
		{
			FString Slots;
			for (const FName& S : TorqueShedParts::SlotsForKind(TorqueShedParts::PartKind(R)))
				Slots += (Slots.IsEmpty() ? TEXT("") : TEXT(", ")) + TorqueShedParts::SlotName(S).ToLower();
			if (!Slots.IsEmpty())
			{
				Y += 10.0f;
				Pen.Text(704, Y, Left(TEXT("Fits: ") + Slots, 34), 16, Amber());
			}
		}
		Pen.Text(704, H - 90, User ? FString(TEXT("Left mouse: order it")) : FString(TEXT("Delivered outside the roller door")), 18, Good());
	}
	// How many boxes are waiting to be unpacked.
	int32 Boxes = 0;
	if (B)
		for (const TPair<int32, FTorqueItem>& KV : B->AllItems())
			if (KV.Value.bBoxed) ++Boxes;
	Pen.Rect(0, H - 50, W, 50, Panel());
	Pen.Text(24, H - 17, Boxes == 0 ? FString(TEXT("No deliveries waiting")) : FString::Printf(TEXT("%d %s waiting to be unpacked"), Boxes, Boxes == 1 ? TEXT("box") : TEXT("boxes")),
		18, Boxes == 0 ? Dim() : Amber());
	Pen.Text(560, H - 17, User ? FString(TEXT("W / S choose   Q / E kind   F done")) : FString(), 16, Dim(), W - 584.0f, 1);
}

void ATorqueShedScreens::DrawDyno(UCanvas* C) const
{
	// dyno_graph.gd: power and torque at the wheel against rpm (the run before dashed behind the
	// latest), drawn as far as the sweep has got, and the headline numbers under them.
	using namespace TSSI;
	FPen Pen;
	Pen.C = C;
	Pen.Font = GEngine ? GEngine->GetLargeFont() : nullptr;
	if (!Pen.Font) return;
	float FW = 0.0f, FH = 0.0f;
	C->StrLen(Pen.Font, TEXT("Hg"), FW, FH);
	Pen.LineH = FH;
	const float W = (float)PageW, H = (float)PageH;
	const FLinearColor Power = Srgb(0.851f, 0.467f, 0.122f), TorqueC = Srgb(0.231f, 0.561f, 0.878f), InkC = Srgb(0.9f, 0.92f, 0.95f);
	const FLinearColor InkDim = Srgb(1.0f, 1.0f, 1.0f, 0.62f), Grid = Srgb(1.0f, 1.0f, 1.0f, 0.08f), Axis = Srgb(1.0f, 1.0f, 1.0f, 0.3f);
	const FLinearColor PanelC = Srgb(1.0f, 1.0f, 1.0f, 0.04f), Hot = Srgb(1.0f, 0.42f, 0.32f);
	auto Line = [C](const FVector2D& A, const FVector2D& B2, const FLinearColor& Colour, float Thick)
	{
		FCanvasLineItem L(A, B2);
		L.SetColor(Colour);
		L.LineThickness = Thick;
		C->DrawItem(L);
	};
	const UTorqueBuildSubsystem* B = UTorqueBuildSubsystem::Of(this);
	const int32 Bay = B ? B->DynoBay() : INDEX_NONE;
	const float Pad = 18.0f;
	Pen.Rect(0, 0, W, H, Srgb(0.035f, 0.045f, 0.06f));
	Pen.Text(Pad, 40, Bay != INDEX_NONE ? FString::Printf(TEXT("DYNO: %s"), *B->Bike(Bay).Name.ToUpper()) : FString(TEXT("DYNO")), 30, Power);
	float Rpm = 0.0f, Progress = 1.0f;
	int32 Gear = 0;
	const bool bLive = ATorqueDynoRoom::Live(this, Rpm, Gear, Progress);
	FString GearText = TEXT("N");
	if (Gear > 0) GearText = FString::Printf(TEXT("GEAR %d"), Gear);
	if (bLive)
		Pen.Text(W - Pad - 420.0f, 40, FString::Printf(TEXT("%5.0f rpm   "), Rpm) + GearText, 26, InkC, 420.0f, 1);
	else Progress = 1.0f;
	TArray<const FTorqueDynoRun*> Runs;
	if (Bay != INDEX_NONE)
		for (const FTorqueDynoRun& R : B->Bike(Bay).DynoRuns) Runs.Add(&R);
	if (Runs.Num() == 0)
	{
		const FString Msg = Bay == INDEX_NONE ? FString(TEXT("Wheel a bike onto the rig and run it from the dyno desk."))
			: B->Strapped() ? FString(TEXT("Strapped down. Run it from the dyno desk.")) : FString::Printf(TEXT("Strap it down at the four posts: %d of 4 done up."), B->DynoRoom().StrapsDone());
		Pen.Text(Pad, H * 0.5f, Msg, 28, InkDim, W - 2.0f * Pad, 2);
		return;
	}
	const FTorqueDynoRun& Latest = *Runs.Last();
	const FTorqueDynoRun* Previous = Runs.Num() >= 2 ? Runs[Runs.Num() - 2] : nullptr;
	// The legend, and how hot the engine ran.
	Line(FVector2D(Pad, 62), FVector2D(Pad + 28, 62), InkC, 2.0f);
	Pen.Text(Pad + 36, 68, FString::Printf(TEXT("Run %d (latest)"), Latest.N), 17, InkC);
	if (Previous)
	{
		for (int32 I = 0; I < 3; ++I) Line(FVector2D(Pad + 230 + I * 10, 62), FVector2D(Pad + 236 + I * 10, 62), InkDim, 2.0f);
		Pen.Text(Pad + 266, 68, FString::Printf(TEXT("Run %d (previous)"), Previous->N), 17, InkDim);
	}
	const bool bHot = Latest.Heat > 0.35f;
	Pen.Text(W - Pad - 640.0f, 68, FString::Printf(TEXT("Engine %.0f C  -  fan %s%s"), Latest.TempC, Latest.bFan ? TEXT("on") : TEXT("off"),
		bHot ? TEXT("  -  heat soaked, down on power") : TEXT("")), 17, bHot ? Hot : InkDim, 640.0f, 1);
	// The two charts, sharing the rpm axis.
	float RpmMax = 0.0f;
	for (const FTorqueDynoRun* R : Runs) RpmMax = FMath::Max(RpmMax, R->Redline);
	RpmMax = FMath::CeilToFloat(FMath::Max(RpmMax, 1000.0f) / 1000.0f) * 1000.0f;
	auto Nice = [](float V)
	{
		const float Mag = FMath::Pow(10.0f, FMath::FloorToFloat(FMath::LogX(10.0f, FMath::Max(V, 1.0f))));
		for (float M : {1.0f, 2.0f, 2.5f, 5.0f, 10.0f})
			if (V <= M * Mag) return M * Mag;
		return 10.0f * Mag;
	};
	const float TilesH = 112.0f, Top = 84.0f;
	const float BodyH = H - Top - TilesH - Pad * 1.5f;
	const float Half = (BodyH - Pad) * 0.5f;
	auto Chart = [&](float CY, const TCHAR* Label, bool bPower, const FLinearColor& Colour, bool bRpmLabels)
	{
		const float CX = Pad, CW = W - 2.0f * Pad;
		Pen.Rect(CX, CY, CW, Half, PanelC);
		Pen.Text(CX + 10, CY + 22, Label, 16, InkDim);
		const float PX = CX + 56, PY = CY + 32, PW = CW - 70, PH = Half - (bRpmLabels ? 58.0f : 42.0f);
		float YMax = 1.0f;
		for (const FTorqueDynoRun* R : Runs)
			for (float V : (bPower ? R->Hp : R->Nm)) YMax = FMath::Max(YMax, V);
		YMax = Nice(YMax * 1.1f);
		auto To = [&](float X, float Y) { return FVector2D(PX + PW * FMath::Clamp(X / RpmMax, 0.0f, 1.0f), PY + PH - PH * FMath::Clamp(Y / YMax, 0.0f, 1.0f)); };
		for (int32 I = 0; I < 5; ++I)
		{
			const float Y = PY + PH - PH * I / 4.0f;
			Line(FVector2D(PX, Y), FVector2D(PX + PW, Y), I > 0 ? Grid : Axis, 1.0f);
			Pen.Text(PX - 52, Y + 6, FString::Printf(TEXT("%.0f"), YMax * I / 4.0f), 14, InkDim, 46.0f, 1);
		}
		const int32 Steps = FMath::RoundToInt(RpmMax / 1000.0f);
		const int32 Every = FMath::Max(1, FMath::CeilToInt(Steps / 8.0f));
		for (int32 I = 0; I <= Steps; I += Every)
		{
			const float X = PX + PW * (I * 1000.0f) / RpmMax;
			Line(FVector2D(X, PY), FVector2D(X, PY + PH), Grid, 1.0f);
			if (bRpmLabels) Pen.Text(X - 30, PY + PH + 20, FString::Printf(TEXT("%dk"), I), 14, InkDim, 60.0f, 2);
		}
		if (bRpmLabels) Pen.Text(PX + PW - 120, PY + PH + 40, TEXT("rpm"), 14, InkDim, 120.0f, 1);
		auto Curve = [&](const FTorqueDynoRun& R, float Upto, bool bDashed, const FLinearColor& Col)
		{
			const TArray<float>& Ys = bPower ? R.Hp : R.Nm;
			const int32 N = FMath::Min(R.Rpm.Num(), Ys.Num());
			if (N < 2 || Upto < 0.0f) return;
			const float Stop = FMath::Lerp(R.Rpm[0], R.Rpm[N - 1], FMath::Clamp(Upto, 0.0f, 1.0f));
			for (int32 I = 1; I < N; ++I)
			{
				if (R.Rpm[I - 1] > Stop) break;
				float X1 = R.Rpm[I], Y1 = Ys[I];
				if (X1 > Stop)
				{
					const float F = (Stop - R.Rpm[I - 1]) / FMath::Max(X1 - R.Rpm[I - 1], 1.0f);
					X1 = Stop;
					Y1 = FMath::Lerp(Ys[I - 1], Ys[I], F);
				}
				if (!bDashed || I % 2 == 0) Line(To(R.Rpm[I - 1], Ys[I - 1]), To(X1, Y1), Col, bDashed ? 2.0f : 3.0f);
			}
		};
		if (Previous) Curve(*Previous, 1.0f, true, FLinearColor(Colour.R, Colour.G, Colour.B, 0.55f));
		Curve(Latest, Progress, false, Colour);
		// The peak, once the sweep has passed it.
		const float PeakX = bPower ? Latest.PeakHpRpm : Latest.PeakNmRpm, Peak = bPower ? Latest.PeakHp : Latest.PeakNm;
		if (Progress >= 1.0f && Peak > 0.0f)
		{
			const FVector2D At = To(PeakX, Peak);
			Pen.Rect(At.X - 5, At.Y - 5, 10, 10, Colour);
			Pen.Text(At.X - 70, At.Y - 12, FString::Printf(TEXT("%.1f @ %.0f"), Peak, PeakX), 16, InkC, 140.0f, 2);
		}
	};
	Chart(Top, TEXT("POWER AT THE WHEEL (hp)"), true, Power, false);
	Chart(Top + Half + Pad, TEXT("TORQUE AT THE WHEEL (Nm)"), false, TorqueC, true);
	// The headline numbers, each with its change since the run before.
	const bool bDone = Progress >= 1.0f;
	struct FTile { const TCHAR* Label; FString Value, Sub; float Now, Was, Sign; const TCHAR* Unit; bool bTenths; float Min; };
	const float HpT = Latest.PeakHp / FMath::Max(Latest.Mass + 75.0f, 1.0f) * 1000.0f;
	const float HpTWas = Previous ? Previous->PeakHp / FMath::Max(Previous->Mass + 75.0f, 1.0f) * 1000.0f : 0.0f;
	const FTile Tiles[] = {
		{TEXT("PEAK POWER"), FString::Printf(TEXT("%.1f hp"), Latest.PeakHp), FString::Printf(TEXT("@ %.0f rpm"), Latest.PeakHpRpm), Latest.PeakHp, Previous ? Previous->PeakHp : 0.0f, 1.0f, TEXT(" hp"), true, 0.05f},
		{TEXT("PEAK TORQUE"), FString::Printf(TEXT("%.1f Nm"), Latest.PeakNm), FString::Printf(TEXT("@ %.0f rpm"), Latest.PeakNmRpm), Latest.PeakNm, Previous ? Previous->PeakNm : 0.0f, 1.0f, TEXT(" Nm"), true, 0.05f},
		{TEXT("REV LIMIT"), FString::Printf(TEXT("%.0f rpm"), Latest.Redline), FString::Printf(TEXT("%d gears"), Latest.Gears), Latest.Redline, Previous ? Previous->Redline : 0.0f, 1.0f, TEXT(" rpm"), false, 1.0f},
		{TEXT("POWER TO WEIGHT"), FString::Printf(TEXT("%.0f hp/t"), HpT), FString::Printf(TEXT("%.0f kg with rider"), Latest.Mass + 75.0f), HpT, HpTWas, 1.0f, TEXT(" hp/t"), false, 0.5f},
		{TEXT("ENGINE"), FString::Printf(TEXT("%.0f C"), Latest.TempC), Latest.bFan ? FString(TEXT("fan on")) : FString(TEXT("fan off")), Latest.TempC, Previous ? Previous->TempC : 0.0f, -1.0f, TEXT(" C"), false, 1.0f},
	};
	const int32 NT = UE_ARRAY_COUNT(Tiles);
	const float TW = (W - 2.0f * Pad - 12.0f * (NT - 1)) / NT, TY = H - TilesH - Pad * 0.5f;
	for (int32 I = 0; I < NT; ++I)
	{
		const FTile& T = Tiles[I];
		const float TX = Pad + I * (TW + 12.0f);
		Pen.Rect(TX, TY, TW, TilesH, PanelC);
		Pen.Text(TX + 12, TY + 26, T.Label, 15, InkDim);
		Pen.Text(TX + 12, TY + 66, bDone ? T.Value : FString(TEXT("--")), 30, InkC);
		Pen.Text(TX + 12, TY + 96, bDone ? T.Sub : FString(TEXT("running...")), 15, InkDim);
		if (bDone && Previous && T.Now > 0.0f && T.Was > 0.0f && FMath::Abs(T.Now - T.Was) >= T.Min)
		{
			const bool bBetter = (T.Now - T.Was) * T.Sign > 0.0f;
			FString DeltaText = T.bTenths ? FString::Printf(TEXT("%+.1f"), T.Now - T.Was) : FString::Printf(TEXT("%+.0f"), T.Now - T.Was);
			DeltaText += T.Unit;
			Pen.Text(TX + TW - 140, TY + 96, DeltaText, 16, bBetter ? Good() : Bad(), 128.0f, 1);
		}
	}
}

// --- [tv] YouTube --------------------------------------------------------------------------------

namespace TSSI
{
// The page the browser opens: YouTube's own embedded player (the IFrame API), the video at its
// shared point, filling the TV. Loaded as a page of our own address (LoadString), so the player has
// a page it's embedded in, as YouTube wants.
FString VideoPage(const FString& Id, int32 StartSeconds)
{
	// Chromium only autoplays a muted video without a click, so it starts muted and the game's click
	// on the tap layer (TapVideo) unmutes it; if that stops it, it goes back to muted and plays on. A watchdog presses play
	// whenever it's paused or cued (the thumbnail with the play button: build 134). Its state goes
	// to the log through the page title ("Torque TV page: TorqueTV ..."). The game clicks the tap layer
	// (TapVideo) so Chromium lets it unmute.
	FString Html = TEXT("<!doctype html><html><head><meta charset='utf-8'><style>html,body{margin:0;height:100%;background:#000;overflow:hidden}")
		TEXT("#p{position:absolute;left:0;top:0;width:100%;height:100%}#e{color:#ddd;font:30px sans-serif;position:absolute;inset:0;display:flex;")
		TEXT("align-items:center;justify-content:center;text-align:center}#t{position:absolute;inset:0;z-index:10;background:transparent}")
		TEXT("</style></head><body><div id='p'></div><div id='t' onmousedown='tap()'></div>")
		TEXT("<script>var player=null,vol=80,tried=0,failed=0,last=-9;")
		TEXT("function say(m){document.title='TorqueTV '+m;try{console.log('TorqueTV '+m);}catch(x){}}")
		TEXT("function tap(){say('tap');if(player&&player.unMute){tried=1;failed=0;player.unMute();player.setVolume(vol);player.playVideo();setTimeout(function(){")
		TEXT("var m=player.isMuted(),st=player.getPlayerState();say(!m&&st==1?'sound on':'still muted (muted '+m+', state '+st+')');},1200);}}")
		TEXT("function onYouTubeIframeAPIReady(){player=new YT.Player('p',{width:'100%',height:'100%',videoId:'{ID}',")
		TEXT("playerVars:{autoplay:1,mute:1,start:{START},controls:0,rel:0,playsinline:1,iv_load_policy:3,disablekb:1,origin:'https://torqueshed.game'},")
		TEXT("events:{onReady:function(e){say('ready');e.target.mute();e.target.playVideo();},")
		TEXT("onStateChange:function(e){if(e.data!=last){last=e.data;say('state '+e.data);}")
		TEXT("if(e.data==2&&tried==1&&!failed){failed=1;e.target.mute();e.target.playVideo();say('unmute stopped it: muted');}},")
		TEXT("onError:function(e){say('error '+e.data);document.body.innerHTML=")
		TEXT("\"<div id='e'>This video won't play outside YouTube (error \"+e.data+\").<br>Copy another link and press Ctrl+V at the TV.</div>\";}}});}")
		TEXT("function setVol(v){vol=v;if(player&&player.setVolume){player.setVolume(v);}}")
		TEXT("setInterval(function(){if(!player||!player.getPlayerState)return;var s=player.getPlayerState();")
		TEXT("if(s==-1||s==2||s==5){player.playVideo();}},1500);")
		TEXT("</script><script src='https://www.youtube.com/iframe_api'></script></body></html>");
	Html.ReplaceInline(TEXT("{ID}"), *Id);
	Html.ReplaceInline(TEXT("{START}"), *FString::FromInt(FMath::Max(0, StartSeconds)));
	return Html;
}
}

void ATorqueShedScreens::VideoTick(float Dt)
{
	const UTorqueBuildSubsystem* B = UTorqueBuildSubsystem::Of(this);
	UWorld* World = GetWorld();
	// Where the TVs on YouTube are (for how loud it is where you stand).
	TArray<FVector> On;
	for (int32 Tv = 0; Tv < TorqueShedTv::Tvs && Tv < Quads.Num(); ++Tv)
		if (Quads[Tv] && ChannelOf(Tv) == TorqueShedTv::YouTube) On.Add(Quads[Tv]->Bounds.Origin);
	for (const TPair<int32, UStaticMeshComponent*>& KV : PieceQuads)
	{
		const FTorqueFurniture* F = B ? B->FurnitureById(KV.Key) : nullptr;
		if (F && KV.Value && F->Channel == TorqueShedTv::YouTube) On.Add(KV.Value->Bounds.Origin);
	}
	const FString Id = B ? B->TvVideo() : FString();
	if (On.Num() == 0 || Id.IsEmpty() || !World)
	{
		if (Browser.IsValid()) StopVideo();
		return;
	}
	if (!Browser.IsValid())
	{
		if (!IWebBrowserModule::Get().IsWebModuleAvailable())
		{
			if (!bVideoWarned) UE_LOG(LogTemp, Warning, TEXT("Torque TV: the web browser (CEF) isn't available: no YouTube"));
			bVideoWarned = true;
			return;
		}
		// The browser window itself (so the game can click in it: Chromium only lets a page play
		// sound after a click), shown by a view drawn into VideoRT.
		FCreateBrowserWindowSettings Settings;
		Settings.InitialURL = TEXT("about:blank");
		Settings.bUseTransparency = false;
		Settings.BrowserFrameRate = 30;
		Settings.BackgroundColor = FColor::Black;
		IWebBrowserSingleton* Web = IWebBrowserModule::Get().GetSingleton();
		BrowserWindow = Web ? Web->CreateBrowserWindow(Settings) : nullptr;
		if (!BrowserWindow.IsValid())
		{
			if (!bVideoWarned) UE_LOG(LogTemp, Warning, TEXT("Torque TV: couldn't make a browser window: no YouTube"));
			bVideoWarned = true;
			return;
		}
		Browser = SNew(SWebBrowserView, BrowserWindow).ViewportSize(FVector2D(VideoW, VideoH));
		VideoRenderer = new FWidgetRenderer(true);
		VideoRT = FWidgetRenderer::CreateTargetFor(FVector2D(VideoW, VideoH), TF_Bilinear, true);
		LoadedVideo.Reset();
	}
	// A new video (or the same one started again): open it at the shared point.
	if (LoadedVideo != Id || LoadedAt != B->TvVideoStart())
	{
		LoadedVideo = Id;
		LoadedAt = B->TvVideoStart();
		const int32 Start = FMath::FloorToInt32(FMath::Max(0.0, B->SharedNow() - LoadedAt));
		BrowserWindow->LoadString(TSSI::VideoPage(Id, Start), TEXT("https://torqueshed.game/tv"));
		SinceLoad = 0.0f;
		Taps = 0;
		VolumeIn = 1.0f;
		bDirty = true;   // the TVs on YouTube swap their page for the picture
		UE_LOG(LogTemp, Log, TEXT("Torque TV: YouTube %s from %d s"), *Id, Start);
	}
	if (VideoRenderer && VideoRT) VideoRenderer->DrawWidget(VideoRT, Browser.ToSharedRef(), FVector2D(VideoW, VideoH), Dt);
	// A click in the page every 3 s (up to 8) until it says its sound is on: its tap layer unmutes and
	// plays (beenster, build 135: the picture but no sound). Only a click unmutes it: unmuting on its
	// own stopped the video in build 138.
	SinceLoad += Dt;
	if (Taps < 8 && SinceLoad > 3.0f * (Taps + 1) && !PageTitle.Contains(TEXT("sound on")))
	{
		++Taps;
		TapVideo();
	}
	// The page's state, as its title says it, into the log.
	TitleIn -= Dt;
	if (TitleIn <= 0.0f)
	{
		TitleIn = 0.5f;
		const FString Title = BrowserWindow->GetTitle();
		if (Title != PageTitle)
		{
			PageTitle = Title;
			UE_LOG(LogTemp, Log, TEXT("Torque TV page: %s"), *Title);
		}
	}
	// Louder the nearer you are to a TV that's on it: full within 3 m, gone past 25 m.
	VolumeIn -= Dt;
	if (VolumeIn <= 0.0f)
	{
		VolumeIn = 0.5f;
		const APlayerController* PC = World->GetFirstPlayerController();
		const FVector Ear = PC && PC->PlayerCameraManager ? PC->PlayerCameraManager->GetCameraLocation() : FVector::ZeroVector;
		double Near = TNumericLimits<double>::Max();
		for (const FVector& P : On) Near = FMath::Min(Near, FVector::Dist(P, Ear));
		const int32 Vol = FMath::RoundToInt32(80.0 * FMath::Clamp(1.0 - (Near - 300.0) / 2200.0, 0.0, 1.0));
		BrowserWindow->ExecuteJavascript(FString::Printf(TEXT("if(window.setVol){setVol(%d);}"), Vol));
	}
}

void ATorqueShedScreens::TapVideo()
{
	if (!BrowserWindow.IsValid()) return;
	// A left click near the top left corner, on the page's tap layer (over the player, so the
	// player itself never gets it and doesn't pause).
	const FVector2D At(24.0, 24.0);
	const FGeometry Geo = FGeometry::MakeRoot(FVector2D(VideoW, VideoH), FSlateLayoutTransform());
	TSet<FKey> Held;
	Held.Add(EKeys::LeftMouseButton);
	const FPointerEvent Press(0, At, At, Held, EKeys::LeftMouseButton, 0.0f, FModifierKeysState());
	const FPointerEvent Release(0, At, At, TSet<FKey>(), EKeys::LeftMouseButton, 0.0f, FModifierKeysState());
	BrowserWindow->OnMouseButtonDown(Geo, Press, false);
	BrowserWindow->OnMouseButtonUp(Geo, Release, false);
	UE_LOG(LogTemp, Display, TEXT("Torque TV: tapped the video page (%d)"), Taps);
}

void ATorqueShedScreens::StopVideo()
{
	Browser.Reset();
	BrowserWindow.Reset();
	PageTitle.Reset();
	if (VideoRenderer)
	{
		BeginCleanup(VideoRenderer);
		VideoRenderer = nullptr;
	}
	if (!LoadedVideo.IsEmpty()) bDirty = true;
	LoadedVideo.Reset();
}

void ATorqueShedScreens::DrawYouTubeCard(UCanvas* C) const
{
	// No video yet (or the browser's still starting): how to put one on.
	const float W = (float)PageW, H = (float)PageH;
	TorqueUi::Rect(C, W * 0.5f - 60.0f, H * 0.5f - 120.0f, 120.0f, 84.0f, FLinearColor(0.8f, 0.05f, 0.05f, 1.0f));
	TorqueUi::Text(C, TEXT(">"), W * 0.5f, H * 0.5f - 112.0f, 60.0f, FLinearColor::White, true, TorqueUi::EAlign::Centre, false);
	TorqueUi::Text(C, TEXT("YouTube"), W * 0.5f, H * 0.5f - 10.0f, 44.0f, TorqueUi::Ink, true, TorqueUi::EAlign::Centre);
	TorqueUi::Text(C, TEXT("Copy a YouTube link, look at this TV and press Ctrl+V"), W * 0.5f, H * 0.5f + 60.0f, 24.0f, TorqueUi::Dim, false, TorqueUi::EAlign::Centre);
}
