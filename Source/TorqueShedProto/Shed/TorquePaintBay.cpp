#include "Shed/TorquePaintBay.h"
#include "Shed/TorqueShedLayout.h"

#include "Shed/TorqueBuildSubsystem.h"
#include "Serialization/Archive.h"

namespace TorquePaintBayImpl
{
namespace TPM = TorquePaintMix;

constexpr int32 SaveLayout = 2;    // bumped when the bytes below change (1: before the effects rack)
constexpr int32 OldBases = 4;      // layout 1's bases (W, B, S, L): C and H came with layout 2

void SerializeMix(FArchive& Ar, TPM::FTinMix& Mix)
{
	uint8 Base = (uint8)FMath::Clamp(Mix.Base, 0, 255);
	Ar << Base << Mix.BaseGrams << Mix.Stir;
	Mix.Base = Base;
	uint8 N = (uint8)FMath::Min(Mix.Tints.Num(), TPM::MaxTints);
	Ar << N;
	if (Ar.IsLoading()) Mix.Tints.SetNum(FMath::Min<int32>(N, TPM::MaxTints));
	for (int32 I = 0; I < N; ++I)
	{
		TPM::FTintGrams Dummy;
		TPM::FTintGrams& T = Mix.Tints.IsValidIndex(I) ? Mix.Tints[I] : Dummy;
		uint8 Tint = (uint8)FMath::Clamp(T.Tint, 0, 255);
		Ar << Tint << T.Grams;
		T.Tint = Tint;
	}
}

void SerializeSpec(FArchive& Ar, TPM::FPaintSpec& S, int32 Layout)
{
	FString Effect = S.Effect.ToString();
	Ar << S.Name << S.Code << Effect << S.Colour << S.Metal << S.Rough << S.Flake << S.Flat << S.Stir << S.Need;
	if (Ar.IsLoading()) S.Effect = FName(*Effect);
	if (Layout < 2) return;
	uint8 bHoloB = S.bHolo ? 1 : 0;
	Ar << S.Pearl << S.Flip << S.Depth << S.PearlTint << S.FlakeColour << S.FlipA << S.FlipB << S.Dye << bHoloB;
	S.bHolo = bHoloB != 0;
}

// A tin from the bay's stock: TS "W", "PB" for the codes, and the stock index of each.
bool Valid(const TPM::FTinMix& Mix)
{
	if (!TPM::Bases().IsValidIndex(Mix.Base) || Mix.Tints.Num() > TPM::MaxTints) return false;
	for (const TPM::FTintGrams& T : Mix.Tints)
		if (!TPM::Tints().IsValidIndex(T.Tint) || !(T.Grams >= 0.0f) || T.Grams > TPM::MaxTinGrams) return false;
	return Mix.BaseGrams > 0.0f && Mix.BaseGrams <= TPM::MaxTinGrams;
}
}

namespace TPBI = TorquePaintBayImpl;

// --- The state -----------------------------------------------------------------------------------------

FTorquePaintBay FTorquePaintBay::Starter()
{
	FTorquePaintBay B;
	const int32 NB = TorquePaintMix::Bases().Num(), NT = TorquePaintMix::Tints().Num();
	B.Stock.SetNumZeroed(NB + NT);
	B.Stock[BaseStock(0)] = 3.0f;   // W
	B.Stock[BaseStock(1)] = 2.0f;   // B
	B.Stock[BaseStock(2)] = 2.0f;   // S
	B.Stock[BaseStock(3)] = 2.0f;   // L
	B.Stock[BaseStock(4)] = 1.0f;   // C (candy's clear)
	B.Stock[BaseStock(5)] = 1.0f;   // H (chrome)
	for (int32 I = 0; I < NT; ++I) B.Stock[TintStock(I)] = StarterTint;
	return B;
}

const FTorqueTin* FTorquePaintBay::HeldTin(int32 Peer) const
{
	const int32* Id = Held.Find(Peer);
	return Id ? Tins.Find(*Id) : nullptr;
}

int32 FTorquePaintBay::TinInSlot(int32 Slot) const
{
	for (const TPair<int32, FTorqueTin>& KV : Tins)
		if (KV.Value.At == ETorqueTinAt::Shelf && KV.Value.Ref == Slot) return KV.Key;
	return 0;
}

int32 FTorquePaintBay::FreeSlot() const
{
	for (int32 I = 0; I < ShelfSlots; ++I)
		if (TinInSlot(I) == 0) return I;
	return -1;
}

float FTorquePaintBay::ScaleGrams() const
{
	const FTorqueTin* T = BenchTinPtr();
	if (!T) return 0.0f;
	return T->Mix.TotalGrams() - (Tared() ? TareGrams : 0.0f);
}

