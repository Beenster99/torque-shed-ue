#include "Shed/TorquePaintMix.h"

namespace TorquePaintMixImpl
{
float Lin(float X)
{
	const float C = FMath::Clamp(X, 0.0f, 1.0f);
	return C <= 0.04045f ? C / 12.92f : FMath::Pow((C + 0.055f) / 1.055f, 2.4f);
}

float Srgb(float X)
{
	const float C = FMath::Clamp(X, 0.0f, 1.0f);
	return C <= 0.0031308f ? C * 12.92f : 1.055f * FMath::Pow(C, 1.0f / 2.4f) - 0.055f;
}

FVector ToLinear(const FLinearColor& C) { return FVector(Lin(C.R), Lin(C.G), Lin(C.B)); }
FLinearColor ToSrgb(const FVector& V) { return FLinearColor(Srgb((float)V.X), Srgb((float)V.Y), Srgb((float)V.Z), 1.0f); }

FVector LnOf(const FVector& V)
{
	return FVector(FMath::Loge(FMath::Max(V.X, 0.0015)), FMath::Loge(FMath::Max(V.Y, 0.0015)), FMath::Loge(FMath::Max(V.Z, 0.0015)));
}

double LabF(double T) { return T > 0.008856 ? FMath::Pow(T, 1.0 / 3.0) : 7.787 * T + 16.0 / 116.0; }

// CIE L*a*b* (D65) of an sRGB colour.
FVector ToLab(const FLinearColor& C)
{
	const FVector L = ToLinear(C);
	const double X = (0.4124 * L.X + 0.3576 * L.Y + 0.1805 * L.Z) / 0.95047;
	const double Y = 0.2126 * L.X + 0.7152 * L.Y + 0.0722 * L.Z;
	const double Z = (0.0193 * L.X + 0.1192 * L.Y + 0.9505 * L.Z) / 1.08883;
	const double Fx = LabF(X), Fy = LabF(Y), Fz = LabF(Z);
	return FVector(116.0 * Fy - 16.0, 500.0 * (Fx - Fy), 200.0 * (Fy - Fz));
}

FName EffectFamily(FName E)
{
	return E == FName(TEXT("lacquer")) ? FName(TEXT("solid")) : E;
}
}

namespace TPMI = TorquePaintMixImpl;

