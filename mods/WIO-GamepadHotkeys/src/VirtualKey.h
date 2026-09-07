#pragma once

#include <optional>

// Maps the "Simulated Key" MCM dropdown (iSlotNVirtualKey - 0 = None, 1-21 in config.json's own
// order) to the Win32 SendInput scan code and extended flag for that physical key.
//
// Scan codes are Set 1. The extended flag is what tells apart entries sharing a base scan code:
// NumPad 7 and Home are both 0x47, and only NumPad 7's meaning depends on NumLock.
//
// Sending the scan code and flag rather than a VK via wVk makes the injected keystroke resolve
// through Windows' own scan-code-to-VK translation exactly as a real press would, NumLock
// included, matching what the player gets by pressing that key in the target mod's MCM.
namespace GMH::VirtualKey
{
	struct ScanCode
	{
		WORD scan = 0;
		bool extended = false;
	};

	// Index 0 = None; 1-21 match config.json's iSlotNVirtualKey option order. Keep them in step.
	[[nodiscard]] inline std::optional<ScanCode> FromDropdownIndex(std::int32_t a_index)
	{
		switch (a_index) {
		case 1: return ScanCode{ 0x52, false };  // NumPad 0
		case 2: return ScanCode{ 0x4F, false };  // NumPad 1
		case 3: return ScanCode{ 0x50, false };  // NumPad 2
		case 4: return ScanCode{ 0x51, false };  // NumPad 3
		case 5: return ScanCode{ 0x4B, false };  // NumPad 4
		case 6: return ScanCode{ 0x4C, false };  // NumPad 5
		case 7: return ScanCode{ 0x4D, false };  // NumPad 6
		case 8: return ScanCode{ 0x47, false };  // NumPad 7
		case 9: return ScanCode{ 0x48, false };  // NumPad 8
		case 10: return ScanCode{ 0x49, false }; // NumPad 9
		case 11: return ScanCode{ 0x35, true };  // NumPad / (Divide) - extended; base 0x35 alone is the regular '/' key
		case 12: return ScanCode{ 0x37, false }; // NumPad * (Multiply) - no extended duplicate exists at this position
		case 13: return ScanCode{ 0x4A, false }; // NumPad - (Subtract) - no extended duplicate exists at this position
		case 14: return ScanCode{ 0x4E, false }; // NumPad + (Add) - no extended duplicate exists at this position
		case 15: return ScanCode{ 0x53, false }; // NumPad . (Decimal)
		case 16: return ScanCode{ 0x52, true };  // Insert - same base scan as NumPad 0, extended
		case 17: return ScanCode{ 0x53, true };  // Delete - same base scan as NumPad .(Decimal), extended
		case 18: return ScanCode{ 0x47, true };  // Home - same base scan as NumPad 7, extended
		case 19: return ScanCode{ 0x4F, true };  // End - same base scan as NumPad 1, extended
		case 20: return ScanCode{ 0x49, true };  // Page Up - same base scan as NumPad 9, extended
		case 21: return ScanCode{ 0x51, true };  // Page Down - same base scan as NumPad 3, extended
		default: return std::nullopt;            // 0 (None), or out of range
		}
	}

	inline void Send(const ScanCode& a_key, bool a_down)
	{
		INPUT input{};
		input.type = INPUT_KEYBOARD;
		input.ki.wScan = a_key.scan;
		DWORD flags = KEYEVENTF_SCANCODE;
		if (a_key.extended) {
			flags |= KEYEVENTF_EXTENDEDKEY;
		}
		if (!a_down) {
			flags |= KEYEVENTF_KEYUP;
		}
		input.ki.dwFlags = flags;
		if (::SendInput(1, &input, sizeof(INPUT)) != 1) {
			REX::ERROR("Gamepad MCM Hotkeys: SendInput failed for scan=0x{:X} extended={} down={}, GetLastError={}"sv,
				a_key.scan, a_key.extended, a_down, ::GetLastError());
		}
	}
}