bool FTorquePaintBay::Tared() const
{
	return BenchTin != 0 && TareTin == BenchTin;
}

TArray<const FTorquePaintRecipe*> FTorquePaintBay::Swatches() const
{
	TArray<const FTorquePaintRecipe*> Out;
	for (int32 I = Recipes.Num() - 1; I >= 0 && Out.Num() < MaxSwatches; --I) Out.Add(&Recipes[I]);
	return Out;
}

const FTorquePaintRecipe* FTorquePaintBay::Recipe(int32 Id) const
{
	for (const FTorquePaintRecipe& R : Recipes)
		if (R.Id == Id) return &R;
	return nullptr;
}

FString FTorquePaintBay::TinText(const FTorqueTin& Tin) const
{
	const TorquePaintMix::FPaintSpec S = Tin.Spec();
	const FString Name = Tin.Name.IsEmpty() ? FString(TEXT("Unnamed mix")) : Tin.Name;
	FString Out = FString::Printf(TEXT("%s (%s"), *Name, *TorquePaintMix::EffectName(S.Effect).ToLower());
	if (S.IsLacquer()) Out += TEXT(", ") + TorquePaintMix::SheenName(S.Flat);
	if (S.Stir < 0.9f) Out += TEXT(", badly stirred");
	Out += FString::Printf(TEXT("), %.2f l"), Tin.Litres);
	return Out;
}

FString FTorquePaintBay::StockName(int32 Index)
{
	const int32 NB = TorquePaintMix::Bases().Num();
	if (Index >= 0 && Index < NB) return TorquePaintMix::Bases()[Index].Name;
	if (TorquePaintMix::Tints().IsValidIndex(Index - NB)) return TorquePaintMix::Tints()[Index - NB].Name;
	return TEXT("paint");
}

FString FTorquePaintBay::StockText(int32 Index) const
{
	if (Index < TorquePaintMix::Bases().Num()) return FString::Printf(TEXT("%s: %.1f l"), *StockName(Index), StockOf(Index));
	return FString::Printf(TEXT("%s: %d g"), *StockName(Index), FMath::RoundToInt(StockOf(Index)));
}

bool FTorquePaintBay::Serialize(FArchive& Ar, bool bNet)
{
	int32 Layout = TPBI::SaveLayout;
	Ar << Layout;
	if (Layout < 1 || Layout > TPBI::SaveLayout || (bNet && Layout != TPBI::SaveLayout)) return false;
	Ar << Stock;
	if (Stock.Num() > 64) return false;
	if (Ar.IsLoading() && Layout < 2)
	{
		// A garage from before the effects rack: room for C and H after the old bases (none in
		// stock), and the effects rack's bottles as a new garage gets them.
		Stock.SetNumZeroed(FMath::Max(Stock.Num(), TPBI::OldBases));
		Stock.InsertZeroed(TPBI::OldBases, TorquePaintMix::Bases().Num() - TPBI::OldBases);
		const int32 Was = Stock.Num();
		Stock.SetNumZeroed(TorquePaintMix::Bases().Num() + TorquePaintMix::Tints().Num());
		for (int32 I = Was; I < Stock.Num(); ++I) Stock[I] = StarterTint;
	}
	int32 NumTins = Tins.Num();
	Ar << NumTins;
	if (NumTins < 0 || NumTins > MaxTins + 8) return false;
	if (Ar.IsLoading())
	{
		Tins.Reset();
		for (int32 I = 0; I < NumTins && !Ar.IsError(); ++I)
		{
			FTorqueTin T;
			uint8 At = 0;
			Ar << T.Id << T.Name << At << T.Ref << T.Litres << T.By;
			TPBI::SerializeMix(Ar, T.Mix);
			T.At = (ETorqueTinAt)FMath::Min<uint8>(At, (uint8)ETorqueTinAt::Shaker);
			if (!TPBI::Valid(T.Mix)) return false;
			Tins.Add(T.Id, T);
		}
	}
	else
	{
		for (TPair<int32, FTorqueTin>& KV : Tins)
		{
			FTorqueTin& T = KV.Value;
			uint8 At = (uint8)T.At;
			Ar << T.Id << T.Name << At << T.Ref << T.Litres << T.By;
			TPBI::SerializeMix(Ar, T.Mix);
		}
	}
	int32 NumRecipes = Recipes.Num();
	Ar << NumRecipes;
	if (NumRecipes < 0 || NumRecipes > MaxRecipes) return false;
	if (Ar.IsLoading()) Recipes.SetNum(NumRecipes);
	for (FTorquePaintRecipe& R : Recipes)
	{
		FString Effect = R.Effect.ToString();
		Ar << R.Id << R.Name << R.Code << R.By << R.Colour << Effect;
		if (Ar.IsLoading()) R.Effect = FName(*Effect);
	}
	Ar << NextId << Mixed << BenchTin << TareGrams << TareTin << CardRecipe;
	TPBI::SerializeMix(Ar, CardMix);
	uint8 bTargetB = bTarget ? 1 : 0, bTestB = bTest ? 1 : 0;
	FString Effect = TargetEffect.ToString();
	Ar << bTargetB << TargetName << TargetColour << Effect;
	bTarget = bTargetB != 0;
	if (Ar.IsLoading()) TargetEffect = FName(*Effect);
	Ar << ShakerTin << ShakerLeft << bTestB << TestBy << TestN;
	bTest = bTestB != 0;
	TPBI::SerializeSpec(Ar, Test, Layout);
	if (bNet)
	{
		Ar << BenchBy << PourTint << Tilt;
		int32 NumCups = Cups.Num();
		Ar << NumCups;
		if (NumCups < 0 || NumCups > 64) return false;
		if (Ar.IsLoading())
		{
			Cups.Reset();
			for (int32 I = 0; I < NumCups && !Ar.IsError(); ++I)
			{
				int32 Peer = 0;
				FTorquePaintCup C;
				Ar << Peer << C.Litres;
				TPBI::SerializeSpec(Ar, C.Spec, Layout);
				Cups.Add(Peer, C);
			}
		}
		else
		{
			for (TPair<int32, FTorquePaintCup>& KV : Cups)
			{
				int32 Peer = KV.Key;
				Ar << Peer << KV.Value.Litres;
				TPBI::SerializeSpec(Ar, KV.Value.Spec, Layout);
			}
		}
		Ar << Held;
		if (Held.Num() > 64) return false;
	}
	// The booth's hook heights, last so a save from before them still reads.
	if (!Ar.IsLoading() || bNet || !Ar.AtEnd())
	{
		Ar << HookTips;
		if (HookTips.Num() > 32) return false;
		for (float& T : HookTips) T = FMath::Clamp(T, HookLow, HookHigh);
	}
	return !Ar.IsError();
}

