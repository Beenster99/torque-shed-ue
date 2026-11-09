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
		{TEXT("C"), TEXT("Clear base (candy)"), FLinearColor(0.9f, 0.9f, 0.9f), false, 0.0f},
		// A mirror-silver base: sprayed (three coats) it looks like chrome plate; a tint smokes or colours it.
		{TEXT("H"), TEXT("Chrome-effect base"), FLinearColor(0.88f, 0.89f, 0.9f), true, 1.0f},
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
		// The effects rack.
		{TEXT("PW"), TEXT("Pearl white"), FLinearColor(0.95f, 0.95f, 0.97f), 0.6f, ETintKind::Pearl, FLinearColor(0.85f, 0.9f, 1.0f)},
		{TEXT("PU"), TEXT("Pearl blue"), FLinearColor(0.7f, 0.8f, 1.0f), 0.8f, ETintKind::Pearl, FLinearColor(0.3f, 0.55f, 1.0f)},
		{TEXT("PO"), TEXT("Pearl gold"), FLinearColor(0.95f, 0.82f, 0.5f), 0.8f, ETintKind::Pearl, FLinearColor(1.0f, 0.75f, 0.25f)},
		{TEXT("CR"), TEXT("Candy red"), FLinearColor(0.75f, 0.02f, 0.05f), 30.0f, ETintKind::Candy},
		{TEXT("CB"), TEXT("Candy blue"), FLinearColor(0.03f, 0.15f, 0.75f), 30.0f, ETintKind::Candy},
		{TEXT("CG"), TEXT("Candy green"), FLinearColor(0.1f, 0.65f, 0.08f), 30.0f, ETintKind::Candy},
		{TEXT("FS"), TEXT("Silver flake"), FLinearColor(0.86f, 0.87f, 0.9f), 0.3f, ETintKind::Flake},
		{TEXT("FG"), TEXT("Gold flake"), FLinearColor(0.95f, 0.75f, 0.3f), 0.3f, ETintKind::Flake},
		{TEXT("FH"), TEXT("Holo flake"), FLinearColor(0.85f, 0.85f, 0.95f), 0.3f, ETintKind::Flake, FLinearColor::Black, FLinearColor::Black, true},
		{TEXT("XP"), TEXT("Chameleon purple-green"), FLinearColor(0.45f, 0.1f, 0.6f), 15.0f, ETintKind::Chameleon,
			FLinearColor(0.05f, 0.6f, 0.3f), FLinearColor(0.1f, 0.3f, 0.75f)},
		{TEXT("XB"), TEXT("Chameleon blue-gold"), FLinearColor(0.1f, 0.25f, 0.75f), 15.0f, ETintKind::Chameleon,
			FLinearColor(0.85f, 0.65f, 0.15f), FLinearColor(0.6f, 0.1f, 0.4f)},
		{TEXT("XR"), TEXT("Chameleon red-gold"), FLinearColor(0.7f, 0.05f, 0.1f), 15.0f, ETintKind::Chameleon,
			FLinearColor(0.95f, 0.7f, 0.1f), FLinearColor(0.3f, 0.6f, 0.2f)},
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
	if (Tints()[Tint].Kind == ETintKind::Candy && !IsClearBase(Tin.Base)) return TEXT("Candy dye goes in a clear base.");
	bool bIn = false;
	for (const FTintGrams& T : Tin.Tints) bIn = bIn || T.Tint == Tint;
	if (!bIn && Tin.Tints.Num() >= MaxTints) return TEXT("Five tints is the most a recipe takes.");
	if (Tin.TotalGrams() >= MaxTinGrams - 0.05f) return TEXT("The tin's full.");
	return FString();
}

