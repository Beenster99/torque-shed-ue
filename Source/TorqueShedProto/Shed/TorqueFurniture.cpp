// The shed's and the yard's movable kit (TorqueFurniture.h): its models and the yard's layout from
// Data/Furniture, the shed's say over it (UTorqueBuildSubsystem's furniture requests), and drawing it.
#include "Shed/TorqueFurniture.h"
#include "Shed/TorqueBuildSubsystem.h"
#include "Shed/TorqueShedKit.h"
#include "Shed/TorqueShedLayout.h"
#include "Shed/TorqueToolModels.h"
#include "Sim/BikeContacts.h"
#include "TorqueAudio/TorqueSfx.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/ConstructorHelpers.h"

namespace TorqueFurnitureImpl
{
bool LoadJson(const TCHAR* Rel, TSharedPtr<FJsonObject>& Out)
{
	FString Text;
	const FString Path = FPaths::Combine(FPaths::ProjectDir(), Rel);
	if (!FFileHelper::LoadFileToString(Text, *Path) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Out) || !Out.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("Torque furniture: couldn't read %s"), *Path);
		return false;
	}
	return true;
}

double Num(const TArray<TSharedPtr<FJsonValue>>& A, int32 I, double Default = 0.0)
{
	return A.IsValidIndex(I) && A[I].IsValid() ? A[I]->AsNumber() : Default;
}

FTransform At(const TSharedPtr<FJsonObject>& O)
{
	const TArray<TSharedPtr<FJsonValue>>* P = nullptr;
	double Yaw = 0.0, Z = 0.0;
	O->TryGetNumberField(TEXT("yaw"), Yaw);
	O->TryGetNumberField(TEXT("z"), Z);
	FVector Loc(0.0, 0.0, Z);
	if (O->TryGetArrayField(TEXT("at"), P))
	{
		Loc.X = Num(*P, 0);
		Loc.Y = Num(*P, 1);
		if (P->Num() > 2) Loc.Z = Num(*P, 2);
	}
	return FTransform(FRotator(0.0, Yaw, 0.0), Loc);
}

int32& Version()
{
	static int32 V = 1;
	return V;
}

// How far in front of the mover a piece goes: clear of their feet by its own depth.
double Reach(const TorqueFurniture::FModel& M) { return FMath::Max(M.Size.Z, M.Size.X) * 0.5 + 55.0; }
} // namespace TorqueFurnitureImpl

namespace TFI = TorqueFurnitureImpl;

// --- What there is ---------------------------------------------------------------------------------------

