// What a bike's TFT screen, the phone sat nav and the HUD's map draw with: a list of filled triangles
// and lines of text, in the screen's own pixels (its top left the origin, y down). Plain C++, so the
// screens are laid out and tested without the engine (SimTest/tft_test writes them out as pictures);
// the game paints the list onto a canvas (TorqueTft.cpp).
//
// Colours are as the eye sees them on a screen (sRGB, 0..1); the game turns them into the engine's.
#pragma once

#include <cmath>
#include <string>
#include <vector>

namespace TorqueTft
{
struct P2
{
	float X = 0.0f, Y = 0.0f;
	P2() = default;
	P2(float InX, float InY) : X(InX), Y(InY) {}
	P2 operator+(const P2& O) const { return P2(X + O.X, Y + O.Y); }
	P2 operator-(const P2& O) const { return P2(X - O.X, Y - O.Y); }
	P2 operator*(float S) const { return P2(X * S, Y * S); }
};

struct Rgba
{
	float R = 1.0f, G = 1.0f, B = 1.0f, A = 1.0f;
	Rgba() = default;
	Rgba(float InR, float InG, float InB, float InA = 1.0f) : R(InR), G(InG), B(InB), A(InA) {}
	Rgba Fade(float Alpha) const { return Rgba(R, G, B, A * Alpha); }
	Rgba Mix(const Rgba& O, float T) const { return Rgba(R + (O.R - R) * T, G + (O.G - G) * T, B + (O.B - B) * T, A + (O.A - A) * T); }
};

enum class Align { Left, Centre, Right };

struct Cmd
{
	bool IsText = false;
	Rgba Colour;
	std::vector<P2> Tris;   // three points a triangle
	std::string Words;      // (text) UTF-8
	P2 At;                  // (text) the baseline's left, middle or right end
	float Px = 16.0f;       // (text) the letters' size
	Align Side = Align::Left;
	bool Bold = false;
};

class Paint
{
public:
	float W = 800.0f, H = 480.0f;
	std::vector<Cmd> Cmds;

	Paint() = default;
	Paint(float InW, float InH) : W(InW), H(InH) {}

	// Only what falls inside this box is kept from here on (triangles are cut to it, a line of text is
	// dropped if its anchor is outside); NoClip ends it.
	void Clip(float X, float Y, float BoxW, float BoxH)
	{
		bClip = true;
		CX0 = X + OX; CY0 = Y + OY; CX1 = X + OX + BoxW; CY1 = Y + OY + BoxH;
	}
	void NoClip() { bClip = false; }
	// Everything drawn from here on is moved by this much (a panel laid out from its own corner).
	void Origin(float X, float Y) { OX = X; OY = Y; }
	float OriginX() const { return OX; }
	float OriginY() const { return OY; }