// The paint a tin makes, but its code (FullCode asks this for the effect's letter).
FPaintSpec Look(const FTinMix& Tin)
{
	FPaintSpec Out;
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
	// The colour: a weighted geometric mean of the reflectances, by weight times strength (in a clear
	// base, the colour pigments are the candy's dye instead).
	const bool bOpaque = B.bOpaque;
	double WSum = 0.0, DyeW = 0.0;
	FVector LSum = FVector::ZeroVector, DyeSum = FVector::ZeroVector;
	if (bOpaque)
	{
		WSum = BaseG;
		LSum = TPMI::LnOf(TPMI::ToLinear(B.Colour)) * BaseG;
	}
	float Metal = B.Metal, Sparkle = 0.0f, PearlAmt = 0.0f, FlakeAmt = 0.0f, FlipAmt = 0.0f;
	float PearlG = 0.0f, FlakeG = 0.0f, FlipG = 0.0f;
	FVector PearlSum = FVector::ZeroVector, FlakeSum = FVector::ZeroVector, ASum = FVector::ZeroVector, BSum = FVector::ZeroVector;
	bool bHolo = false;
	for (const FTintGrams& TG : Tin.Tints)
	{
		if (!Tints().IsValidIndex(TG.Tint) || TG.Grams <= 0.0f) continue;
		const FTint& T = Tints()[TG.Tint];
		const float Share = TG.Grams / BaseG;   // grams per gram of base
		const double W = TG.Grams * T.K;
		switch (T.Kind)
		{
		case ETintKind::Alu:
			Metal = FMath::Max(Metal, FMath::Clamp(Share * 20.0f, 0.0f, 0.85f));
			break;
		case ETintKind::Sparkle:
			Metal = FMath::Max(Metal, FMath::Clamp(Share * 12.0f, 0.0f, 0.7f));
			Sparkle += FMath::Clamp(Share * 15.0f, 0.0f, 1.0f);
			break;
		case ETintKind::Pearl:
			PearlAmt += FMath::Clamp(Share * 18.0f, 0.0f, 1.0f);
			PearlSum += TPMI::ToLinear(T.A) * TG.Grams;
			PearlG += TG.Grams;
			break;
		case ETintKind::Flake:
			FlakeAmt += FMath::Clamp(Share * 22.0f, 0.0f, 1.0f);
			FlakeSum += TPMI::ToLinear(T.Colour) * TG.Grams;
			FlakeG += TG.Grams;
			bHolo = bHolo || T.bHolo;
			break;
		case ETintKind::Chameleon:
			FlipAmt += FMath::Clamp(Share * 16.0f, 0.0f, 1.0f);
			ASum += TPMI::ToLinear(T.A) * TG.Grams;
			BSum += TPMI::ToLinear(T.B) * TG.Grams;
			FlipG += TG.Grams;
			break;
		default:
			break;
		}
		if (!bOpaque)
		{
			if (T.Kind == ETintKind::Tint || T.Kind == ETintKind::Candy)
			{
				DyeW += W;
				DyeSum += TPMI::LnOf(TPMI::ToLinear(T.Colour)) * W;
			}
			continue;
		}
		if (W > 0.0)
		{
			WSum += W;
			LSum += TPMI::LnOf(TPMI::ToLinear(T.Colour)) * W;
		}
	}
	FLinearColor Col = WSum > 0.0 ? TPMI::ToSrgb(FVector(FMath::Exp(LSum.X / WSum), FMath::Exp(LSum.Y / WSum), FMath::Exp(LSum.Z / WSum))) : B.Colour;
	Out.Metal = Metal;
	if (PearlAmt > 0.0f)
	{
		Out.Pearl = FMath::Clamp(PearlAmt, 0.0f, 1.0f);
		Out.PearlTint = TPMI::ToSrgb(PearlSum / FMath::Max(PearlG, 0.001f));
	}
	if (FlakeAmt > 0.0f || Sparkle > 0.0f)
	{
		Out.Flake = FMath::Clamp(FlakeAmt + Sparkle * 0.35f, 0.0f, 1.0f);
		Out.FlakeColour = FlakeAmt > 0.0f ? TPMI::ToSrgb(FlakeSum / FMath::Max(FlakeG, 0.001f)) : FLinearColor(0.86f, 0.87f, 0.9f);
		Out.bHolo = bHolo;
	}
	if (FlipAmt > 0.0f)
	{
		Out.Flip = FMath::Clamp(FlipAmt, 0.0f, 1.0f);
		Out.FlipA = TPMI::ToSrgb(ASum / FMath::Max(FlipG, 0.001f));
		Out.FlipB = TPMI::ToSrgb(BSum / FMath::Max(FlipG, 0.001f));
	}
	if (!bOpaque)
	{
		// A candy: the dye and how deep each coat of it is, shown over silver at its best coats.
		if (DyeW > 0.0)
		{
			Out.Dye = TPMI::ToSrgb(FVector(FMath::Exp(DyeSum.X / DyeW), FMath::Exp(DyeSum.Y / DyeW), FMath::Exp(DyeSum.Z / DyeW)));
			Out.Depth = FMath::Clamp((float)(DyeW / BaseG) * 0.35f, 0.02f, 3.0f);
		}
		Col = CandyColour(FLinearColor(0.72f, 0.73f, 0.75f), Out, CandyBestLow);
	}
	Out.Colour = Col;
	// Which effect it is, the showiest last.
	FName Effect(TEXT("solid"));
	if (Metal >= 0.25f) Effect = FName(TEXT("metallic"));
	if (Out.Pearl >= 0.2f) Effect = FName(TEXT("pearl"));
	if (!bOpaque && Out.Depth > 0.0f) Effect = FName(TEXT("candy"));
	if (FlakeAmt >= 0.25f) Effect = FName(TEXT("flake"));
	if (IsChromeBase(Tin.Base) && FlakeAmt < 0.25f) Effect = FName(TEXT("chrome"));   // the mirror base, whatever smoke or colour's in it
	if (FlipAmt >= 0.3f) Effect = FName(TEXT("chameleon"));
	Out.Effect = Effect;
	if (Effect == FName(TEXT("metallic"))) Out.Rough = 0.3f;
	else if (Effect == FName(TEXT("flake")) || Effect == FName(TEXT("pearl"))) Out.Rough = 0.18f;
	else if (Effect == FName(TEXT("chrome"))) Out.Rough = 0.03f;
	else Out.Rough = 0.1f;
	Out.Need = (Effect == FName(TEXT("solid")) || Effect == FName(TEXT("metallic"))) ? 2 : 3;
	return Out;
}