namespace TorqueFurniture
{
struct FData
{
	TMap<FName, FModel> Models;
	TArray<TPair<FName, FString>> Kinds;
	FYard Yard;
	TArray<FPlaced> ShedStart;
};

static const FData& Data()
{
	static FData D;
	static bool bLoaded = false;
	if (bLoaded) return D;
	bLoaded = true;
	TSharedPtr<FJsonObject> J;
	if (TFI::LoadJson(TEXT("Data/Furniture/models.json"), J))
	{
		const TArray<TSharedPtr<FJsonValue>>* Kinds = nullptr;
		if (J->TryGetArrayField(TEXT("kinds"), Kinds))
			for (const TSharedPtr<FJsonValue>& K : *Kinds)
			{
				const TArray<TSharedPtr<FJsonValue>>& P = K->AsArray();
				if (P.Num() >= 2) D.Kinds.Add({FName(*P[0]->AsString()), P[1]->AsString()});
			}
		const TSharedPtr<FJsonObject>* Models = nullptr;
		if (J->TryGetObjectField(TEXT("models"), Models))
			for (const TPair<FString, TSharedPtr<FJsonValue>>& KV : (*Models)->Values)
			{
				const TSharedPtr<FJsonObject> O = KV.Value->AsObject();
				if (!O.IsValid()) continue;
				FModel M;
				M.Id = FName(*KV.Key);
				M.Kind = FName(*O->GetStringField(TEXT("kind")));
				M.Name = O->GetStringField(TEXT("name"));
				O->TryGetStringField(TEXT("desc"), M.Desc);
				const TArray<TSharedPtr<FJsonValue>>* S = nullptr;
				if (O->TryGetArrayField(TEXT("size"), S)) M.Size = FVector(TFI::Num(*S, 0, 1.0), TFI::Num(*S, 1, 1.0), TFI::Num(*S, 2, 1.0)) * 100.0;
				O->TryGetBoolField(TEXT("yard"), M.bYard);
				O->TryGetBoolField(TEXT("wall"), M.bWall);
				O->TryGetBoolField(TEXT("ride"), M.bRide);
				O->TryGetBoolField(TEXT("solid"), M.bSolid);
				O->TryGetBoolField(TEXT("sold"), M.bSold);
				D.Models.Add(M.Id, M);
			}
	}
	if (TFI::LoadJson(TEXT("Data/Furniture/yard.json"), J))
	{
		const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
		for (const TCHAR* Field : {TEXT("start"), TEXT("fixed")})
			if (J->TryGetArrayField(Field, A))
				for (const TSharedPtr<FJsonValue>& V : *A)
				{
					const TSharedPtr<FJsonObject> O = V->AsObject();
					if (!O.IsValid()) continue;
					FPlaced P{FName(*O->GetStringField(TEXT("model"))), TFI::At(O)};
					if (!D.Models.Contains(P.Model)) continue;
					(FCString::Strcmp(Field, TEXT("start")) == 0 ? D.Yard.Start : D.Yard.Fixed).Add(P);
				}
		if (J->TryGetArrayField(TEXT("paint"), A))
			for (const TSharedPtr<FJsonValue>& V : *A)
			{
				const TArray<TSharedPtr<FJsonValue>>& R = V->AsArray();
				if (R.Num() < 8) continue;
				D.Yard.Marks.Add({FVector2D(TFI::Num(R, 0), TFI::Num(R, 1)), FVector2D(TFI::Num(R, 2), TFI::Num(R, 3)), TFI::Num(R, 4),
					FLinearColor((float)TFI::Num(R, 5), (float)TFI::Num(R, 6), (float)TFI::Num(R, 7))});
			}
		if (J->TryGetArrayField(TEXT("words"), A))
			for (const TSharedPtr<FJsonValue>& V : *A)
			{
				const TArray<TSharedPtr<FJsonValue>>& R = V->AsArray();
				if (R.Num() < 8) continue;
				D.Yard.Words.Add({R[0]->AsString(), FVector2D(TFI::Num(R, 1), TFI::Num(R, 2)), TFI::Num(R, 3), TFI::Num(R, 4),
					FLinearColor((float)TFI::Num(R, 5), (float)TFI::Num(R, 6), (float)TFI::Num(R, 7))});
			}
		if (J->TryGetArrayField(TEXT("drop"), A) && A->Num() >= 4)
			D.Yard.Drop = FBox2D(FVector2D(TFI::Num(*A, 0), TFI::Num(*A, 1)), FVector2D(TFI::Num(*A, 2), TFI::Num(*A, 3)));
	}
	// Indoors: a stool by the workbench, a camp chair, and a shelving unit against the east wall by
	// bay 4 (clear of the stand, the computer and the lift's post), as Torque Shed's new shed has.
	for (const FPlaced& P : {FPlaced{FName(TEXT("seat_stool")), FTransform(FRotator(0.0, -90.0, 0.0), FVector(-500.0, 330.0, 0.0))},
		FPlaced{FName(TEXT("seat_chair")), FTransform(FRotator(0.0, 200.0, 0.0), FVector(420.0, 300.0, 0.0))},
		FPlaced{FName(TEXT("shelf_standard")), FTransform(FRotator(0.0, 90.0, 0.0), FVector(615.0, -220.0, 0.0))}})
		if (D.Models.Contains(P.Model)) D.ShedStart.Add(P);
	UE_LOG(LogTemp, Log, TEXT("Torque furniture: %d models, yard %d to start, %d fixed, %d marks, %d words"), D.Models.Num(),
		D.Yard.Start.Num(), D.Yard.Fixed.Num(), D.Yard.Marks.Num(), D.Yard.Words.Num());
	return D;
}

const TMap<FName, FModel>& Models() { return Data().Models; }
const FModel* Find(FName Model) { return Data().Models.Find(Model); }
const TArray<TPair<FName, FString>>& Kinds() { return Data().Kinds; }
const FYard& Yard() { return Data().Yard; }
const TArray<FPlaced>& ShedStart() { return Data().ShedStart; }

TArray<FName> SoldOfKind(FName Kind)
{
	TArray<FName> Out;
	for (const TPair<FName, FModel>& KV : Models())
		if (KV.Value.bSold && KV.Value.Kind == Kind) Out.Add(KV.Key);
	// Smallest first, as Torque Shed's lists go from compact to big.
	Out.Sort([](const FName& A, const FName& B)
	{
		const FModel& MA = *Find(A);
		const FModel& MB = *Find(B);
		const double VA = MA.Size.X * MA.Size.Y * MA.Size.Z, VB = MB.Size.X * MB.Size.Y * MB.Size.Z;
		return VA != VB ? VA < VB : A.LexicalLess(B);
	});
	return Out;
}

UStaticMesh* Mesh(FName Model)
{
	static TMap<FName, TWeakObjectPtr<UStaticMesh>> Cache;
	if (const TWeakObjectPtr<UStaticMesh>* C = Cache.Find(Model))
		if (C->IsValid()) return C->Get();
	const FString Id = Model.ToString();
	UStaticMesh* M = LoadObject<UStaticMesh>(nullptr, *FString::Printf(TEXT("/Game/Shed/Props/%s/SM_%s.SM_%s"), *Id, *Id, *Id));
	Cache.Add(Model, M);
	return M;
}

FTransform MovedXf(const APawn* Pawn, const FModel& M, double Turn)
{
	if (!Pawn) return FTransform::Identity;
	const FRotator View = Pawn->GetBaseAimRotation();
	const FVector Fwd = FRotator(0.0, View.Yaw, 0.0).Vector();
	// Its front (+y at yaw 0) towards the mover: yaw = view + 90, then turned.
	const FRotator Yaw(0.0, View.Yaw + 90.0 + Turn, 0.0);
	const FVector Feet = Pawn->GetActorLocation() - FVector(0.0, 0.0, Pawn->GetSimpleCollisionHalfHeight());
	if (M.bWall)
	{
		const FVector At = Feet + Fwd * (TFI::Reach(M) + 10.0) + FVector(0.0, 0.0, 150.0 - M.Size.Y * 0.5);
		return FTransform(Yaw, At);
	}
	if (M.Small())
	{
		const FVector At = Feet + Fwd * 55.0 + FVector(0.0, 0.0, 105.0 - M.Size.Y * 0.5);
		return FTransform(Yaw, At);
	}
	return FTransform(Yaw, FVector(Feet.X, Feet.Y, 0.0) + Fwd * TFI::Reach(M));
}

bool PutDownXf(const APawn* Pawn, const FModel& M, double Turn, FTransform& Out, FString& WhyNot)
{
	Out = MovedXf(Pawn, M, Turn);
	if (!Pawn || !(M.bWall || M.Small()))
	{
		Out.SetLocation(FVector(Out.GetLocation().X, Out.GetLocation().Y, 0.0));
		return true;
	}
	UWorld* World = Pawn->GetWorld();
	if (!World) return true;
	// Along the look, from the eyes, past the mover and the piece itself.
	FVector Eye;
	FRotator View;
	Pawn->GetActorEyesViewPoint(Eye, View);
	FCollisionQueryParams P(SCENE_QUERY_STAT(TorqueFurniturePut), true, Pawn);
	for (TActorIterator<ATorqueFurnishings> It(World); It; ++It) P.AddIgnoredActor(*It);   // what's moved is drawn by it
	FHitResult Hit;
	const bool bHit = World->LineTraceSingleByChannel(Hit, Eye, Eye + View.Vector() * 280.0, ECC_Visibility, P);
	if (M.bWall)
	{
		if (!bHit || FMath::Abs(Hit.ImpactNormal.Z) > 0.3)
		{
			WhyNot = TEXT("Hold it up to a wall to hang it.");
			return false;
		}
		// Flat to the wall, its front (+y) out of it, its middle where you look (not on the floor).
		const FVector N = FVector(Hit.ImpactNormal.X, Hit.ImpactNormal.Y, 0.0).GetSafeNormal();
		const double Yaw = FMath::RadiansToDegrees(FMath::Atan2(N.Y, N.X)) - 90.0;
		const double Z = FMath::Clamp(Hit.ImpactPoint.Z - M.Size.Y * 0.5, 20.0, 300.0);
		Out = FTransform(FRotator(0.0, Yaw, 0.0), FVector(Hit.ImpactPoint.X, Hit.ImpactPoint.Y, Z) + N * 0.5);
		return true;
	}
	// A small one goes down on whatever's under the look (a bench, a shelf, the floor).
	if (bHit && Hit.ImpactNormal.Z > 0.7)
	{
		Out.SetLocation(Hit.ImpactPoint);
		return true;
	}
	FHitResult Down;
	const FVector From = Out.GetLocation();
	if (World->LineTraceSingleByChannel(Down, From, From - FVector(0.0, 0.0, 400.0), ECC_Visibility, P)) Out.SetLocation(Down.ImpactPoint);
	else Out.SetLocation(FVector(From.X, From.Y, 0.0));
	return true;
}

int32 ContactVersion() { return TFI::Version(); }
void Touch() { ++TFI::Version(); }

void ContactBoxes(const UObject* WorldContext, std::vector<TorqueSim::ContactBox>& Out)
{
	auto Add = [&Out](const FModel& M, const FTransform& Xf)
	{
		if (!M.bSolid) return;
		const FVector C = Xf.GetLocation();
		const FVector U = Xf.GetRotation().RotateVector(FVector::XAxisVector);
		TorqueSim::ContactBox B;
		// Sim frame: metres, x east, z the shed's y, y up.
		B.Cx = C.X / 100.0;
		B.Cz = C.Y / 100.0;
		B.Ux = U.X;
		B.Uz = U.Y;
		B.Hu = M.Size.X / 200.0;
		B.Hv = M.Size.Z / 200.0;
		B.Y0 = C.Z / 100.0;
		B.Y1 = (C.Z + M.Size.Y) / 100.0;
		// A piece that's both ridden and solid (wreck, bus): its top is Road for the wheels, so the box
		// stops 10 cm under it, or the frame probes and bumpers dip into it on a firm landing and the
		// rider gets knocked off the roof (probes reach 3-6 cm of the tyres' surface at full travel).
		if (M.bRide) B.Y1 = FMath::Max(B.Y0, B.Y1 - 0.10);
		Out.push_back(B);
	};
	for (const FPlaced& P : Yard().Fixed)
		if (const FModel* M = Find(P.Model)) Add(*M, P.Xf);
	if (const UTorqueBuildSubsystem* S = UTorqueBuildSubsystem::Of(WorldContext))
		for (const FTorqueFurniture& F : S->Furniture())
			if (F.Mover == 0)
				if (const FModel* M = Find(F.Model)) Add(*M, F.Xf);
}
} // namespace TorqueFurniture

