#include "TorqueKeys.h"

#include "GameFramework/PlayerController.h"
#include "Misc/ConfigCacheIni.h"

namespace
{
const TCHAR* KeysSection = TEXT("TorqueKeys");
constexpr uint8 F = FTorqueKeys::Foot, R = FTorqueKeys::Ride, FR = FTorqueKeys::Foot | FTorqueKeys::Ride;

// In ETorqueKey's order. The defaults are the Unreal port's keys as they were before rebinding came in.
// (Made the first time it's asked for, not at start-up: Unreal's EKeys must exist first.)
const FTorqueKeys::FAction* ActionList()
{
	static const FTorqueKeys::FAction List[] = {
	{TEXT("WalkForward"), TEXT("Walk forward"), F, {EKeys::W, FKey()}},
	{TEXT("WalkBack"), TEXT("Walk back"), F, {EKeys::S, FKey()}},
	{TEXT("WalkLeft"), TEXT("Walk left"), F, {EKeys::A, FKey()}},
	{TEXT("WalkRight"), TEXT("Walk right"), F, {EKeys::D, FKey()}},
	{TEXT("Run"), TEXT("Run (hold)"), F, {EKeys::LeftShift, FKey()}},
	{TEXT("Crouch"), TEXT("Crouch"), F, {EKeys::C, EKeys::LeftControl}},
	{TEXT("Jump"), TEXT("Jump"), F, {EKeys::SpaceBar, FKey()}},
	{TEXT("Work"), TEXT("Work / use (hold)"), F, {EKeys::LeftMouseButton, FKey()}},
	{TEXT("AltWork"), TEXT("Second work button (hold)"), F, {EKeys::R, EKeys::RightMouseButton}},
	{TEXT("Use"), TEXT("Use / get on (hold on a bike to wheel it)"), F, {EKeys::F, FKey()}},
	{TEXT("NextTool"), TEXT("Next tool"), F, {EKeys::E, EKeys::MouseScrollDown}},
	{TEXT("PrevTool"), TEXT("Previous tool"), F, {EKeys::Q, EKeys::MouseScrollUp}},
	{TEXT("Tool1"), TEXT("Hands"), F, {EKeys::One, FKey()}},
	{TEXT("Tool2"), TEXT("Wrench"), F, {EKeys::Two, FKey()}},
	{TEXT("Tool3"), TEXT("Brush"), F, {EKeys::Three, FKey()}},
	{TEXT("Tool4"), TEXT("Repair"), F, {EKeys::Four, FKey()}},
	{TEXT("Tool5"), TEXT("Spray can"), F, {EKeys::Five, FKey()}},
	{TEXT("Tool6"), TEXT("Pressure washer"), F, {EKeys::Six, FKey()}},
	{TEXT("Tool7"), TEXT("Spray gun"), F, {EKeys::Seven, FKey()}},
	{TEXT("SprayFinish"), TEXT("Spray finish"), F, {EKeys::T, FKey()}},
	{TEXT("GeometryMode"), TEXT("Geometry mode"), F, {EKeys::F2, FKey()}},
	{TEXT("Throttle"), TEXT("Throttle"), R, {EKeys::W, EKeys::Up}},
	{TEXT("FrontBrake"), TEXT("Front brake"), R, {EKeys::S, EKeys::Down}},
	{TEXT("RearBrake"), TEXT("Rear brake"), R, {EKeys::SpaceBar, FKey()}},
	{TEXT("LeanLeft"), TEXT("Lean left"), R, {EKeys::A, EKeys::Left}},
	{TEXT("LeanRight"), TEXT("Lean right"), R, {EKeys::D, EKeys::Right}},
	{TEXT("WeightBack"), TEXT("Weight back"), R, {EKeys::Q, FKey()}},
	{TEXT("WeightForward"), TEXT("Weight forward"), R, {EKeys::E, FKey()}},
	{TEXT("ShiftUp"), TEXT("Shift up"), R, {EKeys::X, EKeys::LeftShift}},
	{TEXT("ShiftDown"), TEXT("Shift down"), R, {EKeys::Z, EKeys::LeftControl}},
	{TEXT("AutoShift"), TEXT("Automatic gearbox on / off"), R, {EKeys::G, FKey()}},
	{TEXT("Abs"), TEXT("ABS on / off"), R, {EKeys::B, FKey()}},
	{TEXT("Clutch"), TEXT("Clutch in (hold): rev it in any gear"), R, {EKeys::F, FKey()}},
	{TEXT("Ignition"), TEXT("Engine on / off"), R, {EKeys::I, FKey()}},
	{TEXT("Camera"), TEXT("Camera: chase, first person, chin, showroom"), R, {EKeys::C, FKey()}},
	{TEXT("LookBack"), TEXT("Reverse view: hold to look behind"), R, {EKeys::Tab, FKey()}},
	{TEXT("BackOnRoad"), TEXT("Back on the road after a crash"), R, {EKeys::R, FKey()}},
	{TEXT("Restart"), TEXT("Restart the ride"), R, {EKeys::Home, FKey()}},
	{TEXT("RealismDown"), TEXT("Riding realism down"), R, {EKeys::LeftBracket, FKey()}},
	{TEXT("RealismUp"), TEXT("Riding realism up"), R, {EKeys::RightBracket, FKey()}},
	{TEXT("Weather"), TEXT("Weather: the next kind (the host's)"), R, {EKeys::V, FKey()}},
	{TEXT("Horn"), TEXT("Horn"), R, {EKeys::J, FKey()}},
	{TEXT("BackToShed"), TEXT("Back to the shed"), R, {EKeys::BackSpace, FKey()}},
	{TEXT("Telemetry"), TEXT("Telemetry overlay on / off"), R, {EKeys::F3, FKey()}},
	{TEXT("Help"), TEXT("Help lines on / off"), FR, {EKeys::H, FKey()}},
	{TEXT("Perf"), TEXT("Performance numbers on / off"), FR, {EKeys::P, FKey()}},
	{TEXT("PerfLog"), TEXT("Performance: log to a file on / off"), FR, {EKeys::O, FKey()}},
	};
	static_assert(UE_ARRAY_COUNT(List) == (int32)ETorqueKey::Count, "one action per ETorqueKey");
	return List;
}

} // namespace

