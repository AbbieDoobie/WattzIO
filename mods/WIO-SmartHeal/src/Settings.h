#pragma once

#include <algorithm>
#include <cstdlib>
#include <cstring>

// NOGDI: <wingdi.h> defines a bare ERROR macro that collides with REX::ERROR wherever a
// translation unit pulls Windows.h in ahead of a header that logs an error.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define NOGDI
#include <Windows.h>

// Reads MCM's runtime settings file via the Win32 GetPrivateProfileString*A APIs, never
// REX::FIniSettingStore/TIniSetting, whose CSimpleIni backend writes a UTF-8 BOM that corrupts
// MCM's own GetPrivateProfileSection reader for the first section. This plugin never writes to
// Data/MCM/Settings/WIO-SmartHeal.ini; MCM owns that file.
#include "MenuContext.h"

namespace SH::Settings
{
	namespace detail
	{
		constexpr auto kSettingsPath = "Data/MCM/Settings/WIO-SmartHeal.ini";

		[[nodiscard]] inline bool GetBool(const char* a_section, const char* a_key, bool a_default)
		{
			return WIO::Ini::GetBool(a_section, a_key, a_default, kSettingsPath);
		}

		[[nodiscard]] inline std::int32_t GetInt(const char* a_section, const char* a_key, std::int32_t a_default)
		{
			return static_cast<std::int32_t>(::GetPrivateProfileIntA(a_section, a_key, a_default, kSettingsPath));
		}

		// MCM writes ModSettingFloat values as decimal text. GetPrivateProfileIntA stops at the
		// '.' and would turn 2.5 into 2.
		[[nodiscard]] inline float GetFloat(const char* a_section, const char* a_key, float a_default)
		{
			char buf[32]{};
			::GetPrivateProfileStringA(a_section, a_key, "", buf, sizeof(buf), kSettingsPath);
			if (buf[0] == 0) {
				return a_default;
			}
			try {
				return std::stof(buf);
			} catch (...) {
				return a_default;
			}
		}
	}

	// Which side of a target it is acceptable to land on when no combination of items hits it
	// exactly. Raw MCM dropdown indices - see config.json's option order.
	enum class Direction : std::int32_t
	{
		kOver = 0,   // must end at or above the target; overshoot is fine, falling short is not
		kUnder = 1   // must end at or below the target; undershoot is fine, overshoot is not
	};

	// "Include Irradiated Food" dropdown. Raw MCM dropdown indices.
	enum class IrradiatedFood : std::int32_t
	{
		kOff = 0,
		kLastResort = 1,  // only when clean food and stimpaks cannot reach the HP target
		kPreferred = 2    // reach for it first
	};

	// "Include Special/Buff Food" dropdown. Raw MCM dropdown indices, the same three-way shape as
	// IrradiatedFood above but a separate enum: irradiated food's cost is radiation, which the
	// solver prices, while special food's cost is a buff it cannot see, so the only thing
	// protecting it is where it sits in the tier order (Solver::detail::Tier).
	enum class SpecialFood : std::int32_t
	{
		kOff = 0,
		kLastResort = 1,  // above stimpaks in the tier order: only when nothing else will do
		kPreferred = 2    // reach for it first, ahead of even plain food
	};

	// "Item Source" dropdown, under Advanced. Which plugins an aid item may have come from. Raw MCM
	// dropdown indices, narrowest first, so the default is the last index. See ItemCatalog::SourceOf.
	enum class ItemSource : std::int32_t
	{
		kBase = 0,        // Fallout4.esm and the six official DLC
		kBaseAndCC = 1,   // ...plus Creation Club content
		kAll = 2          // everything in the load order (default)
	};