// --- The shed's say ----------------------------------------------------------------------------------------

bool UTorqueBuildSubsystem::IsFurnitureAct(ETorqueShedAct A)
{
	return A == ETorqueShedAct::GrabFurniture || A == ETorqueShedAct::TurnFurniture || A == ETorqueShedAct::ReleaseFurniture
		|| A == ETorqueShedAct::OrderFurniture;
}

const FTorqueFurniture* UTorqueBuildSubsystem::FurnitureById(int32 Id) const
{
	return Furnishings.FindByPredicate([Id](const FTorqueFurniture& F) { return F.Id == Id; });
}

int32 UTorqueBuildSubsystem::FurnitureMovedBy(int32 Peer) const
{
	if (Peer == 0) return 0;
	const FTorqueFurniture* F = Furnishings.FindByPredicate([Peer](const FTorqueFurniture& X) { return X.Mover == Peer; });
	return F ? F->Id : 0;
}

void UTorqueBuildSubsystem::FurnishFresh()
{
	Furnishings.Reset();
	FurnitureNextId = 1;
	for (const TorqueFurniture::FPlaced& P : TorqueFurniture::Yard().Start) Furnishings.Add({FurnitureNextId++, P.Model, P.Xf, 0, 0.0f});
	for (const TorqueFurniture::FPlaced& P : TorqueFurniture::ShedStart()) Furnishings.Add({FurnitureNextId++, P.Model, P.Xf, 0, 0.0f});
	bFurnished = true;
	TorqueFurniture::Touch();
}

