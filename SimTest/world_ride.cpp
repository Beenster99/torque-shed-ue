// The stage 1 gate of Ride UK, run from a desk: the roads between two places fetched from the
// world's store, the way by road between them found, its ground fetched, and the road's height read
// all the way along, with the real network (through the curl on the path). The game runs the same
// check itself with -TorqueWorldTest (World/TorqueWorldSubsystem).
//
//   world_ride <folder to keep what is fetched> [latA lonA latB lonB]     (Hawes to Leyburn if none)
#include "CellStore.h"
#include "HeightTiles.h"
#include "WorldFiles.h"
#include "WorldLoad.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <string>

using namespace TorqueWorld;

static std::string GTmp;
static std::atomic<int> GAsked{0};

// A blocking GET through curl: the HTTP code (0 for no answer) and the body.
static int CurlGet(const std::string& Url, std::string& Body)
{
	for (char C : Url)
		if (C == '\'' || C == '\\' || C == ' ') return 0;
	const std::string Out = Files::Join(GTmp, "get_" + std::to_string(++GAsked));
	const std::string Cmd = "curl -sS -L --max-time 120 -A '" + std::string(CellStore::UserAgent) + "' -o '" + Out + "' -w '%{http_code}' '" + Url + "'";
	FILE* P = popen(Cmd.c_str(), "r");
	if (P == nullptr) return 0;
	char Buf[32] = {0};
	const size_t N = fread(Buf, 1, sizeof(Buf) - 1, P);
	pclose(P);
	Buf[N] = 0;
	Body.clear();
	Files::Read(Out, Body);
	Files::Remove(Out);
	return std::atoi(Buf);
}

int main(int Argc, char** Argv)
{
	if (Argc != 2 && Argc != 6)
	{
		std::printf("usage: world_ride <folder> [latA lonA latB lonB]\n");
		return 2;
	}
	const std::string Root = Argv[1];
	// Hawes market place to Leyburn market place.
	double At[4] = {54.3040, -2.1965, 54.3103, -1.8290};
	for (int I = 0; I < 4 && Argc == 6; ++I) At[I] = std::atof(Argv[2 + I]);
	GTmp = Files::Join(Root, "tmp");
	Files::Write(Files::Join(GTmp, "here"), "");
	CellStore Cells;
	Cells.Dir = Files::Join(Root, "cells");
	Cells.Get = CurlGet;
	HeightStore Heights;
	Heights.Disk = Files::Join(Root, "heights");
	const RideCheck R = CheckRide(Cells, Heights, At[0], At[1], At[2], At[3], [](const std::string& Text) { std::printf("  %s\n", Text.c_str()); std::fflush(stdout); });
	std::printf("%s\n", R.Dump().c_str());
	std::printf("%d fetches. %s\n", GAsked.load(), R.Ok ? "PASS" : ("FAIL: " + R.Why).c_str());
	return R.Ok ? 0 : 1;
}
