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
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
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
				if (O->TryGetNumberField(TEXT("sit"), M.Sit)) M.Sit *= 100.0;   // a seat: how high you sit (m in the file)
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
	// Indoors: a stool by the workbench (clear of the tyre changer at its end: beenster, Nov 10), a camp chair against the east wall just left of the parts
	// computer's desk (beenster, Nov 10), and a shelving unit against the east wall by
	// bay 4 (clear of the stand, the computer and the lift's post), as Torque Shed's new shed has.
	for (const FPlaced& P : {FPlaced{FName(TEXT("seat_stool")), FTransform(FRotator(0.0, -90.0, 0.0), FVector(-505.0, 215.0, 0.0))},
		FPlaced{FName(TEXT("seat_chair")), FTransform(FRotator(0.0, 90.0, 0.0), FVector(595.0, 75.0, 0.0))},
		FPlaced{FName(TEXT("shelf_standard")), FTransform(FRotator(0.0, 90.0, 0.0), FVector(615.0, -220.0, 0.0))}})
		if (D.Models.Contains(P.Model)) D.ShedStart.Add(P);
	UE_LOG(LogTemp, Log, TEXT("Torque furniture: %d models, yard %d to start, %d fixed, %d marks, %d words"), D.Models.Num(),
		D.Yard.Start.Num(), D.Yard.Fixed.Num(), D.Yard.Marks.Num(), D.Yard.Words.Num());
	return D;
}

bool HasLabel(const FModel& M) { return M.Kind == FName(TEXT("shelf")); }
bool IsTv(const FModel& M) { return M.Kind == FName(TEXT("tv")); }

FString NameOf(const FTorqueFurniture& F)
{
	const FModel* M = Find(F.Model);
	const FString Name = M ? M->Name : FString(TEXT("Equipment"));
	return M && HasLabel(*M) && !F.Label.IsEmpty() ? FString::Printf(TEXT("%s: %s"), *Name, *F.Label) : Name;
}