FVector UTorqueBuildSubsystem::FurnitureDropAt(const TorqueFurniture::FModel& M) const
{
	// Yard kit goes in the yard's drop-off box, shed kit on the yard outside the roller door, east of
	// the deliveries (Shed.DELIVERY_AREA): a free spot on a grid, nearest first.
	const FBox2D Box = M.bYard && TorqueFurniture::Yard().Drop.bIsValid ? TorqueFurniture::Yard().Drop
		: FBox2D(FVector2D(-1500.0, 750.0), FVector2D(-300.0, 1500.0));
	const double Step = FMath::Max3(M.Size.X, M.Size.Z, 80.0) + 60.0;
	for (double Y = Box.Min.Y + Step * 0.5; Y < Box.Max.Y; Y += Step)
		for (double X = Box.Min.X + Step * 0.5; X < Box.Max.X; X += Step)
		{
			bool bClear = true;
			for (const FTorqueFurniture& F : Furnishings)
			{
				const TorqueFurniture::FModel* O = TorqueFurniture::Find(F.Model);
				const double R = (O ? FMath::Max(O->Size.X, O->Size.Z) * 0.5 : 50.0) + Step * 0.5;
				if (FVector::Dist2D(F.Xf.GetLocation(), FVector(X, Y, 0.0)) < R) { bClear = false; break; }
			}
			if (bClear) return FVector(X, Y, 0.0);
		}
	return FVector(Box.GetCenter(), 0.0);
}