const FTorqueKeys::FAction& FTorqueKeys::Action(ETorqueKey A)
{
	return ActionList()[FMath::Clamp((int32)A, 0, (int32)ETorqueKey::Count - 1)];
}

TArray<FKey>& FTorqueKeys::Table()
{
	const FAction* Actions = ActionList();
	static TArray<FKey> T;
	if (T.Num() == 0)
	{
		T.SetNum((int32)ETorqueKey::Count * Slots);
		for (int32 I = 0; I < (int32)ETorqueKey::Count; ++I)
		{
			for (int32 S = 0; S < Slots; ++S) T[I * Slots + S] = Actions[I].Default[S];
			FString Saved;
			if (GConfig && GConfig->GetString(KeysSection, Actions[I].Id, Saved, GGameUserSettingsIni))
			{
				TArray<FString> Names;
				Saved.ParseIntoArray(Names, TEXT(","), false);
				for (int32 S = 0; S < Slots; ++S)
				{
					const FKey Key = Names.IsValidIndex(S) && !Names[S].IsEmpty() ? FKey(FName(*Names[S])) : FKey();
					T[I * Slots + S] = Bindable(Key) ? Key : FKey();
				}
			}
		}
	}
	return T;
}

FKey FTorqueKeys::Get(ETorqueKey A, int32 Slot)
{
	return Table()[(int32)A * Slots + FMath::Clamp(Slot, 0, Slots - 1)];
}

void FTorqueKeys::Set(ETorqueKey A, int32 Slot, const FKey& Key)
{
	Table()[(int32)A * Slots + FMath::Clamp(Slot, 0, Slots - 1)] = Bindable(Key) ? Key : FKey();
	Save();
}

