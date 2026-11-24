// [co-op] Which build this game is, on the network (build 169: a friend joined beenster's ride, stood in
// the shed and couldn't move; their copy may not have been the same build).
//
// Unreal lets two games connect when their engine and their project version match, and every build of
// ours says project version 1.0.0.0. So any two builds could join each other, and a mismatched pair
// then replicates nonsense (actors and properties that don't line up) and gets stuck instead of saying
// why. Here the network version also carries the build's own round and commit (Data/build.json, the
// title screen's build stamp): a different build is turned away at once, and the joining player's menu
// says both builds must be the same. A dev build with no Data/build.json keeps Unreal's own check.
#pragma once

#include "CoreMinimal.h"

namespace TorqueNetVersion
{
// From the game module's start-up, before any connection is made.
void Install();
// "Build 169", or "a dev build" when Data/build.json isn't there.
FString BuildName();
// The round in Data/build.json (0 for a dev build).
int32 Round();
}