FString UTorqueBuildSubsystem::CheckFurniture(int32 Peer, const FTorqueShedRequest& R) const
{
	const FTorqueFurniture* F = FurnitureById(R.Item);
	const TorqueFurniture::FModel* M = F ? TorqueFurniture::Find(F->Model) : nullptr;
	switch (R.Kind)
	{
	case ETorqueShedAct::GrabFurniture:
		if (!F || !M) return TEXT("That's gone.");
		if (F->Mover == Peer) return FString::Printf(TEXT("You've got the %s."), *M->Name.ToLower());
		if (F->Mover != 0) return FString::Printf(TEXT("Someone else is moving the %s."), *M->Name.ToLower());
		if (FurnitureMovedBy(Peer) != 0) return TEXT("Put down what you're moving first (F).");
		if (HeldBy(Peer) != 0 || Paint.Held.Contains(Peer) || Paint.TapeBy == Peer || PushedBy(Peer) != INDEX_NONE
			|| StandPusher == Peer || ExtinguisherBy == Peer)
			return TEXT("Your hands are full: put it down first.");
		return FString();
	case ETorqueShedAct::TurnFurniture:
		return F && F->Mover == Peer ? FString() : FString(TEXT("You're not moving that."));
	case ETorqueShedAct::ReleaseFurniture:
		if (!F || F->Mover != Peer) return TEXT("You're not moving that.");
		if (!TorqueShedLayout::OnSite(R.Place.GetLocation())) return TEXT("Not out there: keep it in the shed or the yard.");
		return FString();
	case ETorqueShedAct::OrderFurniture:
	{
		const TorqueFurniture::FModel* O = TorqueFurniture::Find(R.Part);
		if (!O || !O->bSold) return TEXT("The shop doesn't sell that.");
		if (Furnishings.Num() >= 400) return TEXT("The shed and the yard are full of kit: that's plenty.");
		return FString();
	}
	default:
		return TEXT("The shed doesn't know how to do that.");
	}
}