	// "Include ... Alcohol" dropdowns. Raw MCM dropdown indices, the same three-way shape as the
	// two food dropdowns above and a different meaning, because alcohol is not scored the same way.
	//
	// Food's Last Resort and Preferred are positions in Solver::detail::Tier, where food competes
	// against stimpaks and RadAway inside the knapsack. Alcohol never enters the knapsack (see
	// Solver::detail::PickReservedAlcohol), so it has no tier.
	//
	// They are instead a rank within the one reserved drink, applied ahead of the need-fit
	// comparison in Solver::detail::PickReservedAlcohol:
	//
	//   kPreferred    rank 0  reach for this class of drink first
	//   (plain drink) rank 1  in none of the three classes: the baseline
	//   kLastResort   rank 2  only when no other eligible drink is carried
	//
	// So the comparison is always against other alcohol, never against food or stimpaks.
	enum class AlcoholInclude : std::int32_t
	{
		kOff = 0,
		kLastResort = 1,
		kPreferred = 2
	};

	// "Alcohol" dropdown. Raw MCM dropdown indices. This says when alcohol may be used; which drinks
	// are eligible is the three iAlcohol* include-dropdowns below.
	//
	// It gates every use of alcohol, healing drinks included - so in kCombatOnly, a holstered weapon
	// out of combat means the mod does not touch alcohol at all.
	//
	// At most one alcohol is spent per press; see Solver::detail::PickReservedAlcohol.
	enum class Alcohol : std::int32_t
	{
		kOff = 0,
		kAlways1 = 1,    // one drink every press, healing needed or not
		kCombatOnly = 2  // ...but only while in combat or with a weapon drawn
	};

	// "Item Order Preference" dropdown, in the Items page's Item Selection section. Biases which
	// kind of item the solver reaches for first by moving the stimpak tier (Solver::detail::Tier).
	// It never changes what is allowed, the category toggles doing that, and never overrides the
	// crippled-limb reserved slot, which is filled before tier scoring runs. kPreferStimpak is
	// guarded by the half-stimpak rule; see Solver::detail::Tier for why an unguarded promotion
	// burns a stimpak on one point of damage.
	enum class ItemOrder : std::int32_t
	{
		kCheapest = 0,       // cheapest category first
		kPreferStimpak = 1,  // stimpak to the cheapest tier, subject to the half-stimpak rule
		kAvoidStimpak = 2    // stimpak above everything, spent only when nothing else will do
	};

	// "Preferred Last Item" dropdown, in the Animation section. Which kind of item the player would
	// rather see the use animation for. Vanilla animates few aid items (see
	// ItemCatalog::Entry::animatesOnUse), so this only becomes a real preference with an animation
	// framework installed. It biases the consume order rather than selecting a route: the preferred category
	// sorts to the end of the queue and whatever ends up last animates, so it can neither cause an
	// item to be spent nor stop one being spent.
	enum class PreferredLast : std::int32_t
	{
		kStimpak = 0,
		kAid = 1,
		kAlcohol = 2,
		kFood = 3,     // plain, special and irradiated food all count
		kRadaway = 4,
		kRandom = 5    // chosen per press, from the categories actually in the plan
	};

	// Live settings, re-read via Load() at kGameLoaded and on every pause-menu close. The keyboard
	// hotkey is not in this ini: it is MCM's native "hotkey" widget, which writes to the shared
	// Data/MCM/Settings/Keybinds.json instead (see Keybinds.h).
	inline std::int32_t iGamepadKey = 0;
	inline std::int32_t iGamepadModifier = 0;

	// Percent of the health the player can actually reach (the RadHealthMax ceiling), re-evaluated
	// after any radiation the same press is going to clear.
	inline std::int32_t iHPTarget = 100;
	inline std::int32_t iHPHysteresis = 0;
	inline bool         bStimpakOnCrippled = true;

	inline bool         bHealRadiation = true;
	inline std::int32_t iMaxRads = 0;
	inline std::int32_t iRadHysteresis = 0;

