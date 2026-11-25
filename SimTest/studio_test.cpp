// The 3D preview windows' photo studio (Source/TorqueShedProto/Shed/TorqueStudio*, beenster Nov 25:
// "it should not be affected by exposure/daylight in main game"). The previews have gone dark or washed
// out three times (builds 125, 155, 171), each time because something of the game's own lighting got into
// the preview's camera. This guards it before a build:
//  1. The rig: a product shoot (key brighter than fill by 2-4x, edge strips behind on both sides, a top
//     light, one shadow, neutral colours, nothing flat on the camera's axis).
//  2. The exposure: a mid-grey, a white and a black part and the backdrop land where a studio shot puts
//     them (by the rig's numbers and the engine's manual exposure: nits x 2^Bias / 1.2).
//  3. The meter: from a renderer anything from 5 stops dark to 3 stops bright, it brings the backdrop to
//     its grey within its 8 reads.
//  4. The source: every scene capture in the game is either a studio (TorqueStudio::Isolate, its soft
//     boxes, its meter, no exposure or post-process settings of its own, no point lights) or on the short
//     list of captures meant to see the game; and Isolate still shuts out each thing that has leaked in
//     before (sky light, fog, sky, clouds, post-process materials, the level's post-process volumes, the
//     Brightness setting's gamma).
#include "TorqueStudioRig.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace
{
namespace R = TorqueStudioRig;
int Failures = 0;
int Checks = 0;

void Expect(bool Ok, const std::string& What)
{
	++Checks;
	std::printf("  %s %s\n", Ok ? "ok  " : "FAIL", What.c_str());
	if (!Ok) ++Failures;
}

std::string Fmt(const char* F, double A, double B = 0.0, double C = 0.0)
{
	char Buf[256];
	std::snprintf(Buf, sizeof(Buf), F, A, B, C);
	return Buf;
}

std::string ReadText(const std::filesystem::path& P)
{
	std::ifstream In(P, std::ios::binary);
	std::stringstream S;
	S << In.rdbuf();
	return S.str();
}

bool Has(const std::string& Text, const std::string& What) { return Text.find(What) != std::string::npos; }

// Illuminance (lux) a light gives a surface facing direction N (camera frame: x toward camera, y right, z up).
double LuxOn(const R::FSoftBox& L, double Nx, double Ny, double Nz)
{
	const double Len = std::sqrt(L.Toward * L.Toward + L.Right * L.Right + L.Up * L.Up);
	const double Cos = (L.Toward * Nx + L.Right * Ny + L.Up * Nz) / Len;
	return Cos > 0.0 ? R::Lux(L) * Cos : 0.0;
}

// The engine's manual exposure: a surface's nits to its value before the tone curve.
double Value(double Nits, double Bias) { return Nits * std::pow(2.0, Bias) / 1.2; }

// A stand-in for the renderer's tone curve and display encoding (a filmic shoulder, then sRGB), to try
// the meter against.
double Display(double Scene)
{
	const double T = Scene * 1.25 / (1.0 + Scene * 1.25);   // shoulder
	return T <= 0.0031308 ? T * 12.92 : 1.055 * std::pow(T, 1.0 / 2.4) - 0.055;
}

void CheckRig()
{
	std::printf("The rig\n");
	const R::FSoftBox* Key = nullptr;
	const R::FSoftBox* Fill = nullptr;
	int Shadows = 0, BehindLeft = 0, BehindRight = 0, Top = 0;
	bool Neutral = true, OffAxis = true;
	for (const R::FSoftBox& L : R::Lights)
	{
		const std::string N = L.Name;
		if (N == "Key") Key = &L;
		if (N == "Fill") Fill = &L;
		if (L.bShadows) ++Shadows;
		if (L.Toward < -0.3 && L.Right < -0.3) ++BehindLeft;
		if (L.Toward < -0.3 && L.Right > 0.3) ++BehindRight;
		const double Len = std::sqrt(L.Toward * L.Toward + L.Right * L.Right + L.Up * L.Up);
		if (L.Up / Len > 0.9) ++Top;
		if (std::max({L.R, L.G, L.B}) - std::min({L.R, L.G, L.B}) > 0.1f) Neutral = false;
		if (L.Toward / Len > std::cos(20.0 * 3.14159265 / 180.0)) OffAxis = false;   // a flash on the lens flattens it
		if (L.Width <= 0.0 || L.Height <= 0.0 || L.Candelas <= 0.0) Expect(false, std::string(L.Name) + " has a size and a brightness");
	}
	Expect(Key && Fill, "there is a key and a fill");
	if (!Key || !Fill) return;
	const double KeyLux = LuxOn(*Key, 1, 0, 0);
	const double FillLux = LuxOn(*Fill, 1, 0, 0);
	const double Ratio = R::Lux(*Key) / R::Lux(*Fill);
	Expect(Ratio >= 2.0 && Ratio <= 4.0, Fmt("key to fill %.2f : 1 (a studio's 2-4 : 1; at the subject facing the camera %.1f and %.1f lux)", Ratio, KeyLux, FillLux));
	Expect(Key->Right < 0.0 && Fill->Right > 0.0 && Key->Up > 0.3, "the key high on one side, the fill on the other");
	for (const R::FSoftBox& L : R::Lights)
		if (&L != Key) Expect(R::Lux(L) < R::Lux(*Key), std::string(L.Name) + " is under the key");
	Expect(BehindLeft >= 1 && BehindRight >= 1, "edge lights behind on both sides (a dark part's outline)");
	Expect(Top >= 1, "a light overhead (the long highlight along a tank or casing)");
	Expect(Shadows == 1 && Key->bShadows, "one shadow, the key's");
	Expect(Neutral, "every light near neutral (no colour cast on the paint)");
	Expect(OffAxis, "no light within 20 degrees of the camera's axis");
	Expect(Key->Width * Key->Height >= 0.25, Fmt("the key is a big soft box (%.2f x %.2f of its distance)", Key->Width, Key->Height));
	Expect(R::Channel == 2, "the studio's lighting channel is 2");
	Expect(std::abs(R::Gain(R::DistanceCm(50.0)) * R::RefDistanceCm * R::RefDistanceCm / (R::DistanceCm(50.0) * R::DistanceCm(50.0)) - 1.0) < 1e-9,
		"a light's brightness follows its distance squared (any size of subject lit alike)");
}

void CheckExposure()
{
	std::printf("The exposure (values before the tone curve; middle grey is 0.18)\n");
	// A card facing the camera, and one turned 45 degrees to the key.
	double Front = 0.0, Turned = 0.0;
	for (const R::FSoftBox& L : R::Lights)
	{
		Front += LuxOn(L, 1, 0, 0);
		Turned += LuxOn(L, 0.707, -0.707, 0);
	}
	const double Bias = R::ShopExposureBias;
	const double Grey = Value(0.18 * Front / 3.14159265, Bias);
	const double White = Value(0.80 * std::max(Front, Turned) / 3.14159265, Bias);
	const double Black = Value(0.04 * Front / 3.14159265, Bias);
	const double SweepTop = Value(R::SrgbToLinear(R::SweepTop) * R::ShopBackdropBrightness, Bias);
	const double SweepFloor = Value(R::SrgbToLinear(R::SweepFloor) * R::ShopBackdropBrightness, Bias);
	Expect(Grey >= 0.12 && Grey <= 0.30, Fmt("a mid-grey part %.3f (%.0f lux on it)", Grey, Front));
	Expect(White >= 0.5 && White <= 1.6, Fmt("a white part %.3f (bright, short of burning out)", White));
	Expect(Black >= 0.02, Fmt("a black part %.3f (its body still holds detail; the soft boxes draw its shape)", Black));
	Expect(SweepTop >= 0.15 && SweepTop <= 0.5, Fmt("the backdrop's top %.3f, its floor %.3f (a light grey sweep)", SweepTop, SweepFloor));
	Expect(SweepTop > Black * 3.0, Fmt("a black part stands out from the backdrop (%.1fx darker)", SweepTop / Black));
	Expect(SweepTop < White, "a white part is brighter than the backdrop");
	Expect(Display(SweepTop * 0.85) > R::ShopBackdropTarget - 0.15 && Display(SweepTop * 0.85) < R::ShopBackdropTarget + 0.15,
		Fmt("the meter's target %.2f is about where the backdrop's corners land on their own (%.2f): little trim when all is well", R::ShopBackdropTarget, Display(SweepTop * 0.85)));
}

void CheckMeter()
{
	std::printf("The meter\n");
	const double Base = Value(R::SrgbToLinear(R::SweepTop) * R::ShopBackdropBrightness * 0.85, R::ShopExposureBias);
	for (double Off = -5.0; Off <= 3.01; Off += 1.0)
	{
		double Trim = 0.0, Read = 0.0;
		int Reads = 0;
		bool Done = false;
		while (Reads < 8)
		{
			Read = Display(Base * std::pow(2.0, Off + Trim));
			++Reads;
			if (std::abs(Read - R::ShopBackdropTarget) <= R::MeterTolerance)
			{
				Done = true;
				break;
			}
			Trim = std::clamp(Trim + R::MeterStep(Read, R::ShopBackdropTarget), R::TrimMin, R::TrimMax);
		}
		Expect(Done, Fmt("a renderer %+.0f stops off: held at %.3f after %.0f reads", Off, Read, Reads));
	}
}

void CheckSource()
{
	std::printf("The source\n");
	const std::filesystem::path Root(TORQUE_SOURCE);
	// Captures that are meant to see the game as it is.
	const std::set<std::string> SeeTheGame = {
		"Shed/TorqueShedScreens.cpp",   // the TVs' rider cam: the ride as it looks
		"World/TorqueImpostors.cpp",    // bakes the trees' far pictures under the level's own light
	};
	int Studios = 0;
	for (const auto& E : std::filesystem::recursive_directory_iterator(Root))
	{
		if (!E.is_regular_file() || E.path().extension() != ".cpp") continue;
		const std::string Rel = std::filesystem::relative(E.path(), Root).generic_string();
		const std::string Text = ReadText(E.path());
		if (!Has(Text, "<USceneCaptureComponent2D>(")) continue;
		if (SeeTheGame.count(Rel))
		{
			std::printf("  (%s films the game: left as it is)\n", Rel.c_str());
			continue;
		}
		++Studios;
		Expect(Has(Text, "TorqueStudio::Isolate("), Rel + ": its capture is isolated (TorqueStudio::Isolate)");
		Expect(Has(Text, "TorqueStudio::MakeLights(") && Has(Text, "TorqueStudio::PlaceLights("), Rel + ": lit by the studio's soft boxes");
		Expect(Has(Text, "Meter.Tick("), Rel + ": its exposure held by the meter");
		Expect(Has(Text, "TorqueStudio::MakeTarget("), Rel + ": its picture made the studio's way (own gamma)");
		Expect(!Has(Text, "UPointLightComponent") && !Has(Text, "USpotLightComponent") && !Has(Text, "UDirectionalLightComponent"),
			Rel + ": no lights of its own besides the studio's");
		Expect(!Has(Text, "PostProcessSettings") && !Has(Text, "AutoExposure"), Rel + ": no exposure or post-processing of its own");
	}
	Expect(Studios >= 2, Fmt("the studios found: %.0f (the parts preview and the profile's)", Studios));

	const std::string S = ReadText(Root / "Shed/TorqueStudio.cpp");
	for (const char* Must : {"SetSkyLighting(false)", "SetFog(false)", "SetVolumetricFog(false)", "SetAtmosphere(false)", "SetCloud(false)",
			 "SetPostProcessMaterial(false)", "\"bOverride_\"", "SetPropertyValue_InContainer(&PP, true)", "AEM_Manual",
			 "AutoExposureApplyPhysicalCameraExposure = 0", "LocalExposureHighlightContrastScale = 1.0f", "LocalExposureShadowContrastScale = 1.0f",
			 "WeightedBlendables.Array.Reset()", "PostProcessBlendWeight = 1.0f", "TargetGamma = 2.2f", "PRM_UseShowOnlyList",
			 "bChannel0 = false", "bChannel1 = false", "bChannel2 = true", "SetLightingChannels(false, false, true)"})
		Expect(Has(S, Must), std::string("Isolate/the rig still has ") + Must);
	// Everything shown goes on the studio's channel, parts hung on later included.
	const std::string Shop = ReadText(Root / "Shed/TorqueShopPreview.cpp");
	Expect(Has(Shop, "TorqueStudio::Enrol(Holder)"), "the parts preview puts everything it shows on the studio's channel");
}
} // namespace

int main()
{
	CheckRig();
	CheckExposure();
	CheckMeter();
	CheckSource();
	std::printf("\nstudio_test: %d of %d checks passed%s\n", Checks - Failures, Checks, Failures ? "" : " (all)");
	return Failures ? 1 : 0;
}
