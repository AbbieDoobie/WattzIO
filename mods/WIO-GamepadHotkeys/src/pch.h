#pragma once

#include <RE/Fallout.h>
#include <F4SE/F4SE.h>

// Compatibility layer over CommonLibF4RD (lib/commonlibf4rd/compat/WattzIO): REX:: logging,
// F4SE::InputMap, the RE:: types F4RD lacks, and the plugin entry-point boilerplate.
#include <WattzIO/Compat.h>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#undef ERROR  // wingdi.h's ERROR macro (=0) collides with REX::ERROR

using namespace std::literals;