	inline bool           bUseStimpak = true;
	// The three alcohol include-dropdowns, all Off by default, so the pool is ordinary drinks:
	// nothing that heals, no SPECIAL buff of +2 or more, nothing that irradiates. How they combine:
	// see ItemCatalog::AlcoholIncludedFor and AlcoholRankFor.
	inline AlcoholInclude alcoholHealing = AlcoholInclude::kOff;
	inline AlcoholInclude alcoholTopShelf = AlcoholInclude::kOff;
	inline AlcoholInclude alcoholIrradiated = AlcoholInclude::kOff;
	// Off by default, unlike Stimpak and Radaway: its items (blood packs, Glowing One Blood,
	// Refreshing Beverage) are the kind a player tends to be saving.
	inline bool           bUseAid = false;
	inline bool           bUseRadaway = true;
	// The master switch for all food: off means no plain, special, or irradiated food is spent,
	// whatever the two Include settings below say. "Special food only" cannot be expressed.
	inline bool           bUseFood = true;
	inline SpecialFood    foodSpecial = SpecialFood::kOff;
	inline IrradiatedFood foodIrradiated = IrradiatedFood::kOff;
	inline Alcohol        alcohol = Alcohol::kOff;
	// "Alcohol Cooldown", in minutes; 0 disables it. While it runs, alcohol is excluded from the
	// plan regardless of the alcohol settings above, and changing them does not reset it.
	inline float          fAlcoholCooldown = 3.0f;
	inline ItemOrder      itemOrder = ItemOrder::kCheapest;
	inline std::int32_t   iMaxItems = 10;

	inline Direction  hpDirection = Direction::kOver;
	inline Direction  radDirection = Direction::kUnder;
	inline bool       bNotify = true;
	inline ItemSource itemSource = ItemSource::kAll;

	// Strips an item-sorter tag such as "[Aid]" out of a name before it goes on screen; see
	// Notify::CleanName. On by default: "x2 [Aid] Stimpak" reads as a bug in this mod rather than
	// as the item sorter the player installed.
	inline bool bCleanNames = true;

	// Exception to this project's usual "new toggles default off" convention: a hotkey that eats a
	// stimpak mid-dialogue or mid-quickloot reads as a bug rather than a feature to opt into.
	inline MenuContext::BlockMode blockMode = MenuContext::BlockMode::kMenusOnly;

	// Hidden diagnostic switch, not in config.json, so it has no MCM control. Add bDebugLog=1 under
	// [Advanced] in Data/MCM/Settings/WIO-SmartHeal.ini by hand and close the pause menu; MCM leaves
	// keys it does not own alone. Turns on the item-catalogue dump and the per-press solver trace.
	//
	// Load() hands it to WIO::SetVerbose, which is what actually lifts the log level: a release
	// build is pinned to Info and REX::DEBUG is spdlog::debug, so without that these lines are
	// formatted and dropped.
	inline bool debugLog = false;

	// Hidden. How Consume hands an item to the engine: 0 applies immediately (default), 1 defers to
	// the engine's own equip queue. See Consume.h.
	inline std::int32_t iEquipMode = 0;

	// "Animate Last Item", the Animation section's switch. On by default, routing the last item of
	// a press through Papyrus Actor.EquipItem, the only call found that reaches the game's
	// use-animation handler; everything before it takes the immediate path. Off means no Papyrus
	// dispatch at all. The animated path is asynchronous and noticeably slower to consume, so a
	// player who does not care about the animation is better off without it.
	inline bool bAnimateLastItem = true;

	inline PreferredLast preferredLast = PreferredLast::kStimpak;

	// Hidden. Which function the items that ask for an animation get handed to:
	//
	//   0  ActorEquipManager::EquipObject - the same path as every other item
	//   1  Actor::DrinkPotion             - virtual 0x119, does not animate
	//   2  Papyrus Actor.EquipItem        (default) - the one that animates
	//
	// Modes 0 and 1 both consume correctly and neither animates, DrinkPotion included even though
	// it is the full use path.
	//
	// Mode 2 fails soft: if the dispatch cannot be set up, the item goes down through DrinkPotion
	// instead, and if it is set up but the item does not leave the inventory, Verify logs it.
	inline std::int32_t iAnimatedUseMode = 2;