	void Tri(const P2& A0, const P2& B0, const P2& C0, const Rgba& Colour)
	{
		if (Colour.A <= 0.0f) return;
		const P2 A(A0.X + OX, A0.Y + OY), B(B0.X + OX, B0.Y + OY), C(C0.X + OX, C0.Y + OY);
		if (!bClip)
		{
			Put(A, B, C, Colour);
			return;
		}
		// Cut to the box, an edge at a time (Sutherland and Hodgman's way), then fanned back into triangles.
		P2 In[10], Out[10];
		int N = 3;
		In[0] = A; In[1] = B; In[2] = C;
		for (int Edge = 0; Edge < 4 && N >= 3; ++Edge)
		{
			int M = 0;
			for (int I = 0; I < N; ++I)
			{
				const P2& P = In[I];
				const P2& Q = In[(I + 1) % N];
				const float Dp = Inside(P, Edge), Dq = Inside(Q, Edge);
				if (Dp >= 0.0f) Out[M++] = P;
				if ((Dp >= 0.0f) != (Dq >= 0.0f))
				{
					const float T = Dp / (Dp - Dq);
					Out[M++] = P2(P.X + (Q.X - P.X) * T, P.Y + (Q.Y - P.Y) * T);
				}
			}
			N = M;
			for (int I = 0; I < N; ++I) In[I] = Out[I];
		}
		for (int I = 1; I + 1 < N; ++I) Put(In[0], In[I], In[I + 1], Colour);
	}
	void Quad(const P2& A, const P2& B, const P2& C, const P2& D, const Rgba& Colour)
	{
		Tri(A, B, C, Colour);
		Tri(A, C, D, Colour);
	}
	void Rect(float X, float Y, float BoxW, float BoxH, const Rgba& Colour)
	{
		if (BoxW <= 0.0f || BoxH <= 0.0f) return;
		Quad(P2(X, Y), P2(X + BoxW, Y), P2(X + BoxW, Y + BoxH), P2(X, Y + BoxH), Colour);
	}
	// A box with its corners rounded by R.
	void RoundRect(float X, float Y, float BoxW, float BoxH, float R, const Rgba& Colour)
	{
		R = std::fmin(R, std::fmin(BoxW, BoxH) * 0.5f);
		if (R <= 0.5f)
		{
			Rect(X, Y, BoxW, BoxH, Colour);
			return;
		}
		Rect(X + R, Y, BoxW - 2.0f * R, BoxH, Colour);
		Rect(X, Y + R, R, BoxH - 2.0f * R, Colour);
		Rect(X + BoxW - R, Y + R, R, BoxH - 2.0f * R, Colour);
		Pie(X + R, Y + R, R, 180.0f, 270.0f, Colour);
		Pie(X + BoxW - R, Y + R, R, 270.0f, 360.0f, Colour);
		Pie(X + BoxW - R, Y + BoxH - R, R, 0.0f, 90.0f, Colour);
		Pie(X + R, Y + BoxH - R, R, 90.0f, 180.0f, Colour);
	}
	// A box's outline, T thick, inside it.
	void Frame(float X, float Y, float BoxW, float BoxH, float T, const Rgba& Colour)
	{
		Rect(X, Y, BoxW, T, Colour);
		Rect(X, Y + BoxH - T, BoxW, T, Colour);
		Rect(X, Y + T, T, BoxH - 2.0f * T, Colour);
		Rect(X + BoxW - T, Y + T, T, BoxH - 2.0f * T, Colour);
	}
	// A shape whose every point the first can see (a convex one, or an arrow from its tip).
	void Fan(const std::vector<P2>& Points, const Rgba& Colour)
	{
		for (size_t I = 1; I + 1 < Points.size(); ++I) Tri(Points[0], Points[I], Points[I + 1], Colour);
	}
	// Angles are in degrees, clockwise on the screen from the +x axis (0 right, 90 down).
	static P2 On(float X, float Y, float R, float Deg)
	{
		const float A = Deg * 0.01745329252f;
		return P2(X + R * std::cos(A), Y + R * std::sin(A));
	}
	void Pie(float X, float Y, float R, float Deg0, float Deg1, const Rgba& Colour)
	{
		const int N = Steps(R, Deg1 - Deg0);
		for (int I = 0; I < N; ++I)
			Tri(P2(X, Y), On(X, Y, R, Deg0 + (Deg1 - Deg0) * float(I) / float(N)), On(X, Y, R, Deg0 + (Deg1 - Deg0) * float(I + 1) / float(N)), Colour);
	}
	void Disc(float X, float Y, float R, const Rgba& Colour) { Pie(X, Y, R, 0.0f, 360.0f, Colour); }
	// A band between two radii from one angle round to another.
	void Arc(float X, float Y, float R0, float R1, float Deg0, float Deg1, const Rgba& Colour)
	{
		const int N = Steps(R1, Deg1 - Deg0);
		for (int I = 0; I < N; ++I)
		{
			const float A = Deg0 + (Deg1 - Deg0) * float(I) / float(N), B = Deg0 + (Deg1 - Deg0) * float(I + 1) / float(N);
			Quad(On(X, Y, R0, A), On(X, Y, R1, A), On(X, Y, R1, B), On(X, Y, R0, B), Colour);
		}
	}
	void Ring(float X, float Y, float R0, float R1, const Rgba& Colour) { Arc(X, Y, R0, R1, 0.0f, 360.0f, Colour); }
	// A straight stroke T thick; Round puts a half disc on each end.
	void Line(const P2& A, const P2& B, float T, const Rgba& Colour, bool Round = false)
	{
		const float DX = B.X - A.X, DY = B.Y - A.Y;
		const float L = std::sqrt(DX * DX + DY * DY);
		if (L < 1.0e-4f)
		{
			if (Round) Disc(A.X, A.Y, T * 0.5f, Colour);
			return;
		}
		const P2 N(-DY / L * T * 0.5f, DX / L * T * 0.5f);
		Quad(A + N, B + N, B - N, A - N, Colour);
		if (Round)
		{
			Disc(A.X, A.Y, T * 0.5f, Colour);
			Disc(B.X, B.Y, T * 0.5f, Colour);
		}
	}
	// A stroke through every point, its corners rounded off (a road on the map).
	void Path(const std::vector<P2>& Points, float T, const Rgba& Colour)
	{
		for (size_t I = 0; I + 1 < Points.size(); ++I)
		{
			Line(Points[I], Points[I + 1], T, Colour);
			if (I > 0 && T >= 3.0f) Pie(Points[I].X, Points[I].Y, T * 0.5f, 0.0f, 360.0f, Colour);
		}
	}
	// A line of text on its baseline.
	void Text(const std::string& Words, float X0, float Base0, float Px, const Rgba& Colour, Align Anchor = Align::Left, bool Bold = false)
	{
		if (Words.empty() || Colour.A <= 0.0f) return;
		const float X = X0 + OX, Base = Base0 + OY;
		if (bClip && (X < CX0 || X > CX1 || Base < CY0 || Base > CY1 + Px)) return;
		Cmd C;
		C.IsText = true;
		C.Colour = Colour;
		C.Words = Words;
		C.At = P2(X, Base);
		C.Px = Px;
		C.Side = Anchor;
		C.Bold = Bold;
		Cmds.push_back(C);
	}