void FTorquePaintBay::AfterLoad()
{
	TArray<int32> Peers;
	Held.GetKeys(Peers);
	for (int32 Peer : Peers) PutBack(Peer);
	Held.Reset();
	Cups.Reset();
	BenchBy = 0;
	PourTint = INDEX_NONE;
	Tilt = 0.0f;
	// The stock always has a line for every base and tint (a garage from before one was added).
	Stock.SetNumZeroed(TorquePaintMix::Bases().Num() + TorquePaintMix::Tints().Num());
	TArray<int32> Ids;
	Tins.GetKeys(Ids);
	for (int32 Id : Ids)
	{
		FTorqueTin& T = Tins[Id];
		if (T.At == ETorqueTinAt::Held)
		{
			const int32 Slot = FreeSlot();
			if (Slot < 0) Tins.Remove(Id);
			else { T.At = ETorqueTinAt::Shelf; T.Ref = Slot; }
		}
	}
	if (ShakerTin != 0) ShakerLeft = 0.01f;   // a tin left in the shaker comes out mixed
	if (BenchTin != 0 && !Tins.Contains(BenchTin)) BenchTin = 0;
}

void FTorquePaintBay::PutBack(int32 Peer)
{
	if (const int32* Id = Held.Find(Peer))
	{
		if (FTorqueTin* T = Tins.Find(*Id))
		{
			const int32 Slot = FreeSlot();
			if (Slot >= 0) { T->At = ETorqueTinAt::Shelf; T->Ref = Slot; }
			else Tins.Remove(*Id);
		}
	}
	Held.Remove(Peer);
	if (BenchBy == Peer)
	{
		BenchBy = 0;
		Tilt = 0.0f;
		PourTint = INDEX_NONE;
	}
}

bool FTorquePaintBay::Tick(float Dt)
{
	if (ShakerTin == 0) return false;
	ShakerLeft -= Dt;
	if (ShakerLeft > 0.0f) return false;
	const int32 Id = ShakerTin;
	ShakerTin = 0;
	ShakerLeft = 0.0f;
	FTorqueTin* T = Tins.Find(Id);
	if (!T) return true;
	T->Mix.Stir = 1.0f;
	// A named tin goes on the shelf; one from the bench back on the scale if it's free.
	const int32 Slot = FreeSlot();
	if ((!T->Name.IsEmpty() || BenchTin != 0) && Slot >= 0)
	{
		T->At = ETorqueTinAt::Shelf;
		T->Ref = Slot;
	}
	else if (BenchTin == 0)
	{
		T->At = ETorqueTinAt::Bench;
		T->Ref = 0;
		BenchTin = Id;
	}
	else Tins.Remove(Id);   // nowhere to put it
	return true;
}

