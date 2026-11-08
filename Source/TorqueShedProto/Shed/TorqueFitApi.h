// Which fitment API the shed uses. The part fitting work owns the real one (Fit/TorqueFit.h and
// its .cpp). Until that is merged the shed compiles against a stand-in with the same declarations
// (TorqueFitStub.h). Never both: as soon as Fit/TorqueFit.h is in the tree the stand-in drops out.
#pragma once

#if defined(__has_include)
#if __has_include("Fit/TorqueFit.h")
#define TORQUE_FIT_STUB 0
#endif
#endif
#ifndef TORQUE_FIT_STUB
#define TORQUE_FIT_STUB 1
#endif

#if TORQUE_FIT_STUB
#include "Shed/TorqueFitStub.h"
#else
#include "Fit/TorqueFit.h"
#endif
