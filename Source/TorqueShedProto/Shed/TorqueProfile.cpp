#include "Shed/TorqueProfile.h"

#include "Misc/ConfigCacheIni.h"

// In the order the Profile page lists them (FTorqueAvatar::SetLook moves the joints).
const FTorqueProfile::FSlider FTorqueProfile::Sliders[FTorqueProfile::NumFace] = {
	{TEXT("Head size"), TEXT("The whole head, a little smaller or bigger")},
	{TEXT("Brow"), TEXT("The brows and forehead lower or higher")},
	{TEXT("Eye spacing"), TEXT("The eyes closer together or further apart")},
	{TEXT("Nose length"), TEXT("The tip of the nose in or out")},
	{TEXT("Nose width"), TEXT("Narrower or wider nostrils")},
	{TEXT("Nose bridge"), TEXT("A flatter or higher bridge")},
	{TEXT("Cheekbones"), TEXT("Softer or stronger cheekbones")},
	{TEXT("Lips"), TEXT("Thinner or fuller lips")},
	{TEXT("Jaw width"), TEXT("A narrower or wider jaw")},
	{TEXT("Chin"), TEXT("A shorter, set back chin, or a longer one further forward")},
	{TEXT("Ears"), TEXT("Smaller or bigger ears")},
};

const FTorqueProfile::FHelmetSwatch FTorqueProfile::HelmetSwatches[FTorqueProfile::NumHelmetSwatch] = {
	{TEXT("White"), 0.0f, 0.0f, 0.95f},
	{TEXT("Black"), 0.0f, 0.0f, 0.06f},
	{TEXT("Silver"), 0.6f, 0.04f, 0.72f},
	{TEXT("Gunmetal"), 0.6f, 0.08f, 0.3f},
	{TEXT("Red"), 0.995f, 0.9f, 0.78f},
	{TEXT("Orange"), 0.07f, 0.92f, 0.95f},
	{TEXT("Yellow"), 0.14f, 0.9f, 0.95f},
	{TEXT("Lime"), 0.24f, 0.8f, 0.85f},
	{TEXT("Racing green"), 0.39f, 0.75f, 0.32f},
	{TEXT("Teal"), 0.49f, 0.8f, 0.6f},
	{TEXT("Sky blue"), 0.56f, 0.6f, 0.92f},
	{TEXT("Royal blue"), 0.62f, 0.85f, 0.7f},
	{TEXT("Navy"), 0.63f, 0.8f, 0.26f},
	{TEXT("Purple"), 0.76f, 0.7f, 0.55f},
	{TEXT("Pink"), 0.92f, 0.55f, 0.95f},
	{TEXT("Sand"), 0.1f, 0.35f, 0.78f},
};

const TCHAR* FTorqueProfile::HelmetFinishName(int32 Finish)
{
	static const TCHAR* Names[NumHelmetFinish] = {TEXT("Gloss"), TEXT("Matt"), TEXT("Metallic"), TEXT("Pearl")};
	return Names[FMath::Clamp(Finish, 0, (int32)NumHelmetFinish - 1)];
}

namespace TorqueProfileHelmet
{
// Hue (0..1 of the circle), richness and brightness, as the eye sees them, to linear colour.
FLinearColor FromHsv(float H, float S, float V)
{
	const FLinearColor Shown = FLinearColor(FMath::Frac(H) * 360.0f, FMath::Clamp(S, 0.0f, 1.0f), FMath::Clamp(V, 0.0f, 1.0f)).HSVToLinearRGB();
	return FLinearColor::FromSRGBColor(Shown.ToFColor(false));
}
} // namespace TorqueProfileHelmet

FLinearColor FTorqueProfile::HelmetColour() const
{
	return TorqueProfileHelmet::FromHsv(HelmetHue, HelmetSat, HelmetVal);
}

FLinearColor FTorqueProfile::HelmetPearlTint() const
{
	return TorqueProfileHelmet::FromHsv(HelmetHue + 0.11f, HelmetSat * 0.45f, FMath::Min(1.0f, HelmetVal + 0.25f));
}

int32 FTorqueProfile::HelmetSwatch() const
{
	if (!bHelmetPainted) return -2;
	for (int32 I = 0; I < NumHelmetSwatch; ++I)
	{
		const FHelmetSwatch& W = HelmetSwatches[I];
		if (FMath::Abs(W.Sat - HelmetSat) < 0.02f && FMath::Abs(W.Val - HelmetVal) < 0.02f
			&& (W.Sat < 0.02f || FMath::Abs(FMath::Frac(W.Hue - HelmetHue + 0.5f) - 0.5f) < 0.01f))
			return I;
	}
	return -1;
}