FString FTorquePaintBay::Summary() const
{
	FString Out = FString::Printf(TEXT("%d tins, %d recipes"), Tins.Num(), Recipes.Num());
	if (const FTorqueTin* T = BenchTinPtr())
		Out += FString::Printf(TEXT("; on the scale %s, %s, stir %.0f%%, %s g"), *TinText(*T), *TorquePaintMix::FullCode(T->Mix),
			T->Mix.Stir * 100.0f, *TorquePaintMix::GramsText(ScaleGrams()));
	if (ShakerTin != 0) Out += FString::Printf(TEXT("; shaking, %.0f s left"), ShakerLeft);
	if (bTest) Out += FString::Printf(TEXT("; test card %s"), *Test.Code);
	if (bTarget) Out += FString::Printf(TEXT("; matching %s"), *TargetName);
	for (const TPair<int32, FTorquePaintCup>& KV : Cups)
		Out += FString::Printf(TEXT("; player %d's cup %s %.2f l"), KV.Key, *KV.Value.Spec.Name, KV.Value.Litres);
	return Out;
}

// --- The requests (paint_bay.gd check and apply), on the shed ------------------------------------------

float UTorqueBuildSubsystem::HookTipOf(int32 Hook) const
{
	return Paint.HookTips.IsValidIndex(Hook) ? Paint.HookTips[Hook] : (float)TorqueShedLayout::HookTip;
}

bool UTorqueBuildSubsystem::IsPaintBayAct(ETorqueShedAct A)
{
	return (uint8)A >= (uint8)ETorqueShedAct::PaintBench && (uint8)A <= (uint8)ETorqueShedAct::PaintHook;
}

