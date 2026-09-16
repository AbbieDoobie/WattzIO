#pragma once

#include <array>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <F4SE/F4SE.h>

// Compatibility layer over CommonLibF4RD (lib/commonlibf4rd/compat/WattzIO): REX:: logging,
// F4SE::InputMap, the RE:: types F4RD lacks, and the plugin entry-point boilerplate.
#include <WattzIO/Compat.h>
#include <RE/Fallout.h>

using namespace std::literals;