void FTorqueProfile::SetHelmetSwatch(int32 I)
{
	if (I < 0 || I >= NumHelmetSwatch) { bHelmetPainted = false; return; }
	bHelmetPainted = true;
	HelmetHue = HelmetSwatches[I].Hue;
	HelmetSat = HelmetSwatches[I].Sat;
	HelmetVal = HelmetSwatches[I].Val;
}

namespace TorqueProfileIni
{
const TCHAR* Section = TEXT("TorqueProfile");
int8 Q(float V) { return (int8)FMath::Clamp(FMath::RoundToInt(V * 127.0f), -127, 127); }
float F(uint8 B) { return FMath::Clamp((float)(int8)B / 127.0f, -1.0f, 1.0f); }
} // namespace TorqueProfileIni

void FTorqueProfile::ResetFace()
{
	for (float& V : Face) V = 0.0f;
}

bool FTorqueProfile::operator==(const FTorqueProfile& O) const
{
	return Pack() == O.Pack();
}

TArray<uint8> FTorqueProfile::Pack() const
{
	using namespace TorqueProfileIni;
	TArray<uint8> B;
	B.Add((uint8)FMath::Clamp(Avatar + 1, 0, 255));
	B.Add((uint8)Q(Height));
	for (float V : Face) B.Add((uint8)Q(V));
	// [helmet plan] The helmet's paint.
	B.Add((uint8)((bHelmetPainted ? 1 : 0) | (FMath::Clamp(HelmetFinish, 0, (int32)NumHelmetFinish - 1) << 1)));
	for (float V : {HelmetHue, HelmetSat, HelmetVal}) B.Add((uint8)FMath::Clamp(FMath::RoundToInt(V * 255.0f), 0, 255));
	return B;
}

FTorqueProfile FTorqueProfile::Unpack(const TArray<uint8>& Bytes)
{
	using namespace TorqueProfileIni;
	FTorqueProfile P;
	if (Bytes.Num() >= 1) P.Avatar = (int32)Bytes[0] - 1;
	if (Bytes.Num() >= 2) P.Height = F(Bytes[1]);
	for (int32 I = 0; I < NumFace && I + 2 < Bytes.Num(); ++I) P.Face[I] = F(Bytes[I + 2]);
	const int32 H = 2 + NumFace;   // [helmet plan] after the face
	if (Bytes.Num() >= H + 4)
	{
		P.bHelmetPainted = (Bytes[H] & 1) != 0;
		P.HelmetFinish = FMath::Clamp((int32)(Bytes[H] >> 1), 0, (int32)NumHelmetFinish - 1);
		P.HelmetHue = Bytes[H + 1] / 255.0f;
		P.HelmetSat = Bytes[H + 2] / 255.0f;
		P.HelmetVal = Bytes[H + 3] / 255.0f;
	}
	return P;
}

FTorqueProfile& FTorqueProfile::Mine()
{
	using namespace TorqueProfileIni;
	static FTorqueProfile P;
	static bool bLoaded = false;
	if (!bLoaded && GConfig)
	{
		bLoaded = true;
		GConfig->GetInt(Section, TEXT("Avatar"), P.Avatar, GGameUserSettingsIni);
		GConfig->GetFloat(Section, TEXT("Height"), P.Height, GGameUserSettingsIni);
		for (int32 I = 0; I < NumFace; ++I)
			GConfig->GetFloat(Section, *FString::Printf(TEXT("Face%d"), I), P.Face[I], GGameUserSettingsIni);
		GConfig->GetFloat(Section, TEXT("ChaseDist"), P.ChaseDist, GGameUserSettingsIni);
		GConfig->GetFloat(Section, TEXT("ChaseHeight"), P.ChaseHeight, GGameUserSettingsIni);
		GConfig->GetFloat(Section, TEXT("ChaseTilt"), P.ChaseTilt, GGameUserSettingsIni);
		GConfig->GetFloat(Section, TEXT("FirstForward"), P.FirstForward, GGameUserSettingsIni);
		GConfig->GetFloat(Section, TEXT("FirstHeight"), P.FirstHeight, GGameUserSettingsIni);
		GConfig->GetFloat(Section, TEXT("FirstTilt"), P.FirstTilt, GGameUserSettingsIni);
		GConfig->GetInt(Section, TEXT("RideView"), P.RideView, GGameUserSettingsIni);
		GConfig->GetArray(Section, TEXT("Visited"), P.Visited, GGameUserSettingsIni);
		GConfig->GetBool(Section, TEXT("HelmetPainted"), P.bHelmetPainted, GGameUserSettingsIni);
		GConfig->GetFloat(Section, TEXT("HelmetHue"), P.HelmetHue, GGameUserSettingsIni);
		GConfig->GetFloat(Section, TEXT("HelmetSat"), P.HelmetSat, GGameUserSettingsIni);
		GConfig->GetFloat(Section, TEXT("HelmetVal"), P.HelmetVal, GGameUserSettingsIni);
		GConfig->GetInt(Section, TEXT("HelmetFinish"), P.HelmetFinish, GGameUserSettingsIni);
		P.HelmetFinish = FMath::Clamp(P.HelmetFinish, 0, (int32)NumHelmetFinish - 1);
		P.ClampChase();
	}
	return P;
}

