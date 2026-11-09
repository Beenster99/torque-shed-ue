// Your profile's character (Torque Shed's profile picture, top right of the Esc menu): which
// MetaHuman you are in the shed, your height, and your face, as everyone in co-op sees you.
//
// Kept in Saved/Config/<platform>/GameUserSettings.ini under [TorqueProfile], sent to the others
// as a few bytes on ATorquePlayerState::Profile (Pack / Unpack), and put on an avatar with
// FTorqueAvatar::SetLook. The face is a set of sliders, each moving a group of the MetaHuman face's
// joints (FACIAL_*) from its rest: a runtime face editor on top of the avatar made on the PC.
#pragma once

#include "CoreMinimal.h"

struct FTorqueProfile
{
	enum { NumFace = 11 };
	int32 Avatar = -1;          // index into avatars.json; -1: the one for your colour
	float Height = 0.0f;        // -1 .. 1: 94% .. 106% of the avatar's own height
	float Face[NumFace] = {};   // each -1 .. 1, 0 the avatar's own face

	// A face slider: its name on the Profile page and the line under it.
	struct FSlider { const TCHAR* Name; const TCHAR* Hint; };
	static const FSlider Sliders[NumFace];

	double HeightScale() const { return 1.0 + 0.06 * FMath::Clamp(Height, -1.0f, 1.0f); }
	// Which avatar a player in Colour shows as.
	int32 AvatarFor(int32 Colour) const { return Avatar >= 0 ? Avatar : Colour; }
	void ResetFace();
	bool operator==(const FTorqueProfile& O) const;

	// About 13 bytes: the avatar, then the height and each slider in steps of 1/127.
	TArray<uint8> Pack() const;
	static FTorqueProfile Unpack(const TArray<uint8>& Bytes);

	// This machine's own, loaded once.
	static FTorqueProfile& Mine();
	void Save() const;
};