FString UTorqueBuildSubsystem::ApplyFurniture(int32 Peer, const FTorqueShedRequest& R)
{
	FTorqueFurniture* F = Furnishings.FindByPredicate([&R](const FTorqueFurniture& X) { return X.Id == R.Item; });
	const TorqueFurniture::FModel* M = F ? TorqueFurniture::Find(F->Model) : nullptr;
	TorqueFurniture::Touch();
	switch (R.Kind)
	{
	case ETorqueShedAct::GrabFurniture:
		if (!F || !M) return FString();
		F->Mover = Peer;
		F->Turn = 0.0f;
		return FString::Printf(TEXT("Moving the %s: R turns it, F puts it down."), *M->Name.ToLower());
	case ETorqueShedAct::TurnFurniture:
		if (F) F->Turn = FMath::UnwindDegrees(F->Turn + R.Amount);
		return FString();
	case ETorqueShedAct::ReleaseFurniture:
	{
		if (!F) return FString();
		const FVector At = R.Place.GetLocation();
		const bool bLifted = M && (M->bWall || M->Small());
		F->Xf = FTransform(FRotator(0.0, R.Place.Rotator().Yaw, 0.0), FVector(At.X, At.Y, bLifted ? FMath::Clamp(At.Z, -10.0, 400.0) : 0.0));
		F->Mover = 0;
		F->Turn = 0.0f;
		return FString();
	}
	case ETorqueShedAct::OrderFurniture:
	{
		const TorqueFurniture::FModel* O = TorqueFurniture::Find(R.Part);
		if (!O) return FString();
		const FVector At = FurnitureDropAt(*O);
		Furnishings.Add({FurnitureNextId++, R.Part, FTransform(FRotator(0.0, 180.0, 0.0), At), 0, 0.0f});
		return FString::Printf(TEXT("%s ordered: it's %s."), *O->Name, O->bYard ? TEXT("in the yard kit drop-off out on the pad") : TEXT("outside, left of the roller door"));
	}
	default:
		return FString();
	}
}

void UTorqueBuildSubsystem::DropFurnitureOf(int32 Peer)
{
	bool bChanged = false;
	for (FTorqueFurniture& F : Furnishings)
		if (F.Mover == Peer)
		{
			// Where they were moving it when they went (a small one onto the floor under it).
			if (const TorqueFurniture::FModel* M = TorqueFurniture::Find(F.Model))
				if (const APawn* P = TorqueShedKit::PawnOf(this, Peer))
				{
					const FTransform Xf = TorqueFurniture::MovedXf(P, *M, F.Turn);
					F.Xf = FTransform(Xf.GetRotation(), FVector(Xf.GetLocation().X, Xf.GetLocation().Y, 0.0));
				}
			F.Mover = 0;
			F.Turn = 0.0f;
			bChanged = true;
		}
	if (!bChanged) return;
	TorqueFurniture::Touch();
	MarkSave();
	OnHostChange.Broadcast();
}

void UTorqueBuildSubsystem::SerializeFurniture(FArchive& Ar, bool bNet, TArray<FTorqueFurniture>& List, int32& NextIdIo)
{
	// Count, then each: id, model, where (and for the net, who's moving it and how it's turned).
	int32 N = List.Num();
	Ar << N << NextIdIo;
	if (Ar.IsLoading())
	{
		if (N < 0 || N > 4000) { Ar.SetError(); return; }
		List.Reset();
		List.SetNum(N);
	}
	for (FTorqueFurniture& F : List)
	{
		FString Model = F.Model.ToString();
		FVector At = F.Xf.GetLocation();
		double Yaw = F.Xf.Rotator().Yaw;
		Ar << F.Id << Model << At << Yaw;
		if (bNet) Ar << F.Mover << F.Turn;
		if (Ar.IsLoading())
		{
			F.Model = FName(*Model);
			F.Xf = FTransform(FRotator(0.0, Yaw, 0.0), At);
			if (!bNet) F.Mover = 0;
			if (Ar.IsError()) return;
		}
	}
	if (Ar.IsLoading())
	{
		// Models that aren't in this game any more go.
		List.RemoveAll([](const FTorqueFurniture& F) { return TorqueFurniture::Find(F.Model) == nullptr; });
		TorqueFurniture::Touch();
	}
}