	// Hidden. Extra milliseconds of pause before an item that animates, on top of the ordinary
	// inter-item gap.
	//
	// Milliseconds, not frames: 20 frames is 333 ms at 60 fps and 20 ms at 1000 fps, while what
	// this wait covers (letting the previous equip stop disturbing the animation graph) is
	// measured in milliseconds on every machine. Ignored when the animated route is off.
	inline std::int32_t iAnimatedUseDelayMS = 350;

	// Hidden. How long, in milliseconds, to give the animated route to consume the item before
	// saying in the log that it did not.
	//
	// A grace period, not a timeout: nothing happens when it expires except a warning. See
	// Consume::detail::Verify for why it never re-issues.
	//
	// Dispatch-to-drop latency is 50-82 ms on a heavy load order, so at 2 s a warning reports
	// something genuinely wrong rather than a briefly busy VM.
	// Consume logs the measured time on every animated use.
	inline std::int32_t iAnimatedUseGraceMS = 2000;

	// Hidden. Plans and logs as normal but consumes nothing, so the solver can be checked against
	// a real inventory without spending anything. Add bDryRun=1 under [Advanced] alongside
	// bDebugLog=1.
	inline bool bDryRun = false;

	// Range-checked dropdown-index to enum conversion. A blind static_cast on an out-of-range
	// value turns a hand-edited ini into a silently dead feature.
	template <class T>
	[[nodiscard]] inline T ToEnum(std::int32_t a_raw, std::int32_t a_max, T a_default)
	{
		return (a_raw >= 0 && a_raw <= a_max) ? static_cast<T>(a_raw) : a_default;
	}

