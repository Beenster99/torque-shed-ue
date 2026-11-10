#include "Shed/TorqueShedScreens.h"

#include "Shed/TorqueBuildSubsystem.h"
#include "Shed/TorqueShedParts.h"
#include "Shed/TorqueShedPower.h"
#include "Shed/TorqueDynoRoom.h"
#include "Shed/TorqueWalker.h"
#include "Coop/TorquePlayerState.h"
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

// Torque Shed's how-to-play pages (tv_channels.gd HOWTO), with this port's keys.
struct FHowTo
{
	const TCHAR* Title;
	TArray<TPair<FString, FString>> Lines;
};

const TArray<FHowTo>& HowTo()
{
	static TArray<FHowTo> Pages;
	if (Pages.Num() == 0)
	{
		auto L = [](const TCHAR* Key, const TCHAR* What) { return TPair<FString, FString>(Key, What); };
		Pages.Add({TEXT("The idea"), {
			L(TEXT(""), TEXT("Strip rusty project bikes down, restore every part,")),
			L(TEXT(""), TEXT("build them back up and ride them out of the roller door.")),
			L(TEXT("Look"), TEXT("at anything: the panel says what it is")),
			L(TEXT("Hold"), TEXT("the work button to do the job the panel shows")),
			L(TEXT("Co-op"), TEXT("everyone works on the same bikes, live"))}});
		Pages.Add({TEXT("On foot"), {
			L(TEXT("WASD"), TEXT("walk  /  mouse: look  /  Shift: run  /  C: crouch")),
			L(TEXT("1 - 6"), TEXT("pick a tool (Q / E or the wheel: next and last)")),
			L(TEXT("Left mouse"), TEXT("hold to work")),
			L(TEXT("Right mouse"), TEXT("the other job (the spray gun: next colour)")),
			L(TEXT("F"), TEXT("use the computer, unpack a box, ride out")),
			L(TEXT("T"), TEXT("the spray gun's finish  /  H: help"))}});
		Pages.Add({TEXT("Restoring a bike"), {
			L(TEXT("2  Wrench"), TEXT("unbolt the part, then take it off with your hands")),
			L(TEXT("3  Brush"), TEXT("scrub off dirt, or leave it in the parts washer")),
			L(TEXT("4  Grinder"), TEXT("rust and wear, on a repair machine")),
			L(TEXT("6  Spray gun"), TEXT("hang the part in the spray booth, then paint it")),
			L(TEXT("1  Fit"), TEXT("hold the part, look at the bike, then bolt it up"))}});
		Pages.Add({TEXT("Building something new"), {
			L(TEXT("Order"), TEXT("parts and project bikes at the parts computer")),
			L(TEXT("Unpack"), TEXT("deliveries come boxed: F opens one")),
			L(TEXT("Frame"), TEXT("put one down in an empty bay to start a bike")),
			L(TEXT("Swaps"), TEXT("a new frame, engine, tank or fork on a built bike")),
			L(TEXT(""), TEXT("owes fabrication: the grinder at the bike makes it"))}});
		Pages.Add({TEXT("Keep the shed tidy"), {
			L(TEXT("General"), TEXT("the black bin takes anything, and old fluid")),
			L(TEXT("Plastic"), TEXT("the blue bin: bottles, cases")),
			L(TEXT("Metal"), TEXT("the green bin: frames, engines, wheels, cans")),
			L(TEXT("Look"), TEXT("at a part: its info says which bin it goes in"))}});
	}
	return Pages;
}

constexpr float HowToPageTime = 9.0f;
constexpr float CrewPageTime = 12.0f;

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

// Where the level's screens are (Tools/ue_build_shed.py): each bay's glass on the back wall
// (y -543, facing into the shed), the crew board's on the west wall (x -643).
constexpr double BayScreenX[4] = {-504.0, -232.0, 232.0, 504.0};
}

ATorqueShedScreens::ATorqueShedScreens()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.1f;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
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
	struct FScreen { FVector Centre, Facing, Right; float W, H; };
	TArray<FScreen> Screens;
	for (int32 I = 0; I < 4; ++I)
		Screens.Add({FVector(TSSI::BayScreenX[I], -542.4, 245.0), FVector(0, 1, 0), FVector(1, 0, 0), 248.0f, 134.0f});
	Screens.Add({FVector(-642.4, 60.0, 255.0), FVector(1, 0, 0), FVector(0, -1, 0), 288.0f, 156.0f});
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
			// Bright enough to read in the lit shed: the bay screens and the board (0 .. 4) out in the shed.
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
	if (UTorqueBuildSubsystem* B = UTorqueBuildSubsystem::Of(this))
	{
		B->OnChanged.Remove(ChangedHandle);
		B->OnLooks.Remove(LooksHandle);
		B->OnItems.Remove(ItemsHandle);
	}
	Super::EndPlay(Reason);
}