void FTorqueKeys::ResetAll()
{
	const FAction* Actions = ActionList();
	TArray<FKey>& T = Table();
	for (int32 I = 0; I < (int32)ETorqueKey::Count; ++I)
		for (int32 S = 0; S < Slots; ++S) T[I * Slots + S] = Actions[I].Default[S];
	Save();
}

void FTorqueKeys::Save()
{
	const FAction* Actions = ActionList();
	if (!GConfig) return;
	const TArray<FKey>& T = Table();
	for (int32 I = 0; I < (int32)ETorqueKey::Count; ++I)
	{
		const FKey Now[Slots] = {T[I * Slots], T[I * Slots + 1]};
		// Only what differs from the defaults (an empty slot is saved as an empty name).
		if (Now[0] == Actions[I].Default[0] && Now[1] == Actions[I].Default[1])
			GConfig->RemoveKey(KeysSection, Actions[I].Id, GGameUserSettingsIni);
		else
			GConfig->SetString(KeysSection, Actions[I].Id,
				*FString::Printf(TEXT("%s,%s"), Now[0].IsValid() ? *Now[0].ToString() : TEXT(""), Now[1].IsValid() ? *Now[1].ToString() : TEXT("")),
				GGameUserSettingsIni);
	}
	GConfig->Flush(false, GGameUserSettingsIni);
}

bool FTorqueKeys::Down(const APlayerController* PC, ETorqueKey A)
{
	if (!PC) return false;
	for (int32 S = 0; S < Slots; ++S)
	{
		const FKey Key = Get(A, S);
		if (Key.IsValid() && PC->IsInputKeyDown(Key)) return true;
	}
	return false;
}

bool FTorqueKeys::Just(const APlayerController* PC, ETorqueKey A)
{
	if (!PC) return false;
	for (int32 S = 0; S < Slots; ++S)
	{
		const FKey Key = Get(A, S);
		if (Key.IsValid() && PC->WasInputKeyJustPressed(Key)) return true;
	}
	return false;
}

FString FTorqueKeys::KeyName(const FKey& Key)
{
	if (!Key.IsValid()) return FString();
	if (Key == EKeys::LeftMouseButton) return TEXT("Left mouse");
	if (Key == EKeys::RightMouseButton) return TEXT("Right mouse");
	if (Key == EKeys::MiddleMouseButton) return TEXT("Middle mouse");
	if (Key == EKeys::MouseScrollUp) return TEXT("Wheel up");
	if (Key == EKeys::MouseScrollDown) return TEXT("Wheel down");
	return Key.GetDisplayName().ToString();
}

FString FTorqueKeys::Text(ETorqueKey A)
{
	TArray<FString> Names;
	for (int32 S = 0; S < Slots; ++S)
	{
		const FKey Key = Get(A, S);
		if (Key.IsValid()) Names.Add(KeyName(Key));
	}
	return Names.IsEmpty() ? FString(TEXT("(none)")) : FString::Join(Names, TEXT(" / "));
}

FString FTorqueKeys::Clash(ETorqueKey A)
{
	const FAction* Actions = ActionList();
	const FAction& Mine = Action(A);
	for (int32 I = 0; I < (int32)ETorqueKey::Count; ++I)
	{
		if (I == (int32)A || (Actions[I].Where & Mine.Where) == 0) continue;
		for (int32 S = 0; S < Slots; ++S)
		{
			const FKey Key = Get(A, S);
			if (!Key.IsValid()) continue;
			for (int32 T = 0; T < Slots; ++T)
				if (Get((ETorqueKey)I, T) == Key) return Actions[I].Label;
		}
	}
	return FString();
}

bool FTorqueKeys::Bindable(const FKey& Key)
{
	return Key.IsValid() && !Key.IsGamepadKey() && !Key.IsTouch() && !Key.IsGesture() && !Key.IsAxis1D() && !Key.IsAxis2D() &&
		   !Key.IsAxis3D() && Key != EKeys::AnyKey && Key != EKeys::Escape && !Key.IsDeprecated();
}