FPaintSpec Spec(const FTinMix& Tin, const FString& Name)
{
	FPaintSpec Out = Look(Tin);
	Out.Name = Name;
	Out.Code = FullCode(Tin);
	return Out;
}

FLinearColor CandyColour(const FLinearColor& Under, const FPaintSpec& S, int32 Coats)
{
	// Each coat filters the light through the dye again: deeper and darker the more goes on.
	const FVector Dye = TPMI::ToLinear(S.Dye);
	const double E = (double)S.Depth * FMath::Max(Coats, 0);
	const FVector U = TPMI::ToLinear(Under);
	return TPMI::ToSrgb(FVector(U.X * FMath::Pow(FMath::Max(Dye.X, 0.0015), E), U.Y * FMath::Pow(FMath::Max(Dye.Y, 0.0015), E),
		U.Z * FMath::Pow(FMath::Max(Dye.Z, 0.0015), E)));
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
	static const TMap<FName, FString> L = {{FName(TEXT("metallic")), TEXT("M")}, {FName(TEXT("pearl")), TEXT("P")}, {FName(TEXT("candy")), TEXT("C")},
		{FName(TEXT("flake")), TEXT("F")}, {FName(TEXT("chrome")), TEXT("H")}, {FName(TEXT("chameleon")), TEXT("X")}, {FName(TEXT("lacquer")), TEXT("L")}};
	const FString* Found = L.Find(Effect);
	return Found ? *Found : FString(TEXT("S"));
}

FString EffectName(FName Effect)
{
	static const TMap<FName, FString> L = {{FName(TEXT("metallic")), TEXT("Metallic")}, {FName(TEXT("pearl")), TEXT("Pearl")},
		{FName(TEXT("candy")), TEXT("Candy")}, {FName(TEXT("flake")), TEXT("Flake")}, {FName(TEXT("chrome")), TEXT("Chrome effect")},
		{FName(TEXT("chameleon")), TEXT("Chameleon")}, {FName(TEXT("lacquer")), TEXT("Lacquer")}};
	const FString* Found = L.Find(Effect);
	return Found ? *Found : FString(TEXT("Solid"));
}

FString FullCode(const FTinMix& Tin)
{
	return CodeOf(Tin) + TEXT(":") + EffectLetter(Look(Tin).Effect);
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
	if (S.Effect == FName(TEXT("candy"))) return CandyColour(Under, S, Coats);
	const int32 Need = FMath::Max(S.Need, 1);
	const float Cover = Coats < Need ? FMath::Clamp(0.65f + 0.35f * float(Coats - 1) / float(FMath::Max(Need - 1, 1)), 0.0f, 1.0f) : 1.0f;
	FLinearColor C = FMath::Lerp(Under, S.Colour, Cover);
	C.A = 1.0f;
	return C;
}

float CoatsFactor(const FPaintSpec& S, int32 Coats)
{
	if (S.Effect == FName(TEXT("candy")))
	{
		if (Coats < CandyBestLow) return 0.7f + 0.1f * Coats;
		return Coats <= CandyBestHigh ? 1.0f : FMath::Max(0.75f, 1.0f - 0.1f * (Coats - CandyBestHigh));
	}
	const int32 Need = FMath::Max(S.Need, 1);
	return Coats >= Need ? 1.0f : 0.75f + 0.25f * float(Coats) / float(Need);
}

const TCHAR* PatternName(int32 Pattern)
{
	static const TCHAR* const Names[] = {TEXT("None"), TEXT("Centre stripe"), TEXT("Twin stripes"), TEXT("Pinstripe"), TEXT("Two-tone (top)")};
	return Pattern >= 0 && Pattern <= TapePatterns ? Names[Pattern] : Names[0];
}

FLinearColor TapeColourOn(const FLinearColor& Under)
{
	const FLinearColor Blue(0.18f, 0.42f, 0.85f), Cream(0.86f, 0.79f, 0.58f);
	const float D = FVector(Under.R - Blue.R, Under.G - Blue.G, Under.B - Blue.B).Size();
	return D < 0.4f ? Cream : Blue;
}

FName FinishOf(const FPaintSpec& S)
{
	// The finishes a part's drawn with: the effect itself is kept as its MixEffect.
	if (S.Effect == FName(TEXT("chrome"))) return FName(TEXT("chrome"));
	if (S.Effect == FName(TEXT("metallic")) || S.Effect == FName(TEXT("pearl")) || S.Effect == FName(TEXT("flake")) || S.Effect == FName(TEXT("chameleon")))
		return FName(TEXT("metallic"));
	return FName(TEXT("gloss"));
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
