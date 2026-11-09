#include "SfxMix.h"

#include <algorithm>
#include <cstring>

namespace TorqueSfxMix
{
namespace
{
constexpr double Pi = 3.14159265358979323846;
uint32_t U32(const uint8_t* P) { return P[0] | (P[1] << 8) | (P[2] << 16) | ((uint32_t)P[3] << 24); }
uint16_t U16(const uint8_t* P) { return (uint16_t)(P[0] | (P[1] << 8)); }
double Clamp(double V, double Lo, double Hi) { return V < Lo ? Lo : (V > Hi ? Hi : V); }

// Freeverb's tunings at 44.1 kHz (Godot's reverb uses the same), the right side offset by the spread.
const int CombTune[8] = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
const int PassTune[4] = {556, 441, 341, 225};
}

std::shared_ptr<FClip> ReadClip(const std::vector<uint8_t>& B)
{
	auto Out = std::make_shared<FClip>();
	if (B.size() < 44 || std::memcmp(B.data(), "RIFF", 4) != 0 || std::memcmp(B.data() + 8, "WAVE", 4) != 0) return Out;
	int Channels = 0, Bits = 0;
	size_t P = 12;
	while (P + 8 <= B.size())
	{
		const uint32_t Len = U32(&B[P + 4]);
		const uint8_t* Body = &B[P + 8];
		if (std::memcmp(&B[P], "fmt ", 4) == 0 && Len >= 16)
		{
			Channels = U16(Body + 2);
			Out->SampleRate = (int)U32(Body + 4);
			Bits = U16(Body + 14);
		}
		else if (std::memcmp(&B[P], "data", 4) == 0 && Bits == 16 && Channels > 0)
		{
			const size_t Frames = std::min<size_t>(Len, B.size() - P - 8) / (2 * Channels);
			Out->S.resize(Frames);
			for (size_t I = 0; I < Frames; ++I) Out->S[I] = (int16_t)U16(Body + I * 2 * Channels);   // the first channel
			return Out;
		}
		P += 8 + Len + (Len & 1);
	}
	return Out;
}

// --- Reverb -----------------------------------------------------------------------------------------

FReverb::FReverb() { Size(0); }

void FReverb::Size(int Spread)
{
	SpreadShown = Spread;
	const double K = (double)Rate / 44100.0;
	for (int C = 0; C < 2; ++C)
	{
		const int Extra = C == 1 ? Spread : 0;
		for (int I = 0; I < 8; ++I)
		{
			Combs[C][I].Buf.assign((size_t)((CombTune[I] + Extra) * K), 0.0f);
			Combs[C][I].I = 0;
			Combs[C][I].Store = 0.0f;
		}
		for (int I = 0; I < 4; ++I)
		{
			Passes[C][I].Buf.assign((size_t)((PassTune[I] + Extra) * K), 0.0f);
			Passes[C][I].I = 0;
		}
	}
	PreBuf.assign((size_t)(Rate * 0.25) * 2, 0.0f);
	PreI = 0;
}

void FReverb::Set(const FRoomPreset& P, double Dry)
{
	Shown = P;
	bSet = true;
	Feedback = (float)(Clamp(P.Size, 0.0, 1.0) * 0.28 + 0.7);
	Damp1 = (float)(Clamp(P.Damp, 0.0, 1.0) * 0.4);
	Damp2 = 1.0f - Damp1;
	Wet = (float)(Clamp(P.Wet, 0.0, 1.0) * 3.0);
	DryGain = (float)Dry;
	PreLen = (size_t)std::max(1.0, Clamp(P.Pre, 0.0, 240.0) / 1000.0 * Rate);
	// Godot's hipass, 0..1: a one-pole high-pass on what goes into the tail (0.1 is about 100 Hz).
	HpA = (float)std::exp(-2.0 * Pi * Clamp(P.Hp, 0.0, 1.0) * 1000.0 / Rate);
	const int Spread = (int)std::lround(Clamp(P.Spread, 0.0, 1.0) * 23.0);
	if (Spread != SpreadShown) Size(Spread);
}

void FReverb::Process(float* LR, int Frames)
{
	if (Wet <= 0.0f && DryGain == 1.0f) return;
	for (int F = 0; F < Frames; ++F)
	{
		// The pre-delay (with Godot's 0.4 feedback) on the mono sum.
		const float In = (LR[F * 2] + LR[F * 2 + 1]) * 0.5f;
		const size_t Read = (PreI + PreBuf.size() / 2 - PreLen) % (PreBuf.size() / 2);
		float Pre = PreBuf[Read * 2];
		PreBuf[PreI * 2] = In + Pre * 0.4f;
		PreI = (PreI + 1) % (PreBuf.size() / 2);
		// High-pass it (the tail's low end), then into the combs.
		HpZ = HpA * (HpZ + Pre - HpX);
		HpX = Pre;
		const float X = HpZ * 0.015f;
		for (int C = 0; C < 2; ++C)
		{
			float Out = 0.0f;
			for (FComb& Cb : Combs[C])
			{
				float& Cell = Cb.Buf[Cb.I];
				const float Y = Cell;
				Cb.Store = Y * Damp2 + Cb.Store * Damp1;
				Cell = X + Cb.Store * Feedback;
				if (++Cb.I >= Cb.Buf.size()) Cb.I = 0;
				Out += Y;
			}
			for (FAllPass& Ap : Passes[C])
			{
				float& Cell = Ap.Buf[Ap.I];
				const float B = Cell;
				Cell = Out + B * 0.5f;
				Out = B - Out;
				if (++Ap.I >= Ap.Buf.size()) Ap.I = 0;
			}
			LR[F * 2 + C] = LR[F * 2 + C] * DryGain + Out * Wet;
		}
	}
}

// --- Low-pass ---------------------------------------------------------------------------------------

void FLowPass::Set(double Hz0, double Q)
{
	Hz = Clamp(Hz0, 20.0, Rate * 0.45);
	Qv = Q;
	const double W = 2.0 * Pi * Hz / Rate, Alpha = std::sin(W) / (2.0 * Qv), A0 = 1.0 + Alpha, C = std::cos(W);
	B0 = (1.0 - C) / 2.0 / A0; B1 = (1.0 - C) / A0; B2 = B0;
	A1 = -2.0 * C / A0; A2 = (1.0 - Alpha) / A0;
}

void FLowPass::Process(float* LR, int Frames)
{
	for (int F = 0; F < Frames; ++F)
		for (int C = 0; C < 2; ++C)
		{
			const double X = LR[F * 2 + C];
			const double Y = B0 * X + Z[C][0];
			Z[C][0] = B1 * X - A1 * Y + Z[C][1];
			Z[C][1] = B2 * X - A2 * Y;
			LR[F * 2 + C] = (float)Y;
		}
}

// --- The mixer --------------------------------------------------------------------------------------

FMixer::FMixer()
{
	RoomVerb.Set(FRoomPreset(), 1.0);
	// sfx.gd _make_room_buses: the Far bus's reverb and low-pass.
	FRoomPreset FarP;
	FarP.Size = 0.5; FarP.Damp = 0.5; FarP.Wet = 0.3; FarP.Pre = 20.0; FarP.Hp = 0.0; FarP.Spread = 1.0;
	FarVerb.Set(FarP, 0.85);
	FarLp.Set(1100.0);
	AmbLp.Set(20000.0);
	MuffleLp.Set(520.0, 0.4 / 0.5 * 0.7071);
	Slap.assign((size_t)(Rate * 0.3) * 2, 0.0f);
}

FMixer::FVoice* FMixer::Find(int Id)
{
	for (FVoice& V : Live)
		if (V.Id == Id) return &V;
	return nullptr;
}

const FMixer::FVoice* FMixer::Find(int Id) const
{
	for (const FVoice& V : Live)
		if (V.Id == Id) return &V;
	return nullptr;
}

int FMixer::Play(const FPlay& P)
{
	if (!P.Gen && (!P.Clip || P.Clip->S.empty())) return 0;
	std::lock_guard<std::mutex> G(Lock);
	FVoice V;
	V.Id = NextId++;
	V.P = P;
	if (P.Clip) V.Pos = Clamp(P.StartAt, 0.0, P.Clip->Seconds()) * P.Clip->SampleRate;
	V.Fade = P.FadeIn > 0.0 ? 0.0 : 1.0;
	V.FadeStep = P.FadeIn > 0.0 ? 1.0 / (P.FadeIn * Rate) : 0.0;
	double Gain, L, R, Air;
	Target(V, Gain, L, R, Air);
	V.Gain = Gain;   // it starts at its level (the clip's own start is soft)
	V.Left = L;
	V.Right = R;
	Live.push_back(V);
	return V.Id;
}

void FMixer::Stop(int Id, double Release)
{
	std::lock_guard<std::mutex> G(Lock);
	if (FVoice* V = Find(Id))
	{
		V->bStopping = true;
		V->Release = std::max(Release, 0.002);
	}
}

bool FMixer::Alive(int Id) const
{
	std::lock_guard<std::mutex> G(Lock);
	const FVoice* V = Find(Id);
	return V && !V->bDone && !V->bStopping;
}

double FMixer::ClipPos(int Id) const
{
	std::lock_guard<std::mutex> G(Lock);
	const FVoice* V = Find(Id);
	return V && V->P.Clip ? V->Pos / V->P.Clip->SampleRate : -1.0;
}

void FMixer::SetDb(int Id, double Db)
{
	std::lock_guard<std::mutex> G(Lock);
	if (FVoice* V = Find(Id)) V->P.Db = Db;
}

void FMixer::SetPitch(int Id, double Pitch)
{
	std::lock_guard<std::mutex> G(Lock);
	if (FVoice* V = Find(Id)) V->P.Pitch = Clamp(Pitch, 0.05, 8.0);
}

void FMixer::SetAt(int Id, const FVec& At)
{
	std::lock_guard<std::mutex> G(Lock);
	if (FVoice* V = Find(Id)) V->P.At = At;
}

void FMixer::SetBus(int Id, EBus Bus)
{
	std::lock_guard<std::mutex> G(Lock);
	if (FVoice* V = Find(Id)) V->P.Bus = Bus;
}

void FMixer::SetListener(const FVec& At, const FVec& Right)
{
	std::lock_guard<std::mutex> G(Lock);
	Ear = At;
	EarRight = Right;
}

void FMixer::SetRoom(const FRoomPreset& P)
{
	std::lock_guard<std::mutex> G(Lock);
	RoomVerb.Set(P, 1.0);
}

void FMixer::SetSlap(bool bOn, double DelayMs, double LevelDb)
{
	std::lock_guard<std::mutex> G(Lock);
	bSlap = bOn;
	SlapMs = Clamp(DelayMs, 1.0, 280.0);
	SlapDb = LevelDb;
}

void FMixer::SetMuffled(bool bOn)
{
	std::lock_guard<std::mutex> G(Lock);
	bMuffled = bOn;
}

void FMixer::SetAmbOut(double Cutoff, double Db)
{
	std::lock_guard<std::mutex> G(Lock);
	AmbCut = Cutoff;
	AmbDb = Db;
}

void FMixer::SetVolume(double Linear)
{
	std::lock_guard<std::mutex> G(Lock);
	Volume = Clamp(Linear, 0.0, 2.0);
}

int FMixer::Voices() const
{
	std::lock_guard<std::mutex> G(Lock);
	int N = 0;
	for (const FVoice& V : Live) N += V.bDone ? 0 : 1;
	return N;
}

void FMixer::Target(const FVoice& V, double& OutGain, double& OutL, double& OutR, double& OutAir) const
{
	OutL = OutR = 1.0;
	OutAir = 1.0;
	if (!V.P.b3D)
	{
		OutGain = DbToLin(V.P.Db);
		return;
	}
	const double Dx = V.P.At.X - Ear.X, Dy = V.P.At.Y - Ear.Y, Dz = V.P.At.Z - Ear.Z;
	const double D = std::sqrt(Dx * Dx + Dy * Dy + Dz * Dz);
	if (V.P.MaxDistance > 0.0 && D > V.P.MaxDistance)
	{
		OutGain = 0.0;
		return;
	}
	// Godot's ATTENUATION_INVERSE_DISTANCE: unit_size / distance, never more than max_db.
	const double Db = std::min(LinToDb(V.P.Unit / std::max(D, 1.0)), V.P.MaxDb);
	OutGain = DbToLin(V.P.Db + Db);
	// Panning at Godot's default strength (0.5): the far ear loses up to half.
	if (D > 1.0)
	{
		const double Side = Clamp((Dx * EarRight.X + Dy * EarRight.Y + Dz * EarRight.Z) / D, -1.0, 1.0) * 0.5;
		OutL = 1.0 - std::max(Side, 0.0);
		OutR = 1.0 + std::min(Side, 0.0);
	}
	// The air's loss (attenuation_filter: -24 dB above 5 kHz, more of it the further off).
	OutAir = DbToLin(-24.0 * Clamp(1.0 - V.P.Unit / std::max(D, 1.0), 0.0, 1.0));
}

void FMixer::Render(float* Out, int Frames)
{
	std::lock_guard<std::mutex> G(Lock);
	for (std::vector<float>& B : BusBuf) B.assign((size_t)Frames * 2, 0.0f);
	Mono.resize((size_t)Frames);
	const double AirA = std::exp(-2.0 * Pi * 5000.0 / Rate);
	for (FVoice& V : Live)
	{
		if (V.bDone) continue;
		double Gain, L, R, Air;
		Target(V, Gain, L, R, Air);
		const double G0 = V.Gain, L0 = V.Left, R0 = V.Right;
		// The source: a generator's block, or the clip resampled (linear) at its pitch.
		bool bEnd = false;
		int N = Frames;
		if (V.P.Gen)
		{
			if (!V.P.Gen->Render(Mono.data(), Frames)) bEnd = true;
		}
		else
		{
			const FClip& C = *V.P.Clip;
			const double Step = (double)C.SampleRate / Rate * V.P.Pitch;
			const double Len = (double)C.S.size();
			const double From = Clamp(V.P.LoopFrom * C.SampleRate, 0.0, std::max(Len - 2.0, 0.0));
			for (int I = 0; I < Frames; ++I)
			{
				if (V.Pos >= Len - 1.0)
				{
					if (V.P.bLoop && Len > 2.0) V.Pos = From + std::fmod(V.Pos - (Len - 1.0), std::max(Len - 1.0 - From, 1.0));
					else
					{
						for (int J = I; J < Frames; ++J) Mono[(size_t)J] = 0.0f;
						N = I;
						bEnd = true;
						break;
					}
				}
				const size_t K = (size_t)V.Pos;
				const double T = V.Pos - (double)K;
				Mono[(size_t)I] = (float)((C.S[K] * (1.0 - T) + C.S[K + 1] * T) / 32768.0);
				V.Pos += Step;
			}
		}
		float* Bus = BusBuf[(int)V.P.Bus].data();
		const double Inv = 1.0 / std::max(Frames, 1);
		for (int I = 0; I < N; ++I)
		{
			const double K = (I + 1) * Inv;
			const double Gi = G0 + (Gain - G0) * K, Li = L0 + (L - L0) * K, Ri = R0 + (R - R0) * K;
			double X = Mono[(size_t)I];
			// The air: a high shelf at 5 kHz.
			V.Air = V.Air * AirA + X * (1.0 - AirA);
			X = V.Air + (X - V.Air) * Air;
			if (V.bStopping) V.Fade -= 1.0 / (V.Release * Rate);
			else if (V.Fade < 1.0) V.Fade = std::min(1.0, V.Fade + V.FadeStep);
			if (V.Fade <= 0.0)
			{
				V.Fade = 0.0;
				bEnd = true;
				break;
			}
			const double Y = X * Gi * V.Fade;
			Bus[I * 2] += (float)(Y * Li);
			Bus[I * 2 + 1] += (float)(Y * Ri);
		}
		V.Gain = Gain;
		V.Left = L;
		V.Right = R;
		V.Age += (double)Frames / Rate;
		if (V.P.Life > 0.0 && V.Age >= V.P.Life && !V.bStopping)
		{
			V.bStopping = true;
			V.Release = 0.03;
		}
		if (bEnd) V.bDone = true;
	}
	Live.erase(std::remove_if(Live.begin(), Live.end(), [](const FVoice& V) { return V.bDone; }), Live.end());

	// The buses, as Torque Shed's.
	float* Room = BusBuf[(int)EBus::Room].data();
	RoomVerb.Process(Room, Frames);
	{
		const size_t Cap = Slap.size() / 2;
		const size_t Delay = std::min(Cap - 1, (size_t)(SlapMs / 1000.0 * Rate));
		const float Level = (float)DbToLin(SlapDb);
		for (int I = 0; I < Frames; ++I)
		{
			const size_t Rd = (SlapI + Cap - Delay) % Cap;
			const float Dl = Slap[Rd * 2], Dr = Slap[Rd * 2 + 1];
			Slap[SlapI * 2] = Room[I * 2];
			Slap[SlapI * 2 + 1] = Room[I * 2 + 1];
			SlapI = (SlapI + 1) % Cap;
			if (bSlap)
			{
				Room[I * 2] += Dl * Level;
				Room[I * 2 + 1] += Dr * Level;
			}
		}
	}
	float* Far = BusBuf[(int)EBus::Far].data();
	FarLp.Process(Far, Frames);
	FarVerb.Process(Far, Frames);
	float* Amb = BusBuf[(int)EBus::AmbOut].data();
	if (std::abs(AmbLp.Cutoff() - AmbCut) > 1.0) AmbLp.Set(AmbCut);
	AmbLp.Process(Amb, Frames);
	const double AmbTo = DbToLin(AmbDb);
	float* Sfx = BusBuf[(int)EBus::Sfx].data();
	for (int I = 0; I < Frames * 2; ++I)
	{
		AmbGain += (AmbTo - AmbGain) * 0.0005;
		Sfx[I] += Room[I] + Far[I] + Amb[I] * (float)AmbGain;
	}
	if (bMuffled) MuffleLp.Process(Sfx, Frames);
	const double MuffleTo = bMuffled ? DbToLin(-12.0) : 1.0;
	for (int I = 0; I < Frames; ++I)
	{
		MuffleGain += (MuffleTo - MuffleGain) * 0.001;
		for (int C = 0; C < 2; ++C)
		{
			const float Y = (float)(Sfx[I * 2 + C] * MuffleGain * Volume);
			Out[I * 2 + C] = Y;
			Peak = std::max(Peak, std::abs(Y));
		}
	}
}
} // namespace TorqueSfxMix
