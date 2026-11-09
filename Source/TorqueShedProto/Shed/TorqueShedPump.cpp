#include "Shed/TorqueShedPump.h"

#include "Shed/TorqueBuildSubsystem.h"
#include "Shed/TorqueShedKit.h"
#include "Shed/TorqueToolModels.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

namespace TorqueShedPumpImpl
{
const TCHAR* const CabinetMesh = TEXT("/Game/Shed/Props/petrol_pump/SM_petrol_pump.SM_petrol_pump");
constexpr int32 HoseBits = 18;
constexpr double HoseRadius = 1.4;
// The nozzle's own frame: x along the spout (out of the front), z up; the hose goes in at the back
// of the handle.
const FVector SpoutTip(19.0, 0.0, -5.5);
const FVector HoseIn(-8.0, 0.0, -9.0);
}

namespace TSPI = TorqueShedPumpImpl;

ATorqueShedPump::ATorqueShedPump()
{
	PrimaryActorTick.bCanEverTick = true;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	CubeMesh = Cube.Object;
	CylinderMesh = Cylinder.Object;
}

void ATorqueShedPump::SpawnMissing(UWorld* World)
{
	if (!World) return;
	if (TActorIterator<ATorqueShedPump>(World)) return;
	const FTransform At = UTorqueBuildSubsystem::PumpAt();
	World->SpawnActor<ATorqueShedPump>(At.GetLocation(), At.Rotator());
}

UStaticMeshComponent* ATorqueShedPump::Piece(USceneComponent* Parent, UStaticMesh* Mesh, const FLinearColor& Colour, float Metallic, float Roughness)
{
	if (!Mesh) return nullptr;
	UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
	C->SetStaticMesh(Mesh);
	C->SetMaterial(0, TorqueToolModels::Finish(this, Colour, Metallic, Roughness));
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetGenerateOverlapEvents(false);
	C->SetMobility(EComponentMobility::Movable);
	C->SetupAttachment(Parent ? Parent : GetRootComponent());
	C->RegisterComponent();
	return C;
}

void ATorqueShedPump::Bar(UStaticMeshComponent* C, const FVector& From, const FVector& To, double Radius)
{
	if (!C) return;
	const FVector D = To - From;
	const double Len = FMath::Max(D.Size(), 0.1);
	C->SetWorldLocationAndRotation((From + To) * 0.5, FRotationMatrix::MakeFromZ(D / Len).Rotator());
	C->SetWorldScale3D(FVector(Radius / 50.0, Radius / 50.0, Len / 100.0));
}

void ATorqueShedPump::BeginPlay()
{
	Super::BeginPlay();
	SetActorTransform(UTorqueBuildSubsystem::PumpAt());
	// The cabinet (Tools/props/make_props.py petrol_pump): solid to walk into, not to look at (the
	// walker points at its place box, TorqueShedLayout "petrol_pump").
	if (UStaticMesh* SM = LoadObject<UStaticMesh>(nullptr, TSPI::CabinetMesh))
	{
		Cabinet = NewObject<UStaticMeshComponent>(this);
		Cabinet->SetStaticMesh(SM);
		Cabinet->SetMobility(EComponentMobility::Movable);
		Cabinet->SetupAttachment(GetRootComponent());
		Cabinet->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Cabinet->SetCollisionResponseToAllChannels(ECR_Ignore);
		Cabinet->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
		Cabinet->RegisterComponent();
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("Torque pump: no SM_petrol_pump (run Tools/ue_import_props.py petrol_pump); drawing a plain box"));
		Cabinet = Piece(nullptr, CubeMesh, FLinearColor(0.55f, 0.05f, 0.04f), 0.0f, 0.4f);
		if (Cabinet)
		{
			Cabinet->SetRelativeLocation(FVector(0.0, 13.0, 105.0));
			Cabinet->SetRelativeScale3D(FVector(0.46, 0.24, 0.9));
		}
	}
	// The display: the sale's litres on the top line, the grade under it (the green digit strips).
	auto Text = [this](double Z, float Size)
	{
		UTextRenderComponent* T = NewObject<UTextRenderComponent>(this);
		T->SetupAttachment(GetRootComponent());
		T->SetRelativeLocationAndRotation(FVector(3.0, 26.0, Z), FRotator(0.0, 90.0, 0.0));
		T->SetHorizontalAlignment(EHTA_Center);
		T->SetVerticalAlignment(EVRTA_TextCenter);
		T->SetWorldSize(Size);
		T->SetTextRenderColor(FColor(120, 255, 140));
		T->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		T->RegisterComponent();
		return T;
	};
	Litres = Text(133.0, 3.6f);
	Grade = Text(127.0, 2.4f);
	Grade->SetText(FText::FromString(TEXT("UNLEADED  95")));
	Litres->SetText(FText::FromString(TEXT("0.00 L")));

	// The nozzle: a black handle and guard, the alloy body and its spout, a dark trigger.
	Nozzle = NewObject<USceneComponent>(this);
	Nozzle->SetMobility(EComponentMobility::Movable);
	Nozzle->SetupAttachment(GetRootComponent());
	Nozzle->RegisterComponent();
	const FLinearColor Black(0.02f, 0.02f, 0.022f), Alloy(0.62f, 0.63f, 0.64f), Green(0.05f, 0.32f, 0.12f);
	auto Box = [this](const FLinearColor& Colour, float Metal, float Rough, const FVector& At, const FVector& Size, const FRotator& Turn)
	{
		if (UStaticMeshComponent* C = Piece(Nozzle, CubeMesh, Colour, Metal, Rough))
		{
			C->SetRelativeLocationAndRotation(At, Turn);
			C->SetRelativeScale3D(Size / 100.0);
		}
	};
	Box(Black, 0.0f, 0.55f, FVector(-3.0, 0.0, -6.0), FVector(13.0, 3.4, 3.6), FRotator(-25.0, 0.0, 0.0));   // handle
	Box(Alloy, 0.9f, 0.3f, FVector(4.0, 0.0, -1.5), FVector(9.0, 4.2, 6.0), FRotator::ZeroRotator);        // body
	Box(Green, 0.0f, 0.45f, FVector(-1.5, 0.0, -11.5), FVector(12.0, 3.0, 1.2), FRotator(-8.0, 0.0, 0.0)); // guard (unleaded green)
	Box(Black, 0.0f, 0.5f, FVector(-1.0, 0.0, -8.5), FVector(6.0, 1.4, 1.4), FRotator(-15.0, 0.0, 0.0));   // trigger
	if (UStaticMeshComponent* S = Piece(Nozzle, CylinderMesh, Alloy, 0.95f, 0.25f))
	{
		// The spout, angled down out of the body's front.
		S->SetRelativeLocationAndRotation(FVector(13.0, 0.0, -3.5), FRotator(-100.0, 0.0, 0.0));
		S->SetRelativeScale3D(FVector(0.024, 0.024, 0.13));
	}
	// The hose: black rubber, from the cabinet's foot to the back of the nozzle.
	for (int32 I = 0; I < TSPI::HoseBits; ++I)
		if (UStaticMeshComponent* H = Piece(nullptr, CylinderMesh, FLinearColor(0.015f, 0.015f, 0.016f), 0.0f, 0.62f))
			Hose.Add(H);
	if (const UTorqueBuildSubsystem* B = UTorqueBuildSubsystem::Of(this)) LastSale = B->PumpSale();
}