// --- Drawing it -------------------------------------------------------------------------------------------

ATorqueFurnishings::ATorqueFurnishings()
{
	PrimaryActorTick.bCanEverTick = true;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	CubeMesh = Cube.Object;
}

void ATorqueFurnishings::SpawnMissing(UWorld* World)
{
	if (!World) return;
	if (TActorIterator<ATorqueFurnishings>(World)) return;
	World->SpawnActor<ATorqueFurnishings>(FVector::ZeroVector, FRotator::ZeroRotator);
}

UStaticMeshComponent* ATorqueFurnishings::Make(FName Model, const FTransform& Xf, bool bMovable)
{
	const TorqueFurniture::FModel* M = TorqueFurniture::Find(Model);
	UStaticMesh* Mesh = TorqueFurniture::Mesh(Model);
	if (!M || !Mesh)
	{
		if (!Missing.Contains(Model))
		{
			Missing.Add(Model);
			UE_LOG(LogTemp, Warning, TEXT("Torque furniture: no model for %s (run Tools/ue_import_props.py)"), *Model.ToString());
		}
		return nullptr;
	}
	AActor* Holder = M->bRide && RideHolder ? RideHolder : this;
	UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(Holder);
	C->SetStaticMesh(Mesh);
	C->SetMobility(EComponentMobility::Movable);
	// Walked into and looked at by its own shape (the props are complex-as-simple: ue_import_props.py),
	// ridden over when it's a ramp; a cone or a flag stops the walker, never the bike.
	C->SetCollisionProfileName(TEXT("BlockAll"));
	C->SetGenerateOverlapEvents(false);
	C->SetupAttachment(Holder->GetRootComponent());
	C->RegisterComponent();
	C->SetWorldTransform(FTransform(FRotator(0.0, TorqueFurniture::ImportYaw, 0.0)) * Xf);
	if (!bMovable) C->ComponentTags.Add(TEXT("FixedKit"));
	return C;
}

void ATorqueFurnishings::BeginPlay()
{
	Super::BeginPlay();
	SetActorLocationAndRotation(FVector::ZeroVector, FRotator::ZeroRotator);
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	RideHolder = GetWorld()->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, Params);
	if (RideHolder)
	{
		USceneComponent* Root = NewObject<USceneComponent>(RideHolder, TEXT("Root"));
		RideHolder->SetRootComponent(Root);
		Root->RegisterComponent();
		RideHolder->Tags.Add(TEXT("Road"));
		RideHolder->Tags.Add(TEXT("FurnitureRide"));
	}
	for (const TorqueFurniture::FPlaced& P : TorqueFurniture::Yard().Fixed)
		if (UStaticMeshComponent* C = Make(P.Model, P.Xf, false)) FixedDrawn.Add(C);
	Paint();
	UE_LOG(LogTemp, Log, TEXT("Torque furniture: the yard's %d fixed pieces drawn (of %d)"), FixedDrawn.Num(), TorqueFurniture::Yard().Fixed.Num());
}

