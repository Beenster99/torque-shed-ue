// The preview studio's rig, as numbers only (no engine): shared by every 3D preview window (the parts
// computer's and the new-bike configurator's ATorqueShopPreview, the profile page's ATorqueProfilePreview)
// and by the cloud check (SimTest/studio_test.cpp), which checks the lighting ratios and the exposure
// before a build.
//
// beenster (Nov 25, build 171): "imagine that these objects were photographed or videoed in a studio for
// preparing the best images of them - thats how the preview window should light them - Same for
// ANYTHING in the preview window. But it should not be affected by exposure/daylight in main game".
// So: a product shoot. A big soft key high on the left, a broad fill on the right, an overhead scrim
// (the long highlight along a tank or a casing), two tall strip lights behind for the edges (what makes a
// black part read against the backdrop), a low bounce card. All soft boxes (rect lights), so paint and
// metal show broad gradients rather than pin-point sparkles. The camera's exposure is fixed (manual),
// and every preview meters its own backdrop to hold it there (TorqueStudio::FMeter).
#pragma once

#include <cmath>

namespace TorqueStudioRig
{
struct FSoftBox
{
	const char* Name;
	// Where it stands, as a direction from the subject in the camera's frame: Toward the camera (+) or
	// behind the subject (-), to the picture's Right (+) or left (-), Up (+) or down (-).
	double Toward, Right, Up;
	double Candelas;      // at the reference distance (RefDistanceCm); scaled by distance squared
	double Width, Height; // the box's size as a fraction of its distance from the subject
	float R, G, B;        // its colour (near neutral: a studio's daylight-balanced heads)
	bool bShadows;
};

// Only the key casts shadows (one clean shadow, as a single big soft box gives).
inline constexpr FSoftBox Lights[] = {
	{"Key",    0.75, -0.80,  0.85, 130.0, 0.60, 0.80, 1.00f, 0.97f, 0.93f, true},
	{"Fill",   0.95,  0.85,  0.05,  50.0, 0.70, 0.70, 0.95f, 0.97f, 1.00f, false},
	{"Top",    0.10,  0.00,  1.00,  60.0, 1.00, 0.50, 1.00f, 1.00f, 1.00f, false},
	{"StripL", -0.85, -0.75, 0.35,  90.0, 0.15, 0.90, 1.00f, 1.00f, 1.00f, false},
	{"StripR", -0.85,  0.75, 0.25,  80.0, 0.15, 0.90, 1.00f, 1.00f, 1.00f, false},
	{"Bounce", 0.60,  0.00, -0.80,  22.0, 0.90, 0.90, 1.00f, 0.99f, 0.97f, false},
};
inline constexpr int NumLights = sizeof(Lights) / sizeof(Lights[0]);

// The lights stand this far from the subject's centre (cm): further for a bigger subject, and brighter
// with it (by the square), so a spark plug and a whole bike are lit alike.
inline double DistanceCm(double RadiusCm) { return RadiusCm * 2.4 + 60.0; }
inline constexpr double RefDistanceCm = 250.0;
inline double Gain(double DistCm) { return (DistCm / RefDistanceCm) * (DistCm / RefDistanceCm); }

// The lighting channel the studio and everything in it are on (nothing in the levels is).
inline constexpr int Channel = 2;

// The parts studio's fixed camera: manual exposure, EV100 0 and this compensation (a pixel's value before
// the tone curve is the surface's nits x 2^Bias / 1.2). FMeter adds a trim on top that holds the backdrop
// at its grey whatever the rest of the pipeline does (it should stay near 0: the check logs it).
inline constexpr double ShopExposureBias = -2.6;
inline constexpr double ProfileExposureBias = -2.3;   // the avatar's card is lit, not glowing: a little more open

// The parts studio's backdrop (an unlit sweep, M_ShedScreenPage): its greys as the texture holds them
// (sRGB), and Brightness, the nits of a white page.
inline constexpr float SweepTop = 0.62f, SweepFloor = 0.34f, SweepGlow = 0.74f, SweepCorner = 0.30f;
inline constexpr double ShopBackdropBrightness = 8.5;

// What the meter holds the backdrop's top corners at, as the picture's grey (0-1, as displayed).
inline constexpr double ShopBackdropTarget = 0.50;
inline constexpr double ProfileBackdropTarget = 0.42;
inline constexpr double MeterTolerance = 0.025;
inline constexpr double TrimMin = -4.0, TrimMax = 8.0;   // stops the meter may move the exposure

// Helpers for the meter and the check.
inline double SrgbToLinear(double V) { return V <= 0.04045 ? V / 12.92 : std::pow((V + 0.055) / 1.055, 2.4); }
inline double Lux(const FSoftBox& L) { return L.Candelas / ((RefDistanceCm / 100.0) * (RefDistanceCm / 100.0)); }

// One step of the meter: from the backdrop's grey as read (0-1) to the change of exposure (stops) that
// brings it to Target. Damped, and at most 3 stops a step, so the tone curve's bend can't make it swing.
inline double MeterStep(double Read, double Target)
{
	const double R = SrgbToLinear(Read < 0.01 ? 0.01 : (Read > 0.99 ? 0.99 : Read));
	const double T = SrgbToLinear(Target);
	double Step = std::log2(T / R) * 0.8;
	if (Step > 3.0) Step = 3.0;
	if (Step < -3.0) Step = -3.0;
	return Step;
}
} // namespace TorqueStudioRig
