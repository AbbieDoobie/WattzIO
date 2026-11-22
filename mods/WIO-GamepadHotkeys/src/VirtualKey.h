#pragma once

#include <optional>

// Maps the "Simulated Key" MCM dropdown (iSlotNVirtualKey - 0 = None, 1-69 in config.json's own
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

	// Index 0 = None; 1-69 match config.json's iSlotNVirtualKey option order. Keep them in step.
	// 22-69 (F1-F12, top-row 0-9, A-Z) send the key's physical position, like every entry here.
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
		case 22: return ScanCode{ 0x3B, false }; // F1
		case 23: return ScanCode{ 0x3C, false }; // F2
		case 24: return ScanCode{ 0x3D, false }; // F3
		case 25: return ScanCode{ 0x3E, false }; // F4
		case 26: return ScanCode{ 0x3F, false }; // F5
		case 27: return ScanCode{ 0x40, false }; // F6
		case 28: return ScanCode{ 0x41, false }; // F7
		case 29: return ScanCode{ 0x42, false }; // F8
		case 30: return ScanCode{ 0x43, false }; // F9
		case 31: return ScanCode{ 0x44, false }; // F10
		case 32: return ScanCode{ 0x57, false }; // F11
		case 33: return ScanCode{ 0x58, false }; // F12
		case 34: return ScanCode{ 0x0B, false }; // 0 (top row)
		case 35: return ScanCode{ 0x02, false }; // 1 (top row)
		case 36: return ScanCode{ 0x03, false }; // 2 (top row)
		case 37: return ScanCode{ 0x04, false }; // 3 (top row)
		case 38: return ScanCode{ 0x05, false }; // 4 (top row)
		case 39: return ScanCode{ 0x06, false }; // 5 (top row)
		case 40: return ScanCode{ 0x07, false }; // 6 (top row)
		case 41: return ScanCode{ 0x08, false }; // 7 (top row)
		case 42: return ScanCode{ 0x09, false }; // 8 (top row)
		case 43: return ScanCode{ 0x0A, false }; // 9 (top row)
		case 44: return ScanCode{ 0x1E, false }; // A
		case 45: return ScanCode{ 0x30, false }; // B
		case 46: return ScanCode{ 0x2E, false }; // C
		case 47: return ScanCode{ 0x20, false }; // D
		case 48: return ScanCode{ 0x12, false }; // E
		case 49: return ScanCode{ 0x21, false }; // F
		case 50: return ScanCode{ 0x22, false }; // G
		case 51: return ScanCode{ 0x23, false }; // H
		case 52: return ScanCode{ 0x17, false }; // I
		case 53: return ScanCode{ 0x24, false }; // J
		case 54: return ScanCode{ 0x25, false }; // K
		case 55: return ScanCode{ 0x26, false }; // L
		case 56: return ScanCode{ 0x32, false }; // M
		case 57: return ScanCode{ 0x31, false }; // N
		case 58: return ScanCode{ 0x18, false }; // O
		case 59: return ScanCode{ 0x19, false }; // P
		case 60: return ScanCode{ 0x10, false }; // Q
		case 61: return ScanCode{ 0x13, false }; // R
		case 62: return ScanCode{ 0x1F, false }; // S
		case 63: return ScanCode{ 0x14, false }; // T
		case 64: return ScanCode{ 0x16, false }; // U
		case 65: return ScanCode{ 0x2F, false }; // V
		case 66: return ScanCode{ 0x11, false }; // W
		case 67: return ScanCode{ 0x2D, false }; // X
		case 68: return ScanCode{ 0x15, false }; // Y
		case 69: return ScanCode{ 0x2C, false }; // Z
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