FString UTorqueBuildSubsystem::CheckPaintBay(int32 Peer, const FTorqueShedRequest& R) const
{
	namespace TPM = TorquePaintMix;
	const FTorquePaintBay& P = Paint;
	const FTorqueTin* Tin = P.BenchTinPtr();
	const bool bAtBench = P.BenchBy == Peer;
	const bool bHandsFull = HeldBy(Peer) != 0 || P.Held.Contains(Peer);
	switch (R.Kind)
	{
	case ETorqueShedAct::PaintBench:
		if (R.Item != 0)
		{
			if (P.BenchBy != 0 && P.BenchBy != Peer) return TEXT("Someone is at the mixing bench.");
			if (HeldBy(Peer) != 0) return TEXT("Put the part down first.");
		}
		return FString();
	case ETorqueShedAct::PaintTinNew:
	{
		if (!bAtBench) return TEXT("Step up to the mixing bench first.");
		if (Tin) return TEXT("There's a tin on the scale already.");
		if (!TPM::Bases().IsValidIndex(R.Colour)) return TEXT("There's no base like that.");
		if (P.StockOf(FTorquePaintBay::BaseStock(R.Colour)) < 1.0f - 0.001f)
			return FString::Printf(TEXT("No %s left. Restock it on the bench's Supplies page."), *FTorquePaintBay::StockName(R.Colour).ToLower());
		if (P.Tins.Num() >= FTorquePaintBay::MaxTins) return TEXT("Too many tins about: bin some empties first.");
		return FString();
	}
	case ETorqueShedAct::PaintPour:
	{
		if (!bAtBench || !Tin) return TEXT("Put a tin on the scale first.");
		const FString Why = TPM::PourProblem(Tin->Mix, R.Colour);
		if (!Why.IsEmpty()) return Why;
		if (P.StockOf(FTorquePaintBay::TintStock(R.Colour)) <= 0.001f)
			return FString::Printf(TEXT("The %s bottle's empty. Restock it on the bench's Supplies page."), TPM::Tints()[R.Colour].Name);
		if (!(R.Amount > 0.0f)) return TEXT("Nothing poured.");
		return FString();
	}
	case ETorqueShedAct::PaintPourStop:
		return bAtBench ? FString() : FString(TEXT("Step up to the mixing bench first."));
	case ETorqueShedAct::PaintStir:
	case ETorqueShedAct::PaintTare:
	case ETorqueShedAct::PaintAddBase:
	case ETorqueShedAct::PaintTinEmpty:
	case ETorqueShedAct::PaintTest:
	case ETorqueShedAct::PaintShake:
	case ETorqueShedAct::PaintTinSave:
		if (!bAtBench) return TEXT("Step up to the mixing bench first.");
		if (!Tin) return TEXT("There's no tin on the scale.");
		if (R.Kind == ETorqueShedAct::PaintAddBase)
		{
			if (Tin->Mix.TotalGrams() + TPM::AddBaseGrams > TPM::MaxTinGrams) return TEXT("The tin won't take any more.");
			if (P.StockOf(FTorquePaintBay::BaseStock(Tin->Mix.Base)) < TPM::AddBaseGrams / 1000.0f - 0.001f)
				return FString::Printf(TEXT("No %s left to add."), *FTorquePaintBay::StockName(Tin->Mix.Base).ToLower());
		}
		else if (R.Kind == ETorqueShedAct::PaintTest)
		{
			if (Tin->Mix.TintGrams() <= 0.0f && TPM::IsLacquerBase(Tin->Mix.Base)) return TEXT("That's just lacquer: nothing to see on a card.");
			if (Tin->Mix.Stir < 0.6f) return TEXT("Stir it first: it'd spray blotchy.");
		}
		else if (R.Kind == ETorqueShedAct::PaintShake)
		{
			if (P.ShakerTin != 0) return TEXT("The shaker's busy.");
		}
		else if (R.Kind == ETorqueShedAct::PaintTinSave)
		{
			const FString Name = R.Text.TrimStartAndEnd();
			if (Name.IsEmpty()) return TEXT("Give it a name first.");
			if (Name.Len() > 32) return TEXT("That name's too long.");
			if (P.FreeSlot() < 0) return TEXT("The tin shelf's full. Use or bin a tin first.");
		}
		return FString();
	case ETorqueShedAct::PaintRecipe:
		if (!bAtBench) return TEXT("Step up to the mixing bench first.");
		if (!P.Recipe(R.Item)) return TEXT("That card's gone.");
		{
			TPM::FTinMix Card;
			if (!TPM::ParseCode(P.Recipe(R.Item)->Code, Card)) return TEXT("That recipe card doesn't read.");
		}
		return FString();
	case ETorqueShedAct::PaintTarget:
		return bAtBench ? FString() : FString(TEXT("Step up to the mixing bench first."));
	case ETorqueShedAct::PaintBuy:
	{
		const int32 N = FMath::Max(R.Item, 1);
		if (!P.Stock.IsValidIndex(R.Colour)) return TEXT("There's no paint like that.");
		if (N > 20) return TEXT("Order between 1 and 20.");
		const float Pack = R.Colour < TPM::Bases().Num() ? 1.0f : TPM::BottleGrams;
		if (P.StockOf(R.Colour) + Pack * N > FTorquePaintBay::MaxStock) return TEXT("The bay's shelves are full of it.");
		return FString();
	}
	case ETorqueShedAct::PaintTinTake:
	{
		const FTorqueTin* T = P.Tins.Find(R.Item);
		if (!T) return TEXT("That tin has gone.");
		if (T->At != ETorqueTinAt::Shelf) return TEXT("That tin's in use.");
		if (bHandsFull) return TEXT("Your hands are full.");
		if (PushedBy(Peer) != INDEX_NONE) return TEXT("Let go of the bike first.");
		return FString();
	}
	case ETorqueShedAct::PaintTinPut:
		if (!P.HeldTin(Peer)) return TEXT("You're not holding a tin.");
		if (R.Item < 0 || R.Item >= FTorquePaintBay::ShelfSlots) return TEXT("Put it on the tin shelf.");
		if (P.TinInSlot(R.Item) != 0) return TEXT("There's a tin there.");
		return FString();
	case ETorqueShedAct::PaintTinBench:
		if (!P.HeldTin(Peer)) return TEXT("You're not holding a tin.");
		if (Tin) return TEXT("There's a tin on the scale already.");
		if (P.BenchBy != 0 && P.BenchBy != Peer) return TEXT("Someone's at the bench.");
		return FString();
	case ETorqueShedAct::PaintTinBin:
		return P.HeldTin(Peer) ? FString() : FString(TEXT("You're not holding a tin."));
	case ETorqueShedAct::PaintCup:
	{
		const FTorqueTin* T = P.HeldTin(Peer);
		if (!T) return TEXT("Hold a tin of paint to fill the gun's cup.");
		if (T->Litres <= 0.005f) return TEXT("The tin's empty.");
		if (T->Name.IsEmpty()) return TEXT("Name the tin at the bench first.");
		const FTorquePaintCup* C = P.Cups.Find(Peer);
		if (C && C->Spec.Code == T->Spec().Code && C->Spec.Name == T->Name && C->Litres >= TPM::CupLitres - 0.005f)
			return TEXT("The cup's full of it already.");
		return FString();
	}
	case ETorqueShedAct::PaintHook:
		if (R.Item < 0 || R.Item >= TorqueShedLayout::HookCount) return TEXT("That isn't a hook.");
		if (!FMath::IsFinite(R.Amount)) return TEXT("That isn't a height.");
		return FString();
	default:
		return TEXT("The shed doesn't know how to do that.");
	}
}