FString CleanLabel(const FString& Text)
{
	// What can be written on: printable letters only, trimmed, LabelMax long (shed_state.gd "label").
	FString Out;
	for (const TCHAR C : Text)
		if (C >= 32 && C != 127) Out.AppendChar(C);
	return Out.TrimStartAndEnd().Left(LabelMax).TrimEnd();
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

bool IsTrolley(const FModel& M)
{
	return M.Kind == FName(TEXT("trolley"));
}

bool OnTrolley(const FModel& M, const FTransform& TrolleyXf, const FVector& At)
{
	if (!IsTrolley(M)) return false;
	// The model's x is its width, its y its depth (front to back), up its height.
	const FVector L = TrolleyXf.InverseTransformPosition(At);
	return FMath::Abs(L.X) <= M.Size.X * 0.5 + 4.0 && FMath::Abs(L.Y) <= M.Size.Z * 0.5 + 4.0 && L.Z > 4.0 && L.Z < M.Size.Y + 250.0;
}

FTransform DrawnXf(const UObject* WorldContext, const FTorqueFurniture& F)
{
	const FModel* M = Find(F.Model);
	const APawn* Mover = F.Mover != 0 ? TorqueShedKit::PawnOf(WorldContext, F.Mover) : nullptr;
	if (!M || !Mover) return F.Xf;
	return Snapped(Mover->GetWorld(), *M, MovedXf(Mover, *M, F.Turn, F.Reach), F.Id);
}

FTransform MovedXf(const APawn* Pawn, const FModel& M, double Turn, double Reach)
{
	if (!Pawn) return FTransform::Identity;
	const FRotator View = Pawn->GetBaseAimRotation();
	const FVector Fwd = FRotator(0.0, View.Yaw, 0.0).Vector();
	// Its front (+y at yaw 0) towards the mover: yaw = view + 90, then turned.
	const FRotator Yaw(0.0, View.Yaw + 90.0 + Turn, 0.0);
	const FVector Feet = Pawn->GetActorLocation() - FVector(0.0, 0.0, Pawn->GetSimpleCollisionHalfHeight());
	const double Out = FMath::Clamp(Reach, ReachMin, ReachMax);
	if (M.bWall)
	{
		// (A wall piece's origin is its middle, on the wall: make_props.py's TVs, posters, clocks.)
		const FVector At = Feet + Fwd * (TFI::Reach(M) + 10.0 + Out) + FVector(0.0, 0.0, FMath::Max(150.0, M.Size.Y * 0.5 + 10.0));
		return FTransform(Yaw, At);
	}
	if (M.Small())
	{
		const FVector At = Feet + Fwd * (55.0 + FMath::Max(Out, 0.0) * 0.4) + FVector(0.0, 0.0, 105.0 - M.Size.Y * 0.5);
		return FTransform(Yaw, At);
	}
	return FTransform(Yaw, FVector(Feet.X, Feet.Y, 0.0) + Fwd * (TFI::Reach(M) + Out));
}

FTransform Snapped(const UWorld* World, const FModel& M, const FTransform& Xf, int32 Self)
{
	if (!World || M.bWall || M.Small()) return Xf;
	// Square: within 12 degrees of a wall's line, onto it.
	double Yaw = Xf.Rotator().Yaw;
	const double Square = 90.0 * FMath::RoundToDouble(Yaw / 90.0);
	if (FMath::Abs(FMath::UnwindDegrees(Yaw - Square)) > 12.0) return Xf;
	Yaw = FRotator::NormalizeAxis(Square);
	const bool bAcross = FMath::Abs(FMath::Abs(Yaw) - 90.0) < 1.0;   // its width along y
	const double Hx = (bAcross ? M.Size.Z : M.Size.X) * 0.5, Hy = (bAcross ? M.Size.X : M.Size.Z) * 0.5;
	FVector At = Xf.GetLocation();
	// Flush to what's static (walls, benches, machines) within 25 cm of a side, looked for low down.
	FCollisionQueryParams P(SCENE_QUERY_STAT(TorqueFurnitureSnap), false);
	for (TActorIterator<ATorqueFurnishings> It(const_cast<UWorld*>(World)); It; ++It)
	{
		P.AddIgnoredActor(*It);   // (the kit is snapped to below, as pieces, not by its shapes)
		if (It->Rides()) P.AddIgnoredActor(It->Rides());
	}
	const FCollisionObjectQueryParams Static(ECC_WorldStatic);
	const FVector Dirs[] = {FVector(1, 0, 0), FVector(-1, 0, 0), FVector(0, 1, 0), FVector(0, -1, 0)};
	bool bSnappedX = false, bSnappedY = false;
	for (const FVector& D : Dirs)
	{
		const double Half = D.X != 0.0 ? Hx : Hy;
		if ((D.X != 0.0 && bSnappedX) || (D.Y != 0.0 && bSnappedY)) continue;
		FHitResult Hit;
		const FVector From = FVector(At.X, At.Y, 30.0);
		if (!World->LineTraceSingleByObjectType(Hit, From, From + D * (Half + 25.0), Static, P)) continue;
		if (FVector::DotProduct(Hit.ImpactNormal, -D) < 0.8 || Hit.Distance < Half - 30.0) continue;
		At += D * (Hit.Distance - Half - 1.0);
		(D.X != 0.0 ? bSnappedX : bSnappedY) = true;
	}
	// Butted up to and lined up with the kit near it that's square too.
	if (const UTorqueBuildSubsystem* B = UTorqueBuildSubsystem::Of(World))
		for (const FTorqueFurniture& F : B->Furniture())
		{
			if (F.Id == Self || F.Mover != 0) continue;
			const FModel* O = Find(F.Model);
			if (!O || O->bWall || O->Small()) continue;
			const double OYaw = F.Xf.Rotator().Yaw;
			const double OSquare = 90.0 * FMath::RoundToDouble(OYaw / 90.0);
			if (FMath::Abs(FMath::UnwindDegrees(OYaw - OSquare)) > 1.0) continue;
			const bool bOAcross = FMath::Abs(FMath::Abs(FRotator::NormalizeAxis(OSquare)) - 90.0) < 1.0;
			const double Ox = (bOAcross ? O->Size.Z : O->Size.X) * 0.5, Oy = (bOAcross ? O->Size.X : O->Size.Z) * 0.5;
			const FVector C = F.Xf.GetLocation();
			const double GapX = FMath::Abs(At.X - C.X) - Hx - Ox, GapY = FMath::Abs(At.Y - C.Y) - Hy - Oy;
			// Side by side along x (overlapping in y): close the gap, and line up the fronts.
			if (!bSnappedX && GapX > -6.0 && GapX < 18.0 && GapY < -10.0)
			{
				At.X += (At.X > C.X ? -1.0 : 1.0) * (GapX - 0.5);
				bSnappedX = true;
				if (!bSnappedY)
					for (const double Edge : {(C.Y + Oy) - (At.Y + Hy), (C.Y - Oy) - (At.Y - Hy)})
						if (FMath::Abs(Edge) < 12.0) { At.Y += Edge; bSnappedY = true; break; }
			}
			else if (!bSnappedY && GapY > -6.0 && GapY < 18.0 && GapX < -10.0)
			{
				At.Y += (At.Y > C.Y ? -1.0 : 1.0) * (GapY - 0.5);
				bSnappedY = true;
				if (!bSnappedX)
					for (const double Edge : {(C.X + Ox) - (At.X + Hx), (C.X - Ox) - (At.X - Hx)})
						if (FMath::Abs(Edge) < 12.0) { At.X += Edge; bSnappedX = true; break; }
			}
		}
	return FTransform(FRotator(0.0, Yaw, 0.0), FVector(At.X, At.Y, Xf.GetLocation().Z));
}

bool PutDownXf(const APawn* Pawn, const FModel& M, double Turn, FTransform& Out, FString& WhyNot, double Reach, int32 Self)
{
	Out = MovedXf(Pawn, M, Turn, Reach);
	if (!Pawn || !(M.bWall || M.Small()))
	{
		Out.SetLocation(FVector(Out.GetLocation().X, Out.GetLocation().Y, 0.0));
		if (Pawn) Out = Snapped(Pawn->GetWorld(), M, Out, Self);   // (as it's drawn while it's moved)
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
		// Its middle where you look (its origin), its bottom clear of the floor.
		const double Z = FMath::Clamp(Hit.ImpactPoint.Z, M.Size.Y * 0.5 + 20.0, FMath::Max(M.Size.Y * 0.5 + 20.0, 330.0 - M.Size.Y * 0.5));
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
		|| A == ETorqueShedAct::OrderFurniture || A == ETorqueShedAct::LabelFurniture;
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
	// Shed kit comes to the delivery area with the boxes (beenster, Nov 10: not in front of the wash
	// bay), from its far corner so the boxes keep the end by the door, clear of them and of each other.
	if (!M.bYard)
	{
		FVector At;
		if (FreeDeliverySpot(FVector(M.Size.X, M.Size.Z, M.Size.Y), true, false, At)) return At;
	}
	// Yard kit goes in the yard's drop-off box (and shed kit, when the delivery area's full, on the yard
	// just past it): a free spot on a grid, nearest first.
	const FBox2D Box = M.bYard && TorqueFurniture::Yard().Drop.bIsValid ? TorqueFurniture::Yard().Drop
		: FBox2D(FVector2D(290.0, 1330.0), FVector2D(1500.0, 1900.0));
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
	case ETorqueShedAct::LabelFurniture:   // [label] shed_state.gd check "label"
		if (!F || !M) return TEXT("That's gone.");
		if (!TorqueFurniture::HasLabel(*M)) return TEXT("Only shelves take labels.");
		if (F->Mover != 0 && F->Mover != Peer) return TEXT("Someone's moving it.");
		return FString();
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
		F->Turn = F->Reach = 0.0f;
		return FString::Printf(TEXT("Moving the %s: R turns it, the wheel (Q / E) pushes it out or in, F puts it down."), *M->Name.ToLower());
	case ETorqueShedAct::TurnFurniture:
		// (Spot "reach": the wheel pushing it out or bringing it in, Amount cm.)
		if (F && R.Spot == FName(TEXT("reach"))) F->Reach = (float)FMath::Clamp((double)F->Reach + R.Amount, TorqueFurniture::ReachMin, TorqueFurniture::ReachMax);
		else if (F) F->Turn = FMath::UnwindDegrees(F->Turn + R.Amount);
		return FString();
	case ETorqueShedAct::ReleaseFurniture:
	{
		if (!F) return FString();
		const FVector At = R.Place.GetLocation();
		const bool bLifted = M && (M->bWall || M->Small());
		const FTransform Was = F->Xf;
		F->Xf = FTransform(FRotator(0.0, R.Place.Rotator().Yaw, 0.0), FVector(At.X, At.Y, bLifted ? FMath::Clamp(At.Z, -10.0, 400.0) : 0.0));
		// [trolley] What's on its deck (boxes, stacks of them, parts) goes where it went.
		if (M && TorqueFurniture::IsTrolley(*M))
		{
			int32 Carried = 0;
			for (TPair<int32, FTorqueItem>& KV : Items)
				if (KV.Value.At == ETorqueAt::Floor && TorqueFurniture::OnTrolley(*M, Was, KV.Value.Place.GetLocation()))
				{
					KV.Value.Place = KV.Value.Place * Was.Inverse() * F->Xf;
					++Carried;
				}
			if (Carried > 0)
			{
				OnItems.Broadcast();
				UE_LOG(LogTemp, Log, TEXT("Torque: the %s carried %d things"), *M->Name.ToLower(), Carried);
			}
		}
		F->Mover = 0;
		F->Turn = F->Reach = 0.0f;
		return FString();
	}
	case ETorqueShedAct::OrderFurniture:
	{
		const TorqueFurniture::FModel* O = TorqueFurniture::Find(R.Part);
		if (!O) return FString();
		FVector At = FurnitureDropAt(*O);
		if (O->bWall) At.Z += O->Size.Y * 0.5;   // stood on its bottom edge until it's hung (its origin's its middle)
		Furnishings.Add({FurnitureNextId++, R.Part, FTransform(FRotator(0.0, 180.0, 0.0), At), 0, 0.0f});
		return FString::Printf(TEXT("%s ordered: it's %s."), *O->Name, O->bYard ? TEXT("in the yard kit drop-off out on the pad") : TEXT("in the delivery area outside the roller door"));
	}
	case ETorqueShedAct::LabelFurniture:   // [label] shed_state.gd apply "label"
	{
		if (!F) return FString();
		F->Label = TorqueFurniture::CleanLabel(R.Text);
		return F->Label.IsEmpty() ? FString(TEXT("Wiped the label off the shelves."))
			: FString::Printf(TEXT("Labelled the shelves \"%s\"."), *F->Label);
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
					const FTransform Xf = TorqueFurniture::Snapped(P->GetWorld(), *M, TorqueFurniture::MovedXf(P, *M, F.Turn, F.Reach), F.Id);
					F.Xf = FTransform(Xf.GetRotation(), FVector(Xf.GetLocation().X, Xf.GetLocation().Y, 0.0));
				}
			F.Mover = 0;
			F.Turn = F.Reach = 0.0f;
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
		if (bNet) Ar << F.Mover << F.Turn << F.Reach << F.Label << F.Channel;   // [label] [tv] (BytesLayout 21)
		if (Ar.IsLoading())
		{
			F.Model = FName(*Model);
			F.Xf = FTransform(FRotator(0.0, Yaw, 0.0), At);
			if (!bNet) F.Mover = 0;
			if (Ar.IsError()) return;
		}
	}
	if (!bNet)
	{
		// [label] The shelves' labels, after the list (a save from before them ends here: none).
		constexpr int32 LabelsTag = 0x4C424C53;   // "SLBL"
		// [tv] Then the TVs' channels (a save from before them ends after the labels).
		constexpr int32 ChannelsTag = 0x4C4E4843;   // "CHNL"
		if (Ar.IsSaving())
		{
			int32 Tag = LabelsTag;
			Ar << Tag;
			for (FTorqueFurniture& F : List) Ar << F.Label;
			Tag = ChannelsTag;
			Ar << Tag;
			for (FTorqueFurniture& F : List) Ar << F.Channel;
		}
		else
		{
			while (!Ar.AtEnd() && !Ar.IsError())
			{
				int32 Tag = 0;
				Ar << Tag;
				if (Tag == LabelsTag)
					for (FTorqueFurniture& F : List) Ar << F.Label;
				else if (Tag == ChannelsTag)
					for (FTorqueFurniture& F : List) Ar << F.Channel;
				else break;   // something from a later game: left as it was
			}
			if (Ar.IsError()) return;
		}
	}
	if (Ar.IsLoading())
	{
		for (FTorqueFurniture& F : List)
		{
			F.Label = TorqueFurniture::CleanLabel(F.Label);
			if (!TorqueShedTv::Has(-1, F.Channel)) F.Channel = TorqueShedTv::Rider;   // [tv] (no bay of its own; the shed cam's gone)
		}
		// Models that aren't in this game any more go.
		List.RemoveAll([](const FTorqueFurniture& F) { return TorqueFurniture::Find(F.Model) == nullptr; });
		TorqueFurniture::Touch();
	}
}

// --- Drawing it -------------------------------------------------------------------------------------------

namespace TorqueFurnitureImpl
{
// Data/Furniture/fab_looks.json: pieces drawn with a Fab model over our own (see its "about").
struct FFabLook
{
	FString Mesh;
	FVector Box = FVector(100.0);
	TArray<FVector> Copies;   // x, y (cm, the piece's frame), yaw (degrees)
	TArray<FString> Paint;    // our MI_Prop_<key> over its body paint, one a copy in turn
	bool bFillHeight = false; // stretched up to the box's height (its roof where the piece's collision is)
};

const TMap<FName, FFabLook>& FabLooks()
{
	static TMap<FName, FFabLook> Looks;
	static bool bRead = false;
	if (bRead) return Looks;
	bRead = true;
	TSharedPtr<FJsonObject> J;
	if (!LoadJson(TEXT("Data/Furniture/fab_looks.json"), J)) return Looks;
	const TSharedPtr<FJsonObject>* All = nullptr;
	if (!J->TryGetObjectField(TEXT("looks"), All)) return Looks;
	for (const TPair<FString, TSharedPtr<FJsonValue>>& KV : (*All)->Values)
	{
		const TSharedPtr<FJsonObject> O = KV.Value->AsObject();
		if (!O.IsValid()) continue;
		FFabLook L;
		O->TryGetStringField(TEXT("mesh"), L.Mesh);
		const TArray<TSharedPtr<FJsonValue>>* BoxA = nullptr;
		if (O->TryGetArrayField(TEXT("box"), BoxA)) L.Box = FVector(Num(*BoxA, 0, 100.0), Num(*BoxA, 1, 100.0), Num(*BoxA, 2, 100.0));
		const TArray<TSharedPtr<FJsonValue>>* CopiesA = nullptr;
		if (O->TryGetArrayField(TEXT("copies"), CopiesA))
			for (const TSharedPtr<FJsonValue>& V : *CopiesA)
			{
				const TArray<TSharedPtr<FJsonValue>>& R = V->AsArray();
				L.Copies.Add(FVector(Num(R, 0), Num(R, 1), Num(R, 2)));
			}
		if (L.Copies.Num() == 0) L.Copies.Add(FVector::ZeroVector);
		O->TryGetBoolField(TEXT("fill_height"), L.bFillHeight);
		const TArray<TSharedPtr<FJsonValue>>* PaintA = nullptr;
		if (O->TryGetArrayField(TEXT("paint"), PaintA))
			for (const TSharedPtr<FJsonValue>& V : *PaintA) L.Paint.Add(V->AsString());
		if (!L.Mesh.IsEmpty()) Looks.Add(FName(*KV.Key), L);
	}
	return Looks;
}

// Draws Piece's Fab look over it, when it has one that's in the project: the piece keeps its own
// mesh for colliding, being looked at and the outline (custom depth), but that mesh isn't drawn.
void DressFab(UStaticMeshComponent* Piece, FName Model)
{
	const FFabLook* Look = FabLooks().Find(Model);
	if (!Piece || !Look) return;
	static TMap<FString, TWeakObjectPtr<UStaticMesh>> Loaded;
	UStaticMesh* Fab = nullptr;
	if (const TWeakObjectPtr<UStaticMesh>* Was = Loaded.Find(Look->Mesh)) Fab = Was->Get();
	if (!Fab)
	{
		Fab = LoadObject<UStaticMesh>(nullptr, *Look->Mesh);
		Loaded.Add(Look->Mesh, Fab);
		if (!Fab) UE_LOG(LogTemp, Warning, TEXT("Torque furniture: %s's Fab look %s isn't in the project: our own model is drawn"), *Model.ToString(), *Look->Mesh);
	}
	if (!Fab) return;
	const FBox FabBox = Fab->GetBoundingBox();
	const FVector FabSize = FabBox.GetSize();
	if (FabSize.X < 1.0 || FabSize.Y < 1.0) return;
	// Its long side along the box's, then as long as the box (one scale: never squashed).
	const bool bFabAlongX = FabSize.X >= FabSize.Y;
	const bool bBoxAlongX = Look->Box.X >= Look->Box.Y;
	const double Turn = bFabAlongX == bBoxAlongX ? 0.0 : 90.0;
	const double Scale = FMath::Max(Look->Box.X, Look->Box.Y) / FMath::Max(FabSize.X, FabSize.Y);
	for (int32 I = 0; I < Look->Copies.Num(); ++I)
	{
		const FVector& Copy = Look->Copies[I];
		const FQuat Rot = FRotator(0.0, Turn + Copy.Z, 0.0).Quaternion();
		const FVector Centre = Rot.RotateVector(FabBox.GetCenter() * Scale);
		// Stood on top of a bus you stand on the piece's own (hidden) mesh: with fill_height the Fab
		// roof is there too, not a hand's breadth under your feet (beenster, build 135).
		const double ScaleZ = Look->bFillHeight && FabSize.Z > 1.0 ? Look->Box.Z / FabSize.Z : Scale;
		const FVector Offset(Copy.X - Centre.X, Copy.Y - Centre.Y, -FabBox.Min.Z * ScaleZ);
		UStaticMeshComponent* Shell = NewObject<UStaticMeshComponent>(Piece->GetOwner());
		Shell->SetStaticMesh(Fab);
		Shell->SetMobility(EComponentMobility::Movable);
		Shell->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Shell->SetGenerateOverlapEvents(false);
		Shell->ComponentTags.Add(TEXT("FabLook"));
		Shell->SetupAttachment(Piece);
		Shell->SetRelativeTransform(FTransform(Rot, Offset, FVector(Scale, Scale, ScaleZ)));
		// Its body paint: City Sample's car paint takes its colour from the Blueprint (custom
		// primitive data), so on its own it's black (build 131's black bus roofs). Our own paint
		// goes over any slot whose material is a car paint.
		if (Look->Paint.Num() > 0)
		{
			const FString& Key = Look->Paint[I % Look->Paint.Num()];
			UMaterialInterface* Ours = LoadObject<UMaterialInterface>(nullptr, *FString::Printf(TEXT("/Game/Shed/Props/Materials/MI_Prop_%s.MI_Prop_%s"), *Key, *Key));
			for (int32 Slot = 0; Slot < Shell->GetNumMaterials(); ++Slot)
			{
				UMaterialInterface* Theirs = Shell->GetMaterial(Slot);
				const UMaterial* Base = Theirs ? Theirs->GetBaseMaterial() : nullptr;
				const FString BaseName = Base ? Base->GetName() : FString();
				if (I == 0)
					UE_LOG(LogTemp, Log, TEXT("Torque furniture: %s Fab look slot %d: %s (base %s)"), *Model.ToString(), Slot,
						Theirs ? *Theirs->GetName() : TEXT("none"), *BaseName);
				if (Ours && BaseName.Contains(TEXT("Paint"))) Shell->SetMaterial(Slot, Ours);
			}
		}
		Shell->RegisterComponent();
	}
	// Out of the depth pass too: drawn only there it fills its shape with an unlit black that hides
	// the Fab model inside it (build 131-132's black bus row and black wreck).
	Piece->SetRenderInMainPass(false);
	Piece->SetRenderInDepthPass(false);
	Piece->SetCastShadow(false);
	Piece->SetVisibleInRayTracing(false);
	Piece->SetAffectDistanceFieldLighting(false);
}

// A piece's mesh goes, and its Fab look with it.
void Unmake(UStaticMeshComponent* Piece)
{
	if (!Piece) return;
	const TArray<TObjectPtr<USceneComponent>> Kids = Piece->GetAttachChildren();
	for (USceneComponent* Kid : Kids)
		if (Kid && Kid->ComponentHasTag(TEXT("FabLook"))) Kid->DestroyComponent();
	Piece->DestroyComponent();
}
} // namespace TorqueFurnitureImpl


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
	TFI::DressFab(C, Model);
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
	const int32 NumFixed = FixedDrawn.Num();
	Paint();
	UE_LOG(LogTemp, Log, TEXT("Torque furniture: the yard's %d fixed pieces drawn (of %d), %d painted marks, %d words"), NumFixed,
		TorqueFurniture::Yard().Fixed.Num(), FixedDrawn.Num() - NumFixed, WordsDrawn.Num());
}