void ATorqueFurnishings::Paint()
{
	// The test area's markings: thin painted strips just proud of the concrete, no collision.
	const TorqueFurniture::FYard& Y = TorqueFurniture::Yard();
	TMap<uint32, UMaterialInterface*> Looks;
	auto Look = [this, &Looks](const FLinearColor& C) -> UMaterialInterface*
	{
		const uint32 Key = GetTypeHash(C.ToFColor(false));
		if (UMaterialInterface** M = Looks.Find(Key)) return *M;
		UMaterialInterface* M = TorqueToolModels::Finish(this, C, 0.0f, 0.62f);
		Looks.Add(Key, M);
		return M;
	};
	for (const TorqueFurniture::FMark& K : Y.Marks)
	{
		UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
		C->SetStaticMesh(CubeMesh);
		C->SetMaterial(0, Look(K.Colour));
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetCastShadow(false);
		C->SetMobility(EComponentMobility::Static);
		C->SetupAttachment(GetRootComponent());
		C->RegisterComponent();
		// The engine cube is 1 m: its x along the mark's length, y its width, a few mm thick.
		C->SetWorldTransform(FTransform(FRotator(0.0, K.Yaw, 0.0), FVector(K.At, 0.4), FVector(K.Size.X / 100.0, K.Size.Y / 100.0, 0.006)));
		FixedDrawn.Add(C);
	}
	for (const TorqueFurniture::FWord& W : Y.Words)
	{
		UTextRenderComponent* T = NewObject<UTextRenderComponent>(this);
		T->SetText(FText::FromString(W.Text));
		T->SetTextRenderColor(W.Colour.ToFColor(true));
		T->SetWorldSize(W.Height);
		T->SetHorizontalAlignment(EHTA_Center);
		T->SetVerticalAlignment(EVRTA_TextCenter);
		T->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		T->SetupAttachment(GetRootComponent());
		T->RegisterComponent();
		// Lying face up on the concrete, read along its yaw.
		T->SetWorldLocationAndRotation(FVector(W.At, 1.0), FRotator(90.0, W.Yaw, 0.0));
		WordsDrawn.Add(T);
	}
}

int32 ATorqueFurnishings::PieceOf(const UPrimitiveComponent* C) const
{
	for (const TPair<int32, UStaticMeshComponent*>& KV : Drawn)
		if (KV.Value == C) return KV.Key;
	return 0;
}

void ATorqueFurnishings::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const UTorqueBuildSubsystem* B = UTorqueBuildSubsystem::Of(this);
	if (!B) return;
	TSet<int32> Seen;
	for (const FTorqueFurniture& F : B->Furniture())
	{
		Seen.Add(F.Id);
		UStaticMeshComponent** Have = Drawn.Find(F.Id);
		if (Have && DrawnModel.FindRef(F.Id) != F.Model)
		{
			(*Have)->DestroyComponent();
			Drawn.Remove(F.Id);
			Have = nullptr;
		}
		if (!Have)
		{
			UStaticMeshComponent* C = Make(F.Model, F.Xf, true);
			if (!C) continue;
			Drawn.Add(F.Id, C);
			DrawnModel.Add(F.Id, F.Model);
			Have = Drawn.Find(F.Id);
		}
		UStaticMeshComponent* C = *Have;
		FTransform Xf = F.Xf;
		const APawn* Mover = F.Mover != 0 ? TorqueShedKit::PawnOf(this, F.Mover) : nullptr;
		if (Mover)
			if (const TorqueFurniture::FModel* M = TorqueFurniture::Find(F.Model)) Xf = TorqueFurniture::MovedXf(Mover, *M, F.Turn);
		// Moved, it doesn't shove its mover about (nor anyone it's pushed into).
		const ECollisionEnabled::Type Want = Mover ? ECollisionEnabled::NoCollision : ECollisionEnabled::QueryAndPhysics;
		if (C->GetCollisionEnabled() != Want) C->SetCollisionEnabled(Want);
		const FTransform Draw = FTransform(FRotator(0.0, TorqueFurniture::ImportYaw, 0.0)) * Xf;
		if (!C->GetComponentTransform().Equals(Draw, 0.01)) C->SetWorldTransform(Draw);
	}
	for (auto It = Drawn.CreateIterator(); It; ++It)
		if (!Seen.Contains(It.Key()))
		{
			if (It.Value()) It.Value()->DestroyComponent();
			DrawnModel.Remove(It.Key());
			It.RemoveCurrent();
		}
}
