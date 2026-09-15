#pragma once

#include <RE/Fallout.h>
#include <F4SE/F4SE.h>

// Compatibility layer over CommonLibF4RD (lib/commonlibf4rd/compat/WattzIO): REX:: logging,
// F4SE::InputMap, the RE:: types F4RD lacks, and the plugin entry-point boilerplate.
#include <WattzIO/Compat.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <string_view>

#include <windows.h>

using namespace std::literals;