void ATorqueFurnishings::Paint()
{
	// The test area's markings: thin painted strips just proud of the concrete, no collision.
	const TorqueFurniture::FYard& Y = TorqueFurniture::Yard();
	// Road paint: the concrete's scan showing through the paint, laid by world position so it doesn't
	// smear along a long line (MI_Prop_yard_line, Tools/ue_import_props.py), tinted per mark. Plain
	// paint if the props' materials aren't imported yet.
	UMaterialInterface* LinePaint = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Shed/Props/Materials/MI_Prop_yard_line.MI_Prop_yard_line"));
	TMap<uint32, UMaterialInterface*> Looks;
	auto Look = [this, &Looks, LinePaint](const FLinearColor& C) -> UMaterialInterface*
	{
		const uint32 Key = GetTypeHash(C.ToFColor(false));
		if (UMaterialInterface** Found = Looks.Find(Key)) return *Found;
		UMaterialInterface* Made = nullptr;
		if (LinePaint)
		{
			if (UMaterialInstanceDynamic* D = UMaterialInstanceDynamic::Create(LinePaint, this))
			{
				D->SetVectorParameterValue(TEXT("Tint"), C);
				Made = D;
			}
		}
		if (!Made) Made = TorqueToolModels::Finish(this, C, 0.0f, 0.62f);
		Looks.Add(Key, Made);
		return Made;
	};
	for (const TorqueFurniture::FMark& K : Y.Marks)
	{
		UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
		C->SetStaticMesh(CubeMesh);
		C->SetMaterial(0, Look(K.Colour));
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetCastShadow(false);
		// Movable like the actor's root: a Static one won't attach to it ("AttachTo ... is not static").
		C->SetMobility(EComponentMobility::Movable);
		C->SetupAttachment(GetRootComponent());
		// The engine cube is 1 m: its x along the mark's length, y its width, a few mm thick. Placed
		// before it's registered (this actor sits at the origin): a Static component can't be moved
		// once play has begun, which left only the scale applied and every mark lying across the
		// middle of the shed floor, flickering against it (beenster, build 125).
		C->SetRelativeTransform(FTransform(FRotator(0.0, K.Yaw, 0.0), FVector(K.At, 0.4), FVector(K.Size.X / 100.0, K.Size.Y / 100.0, 0.006)));
		C->RegisterComponent();
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
			TFI::Unmake(*Have);
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
			if (const TorqueFurniture::FModel* M = TorqueFurniture::Find(F.Model))
				Xf = TorqueFurniture::Snapped(GetWorld(), *M, TorqueFurniture::MovedXf(Mover, *M, F.Turn, F.Reach), F.Id);   // (where it goes down)
		// Moved, it doesn't shove its mover about (nor anyone it's pushed into).
		const ECollisionEnabled::Type Want = Mover ? ECollisionEnabled::NoCollision : ECollisionEnabled::QueryAndPhysics;
		if (C->GetCollisionEnabled() != Want) C->SetCollisionEnabled(Want);
		const FTransform Draw = FTransform(FRotator(0.0, TorqueFurniture::ImportYaw, 0.0)) * Xf;
		if (!C->GetComponentTransform().Equals(Draw, 0.01)) C->SetWorldTransform(Draw);
		// A Fab look is outlined with its piece (the look outline marks the piece's own mesh, which
		// isn't drawn: the Fab model in front of it would hide the outline's depth test).
		for (USceneComponent* Kid : C->GetAttachChildren())
			if (UPrimitiveComponent* Look = Cast<UPrimitiveComponent>(Kid))
				if (Look->ComponentHasTag(TEXT("FabLook")))
				{
					if (Look->bRenderCustomDepth != C->bRenderCustomDepth) Look->SetRenderCustomDepth(C->bRenderCustomDepth);
					if (Look->CustomDepthStencilValue != C->CustomDepthStencilValue) Look->SetCustomDepthStencilValue(C->CustomDepthStencilValue);
				}
	}
	for (auto It = Drawn.CreateIterator(); It; ++It)
		if (!Seen.Contains(It.Key()))
		{
			TFI::Unmake(It.Value());
			DrawnModel.Remove(It.Key());
			It.RemoveCurrent();
		}
	DrawLabels();
}

