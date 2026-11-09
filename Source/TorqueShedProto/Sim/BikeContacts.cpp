#include "BikeContacts.h"

#include <algorithm>
#include <cmath>

namespace TorqueSim
{
void ContactWorld::SetBoxes(const std::vector<ContactBox>& InBoxes)
{
	Boxes = InBoxes;
	Grid.clear();
	for (int K = 0; K < (int)Boxes.size(); ++K)
	{
		const ContactBox& B = Boxes[K];
		const double Reach = std::sqrt(B.Hu * B.Hu + B.Hv * B.Hv);
		const int I0 = (int)std::floor((B.Cx - Reach) / CELL), I1 = (int)std::floor((B.Cx + Reach) / CELL);
		const int J0 = (int)std::floor((B.Cz - Reach) / CELL), J1 = (int)std::floor((B.Cz + Reach) / CELL);
		for (int I = I0; I <= I1; ++I)
			for (int J = J0; J <= J1; ++J) Grid[Key(I, J)].push_back(K);
	}
}

void ContactWorld::Query(const V3& C, double R, std::vector<ContactHit>& Out) const
{
	// Boxes: the nearest point of the box to the sphere's centre, or, with the centre inside, the
	// nearest way out (sideways or up, never down into the ground).
	const int I0 = (int)std::floor((C.x - R) / CELL), I1 = (int)std::floor((C.x + R) / CELL);
	const int J0 = (int)std::floor((C.z - R) / CELL), J1 = (int)std::floor((C.z + R) / CELL);
	int Seen[64];
	int NSeen = 0;
	for (int I = I0; I <= I1; ++I)
		for (int J = J0; J <= J1; ++J)
		{
			const auto It = Grid.find(Key(I, J));
			if (It == Grid.end()) continue;
			for (const int K : It->second)
			{
				bool Dup = false;
				for (int S = 0; S < NSeen && !Dup; ++S) Dup = Seen[S] == K;
				if (Dup) continue;
				if (NSeen < 64) Seen[NSeen++] = K;
				const ContactBox& B = Boxes[K];
				const double Dx = C.x - B.Cx, Dz = C.z - B.Cz;
				const double A = Dx * B.Ux + Dz * B.Uz;     // along the first axis
				const double Bv = -Dx * B.Uz + Dz * B.Ux;   // along (-Uz, Ux)
				if (std::fabs(A) > B.Hu + R || std::fabs(Bv) > B.Hv + R || C.y < B.Y0 - R || C.y > B.Y1 + R) continue;
				const double Ca = std::clamp(A, -B.Hu, B.Hu), Cb = std::clamp(Bv, -B.Hv, B.Hv), Cy = std::clamp(C.y, B.Y0, B.Y1);
				const double Ea = A - Ca, Eb = Bv - Cb, Ey = C.y - Cy;
				const double Dist2 = Ea * Ea + Eb * Eb + Ey * Ey;
				ContactHit H;
				if (Dist2 > 1e-12)
				{
					if (Dist2 >= R * R) continue;
					const double Dist = std::sqrt(Dist2);
					// Back to the sim frame: a along (Ux, 0, Uz), b along (-Uz, 0, Ux).
					H.Normal = V3(Ea * B.Ux - Eb * B.Uz, Ey, Ea * B.Uz + Eb * B.Ux) / Dist;
					H.Depth = R - Dist;
				}
				else
				{
					const double OutA = B.Hu - std::fabs(A), OutB = B.Hv - std::fabs(Bv), OutY = B.Y1 - C.y;
					if (OutY <= OutA && OutY <= OutB) { H.Normal = V3(0.0, 1.0, 0.0); H.Depth = OutY + R; }
					else if (OutA <= OutB)
					{
						const double S = A >= 0.0 ? 1.0 : -1.0;
						H.Normal = V3(B.Ux * S, 0.0, B.Uz * S);
						H.Depth = OutA + R;
					}
					else
					{
						const double S = Bv >= 0.0 ? 1.0 : -1.0;
						H.Normal = V3(-B.Uz * S, 0.0, B.Ux * S);
						H.Depth = OutB + R;
					}
				}
				Out.push_back(H);
			}
		}
	// Other bikes.
	for (const ContactCapsule& Cap : Capsules)
	{
		const V3 Ab = Cap.B - Cap.A;
		const double L2 = Ab.Dot(Ab);
		const double T = L2 > 1e-9 ? std::clamp((C - Cap.A).Dot(Ab) / L2, 0.0, 1.0) : 0.0;
		const V3 Q = Cap.A + Ab * T;
		const V3 D = C - Q;
		const double Dist = D.Length(), Reach = R + Cap.Radius;
		if (Dist >= Reach) continue;
		ContactHit H;
		H.Normal = Dist > 1e-6 ? D / Dist : V3(0.0, 1.0, 0.0);
		H.Depth = Reach - Dist;
		H.OtherVelocity = Cap.Velocity;
		H.Bike = true;
		Out.push_back(H);
	}
}
} // namespace TorqueSim