namespace TorquePaintMix
{
const TArray<FBase>& Bases()
{
	static const TArray<FBase> L = {
		{TEXT("W"), TEXT("White base"), FLinearColor(0.95f, 0.95f, 0.94f), true, 0.0f},
		{TEXT("B"), TEXT("Black base"), FLinearColor(0.025f, 0.025f, 0.03f), true, 0.0f},
		{TEXT("S"), TEXT("Metallic silver base"), FLinearColor(0.72f, 0.73f, 0.75f), true, 0.8f},
		{TEXT("L"), TEXT("Clear lacquer"), FLinearColor(0.9f, 0.9f, 0.9f), false, 0.0f},
	};
	return L;
}

const TArray<FTint>& Tints()
{
	static const TArray<FTint> L = {
		{TEXT("LY"), TEXT("Lemon yellow"), FLinearColor(0.98f, 0.88f, 0.1f), 14.0f, ETintKind::Tint},
		{TEXT("YO"), TEXT("Yellow oxide"), FLinearColor(0.8f, 0.58f, 0.18f), 8.0f, ETintKind::Tint},
		{TEXT("OR"), TEXT("Orange"), FLinearColor(0.98f, 0.45f, 0.05f), 16.0f, ETintKind::Tint},
		{TEXT("SC"), TEXT("Scarlet"), FLinearColor(0.88f, 0.1f, 0.08f), 22.0f, ETintKind::Tint},
		{TEXT("OX"), TEXT("Oxide red"), FLinearColor(0.55f, 0.15f, 0.1f), 9.0f, ETintKind::Tint},
		{TEXT("MG"), TEXT("Magenta"), FLinearColor(0.75f, 0.05f, 0.45f), 18.0f, ETintKind::Tint},
		{TEXT("VI"), TEXT("Violet"), FLinearColor(0.35f, 0.1f, 0.6f), 12.0f, ETintKind::Tint},
		{TEXT("UB"), TEXT("Ultramarine blue"), FLinearColor(0.12f, 0.15f, 0.7f), 16.0f, ETintKind::Tint},
		{TEXT("PB"), TEXT("Phthalo blue"), FLinearColor(0.02f, 0.2f, 0.55f), 20.0f, ETintKind::Tint},
		{TEXT("PG"), TEXT("Phthalo green"), FLinearColor(0.0f, 0.42f, 0.3f), 26.0f, ETintKind::Tint},
		{TEXT("BR"), TEXT("Oxide brown"), FLinearColor(0.32f, 0.2f, 0.1f), 7.0f, ETintKind::Tint},
		{TEXT("BK"), TEXT("Jet black"), FLinearColor(0.02f, 0.02f, 0.025f), 25.0f, ETintKind::Tint},
		{TEXT("WH"), TEXT("Titanium white"), FLinearColor(0.97f, 0.97f, 0.96f), 4.0f, ETintKind::Tint},
		{TEXT("AL"), TEXT("Fine aluminium"), FLinearColor(0.78f, 0.79f, 0.8f), 3.0f, ETintKind::Alu},
		{TEXT("AS"), TEXT("Sparkle aluminium"), FLinearColor(0.82f, 0.83f, 0.85f), 2.0f, ETintKind::Sparkle},
		{TEXT("FL"), TEXT("Flattener"), FLinearColor(0.92f, 0.92f, 0.92f), 0.0f, ETintKind::Flattener},
	};
	return L;
}

int32 BaseIndex(const FString& Code)
{
	for (int32 I = 0; I < Bases().Num(); ++I)
		if (Code == Bases()[I].Code) return I;
	return INDEX_NONE;
}

int32 TintIndex(const FString& Code)
{
	for (int32 I = 0; I < Tints().Num(); ++I)
		if (Code == Tints()[I].Code) return I;
	return INDEX_NONE;
}

float FTinMix::GramsOf(int32 Tint) const
{
	for (const FTintGrams& T : Tints)
		if (T.Tint == Tint) return T.Grams;
	return 0.0f;
}

float FTinMix::TintGrams() const
{
	float G = 0.0f;
	for (const FTintGrams& T : Tints) G += T.Grams;
	return G;
}

void FTinMix::Add(int32 Tint, float Grams)
{
	for (FTintGrams& T : Tints)
	{
		if (T.Tint == Tint)
		{
			T.Grams += Grams;
			return;
		}
	}
	FTintGrams New;
	New.Tint = Tint;
	New.Grams = Grams;
	Tints.Add(New);
}

FTinMix NewTin(int32 Base)
{
	FTinMix T;
	T.Base = Bases().IsValidIndex(Base) ? Base : 0;
	return T;
}

FString PourProblem(const FTinMix& Tin, int32 Tint)
{
	if (!Tints().IsValidIndex(Tint)) return TEXT("There's no tint like that.");
	const bool bFlattener = Tints()[Tint].Kind == ETintKind::Flattener;
	if (IsLacquerBase(Tin.Base) && !bFlattener) return TEXT("Clear lacquer only takes flattener.");
	if (!IsLacquerBase(Tin.Base) && bFlattener) return TEXT("Flattener goes in a tin of clear lacquer.");
	bool bIn = false;
	for (const FTintGrams& T : Tin.Tints) bIn = bIn || T.Tint == Tint;
	if (!bIn && Tin.Tints.Num() >= MaxTints) return TEXT("Five tints is the most a recipe takes.");
	if (Tin.TotalGrams() >= MaxTinGrams - 0.05f) return TEXT("The tin's full.");
	return FString();
}

FPaintSpec Spec(const FTinMix& Tin, const FString& Name)
{
	FPaintSpec Out;
	Out.Name = Name;
	Out.Code = FullCode(Tin);
	Out.Stir = FMath::Clamp(Tin.Stir, 0.0f, 1.0f);
	const FBase& B = Bases()[Bases().IsValidIndex(Tin.Base) ? Tin.Base : 0];
	const float BaseG = FMath::Max(Tin.BaseGrams, 1.0f);
	if (IsLacquerBase(Tin.Base))
	{
		Out.Effect = FName(TEXT("lacquer"));
		Out.Flat = FMath::Clamp(Tin.GramsOf(TintIndex(TEXT("FL"))) / BaseG, 0.0f, 0.5f);
		Out.Colour = FLinearColor(0.9f, 0.9f, 0.9f);
		Out.Need = 1;
		return Out;
	}
	// The colour: a weighted geometric mean of the reflectances, by weight times strength.
	double WSum = BaseG;
	FVector LSum = TPMI::LnOf(TPMI::ToLinear(B.Colour)) * BaseG;
	float Metal = B.Metal, Sparkle = 0.0f;
	for (const FTintGrams& TG : Tin.Tints)
	{
		if (!Tints().IsValidIndex(TG.Tint) || TG.Grams <= 0.0f) continue;
		const FTint& T = Tints()[TG.Tint];
		const float Share = TG.Grams / BaseG;   // grams per gram of base
		const double W = TG.Grams * T.K;
		if (T.Kind == ETintKind::Alu) Metal = FMath::Max(Metal, FMath::Clamp(Share * 20.0f, 0.0f, 0.85f));
		else if (T.Kind == ETintKind::Sparkle)
		{
			Metal = FMath::Max(Metal, FMath::Clamp(Share * 12.0f, 0.0f, 0.7f));
			Sparkle += FMath::Clamp(Share * 15.0f, 0.0f, 1.0f);
		}
		if (W > 0.0)
		{
			WSum += W;
			LSum += TPMI::LnOf(TPMI::ToLinear(T.Colour)) * W;
		}
	}
	const FVector Lin(FMath::Exp(LSum.X / WSum), FMath::Exp(LSum.Y / WSum), FMath::Exp(LSum.Z / WSum));
	Out.Colour = TPMI::ToSrgb(Lin);
	Out.Metal = Metal;
	if (Sparkle > 0.0f) Out.Flake = FMath::Clamp(Sparkle * 0.35f, 0.0f, 1.0f);
	Out.Effect = Metal >= 0.25f ? FName(TEXT("metallic")) : FName(TEXT("solid"));
	Out.Rough = Metal >= 0.25f ? 0.3f : 0.1f;
	Out.Need = 2;
	return Out;
}

FString GramsText(float Grams)
{
	const float G = FMath::RoundToFloat(Grams * 10.0f) / 10.0f;
	if (FMath::IsNearlyEqual(G, FMath::RoundToFloat(G), 0.01f)) return FString::Printf(TEXT("%d"), FMath::RoundToInt(G));
	return FString::Printf(TEXT("%.1f"), G);
}

FString CodeOf(const FTinMix& Tin)
{
	TArray<FString> Parts;
	Parts.Add(Bases()[Bases().IsValidIndex(Tin.Base) ? Tin.Base : 0].Code);
	// Base added ("+250"), or taken by a test card ("~15": not "+-15", which the "-" between parts splits).
	if (Tin.BaseGrams - TinGrams > 0.05f) Parts.Add(TEXT("+") + GramsText(Tin.BaseGrams - TinGrams));
	else if (TinGrams - Tin.BaseGrams > 0.05f) Parts.Add(TEXT("~") + GramsText(TinGrams - Tin.BaseGrams));
	// The tints in the rack's order.
	for (int32 I = 0; I < Tints().Num(); ++I)
	{
		const float G = FMath::RoundToFloat(Tin.GramsOf(I) * 10.0f) / 10.0f;
		if (G > 0.0f) Parts.Add(FString(Tints()[I].Code) + GramsText(G));
	}
	return FString::Join(Parts, TEXT("-"));
}

FString EffectLetter(FName Effect)
{
	if (Effect == FName(TEXT("metallic"))) return TEXT("M");
	if (Effect == FName(TEXT("lacquer"))) return TEXT("L");
	return TEXT("S");
}

FString EffectName(FName Effect)
{
	if (Effect == FName(TEXT("metallic"))) return TEXT("Metallic");
	if (Effect == FName(TEXT("lacquer"))) return TEXT("Lacquer");
	return TEXT("Solid");
}

FString FullCode(const FTinMix& Tin)
{
	// The effect, without working the code out again (Spec calls this).
	FName Effect(TEXT("solid"));
	if (IsLacquerBase(Tin.Base)) Effect = FName(TEXT("lacquer"));
	else
	{
		const float BaseG = FMath::Max(Tin.BaseGrams, 1.0f);
		float Metal = Bases()[Bases().IsValidIndex(Tin.Base) ? Tin.Base : 0].Metal;
		for (const FTintGrams& TG : Tin.Tints)
		{
			if (!Tints().IsValidIndex(TG.Tint)) continue;
			const float Share = TG.Grams / BaseG;
			if (Tints()[TG.Tint].Kind == ETintKind::Alu) Metal = FMath::Max(Metal, FMath::Clamp(Share * 20.0f, 0.0f, 0.85f));
			if (Tints()[TG.Tint].Kind == ETintKind::Sparkle) Metal = FMath::Max(Metal, FMath::Clamp(Share * 12.0f, 0.0f, 0.7f));
		}
		if (Metal >= 0.25f) Effect = FName(TEXT("metallic"));
	}
	return CodeOf(Tin) + TEXT(":") + EffectLetter(Effect);
}

bool ParseCode(const FString& Code, FTinMix& Out)
{
	FString Body = Code.TrimStartAndEnd();
	int32 Colon = INDEX_NONE;
	if (Body.FindChar(TEXT(':'), Colon)) Body = Body.Left(Colon);
	TArray<FString> Parts;
	Body.ParseIntoArray(Parts, TEXT("-"), true);
	if (Parts.Num() == 0) return false;
	const int32 Base = BaseIndex(Parts[0]);
	if (Base == INDEX_NONE) return false;
	FTinMix Tin = NewTin(Base);
	for (int32 I = 1; I < Parts.Num(); ++I)
	{
		const FString& P = Parts[I];
		if (P.StartsWith(TEXT("+")) || P.StartsWith(TEXT("~")))
		{
			const FString Num = P.Mid(1);
			if (!Num.IsNumeric()) return false;
			const float Sign = P.StartsWith(TEXT("~")) ? -1.0f : 1.0f;
			Tin.BaseGrams = FMath::Clamp(TinGrams + Sign * FCString::Atof(*Num), 1.0f, MaxTinGrams);
			continue;
		}
		const int32 Tint = TintIndex(P.Left(2));
		const FString Num = P.Mid(2);
		if (Tint == INDEX_NONE || !Num.IsNumeric() || FCString::Atof(*Num) <= 0.0f) return false;
		Tin.Add(Tint, FCString::Atof(*Num));
	}
	if (Tin.Tints.Num() > MaxTints) return false;
	Out = Tin;
	return true;
}

FString SheenName(float Flat)
{
	if (Flat >= 0.3f) return TEXT("matt");
	if (Flat >= 0.08f) return TEXT("satin");
	return TEXT("gloss");
}

float PourRate(float Tilt)
{
	const float T = FMath::Clamp(Tilt, 0.0f, 1.0f);
	if (T < 0.15f) return 0.0f;
	return 0.25f + 30.0f * FMath::Pow((T - 0.15f) / 0.85f, 3.0f);
}

float Unstir(float Stir, float Grams, float Total)
{
	return FMath::Clamp(Stir - Grams / FMath::Max(Total, 1.0f) * 12.0f, 0.0f, 1.0f);
}

FLinearColor SprayedColour(const FPaintSpec& S, int32 Coats, const FLinearColor& Under)
{
	const int32 Need = FMath::Max(S.Need, 1);
	const float Cover = Coats < Need ? FMath::Clamp(0.65f + 0.35f * float(Coats - 1) / float(FMath::Max(Need - 1, 1)), 0.0f, 1.0f) : 1.0f;
	FLinearColor C = FMath::Lerp(Under, S.Colour, Cover);
	C.A = 1.0f;
	return C;
}

float CoatsFactor(const FPaintSpec& S, int32 Coats)
{
	const int32 Need = FMath::Max(S.Need, 1);
	return Coats >= Need ? 1.0f : 0.75f + 0.25f * float(Coats) / float(Need);
}

FName FinishOf(const FPaintSpec& S)
{
	return S.Effect == FName(TEXT("metallic")) ? FName(TEXT("metallic")) : FName(TEXT("gloss"));
}

float DeltaE(const FLinearColor& A, const FLinearColor& B)
{
	return (float)FVector::Dist(TPMI::ToLab(A), TPMI::ToLab(B));
}

float MatchOf(const FLinearColor& Have, const FLinearColor& Want, FName HaveEffect, FName WantEffect)
{
	float M = FMath::Clamp(1.0f - DeltaE(Have, Want) / 100.0f, 0.0f, 1.0f);
	if (!HaveEffect.IsNone() && !WantEffect.IsNone() && TPMI::EffectFamily(HaveEffect) != TPMI::EffectFamily(WantEffect))
		M = FMath::Max(0.0f, M - EffectMiss);
	return M;
}

FString MatchText(float Match)
{
	return FString::Printf(TEXT("%.1f%%"), Match * 100.0f);
}

float RecipeAccuracy(const FTinMix& Tin, const FTinMix& Recipe)
{
	float Total = 0.0f, Off = 0.0f;
	for (const FTintGrams& W : Recipe.Tints)
	{
		Total += W.Grams;
		Off += FMath::Abs(Tin.GramsOf(W.Tint) - W.Grams);
	}
	for (const FTintGrams& H : Tin.Tints)
		if (Recipe.GramsOf(H.Tint) <= 0.0f) Off += H.Grams;
	if (Total <= 0.0f) return Off <= 0.0f ? 1.0f : 0.0f;
	return FMath::Clamp(1.0f - Off / Total, 0.0f, 1.0f);
}

float LitresFor(FName Kind)
{
	static TMap<FName, float> L;
	if (L.Num() == 0)
	{
		const TCHAR* const Kinds[] = {TEXT("tank"), TEXT("fairing"), TEXT("side_panels"), TEXT("front_mudguard"), TEXT("guards"),
			TEXT("tail"), TEXT("frame"), TEXT("number_boards"), TEXT("top_box"), TEXT("panniers"), TEXT("headlight"), TEXT("wheel"),
			TEXT("fork"), TEXT("engine"), TEXT("shock"), TEXT("handlebars"), TEXT("saddle"), TEXT("mirrors")};
		const float Litres[] = {0.12f, 0.2f, 0.07f, 0.05f, 0.06f, 0.08f, 0.25f, 0.04f, 0.08f, 0.12f, 0.03f, 0.08f, 0.06f, 0.15f,
			0.04f, 0.03f, 0.06f, 0.02f};
		for (int32 I = 0; I < (int32)UE_ARRAY_COUNT(Kinds); ++I) L.Add(FName(Kinds[I]), Litres[I]);
	}
	const float* Found = L.Find(Kind);
	return Found ? *Found : 0.04f;
}
}
