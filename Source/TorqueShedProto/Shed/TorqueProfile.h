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

	// Riding cameras (kept with your character on this machine, not sent to the others): the
	// third-person camera as you set it (Settings > Gameplay, or while riding: the mouse wheel,
	// Page Up / Down and Insert / Delete, or R1 held with the right stick and D-pad), and the view
	// you last rode in (ATorqueBikePawn's CameraMode: 0 third person, 1 first person, 4 drive-by).
	float ChaseDist = 480.0f;   // cm behind the bike
	float ChaseHeight = 70.0f;  // cm above the rider's shoulders
	float ChaseTilt = 0.0f;     // degrees looking further down (below 0: up)
	int32 RideView = 0;
	static constexpr float ChaseDistMin = 250.0f, ChaseDistMax = 1200.0f;
	static constexpr float ChaseHeightMin = -40.0f, ChaseHeightMax = 300.0f;
	static constexpr float ChaseTiltMin = -10.0f, ChaseTiltMax = 25.0f;
	void ClampChase();
	void ResetChase() { ChaseDist = 480.0f; ChaseHeight = 70.0f; ChaseTilt = 0.0f; }
	// First person, the same three (beenster): the eye moved forward or back and up or down from the
	// rider's own eyes (cm), and the view tilted further down (degrees) than the aim that keeps the
	// hands on the bars in view (ATorqueBikePawn::FirstPersonAim). Set as third person's are.
	float FirstForward = 0.0f;  // cm, + towards the bars
	float FirstHeight = 0.0f;   // cm, + up
	float FirstTilt = 0.0f;     // degrees further down (below 0: up)
	static constexpr float FirstForwardMin = -20.0f, FirstForwardMax = 20.0f;
	static constexpr float FirstHeightMin = -15.0f, FirstHeightMax = 25.0f;
	static constexpr float FirstTiltMin = -15.0f, FirstTiltMax = 15.0f;
	void ResetFirst() { FirstForward = 0.0f; FirstHeight = 0.0f; FirstTilt = 0.0f; }

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
