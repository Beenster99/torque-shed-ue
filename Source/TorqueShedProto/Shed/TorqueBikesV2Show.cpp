#include "Shed/TorqueBikesV2Show.h"

#include "Fit/FitJson.h"
#include "Shed/TorqueShedCatalogue.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"

#include <string>

namespace TorqueBikesV2ShowImpl
{
// Data/BikesV2/show.json, or false if it isn't there or doesn't parse.
bool ReadShow(TorqueFit::Json& Out)
{
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *FPaths::Combine(FPaths::ProjectDir(), TEXT("Data/BikesV2/show.json")))) return false;
	const FTCHARToUTF8 Utf8(*Text);
	std::string Error;
	if (!TorqueFit::Json::Parse(std::string(Utf8.Get(), Utf8.Length()), Out, &Error))
	{
		UE_LOG(LogTemp, Warning, TEXT("Torque: Data/BikesV2/show.json didn't parse (%s)"), UTF8_TO_TCHAR(Error.c_str()));
		return false;
	}
	return true;
}
} // namespace TorqueBikesV2ShowImpl

ATorqueBikesV2Show::ATorqueBikesV2Show()
{
	PrimaryActorTick.bCanEverTick = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void ATorqueBikesV2Show::SpawnIn(UWorld* World)
{
	if (!World || !World->GetMapName().Contains(TEXT("Shed"))) return;
	// -TorqueNoBikesV2: off for this run, whatever show.json says.
	if (FParse::Param(FCommandLine::Get(), TEXT("TorqueNoBikesV2"))) return;
	TorqueFit::Json Show;
	if (!TorqueBikesV2ShowImpl::ReadShow(Show) || !Show.Flag("enabled", false)) return;
	World->SpawnActor<ATorqueBikesV2Show>(FVector::ZeroVector, FRotator::ZeroRotator);
}

void ATorqueBikesV2Show::BeginPlay()
{
	Super::BeginPlay();
	TorqueFit::Json Show;
	if (!TorqueBikesV2ShowImpl::ReadShow(Show)) return;
	const TorqueFit::Json* Bikes = Show.Get("bikes");
	if (!Bikes) return;
	int32 Index = 0;
	for (const TorqueFit::Json& B : Bikes->A)
	{
		const FString Model = UTF8_TO_TCHAR(B.Str("model").c_str());
		const TorqueFit::Json* At = B.Get("at_cm");
		if (Model.IsEmpty() || !At || At->A.size() < 2) continue;
		USceneComponent* Stand = NewObject<USceneComponent>(this, *FString::Printf(TEXT("BikeV2_%d"), Index++));
		Stand->SetupAttachment(RootComponent);
		Stand->RegisterComponent();
		// (The model's origin is on the ground under its rear axle: stood so its middle is over the spot.)
		const FRotator Facing(0.0, B.Num("yaw", 90.0), 0.0);
		Stand->SetRelativeLocationAndRotation(FVector(At->A[0].N, At->A[1].N, 0.0) - Facing.Vector() * 72.0, Facing);
		TArray<UStaticMeshComponent*> Made;
		TorqueShedCatalogue::MakeV2Display(this, Stand, Model, Made);
	}
}
