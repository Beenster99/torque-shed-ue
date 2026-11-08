#include "Shed/TorqueShedScreens.h"

#include "Shed/TorqueBuildSubsystem.h"
#include "Shed/TorqueShedParts.h"
#include "Coop/TorquePlayerState.h"
#include "CanvasItem.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
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
const FLinearColor Dim() { return Srgb(1.0f, 1.0f, 1.0f, 0.55f); }
const FLinearColor Orange() { return Srgb(1.0f, 0.55f, 0.15f, 0.9f); }
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
		Out.NextTool = TEXT("6  Pressure wash");
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
			L(TEXT("Right mouse"), TEXT("the other job (the spray can: next colour)")),
			L(TEXT("F"), TEXT("use the computer, unpack a box, ride out")),
			L(TEXT("T"), TEXT("the spray can's finish  /  H: help"))}});
		Pages.Add({TEXT("Restoring a bike"), {
			L(TEXT("2  Wrench"), TEXT("unbolt the part, then take it off with your hands")),
			L(TEXT("3  Brush"), TEXT("scrub off dirt, or leave it in the parts washer")),
			L(TEXT("4  Grinder"), TEXT("rust and wear, on a repair machine")),
			L(TEXT("5  Spray"), TEXT("paint a part: pick the colour and the finish")),
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
	PrimaryActorTick.TickInterval = 0.25f;
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
	for (int32 I = 0; I < Screens.Num(); ++I)
	{
		const FScreen& S = Screens[I];
		UTextureRenderTarget2D* RT = UKismetRenderingLibrary::CreateRenderTarget2D(this, PageW, PageH, RTF_RGBA8_SRGB, TSSI::Bg());
		UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(Base, this);
		if (RT && M) M->SetTextureParameterValue(TEXT("Page"), RT);
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
}

void ATorqueShedScreens::Redraw()
{
	bDirty = false;
	Clock = 1.0f;   // the crew's doings and the board's pages
	UWorld* World = GetWorld();
	if (!World) return;
	const float Now = (float)World->GetRealTimeSeconds();
	for (int32 I = 0; I < Pages.Num(); ++I)
	{
		UTextureRenderTarget2D* RT = Pages[I];
		if (!RT) continue;
		UKismetRenderingLibrary::ClearRenderTarget2D(this, RT, TSSI::Bg());
		UCanvas* C = nullptr;
		FVector2D Size;
		FDrawToRenderTargetContext Context;
		UKismetRenderingLibrary::BeginDrawCanvasToRenderTarget(this, RT, C, Size, Context);
		if (C)
		{
			if (I < 4) DrawBay(C, I);
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

	// Left: the build. How far along it is, then the counts, condition, fabrication and paint.
	float Y = 106.0f;
	Pen.Text(24, Y, TEXT("BUILD"), 16, Dim());
	Pen.Text(24, Y, FString::Printf(TEXT("%.0f%%"), D.Progress * 100.0f), 16, D.StatusColour, 496.0f, 1);
	Pen.Bar(24, Y + 8, 496, 16, D.Progress, D.StatusColour);
	Y += 60.0f;
	auto Row = [&Pen](float RowY, const FString& Label, const FString& Value, const FLinearColor& Colour, float Fraction, const FLinearColor& BarColour)
	{
		Pen.Text(24, RowY, Label, 22, Dim());
		Pen.Text(200, RowY, Value, 24, Colour, 120.0f, 1);
		if (Fraction >= 0.0f) Pen.Bar(340, RowY - 16, 180, 14, Fraction, BarColour);
	};
	const float Fit = D.Needed > 0 ? float(D.Fitted) / D.Needed : 0.0f;
	const float Bolt = D.Fitted > 0 ? float(D.Bolted) / D.Fitted : 0.0f;
	Row(Y, TEXT("Parts on"), FString::Printf(TEXT("%d / %d"), D.Fitted, D.Needed), D.Fitted == D.Needed ? Good() : FLinearColor::White, Fit, D.Fitted == D.Needed ? Good() : Bad());
	Y += 36.0f;
	Row(Y, TEXT("Bolted up"), FString::Printf(TEXT("%d / %d"), D.Bolted, D.Fitted), D.Bolted == D.Fitted ? Good() : FLinearColor::White, Bolt, D.Bolted == D.Fitted ? Good() : Amber());
	Y += 36.0f;
	const FLinearColor HealthColour = D.Health >= 0.95f ? Good() : FMath::Lerp(Bad(), Good(), D.Health);
	Row(Y, TEXT("Condition"), FString::Printf(TEXT("%.0f %%"), D.Health * 100.0f), FLinearColor::White, D.Health, HealthColour);
	Y += 36.0f;
	Row(Y, TEXT("Rust"), FString::Printf(TEXT("%.0f %%"), D.Rust * 100.0f), D.Rust > 0.3f ? Bad() : FLinearColor::White, D.Rust, Srgb(0.75f, 0.4f, 0.2f));
	Y += 36.0f;
	Row(Y, TEXT("Dirt"), FString::Printf(TEXT("%.0f %%"), D.Dirt * 100.0f), D.Dirt > 0.3f ? Amber() : FLinearColor::White, D.Dirt, Srgb(0.55f, 0.45f, 0.3f));
	Y += 36.0f;
	if (D.Owed.Num() + D.Made.Num() > 0)
	{
		Row(Y, TEXT("Fabrication"), FString::Printf(TEXT("%d / %d"), D.Made.Num(), D.Owed.Num() + D.Made.Num()),
			D.Owed.Num() == 0 ? Good() : Amber(), float(D.Made.Num()) / (D.Owed.Num() + D.Made.Num()), D.Owed.Num() == 0 ? Good() : Amber());
		Y += 36.0f;
	}
	// Its paint: the tank's and the frame's colours.
	if (D.bHasTankPaint || D.bHasFramePaint)
	{
		Pen.Text(24, Y, TEXT("Paint"), 22, Dim());
		float PX = 200.0f;
		if (D.bHasTankPaint)
		{
			Pen.Rect(PX, Y - 20, 26, 22, FLinearColor(D.TankPaint.R, D.TankPaint.G, D.TankPaint.B, 1.0f));
			Pen.Text(PX + 34, Y, TEXT("tank"), 16, Dim());
			PX += 110.0f;
		}
		if (D.bHasFramePaint)
		{
			Pen.Rect(PX, Y - 20, 26, 22, FLinearColor(D.FramePaint.R, D.FramePaint.G, D.FramePaint.B, 1.0f));
			Pen.Text(PX + 34, Y, TEXT("frame"), 16, Dim());
		}
		Y += 36.0f;
	}
	// What to do next, and with which tool.
	Y += 8.0f;
	Pen.Rect(16, Y - 22, 512, 4, Panel());
	Pen.Text(24, Y + 4, TEXT("NEXT"), 16, Amber());
	Pen.Text(90, Y + 4, D.NextTool, 16, FLinearColor::White, 430.0f, 1);
	Y += 30.0f;
	for (const FString& L : Wrap(D.Next, 44))
	{
		if (Y > H - 70.0f) break;
		Pen.Text(24, Y, L, 19, Amber());
		Y += 25.0f;
	}
	// Who's on it now (the crew's doings naming this bay).
	const FString Tag = FString::Printf(TEXT("bay %d"), Bay + 1);
	TArray<TSSI::FCrewRow> On;
	for (const TSSI::FCrewRow& R : Crew(GetWorld(), UTorqueBuildSubsystem::Of(this)))
		if (R.Doing.Contains(Tag)) On.Add(R);
	float CY = H - 46.0f - 26.0f * FMath::Max(0, FMath::Min(On.Num(), 2) - 1);
	if (On.Num() > 0) Pen.Text(24, CY - 26.0f, TEXT("ON IT NOW"), 14, Dim());
	for (int32 I = 0; I < On.Num() && I < 2; ++I)
	{
		Pen.Rect(24, CY - 14, 12, 12, On[I].Colour);
		Pen.Text(44, CY, Left(On[I].Name + TEXT(": ") + On[I].Doing, 46), 17, On[I].Colour);
		CY += 26.0f;
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