	inline void Load()
	{
		iGamepadKey = detail::GetInt("Input", "iGamepadKey", 0);
		iGamepadModifier = detail::GetInt("Input", "iGamepadModifier", 0);

		// Clamped rather than trusted: a stale or hand-edited ini can hold anything, and an
		// out-of-range value would silently mean "never satisfied" or "never act".
		iHPTarget = std::clamp(detail::GetInt("Health", "iHPTarget", 100), 0, 100);
		iHPHysteresis = std::clamp(detail::GetInt("Health", "iHPHysteresis", 0), 0, 25);
		bStimpakOnCrippled = detail::GetBool("Health", "bStimpakOnCrippled", true);

		bHealRadiation = detail::GetBool("Radiation", "bHealRadiation", true);
		iMaxRads = std::clamp(detail::GetInt("Radiation", "iMaxRads", 0), 0, 100);
		iRadHysteresis = std::clamp(detail::GetInt("Radiation", "iRadHysteresis", 0), 0, 25);

		bUseStimpak = detail::GetBool("Items", "bUseStimpak", true);
		bUseAid = detail::GetBool("Items", "bUseAid", false);
		bUseRadaway = detail::GetBool("Items", "bUseRadaway", true);
		bUseFood = detail::GetBool("Items", "bUseFood", true);
		foodSpecial = ToEnum<SpecialFood>(detail::GetInt("Items", "iFoodSpecial", 0), 2, SpecialFood::kOff);
		foodIrradiated = ToEnum<IrradiatedFood>(detail::GetInt("Items", "iFoodIrradiated", 0), 2, IrradiatedFood::kOff);
		alcohol = ToEnum<Alcohol>(detail::GetInt("Items", "iAlcohol", 0), 2, Alcohol::kOff);
		alcoholHealing = ToEnum<AlcoholInclude>(detail::GetInt("Items", "iAlcoholHealing", 0), 2,
			AlcoholInclude::kOff);
		alcoholTopShelf = ToEnum<AlcoholInclude>(detail::GetInt("Items", "iAlcoholTopShelf", 0), 2,
			AlcoholInclude::kOff);
		alcoholIrradiated = ToEnum<AlcoholInclude>(detail::GetInt("Items", "iAlcoholIrradiated", 0), 2,
			AlcoholInclude::kOff);
		fAlcoholCooldown = std::clamp(detail::GetFloat("Items", "fAlcoholCooldown", 3.0f), 0.0f, 10.0f);
		itemOrder = ToEnum<ItemOrder>(detail::GetInt("Items", "iItemOrder", 0), 2, ItemOrder::kCheapest);
		iMaxItems = std::clamp(detail::GetInt("Items", "iMaxItems", 10), 2, 50);

		hpDirection = ToEnum<Direction>(detail::GetInt("Advanced", "iHPDirection", 0), 1, Direction::kOver);
		radDirection = ToEnum<Direction>(detail::GetInt("Advanced", "iRadDirection", 1), 1, Direction::kUnder);
		bNotify = detail::GetBool("Advanced", "bNotify", true);
		itemSource = ToEnum<ItemSource>(detail::GetInt("Advanced", "iItemSource", 2), 2, ItemSource::kAll);
		bCleanNames = detail::GetBool("Advanced", "bCleanNotificationNames", true);
		blockMode = ToEnum<MenuContext::BlockMode>(detail::GetInt("Advanced", "iBlockHotkeysInMenus", 0), 2,
			MenuContext::BlockMode::kMenusOnly);

		bAnimateLastItem = detail::GetBool("Animation", "bAnimateLastItem", true);
		preferredLast = ToEnum<PreferredLast>(detail::GetInt("Animation", "iPreferredLastItem", 0), 5,
			PreferredLast::kStimpak);

		debugLog = detail::GetBool("Advanced", "bDebugLog", false);
		WIO::SetVerbose(debugLog);
		iEquipMode = std::clamp(detail::GetInt("Advanced", "iEquipMode", 0), 0, 1);
		iAnimatedUseMode = std::clamp(detail::GetInt("Advanced", "iAnimatedUseMode", 2), 0, 2);
		iAnimatedUseDelayMS = std::clamp(detail::GetInt("Advanced", "iAnimatedUseDelayMS", 350), 0, 5000);
		iAnimatedUseGraceMS = std::clamp(detail::GetInt("Advanced", "iAnimatedUseGraceMS", 2000), 50, 10000);
		bDryRun = detail::GetBool("Advanced", "bDryRun", false);

		// Printed because a hand-edited hidden key that did not take is otherwise
		// indistinguishable from one that took and changed nothing.
		if (debugLog) {
			// Every {} here must have exactly one argument below. std::format silently ignores a
			// surplus argument, so a missing placeholder does not fail to build - it shifts every
			// later field onto the previous one's value.
			REX::DEBUG("Smart Heal Button: [SETTINGS] hpTarget={} hpHyst={} crippled={} | healRads={} "
			          "maxRads={} radHyst={} | stimpak={} aid={} radaway={} food={} special={} "
			          "irradiated={} alcohol={} alcHeal={} alcTop={} alcRad={} "
			          "alcoholCooldown={} itemOrder={} maxItems={} | "
			          "hpDir={} radDir={} notify={} "
			          "clean={} itemSource={} | animateLast={} preferredLast={} | equipMode={} "
			          "animMode={} animDelayMS={} animGraceMS={} dryRun={}"sv,
				iHPTarget, iHPHysteresis, bStimpakOnCrippled,
				bHealRadiation, iMaxRads, iRadHysteresis,
				bUseStimpak, bUseAid, bUseRadaway, bUseFood,
				std::to_underlying(foodSpecial), std::to_underlying(foodIrradiated),
				std::to_underlying(alcohol), std::to_underlying(alcoholHealing),
				std::to_underlying(alcoholTopShelf), std::to_underlying(alcoholIrradiated),
				fAlcoholCooldown, std::to_underlying(itemOrder), iMaxItems,
				std::to_underlying(hpDirection), std::to_underlying(radDirection), bNotify,
				bCleanNames, std::to_underlying(itemSource),
				bAnimateLastItem, std::to_underlying(preferredLast),
				iEquipMode, iAnimatedUseMode, iAnimatedUseDelayMS, iAnimatedUseGraceMS, bDryRun);
		}
	}

}