FString UTorqueBuildSubsystem::ApplyPaintBay(int32 Peer, const FTorqueShedRequest& R)
{
	namespace TPM = TorquePaintMix;
	FTorquePaintBay& P = Paint;
	FTorqueTin* Tin = P.BenchTinPtr();
	auto NewId = [&P]() { return P.NextId++; };
	switch (R.Kind)
	{
	case ETorqueShedAct::PaintBench:
		if (R.Item != 0) P.BenchBy = Peer;
		else if (P.BenchBy == Peer) P.BenchBy = 0;
		P.Tilt = 0.0f;
		P.PourTint = INDEX_NONE;
		return FString();
	case ETorqueShedAct::PaintTinNew:
	{
		P.Stock[FTorquePaintBay::BaseStock(R.Colour)] -= 1.0f;
		FTorqueTin T;
		T.Id = NewId();
		T.Mix = TPM::NewTin(R.Colour);
		T.At = ETorqueTinAt::Bench;
		T.Litres = T.Mix.Litres();
		P.Tins.Add(T.Id, T);
		P.BenchTin = T.Id;
		return FString::Printf(TEXT("A litre of %s on the scale."), *FTorquePaintBay::StockName(R.Colour).ToLower());
	}
	case ETorqueShedAct::PaintPour:
	{
		const int32 Stock = FTorquePaintBay::TintStock(R.Colour);
		float G = FMath::Min(FMath::Clamp(R.Amount, 0.0f, 60.0f), P.StockOf(Stock));
		G = FMath::Min(G, TPM::MaxTinGrams - Tin->Mix.TotalGrams());
		P.Stock[Stock] = FMath::Max(0.0f, P.StockOf(Stock) - G);
		Tin->Mix.Add(R.Colour, G);
		Tin->Mix.Stir = TPM::Unstir(Tin->Mix.Stir, G, Tin->Mix.TotalGrams());
		Tin->Litres = Tin->Mix.Litres();
		P.Tilt = FMath::Clamp(R.Tilt, 0.0f, 1.0f);
		P.PourTint = R.Colour;
		return FString();
	}
	case ETorqueShedAct::PaintPourStop:
		// The bottle goes back on the rack.
		P.Tilt = 0.0f;
		P.PourTint = INDEX_NONE;
		return FString();
	case ETorqueShedAct::PaintTare:
		P.TareGrams = Tin->Mix.TotalGrams();
		P.TareTin = Tin->Id;
		return FString();
	case ETorqueShedAct::PaintStir:
	{
		const float Was = Tin->Mix.Stir;
		Tin->Mix.Stir = FMath::Clamp(Was + FMath::Clamp(R.Amount, 0.0f, 0.5f), 0.0f, 1.0f);
		if (Was < 0.98f && Tin->Mix.Stir >= 0.98f && Tin->Mix.TintGrams() > 0.0f) return TEXT("Stirred to an even mix.");
		return FString();
	}
	case ETorqueShedAct::PaintAddBase:
	{
		const int32 Base = Tin->Mix.Base;
		P.Stock[FTorquePaintBay::BaseStock(Base)] -= TPM::AddBaseGrams / 1000.0f;
		Tin->Mix.BaseGrams += TPM::AddBaseGrams;
		Tin->Mix.Stir = TPM::Unstir(Tin->Mix.Stir, TPM::AddBaseGrams, Tin->Mix.TotalGrams());
		Tin->Litres = Tin->Mix.Litres();
		return FString::Printf(TEXT("Added %d g more %s to bring it back. Stir it in."), FMath::RoundToInt(TPM::AddBaseGrams),
			*FTorquePaintBay::StockName(Base).ToLower());
	}
	case ETorqueShedAct::PaintTinEmpty:
		P.Tins.Remove(Tin->Id);
		P.BenchTin = 0;
		return TEXT("Tipped the tin away.");
	case ETorqueShedAct::PaintTest:
	{
		// A test card sprayed from the tin, on the spray-out stand in the daylight booth.
		P.bTest = true;
		P.Test = Tin->Spec();
		P.TestN += 1;
		Tin->Mix.BaseGrams = FMath::Max(1.0f, Tin->Mix.BaseGrams - TPM::TestCardGrams);
		Tin->Litres = Tin->Mix.Litres();
		FString Text = TEXT("Test card sprayed: it's on the spray-out stand.");
		if (P.bTarget)
			Text += FString::Printf(TEXT(" Against %s: a %s match."), *P.TargetName,
				*TPM::MatchText(TPM::MatchOf(P.Test.Colour, P.TargetColour, P.Test.Effect, P.TargetEffect)));
		return Text;
	}
	case ETorqueShedAct::PaintShake:
		P.ShakerTin = Tin->Id;
		P.ShakerLeft = FTorquePaintBay::ShakeTime;
		Tin->At = ETorqueTinAt::Shaker;
		P.BenchTin = 0;
		return FString::Printf(TEXT("In the shaker: %d seconds."), FMath::RoundToInt(P.ShakerLeft));
	case ETorqueShedAct::PaintTinSave:
	{
		// paint_bay.gd _save_tin: named and put on the shelf, its recipe on the swatch wall.
		const FString Name = R.Text.TrimStartAndEnd();
		const int32 Slot = P.FreeSlot();   // (before it leaves the scale: it isn't on the shelf yet)
		Tin->Name = Name;
		Tin->At = ETorqueTinAt::Shelf;
		Tin->Ref = Slot;
		P.BenchTin = 0;
		const TPM::FPaintSpec S = Tin->Spec();
		bool bKnown = false;
		for (const FTorquePaintRecipe& Old : P.Recipes) bKnown = bKnown || (Old.Name == Name && Old.Code == S.Code);
		if (!bKnown)
		{
			FTorquePaintRecipe Card;
			Card.Id = NewId();
			Card.Name = Name;
			Card.Code = S.Code;
			Card.Colour = S.Colour;
			Card.Effect = S.Effect;
			P.Recipes.Add(Card);
			while (P.Recipes.Num() > FTorquePaintBay::MaxRecipes) P.Recipes.RemoveAt(0);
		}
		P.Mixed += 1;
		// How well it was done: close to the recipe card, close to the target.
		TArray<FString> Notes;
		if (P.CardRecipe != 0)
			Notes.Add(FString::Printf(TEXT("%d%% to the recipe"), FMath::RoundToInt(TPM::RecipeAccuracy(Tin->Mix, P.CardMix) * 100.0f)));
		if (P.bTarget)
			Notes.Add(TEXT("a ") + TPM::MatchText(TPM::MatchOf(S.Colour, P.TargetColour, S.Effect, P.TargetEffect)) + TEXT(" match"));
		if (Tin->Mix.Stir < 0.9f) Notes.Add(TEXT("not stirred right in"));
		FString Text = FString::Printf(TEXT("%s saved: on the tin shelf and the swatch wall"), *Name);
		if (Notes.Num() > 0) Text += TEXT(" (") + FString::Join(Notes, TEXT(", ")) + TEXT(")");
		return Text + TEXT(".");
	}
	case ETorqueShedAct::PaintRecipe:
	{
		// A card on the bench: its tints to pour, and its colour to match. R.Colour 1: match it by eye
		// (just the colour, no weights).
		const FTorquePaintRecipe* Card = P.Recipe(R.Item);
		TPM::FTinMix Mix;
		TPM::ParseCode(Card->Code, Mix);
		P.bTarget = true;
		P.TargetName = Card->Name;
		P.TargetColour = Card->Colour;
		P.TargetEffect = Card->Effect;
		if (R.Colour == 1)
		{
			P.CardRecipe = 0;
			P.CardMix = TPM::FTinMix();
			return FString::Printf(TEXT("Matching %s by eye."), *Card->Name);
		}
		P.CardRecipe = Card->Id;
		P.CardMix = Mix;
		return FString::Printf(TEXT("Recipe card on the bench: %s."), *Card->Name);
	}
	case ETorqueShedAct::PaintTarget:
		P.bTarget = false;
		P.CardRecipe = 0;
		P.CardMix = TPM::FTinMix();
		return TEXT("Recipe card and target put away.");
	case ETorqueShedAct::PaintBuy:
	{
		// Restocked from the paint supplier (free: the UE shed has no garage money yet).
		const int32 N = FMath::Max(R.Item, 1);
		const bool bBase = R.Colour < TPM::Bases().Num();
		P.Stock[R.Colour] = P.StockOf(R.Colour) + (bBase ? 1.0f : TPM::BottleGrams) * N;
		return FString::Printf(TEXT("Restocked the mixing bay: %d %s of %s."), N, bBase ? (N == 1 ? TEXT("tin") : TEXT("tins"))
			: (N == 1 ? TEXT("bottle") : TEXT("bottles")), *FTorquePaintBay::StockName(R.Colour).ToLower());
	}
	case ETorqueShedAct::PaintTinTake:
	{
		FTorqueTin& T = P.Tins[R.Item];
		T.At = ETorqueTinAt::Held;
		T.Ref = Peer;
		P.Held.Add(Peer, T.Id);
		return FString();
	}
	case ETorqueShedAct::PaintTinPut:
	{
		FTorqueTin& T = P.Tins[P.Held[Peer]];
		T.At = ETorqueTinAt::Shelf;
		T.Ref = R.Item;
		P.Held.Remove(Peer);
		return FString();
	}
	case ETorqueShedAct::PaintTinBench:
	{
		FTorqueTin& T = P.Tins[P.Held[Peer]];
		T.At = ETorqueTinAt::Bench;
		T.Ref = 0;
		P.Held.Remove(Peer);
		P.BenchTin = T.Id;
		return T.Name.IsEmpty() ? FString(TEXT("On the scale.")) : FString::Printf(TEXT("On the scale: %s."), *T.Name);
	}
	case ETorqueShedAct::PaintTinBin:
		P.Tins.Remove(P.Held[Peer]);
		P.Held.Remove(Peer);
		return TEXT("Binned the tin.");
	case ETorqueShedAct::PaintCup:
	{
		FTorqueTin& T = P.Tins[P.Held[Peer]];
		const TPM::FPaintSpec S = T.Spec();
		const FTorquePaintCup* Old = P.Cups.Find(Peer);
		const float Have = Old && Old->Spec.Code == S.Code && Old->Spec.Name == T.Name ? Old->Litres : 0.0f;
		const float Pour = FMath::Min(TPM::CupLitres - Have, T.Litres);
		T.Litres = FMath::Max(0.0f, T.Litres - Pour);
		FTorquePaintCup Cup;
		Cup.Spec = S;
		Cup.Litres = Have + Pour;
		P.Cups.Add(Peer, Cup);
		return FString::Printf(TEXT("Spray gun's cup filled with %s (%.2f l). Pick Cup on the spray gun (T)."), *T.Name, Cup.Litres);
	}
	case ETorqueShedAct::PaintHook:
	{
		while (P.HookTips.Num() < TorqueShedLayout::HookCount) P.HookTips.Add((float)TorqueShedLayout::HookTip);
		P.HookTips[R.Item] = FMath::Clamp(R.Amount, FTorquePaintBay::HookLow, FTorquePaintBay::HookHigh);
		return FString::Printf(TEXT("Hook %d at %d cm."), R.Item + 1, FMath::RoundToInt(P.HookTips[R.Item]));
	}
	default:
		return FString();
	}
}