void ATorqueShedScreens::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Clock -= DeltaSeconds;
	if (bDirty || Clock <= 0.0f) Redraw();
	else if (UWorld* World = GetWorld())
	{
		// Someone here at the parts computer: its monitor follows what they pick.
		for (TActorIterator<ATorqueWalker> It(World); It; ++It)
			if (It->IsLocallyControlled() && It->AtComputer())
			{
				DrawPage(5, (float)World->GetRealTimeSeconds());
				break;
			}
		// A dyno run playing: its graph grows as the sweep goes.
		float Rpm = 0.0f, Progress = 0.0f;
		int32 Gear = 0;
		if (ATorqueDynoRoom::Live(this, Rpm, Gear, Progress) || bDynoWasLive)
		{
			bDynoWasLive = ATorqueDynoRoom::Live(this, Rpm, Gear, Progress);
			DrawPage(6, (float)World->GetRealTimeSeconds());
		}
	}
}

void ATorqueShedScreens::Redraw()
{
	bDirty = false;
	Clock = 1.0f;   // the crew's doings and the board's pages
	UWorld* World = GetWorld();
	if (!World) return;
	const float Now = (float)World->GetRealTimeSeconds();
	for (int32 I = 0; I < Pages.Num(); ++I) DrawPage(I, Now);
}

void ATorqueShedScreens::DrawPage(int32 Index, float Now)
{
	UTextureRenderTarget2D* RT = Pages.IsValidIndex(Index) ? Pages[Index] : nullptr;
	if (!RT) return;
	if (Index < 4)
	{
		// A bay's screen is on while there's a bike in the bay (wherever it's been wheeled), and goes
		// blank, off, when it's scrapped or stripped to nothing.
		const UTorqueBuildSubsystem* B = UTorqueBuildSubsystem::Of(this);
		const bool bOn = B && B->Exists(Index);
		if (Quads.IsValidIndex(Index) && Quads[Index]) Quads[Index]->SetVisibility(bOn);
		if (!bOn)
		{
			UKismetRenderingLibrary::ClearRenderTarget2D(this, RT, FLinearColor::Black);
			return;
		}
	}
	UKismetRenderingLibrary::ClearRenderTarget2D(this, RT, TSSI::Bg());
	UCanvas* C = nullptr;
	FVector2D Size;
	FDrawToRenderTargetContext Context;
	UKismetRenderingLibrary::BeginDrawCanvasToRenderTarget(this, RT, C, Size, Context);
	if (C)
	{
		if (Index < 4) DrawBay(C, Index);
		else if (Index == 5) DrawComputer(C, Now);
		else if (Index == 6) DrawDyno(C);
		else
		{
			// The crew board: the crew, then each how-to page in turn.
			const int32 N = TSSI::HowTo().Num();
			const float Cycle = TSSI::CrewPageTime + N * TSSI::HowToPageTime;
			const float T = FMath::Fmod(Now, Cycle);
			if (T < TSSI::CrewPageTime) DrawCrew(C);
			else DrawHowTo(C, FMath::Clamp(int32((T - TSSI::CrewPageTime) / TSSI::HowToPageTime), 0, N - 1));
		}
	}
	UKismetRenderingLibrary::EndDrawCanvasToRenderTarget(this, Context);
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
		Pen.Text(24, 160, TEXT("Order a project bike at the parts computer,"), 28, Dim());
		Pen.Text(24, 200, TEXT("or put a frame down here to start one."), 28, Dim());
		Pen.Text(24, 270, TEXT("Then fit an engine, wheels, forks and the rest,"), 22, Dim());
		Pen.Text(24, 302, TEXT("bolt everything up and ride it out of the door."), 22, Dim());
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
	// What to do next, and with which tool.
	Y += 8.0f;
	Pen.Rect(16, Y - 22, 512, 4, Panel());
	Pen.Text(24, Y + 4, TEXT("NEXT"), 16, Amber());
	Pen.Text(90, Y + 4, D.NextTool, 16, FLinearColor::White, 430.0f, 1);
	Y += 30.0f;
	for (const FString& L : Wrap(D.Next, 44))
	{
		if (Y > H - 124.0f) break;
		Pen.Text(24, Y, L, 19, Amber());
		Y += 25.0f;
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

	// Right: every part, in two columns, with its condition (dashboard.gd's part list).
	const int32 RowsPerCol = 16;
	for (int32 I = 0; I < D.Parts.Num() && I < 2 * RowsPerCol; ++I)
	{
		const TSSI::FPartRow& P = D.Parts[I];
		const float PX = 560.0f + (I / RowsPerCol) * 252.0f;
		const float PY = 84.0f + (I % RowsPerCol) * 30.0f;
		Pen.Text(PX, PY + 14, Left(P.Name, 19), 14, P.bFitted ? FLinearColor::White : Bad());
		if (P.bFitted)
		{
			const FLinearColor Bc = P.Health < 0.95f ? FMath::Lerp(Bad(), Good(), P.Health) : Good();
			Pen.Bar(PX + 160, PY + 3, 76, 12, P.Health, Bc);
			if (!P.bBolted) Pen.Text(PX + 162, PY + 26, TEXT("LOOSE"), 11, Srgb(1.0f, 0.85f, 0.3f));
		}
		else
		{
			Pen.Text(PX + 160, PY + 14, TEXT("MISSING"), 12, Bad());
		}
	}
	if (D.Parts.Num() > 2 * RowsPerCol)
		Pen.Text(560, H - 12, FString::Printf(TEXT("and %d more"), D.Parts.Num() - 2 * RowsPerCol), 12, Dim());
}

void ATorqueShedScreens::DrawCrew(UCanvas* C) const
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
	Pen.Rect(0, 0, W, 70, Orange());
	Pen.Text(24, 50, TEXT("CREW BOARD"), 40, Ink());
	const TArray<FCrewRow> Rows = Crew(GetWorld(), B);
	Pen.Text(560, 56, Rows.Num() == 1 ? FString(TEXT("1 IN THE SHED")) : FString::Printf(TEXT("%d IN THE SHED"), Rows.Num()), 24, Ink(), W - 584.0f, 1);
	// Left: everyone, in their colour, and what they're doing.
	float Y = 116.0f;
	for (int32 I = 0; I < Rows.Num() && I < 8; ++I)
	{
		const FCrewRow& R = Rows[I];
		Pen.Rect(16, Y - 30, 500, 50, Panel());
		Pen.Rect(16, Y - 30, 6, 50, R.Colour);
		Pen.Text(34, Y - 4, R.Name + (R.bMe ? TEXT("  (you)") : TEXT("")), 24, R.Colour);
		Pen.Text(34, Y + 16, R.Doing.IsEmpty() ? FString(TEXT("in the shed")) : Left(R.Doing, 50), 16, R.Doing.IsEmpty() ? Dim() : FLinearColor::White);
		Y += 58.0f;
	}
	// Right: each bay's bike, how far along and how it stands.
	for (int32 Bay = 0; Bay < UTorqueBuildSubsystem::Bays; ++Bay)
	{
		const FBay D = Read(B, Bay);
		const float BY = 90.0f + Bay * 112.0f;
		Pen.Rect(548, BY, W - 572.0f, 100, Panel());
		Pen.Text(564, BY + 28, FString::Printf(TEXT("BAY %d"), Bay + 1), 16, Dim());
		if (!D.bExists)
		{
			Pen.Text(564, BY + 62, TEXT("Empty"), 24, Dim());
			continue;
		}
		Pen.Text(640, BY + 30, Left(D.Name, 24), 22, FLinearColor::White);
		Pen.Text(564, BY + 62, D.Status, 18, D.StatusColour);
		Pen.Text(564, BY + 62, FString::Printf(TEXT("%.0f%%"), D.Progress * 100.0f), 18, FLinearColor::White, W - 604.0f, 1);
		Pen.Bar(564, BY + 74, W - 604.0f, 12, D.Progress, D.StatusColour);
	}
	Pen.Text(24, H - 20, TEXT("Next: how to play"), 14, Dim());
}

