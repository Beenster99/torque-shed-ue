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
	return B;
}

FTorqueProfile FTorqueProfile::Unpack(const TArray<uint8>& Bytes)
{
	using namespace TorqueProfileIni;
	FTorqueProfile P;
	if (Bytes.Num() >= 1) P.Avatar = (int32)Bytes[0] - 1;
	if (Bytes.Num() >= 2) P.Height = F(Bytes[1]);
	for (int32 I = 0; I < NumFace && I + 2 < Bytes.Num(); ++I) P.Face[I] = F(Bytes[I + 2]);
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
		GConfig->GetInt(Section, TEXT("RideView"), P.RideView, GGameUserSettingsIni);
		P.ClampChase();
	}
	return P;
}

void FTorqueProfile::ClampChase()
{
	ChaseDist = FMath::Clamp(ChaseDist, ChaseDistMin, ChaseDistMax);
	ChaseHeight = FMath::Clamp(ChaseHeight, ChaseHeightMin, ChaseHeightMax);
	ChaseTilt = FMath::Clamp(ChaseTilt, ChaseTiltMin, ChaseTiltMax);
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
	GConfig->SetInt(Section, TEXT("RideView"), RideView, GGameUserSettingsIni);
	GConfig->Flush(false, GGameUserSettingsIni);
}