void ATorqueShedPump::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const UTorqueBuildSubsystem* B = UTorqueBuildSubsystem::Of(this);
	if (!B || !Nozzle) return;
	const float Sale = B->PumpSale();
	if (Sale > LastSale + 0.0001f) Pumping = 0.0;
	else Pumping += DeltaSeconds;
	LastSale = Sale;
	if (Litres) Litres->SetText(FText::FromString(FString::Printf(TEXT("%.2f L"), Sale)));

	// Where the nozzle is: in its holster (spout down into it), in the hand of whoever has it
	// (low and to the right of their view), or in the filler of the bike they're filling.
	FTransform Want(FRotator(-80.0, 90.0, 0.0), UTorqueBuildSubsystem::PumpHolster() + FVector(0.0, 0.0, 14.0));
	const int32 By = B->PumpHeldBy();
	bool bSnap = false;
	if (By != 0)
	{
		if (const APawn* P = TorqueShedKit::PawnOf(this, By))
		{
			const FRotator Aim = P->GetBaseAimRotation();
			const FRotator Yaw(0.0, Aim.Yaw, 0.0);
			Want = FTransform(FRotator(FMath::Clamp(Aim.Pitch, -50.0, 30.0) - 20.0, Aim.Yaw, 0.0),
				P->GetPawnViewLocation() + Yaw.RotateVector(FVector(38.0, 20.0, -42.0)));
			if (Pumping < 0.7)
			{
				// Pumping: into the filler of the nearest bike within an arm's reach.
				int32 Best = INDEX_NONE;
				double BestD = 220.0;
				for (int32 Bay = 0; Bay < 8; ++Bay)
				{
					if (!B->Exists(Bay)) continue;
					const double D = FVector::Dist(B->FillerOf(Bay), P->GetActorLocation());
					if (D < BestD) { BestD = D; Best = Bay; }
				}
				if (Best != INDEX_NONE)
				{
					const FRotator Into(-45.0, Aim.Yaw, 0.0);
					const FVector Filler = B->FillerOf(Best);
					Want = FTransform(Into, Filler - Into.RotateVector(TSPI::SpoutTip) + FVector(0.0, 0.0, 2.0));
				}
			}
		}
	}
	else if (!bPlaced) bSnap = true;
	if (bSnap || !bPlaced)
	{
		NozzleAt = Want;
		bPlaced = true;
	}
	else
	{
		const double A = 1.0 - FMath::Exp(-18.0 * DeltaSeconds);
		NozzleAt.SetLocation(FMath::Lerp(NozzleAt.GetLocation(), Want.GetLocation(), A));
		NozzleAt.SetRotation(FQuat::Slerp(NozzleAt.GetRotation(), Want.GetRotation(), A));
	}
	Nozzle->SetWorldTransform(NozzleAt);

	// The hose: a sagging curve from the cabinet to the nozzle, as long as it is, lying on the ground
	// where it reaches it.
	const FVector From = UTorqueBuildSubsystem::PumpHoseRoot();
	const FVector To = NozzleAt.TransformPosition(TSPI::HoseIn);
	const double Span = FVector::Dist(From, To);
	const double L = UTorqueBuildSubsystem::PumpHose;
	const double Sag = 0.5 * FMath::Sqrt(FMath::Max(0.0, L * L - Span * Span));
	const int32 N = Hose.Num();
	if (N == 0) return;
	auto At = [&](double T)
	{
		FVector Pt = FMath::Lerp(From, To, T);
		// Hanging straight down out of the cabinet first, then the sag.
		Pt.Z -= Sag * 4.0 * T * (1.0 - T);
		Pt.Z = FMath::Max(Pt.Z, TSPI::HoseRadius + 0.5);
		return Pt;
	};
	FVector Prev = At(0.0);
	for (int32 I = 0; I < N; ++I)
	{
		const FVector Next = At(double(I + 1) / N);
		Bar(Hose[I], Prev, Next, TSPI::HoseRadius);
		Prev = Next;
	}
}