void ATorqueShedScreens::DrawHowTo(UCanvas* C, int32 Page) const
{
	using namespace TSSI;
	FPen Pen;
	Pen.C = C;
	Pen.Font = GEngine ? GEngine->GetLargeFont() : nullptr;
	const TArray<FHowTo>& All = HowTo();
	if (!Pen.Font || !All.IsValidIndex(Page)) return;
	float FW = 0.0f, FH = 0.0f;
	C->StrLen(Pen.Font, TEXT("Hg"), FW, FH);
	Pen.LineH = FH;
	const float W = (float)PageW, H = (float)PageH;
	const FHowTo& P = All[Page];
	Pen.Rect(0, 0, W, 70, Orange());
	Pen.Text(24, 50, TEXT("HOW TO PLAY"), 40, Ink());
	Pen.Text(560, 56, FString::Printf(TEXT("%d / %d"), Page + 1, All.Num()), 24, Ink(), W - 584.0f, 1);
	Pen.Text(24, 130, P.Title, 34, Amber());
	float Y = 196.0f;
	for (const TPair<FString, FString>& L : P.Lines)
	{
		if (L.Key.IsEmpty())
		{
			Pen.Text(24, Y, L.Value, 24, FLinearColor::White);
		}
		else
		{
			Pen.Rect(16, Y - 28, 220, 40, Panel());
			Pen.Text(28, Y, L.Key, 22, Amber());
			Pen.Text(256, Y, L.Value, 24, FLinearColor::White);
		}
		Y += 56.0f;
	}
	// A dot for each page, the one showing lit.
	for (int32 I = 0; I < All.Num(); ++I)
		Pen.Rect(W * 0.5f - All.Num() * 12.0f + I * 24.0f, H - 28, 12, 12, I == Page ? Amber() : Dim());
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
