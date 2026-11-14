#include "TftMenu.h"

#include <algorithm>
#include <cctype>

namespace TorqueTft
{
using TorqueSim::ModeRow;
using TorqueSim::ModeSet;
using TorqueSim::RideMode;

namespace
{
std::string Upper(std::string S)
{
	for (char& C : S) C = char(std::toupper(static_cast<unsigned char>(C)));
	return S;
}
const char* RowKey(ModeRow R)
{
	switch (R)
	{
	case ModeRow::Power: return "power";
	case ModeRow::Response: return "throttle";
	case ModeRow::Traction: return "traction";
	case ModeRow::Abs: return "abs";
	case ModeRow::Wheelie: return "wheelie";
	case ModeRow::Stability: return "stability";
	case ModeRow::EngineBrake: return "engine_brake";
	default: return "suspension";
	}
}
} // namespace

void Menu::Setup(const TorqueSim::BikeSpec::AidCaps& InCaps, bool InUltra, const std::string& StartMode)
{
	Caps = InCaps;
	Ultra = InUltra;
	Modes = TorqueSim::ModesFor(Caps);
	for (int M = 0; M < int(RideMode::Count); ++M) Sets[M] = TorqueSim::Template(RideMode(M), Caps);
	ModeNow = 0;
	for (size_t I = 0; I < Modes.size(); ++I)
		if (Modes[I] == RideMode::Touring) ModeNow = int(I);
	for (size_t I = 0; I < Modes.size(); ++I)
		if (!StartMode.empty() && StartMode == TorqueSim::ModeKey(Modes[I])) ModeNow = int(I);
	ModeCursor = ModeNow;
	SetCursor = 0;
	Editing = false;
	if (Now == View::Split && !Ultra) Now = View::Map;
	if (Now == View::Modes && Modes.empty()) Now = View::Ride;
}

void Menu::Take(const std::map<std::string, int>& Saved)
{
	for (const RideMode M : Modes)
	{
		// (Touring is the dyno desk's: not altered here, so the desk and the screen never disagree.)
		if (M == RideMode::Touring) continue;
		for (int R = 0; R < int(ModeRow::Count); ++R)
		{
			const auto It = Saved.find(std::string(TorqueSim::ModeKey(M)) + "." + RowKey(ModeRow(R)));
			if (It != Saved.end() && TorqueSim::RowOn(ModeRow(R), Caps)) TorqueSim::SetRow(ModeRow(R), Sets[size_t(M)], It->second);
		}
	}
}

std::map<std::string, int> Menu::Give() const
{
	std::map<std::string, int> Out;
	for (const RideMode M : Modes)
	{
		if (M == RideMode::Touring) continue;
		const ModeSet T = TorqueSim::Template(M, Caps);
		for (int R = 0; R < int(ModeRow::Count); ++R)
		{
			const int V = TorqueSim::RowValue(ModeRow(R), Sets[size_t(M)]);
			if (V != TorqueSim::RowValue(ModeRow(R), T)) Out[std::string(TorqueSim::ModeKey(M)) + "." + RowKey(ModeRow(R))] = V;
		}
	}
	return Out;
}

std::vector<View> Menu::Views() const
{
	std::vector<View> V = {View::Ride, View::Map};
	if (Ultra) V.push_back(View::Split);
	if (!Modes.empty()) V.push_back(View::Modes);
	V.push_back(View::Settings);
	return V;
}

std::vector<Menu::Item> Menu::Items() const
{
	std::vector<Item> Out;
	Out.push_back({0, ModeRow::Power});
	for (int R = 0; R < int(ModeRow::Count); ++R)
		if (!Modes.empty() && TorqueSim::RowOn(ModeRow(R), Caps)) Out.push_back({1, ModeRow(R)});
	Out.push_back({2, ModeRow::Power});
	if (!Modes.empty()) Out.push_back({3, ModeRow::Power});
	return Out;
}

int Menu::Do(Press P, double SpeedMs, double Throttle)
{
	const bool Moving = SpeedMs > 1.5 || SpeedMs < -1.5;
	// Changing a setting: up and down alter it, left (or right) is done.
	if (Now == View::Settings && Editing)
	{
		if (P == Press::Left || P == Press::Right)
		{
			Editing = false;
			return Moved;
		}
		const std::vector<Item> All = Items();
		const Item& It = All[size_t(std::min(SetCursor, int(All.size()) - 1))];
		const int Step = P == Press::Up ? 1 : -1;
		if (It.Kind == 0)
		{
			Look = Design((int(Look) + Step + int(Design::Count)) % int(Design::Count));
			return LookChanged;
		}
		if (It.Kind == 2)
		{
			NorthUp = !NorthUp;
			return MapChanged;
		}
		if (It.Kind == 1)
		{
			ModeSet& S = Sets[size_t(Mode())];
			const int Was = TorqueSim::RowValue(It.Row, S);
			TorqueSim::SetRow(It.Row, S, Was + Step);
			return TorqueSim::RowValue(It.Row, S) != Was ? (ModeChanged | SetChanged) : Nothing;
		}
		return Nothing;
	}
	if (P == Press::Left || P == Press::Right)
	{
		// In the modes, right takes the mode the bar is on (if it isn't the one in use already).
		if (Now == View::Modes && P == Press::Right && ModeCursor != ModeNow)
		{
			if (Throttle > 0.05)
			{
				Say("Close the throttle to change mode");
				return Nothing;
			}
			ModeNow = ModeCursor;
			Say(Upper(TorqueSim::ModeName(Mode())) + " mode");
			return ModeChanged;
		}
		// In the settings, right goes into the row the bar is on.
		if (Now == View::Settings && P == Press::Right)
		{
			const std::vector<Item> All = Items();
			const Item& It = All[size_t(std::min(SetCursor, int(All.size()) - 1))];
			if (It.Kind == 3)
			{
				if (Moving) { Say("Stop to change settings"); return Nothing; }
				if (Mode() == RideMode::Touring) { Say("Touring is set at the dyno desk"); return Nothing; }
				Sets[size_t(Mode())] = TorqueSim::Template(Mode(), Caps);
				Say(Upper(TorqueSim::ModeName(Mode())) + " put back as it came");
				return ModeChanged | SetChanged;
			}
			if (It.Kind == 1 && Moving) { Say("Stop to change settings"); return Nothing; }
			if (It.Kind == 1 && Mode() == RideMode::Touring) { Say("Touring is set at the dyno desk"); return Nothing; }
			Editing = true;
			return Moved;
		}
		const std::vector<View> V = Views();
		int At = 0;
		for (size_t I = 0; I < V.size(); ++I)
			if (V[I] == Now) At = int(I);
		At = (At + (P == Press::Right ? 1 : -1) + int(V.size())) % int(V.size());
		Now = V[size_t(At)];
		if (Now == View::Modes) ModeCursor = ModeNow;
		return Moved;
	}
	const int Step = P == Press::Down ? 1 : -1;
	switch (Now)
	{
	case View::Ride:
		TripPage = (TripPage + Step + 3) % 3;
		return Moved;
	case View::Map:
	case View::Split:
	{
		const int Was = Zoom;
		Zoom = std::max(-2, std::min(2, Zoom + Step));   // (down: further out)
		return Zoom != Was ? MapChanged : Nothing;
	}
	case View::Modes:
		ModeCursor = std::max(0, std::min(int(Modes.size()) - 1, ModeCursor + Step));
		return Moved;
	default:
		SetCursor = std::max(0, std::min(int(Items().size()) - 1, SetCursor + Step));
		return Moved;
	}
}

void Menu::Tick(double Dt)
{
	if (NoticeFor > 0.0 && (NoticeFor -= Dt) <= 0.0) Notice.clear();
}

void Menu::Fill(State& St) const
{
	St.Now = Now;
	St.Look = Look;
	St.TripPage = TripPage;
	St.Notice = Notice;
	St.Mode = Modes.empty() ? std::string() : Upper(TorqueSim::ModeName(Mode()));
	const ModeSet& S = Set();
	St.Aids.clear();
	if (!Modes.empty())
	{
		if (Caps.Traction) St.Aids.push_back({"TC", S.Traction, false});
		if (Caps.Abs) St.Aids.push_back({"ABS", S.Abs, false});
		if (Caps.Wheelie) St.Aids.push_back({"WC", S.Wheelie, false});
		St.Aids.push_back({"EB", S.EngineBrake, false});
	}
	else if (Caps.Abs) St.Aids.push_back({"ABS", Caps.Desk[0] > 0 ? 1 : 0, false});
	St.Modes.clear();
	for (const RideMode M : Modes) St.Modes.push_back(Upper(TorqueSim::ModeName(M)));
	St.ModeNow = ModeNow;
	St.ModeCursor = ModeCursor;
	St.ModeRows.clear();
	if (!Modes.empty())
	{
		const ModeSet& Shown = Sets[size_t(Modes[size_t(std::min(ModeCursor, int(Modes.size()) - 1))])];
		for (int R = 0; R < int(ModeRow::Count); ++R)
		{
			if (!TorqueSim::RowOn(ModeRow(R), Caps) || ModeRow(R) == ModeRow::Response) continue;
			const int V = TorqueSim::RowValue(ModeRow(R), Shown);
			std::string Words = TorqueSim::RowText(ModeRow(R), V);
			if (ModeRow(R) == ModeRow::Power) Words += std::string(", ") + (Shown.Response <= 1 ? "soft" : Shown.Response == 2 ? "smooth" : "sharp");
			St.ModeRows.push_back({TorqueSim::RowName(ModeRow(R)), Words, -1, 0, false});
		}
	}
	St.Settings.clear();
	const std::vector<Item> All = Items();
	for (const Item& It : All)
	{
		Row R;
		if (It.Kind == 0)
		{
			R.Label = "Screen design";
			R.Value = DesignName(Look);
			R.Level = int(Look) + 1;
			R.Levels = int(Design::Count);
		}
		else if (It.Kind == 2)
		{
			R.Label = "Map";
			R.Value = NorthUp ? "North up" : "Heading up";
		}
		else if (It.Kind == 3)
		{
			R.Label = "Put " + std::string(TorqueSim::ModeName(Mode())) + " back as it came";
			R.Locked = Mode() == RideMode::Touring;
		}
		else
		{
			int Lo = 0, Hi = 0;
			TorqueSim::RowRange(It.Row, Lo, Hi);
			const int V = TorqueSim::RowValue(It.Row, S);
			R.Label = TorqueSim::RowName(It.Row);
			R.Value = TorqueSim::RowText(It.Row, V);
			R.Level = V - Lo + (Lo > 0 ? 1 : 0);
			R.Levels = Hi - Lo + (Lo > 0 ? 1 : 0);
			R.Locked = Mode() == RideMode::Touring;
		}
		St.Settings.push_back(R);
	}
	St.SetCursor = std::min(SetCursor, int(All.size()) - 1);
	St.SetEditing = Editing;
	const Item& Here = All[size_t(std::max(0, St.SetCursor))];
	St.SetHelp = Here.Kind == 0 ? "Four ways to lay out the clocks. Pick the one you read best."
		: Here.Kind == 2 ? "Whether the map turns with the bike or keeps north at the top."
		: Here.Kind == 3 ? "Throws away your changes to this mode."
		: Mode() == RideMode::Touring ? std::string("Touring is the bike as the dyno desk set it. ") + TorqueSim::RowHelp(Here.Row) : TorqueSim::RowHelp(Here.Row);
}
} // namespace TorqueTft