// --- Spraying from the cup (paint_bay.gd cup_use, spray_spec) ------------------------------------------

FString UTorqueBuildSubsystem::CheckCup(int32 Peer, const FTorqueItem& It) const
{
	const FTorquePaintCup* Cup = Paint.Cups.Find(Peer);
	if (!Cup || Cup->Litres <= 0.0f) return TEXT("The gun's cup is empty. Fill it from a tin of mixed paint (hold the tin, R).");
	if (Cup->Litres + 0.001f < TorquePaintMix::LitresFor(It.Kind) * 0.5f)
		return FString::Printf(TEXT("Not enough %s left in the cup for that. Top it up from the tin."), *Cup->Spec.Name);
	return FString();
}

float UTorqueBuildSubsystem::SprayFromCup(int32 Peer, FTorqueItem& It)
{
	namespace TPM = TorquePaintMix;
	FTorquePaintCup& Cup = Paint.Cups.FindOrAdd(Peer);
	const TPM::FPaintSpec S = Cup.Spec;
	Cup.Litres = FMath::Max(0.0f, Cup.Litres - TPM::LitresFor(It.Kind));
	if (S.IsLacquer())
	{
		// Lacquer from the cup is a clear top coat: the colour stays, the sheen changes.
		It.Flat = S.Flat;
		if (It.Finish.IsNone()) It.Finish = FName(TEXT("gloss"));
		return 1.0f;
	}
	const bool bSame = !It.MixCode.IsEmpty() && It.MixCode == S.Code && It.MixName == S.Name && !It.bPrimerTop;
	const int32 Coats = bSame ? It.MixCoats + 1 : 1;
	if (!bSame) It.MixUnder = ShownColour(It);
	It.MixCode = S.Code;
	It.MixName = S.Name;
	It.MixEffect = S.Effect;
	It.MixCoats = FMath::Min(Coats, 8);
	It.Colour = TPM::SprayedColour(S, Coats, It.MixUnder);
	It.Finish = TPM::FinishOf(S);
	It.Flat = 0.0f;
	return TPM::CoatsFactor(S, Coats) * (0.7f + 0.3f * S.Stir);
}