void ATorqueFurnishings::DrawLabels()
{
	// [label] Each labelled shelving unit's words on its label plate (equipment_view.gd's Label3D:
	// dark on the white plate, 12 cm under the top, just proud of its front), riding along with the
	// piece's mesh wherever it's moved.
	const UTorqueBuildSubsystem* B = UTorqueBuildSubsystem::Of(this);
	TSet<int32> Want;
	if (B)
		for (const FTorqueFurniture& F : B->Furniture())
		{
			const TorqueFurniture::FModel* M = TorqueFurniture::Find(F.Model);
			UStaticMeshComponent* const* Mesh = Drawn.Find(F.Id);
			if (!M || !TorqueFurniture::HasLabel(*M) || F.Label.IsEmpty() || !Mesh || !*Mesh) continue;
			Want.Add(F.Id);
			UTextRenderComponent* T = LabelsDrawn.FindRef(F.Id);
			if (T && T->GetAttachParent() != *Mesh)
			{
				T->DestroyComponent();   // its piece was drawn afresh
				T = nullptr;
				LabelsText.Remove(F.Id);
			}
			if (!T)
			{
				T = NewObject<UTextRenderComponent>((*Mesh)->GetOwner());
				T->SetHorizontalAlignment(EHTA_Center);
				T->SetVerticalAlignment(EVRTA_TextCenter);
				T->SetTextRenderColor(FColor(31, 26, 26));
				T->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				T->SetCastShadow(false);
				T->SetMobility(EComponentMobility::Movable);
				T->SetupAttachment(*Mesh);
				// Facing out of the front (+y at yaw 0): the text's own face is its +x.
				T->SetRelativeLocationAndRotation(FVector(0.0, M->Size.Z * 0.5 + 1.1, M->Size.Y - 12.0), FRotator(0.0, 90.0, 0.0));
				T->RegisterComponent();
				LabelsDrawn.Add(F.Id, T);
			}
			if (LabelsText.FindRef(F.Id) != F.Label)
			{
				LabelsText.Add(F.Id, F.Label);
				T->SetText(FText::FromString(F.Label));
				// As big as fits the 70 cm plate: 7.5 cm letters for a short label, smaller for a long one.
				T->SetWorldSize((float)FMath::Clamp(62.0 / (F.Label.Len() * 0.58), 3.0, 7.5));
			}
		}
	for (auto It = LabelsDrawn.CreateIterator(); It; ++It)
		if (!Want.Contains(It.Key()))
		{
			if (It.Value()) It.Value()->DestroyComponent();
			LabelsText.Remove(It.Key());
			It.RemoveCurrent();
		}
}