void FTorqueProfile::ClampChase()
{
	ChaseDist = FMath::Clamp(ChaseDist, ChaseDistMin, ChaseDistMax);
	ChaseHeight = FMath::Clamp(ChaseHeight, ChaseHeightMin, ChaseHeightMax);
	ChaseTilt = FMath::Clamp(ChaseTilt, ChaseTiltMin, ChaseTiltMax);
	FirstForward = FMath::Clamp(FirstForward, FirstForwardMin, FirstForwardMax);
	FirstHeight = FMath::Clamp(FirstHeight, FirstHeightMin, FirstHeightMax);
	FirstTilt = FMath::Clamp(FirstTilt, FirstTiltMin, FirstTiltMax);
	if (RideView != 0 && RideView != 1 && RideView != 4) RideView = 0;
}

void FTorqueProfile::Save() const
{
	using namespace TorqueProfileIni;
	if (!GConfig) return;
	GConfig->SetInt(Section, TEXT("Avatar"), Avatar, GGameUserSettingsIni);
	GConfig->SetFloat(Section, TEXT("Height"), Height, GGameUserSettingsIni);
	for (int32 I = 0; I < NumFace; ++I)
		GConfig->SetFloat(Section, *FString::Printf(TEXT("Face%d"), I), Face[I], GGameUserSettingsIni);
	GConfig->SetFloat(Section, TEXT("ChaseDist"), ChaseDist, GGameUserSettingsIni);
	GConfig->SetFloat(Section, TEXT("ChaseHeight"), ChaseHeight, GGameUserSettingsIni);
	GConfig->SetFloat(Section, TEXT("ChaseTilt"), ChaseTilt, GGameUserSettingsIni);
	GConfig->SetFloat(Section, TEXT("FirstForward"), FirstForward, GGameUserSettingsIni);
	GConfig->SetFloat(Section, TEXT("FirstHeight"), FirstHeight, GGameUserSettingsIni);
	GConfig->SetFloat(Section, TEXT("FirstTilt"), FirstTilt, GGameUserSettingsIni);
	GConfig->SetInt(Section, TEXT("RideView"), RideView, GGameUserSettingsIni);
	GConfig->SetArray(Section, TEXT("Visited"), Visited, GGameUserSettingsIni);
	GConfig->SetBool(Section, TEXT("HelmetPainted"), bHelmetPainted, GGameUserSettingsIni);
	GConfig->SetFloat(Section, TEXT("HelmetHue"), HelmetHue, GGameUserSettingsIni);
	GConfig->SetFloat(Section, TEXT("HelmetSat"), HelmetSat, GGameUserSettingsIni);
	GConfig->SetFloat(Section, TEXT("HelmetVal"), HelmetVal, GGameUserSettingsIni);
	GConfig->SetInt(Section, TEXT("HelmetFinish"), HelmetFinish, GGameUserSettingsIni);
	GConfig->Flush(false, GGameUserSettingsIni);
}

bool FTorqueProfile::HasVisited(const FString& Key) const
{
	const FString Lead = Key + TEXT("|");
	for (const FString& V : Visited)
		if (V.StartsWith(Lead, ESearchCase::CaseSensitive)) return true;
	return false;
}

bool FTorqueProfile::Visit(const FString& Key, const FString& Name)
{
	if (Key.IsEmpty() || HasVisited(Key)) return false;
	// ('|' would split the entry: none in the name)
	Visited.Add(FString::Printf(TEXT("%s|%s|%s"), *Key, *Name.Replace(TEXT("|"), TEXT("/")), *FDateTime::Now().ToString(TEXT("%Y-%m-%d"))));
	return true;
}

FString FTorqueProfile::LastVisitedName() const
{
	if (Visited.Num() == 0) return FString();
	TArray<FString> Bits;
	Visited.Last().ParseIntoArray(Bits, TEXT("|"), false);
	return Bits.Num() >= 2 ? Bits[1] : FString();
}