	size_t Triangles() const
	{
		size_t N = 0;
		for (const Cmd& C : Cmds) N += C.Tris.size() / 3;
		return N;
	}

private:
	bool bClip = false;
	float OX = 0.0f, OY = 0.0f;
	float CX0 = 0.0f, CY0 = 0.0f, CX1 = 0.0f, CY1 = 0.0f;
	float Inside(const P2& P, int Edge) const
	{
		switch (Edge)
		{
		case 0: return P.X - CX0;
		case 1: return CX1 - P.X;
		case 2: return P.Y - CY0;
		default: return CY1 - P.Y;
		}
	}
	// A curve's pieces: about one every 4 pixels of its edge, 6 degrees at the finest.
	static int Steps(float R, float Sweep)
	{
		const float Arc = std::fabs(Sweep) * 0.01745329252f * std::fmax(R, 1.0f);
		int N = int(Arc / 4.0f) + 1;
		const int Most = int(std::fabs(Sweep) / 3.0f) + 1;
		if (N > Most) N = Most;
		if (N < 2) N = 2;
		return N;
	}
	// Triangles of one colour in a row share a command (one draw in the game).
	void Put(const P2& A, const P2& B, const P2& C, const Rgba& Colour)
	{
		if (Cmds.empty() || Cmds.back().IsText || !Same(Cmds.back().Colour, Colour))
		{
			Cmd New;
			New.Colour = Colour;
			Cmds.push_back(New);
		}
		std::vector<P2>& T = Cmds.back().Tris;
		T.push_back(A);
		T.push_back(B);
		T.push_back(C);
	}
	static bool Same(const Rgba& A, const Rgba& B) { return A.R == B.R && A.G == B.G && A.B == B.B && A.A == B.A; }
};
} // namespace TorqueTft
