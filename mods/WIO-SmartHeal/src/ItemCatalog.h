#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "Effects.h"
#include "Settings.h"

// The prebuilt list of everything in the load order this mod could feed the player, built once
// at game load so a keypress never has to touch the form arrays.
namespace SH::ItemCatalog
{
	// Which MCM "Items" toggle owns this item. One category per item, decided once at load.
	enum class Category : std::uint8_t
	{
		kUnusable = 0,    // nothing this mod can act on
		kStimpak,         // mends limbs, or is a stimpak by keyword
		kAid,             // Medicine-flagged and heals, but is not a stimpak - blood packs and the like
		kRadaway,         // Medicine-flagged, removes radiation, restores no health
		kFood,            // restores health or removes radiation, adds none, and does nothing else
		kSpecialFood,     // restores health and does something else: a buff, a Fortify, a script
		kIrradiatedFood,  // restores health, adds radiation
		kAlcohol          // ObjectTypeAlcohol
	};

	// How good a drink is, for Include Top-Shelf Alcohol. Meaningful on kAlcohol entries only;
	// everything else is left at the default.
	enum class Shelf : std::uint8_t
	{
		kMiddleBottom = 0,
		kTop = 1
	};

	// "+2 or higher", which is what the MCM help text promises. Keep the two in step.
	inline constexpr float kTopShelfAtLeast = 2.0F;

	[[nodiscard]] constexpr Shelf ShelfFor(float a_specialBuff)
	{
		// Epsilon so a drink stored as 1.9999999 still reads as top shelf.
		return a_specialBuff >= (kTopShelfAtLeast - 0.0001F) ? Shelf::kTop : Shelf::kMiddleBottom;
	}

	[[nodiscard]] constexpr std::string_view ShelfName(Shelf a_shelf)
	{
		return a_shelf == Shelf::kTop ? "Top"sv : "Mid/Bottom"sv;
	}

	// Which plugin introduced the item, which is not the same question as which plugin last
	// touched it. See SourceOf().
	enum class Source : std::uint8_t
	{
		kBaseGame = 0,      // Fallout4.esm or one of the six official DLC
		kCreationClub = 1,  // a cc* plugin
		kOther = 2          // anything else: a mod
	};

	[[nodiscard]] constexpr std::string_view SourceName(Source a_source)
	{
		switch (a_source) {
		case Source::kBaseGame:     return "Base"sv;
		case Source::kCreationClub: return "CC"sv;
		default:                    return "Mod"sv;
		}
	}

	[[nodiscard]] constexpr std::string_view CategoryName(Category a_category)
	{
		switch (a_category) {
		case Category::kStimpak:        return "Stimpak"sv;
		case Category::kAid:            return "Aid"sv;
		case Category::kRadaway:        return "Radaway"sv;
		case Category::kFood:           return "Food"sv;
		case Category::kSpecialFood:    return "SpecialFood"sv;
		case Category::kIrradiatedFood: return "IrradiatedFood"sv;
		case Category::kAlcohol:        return "Alcohol"sv;
		default:                        return "Unusable"sv;
		}
	}

	// An effect whose CTDA conditions are non-empty. Vanilla uses these for perk-gated variants
	// (MelonWild carries a 1.0 and a 1.5 RestoreHealthFood, mutually exclusive on a perk check),
	// so summing them blind over-counts. Re-tested against the live player per press; see Evaluate().
	struct ConditionalEffect
	{
		const RE::EffectItem* effect = nullptr;
		Effects::Contribution contribution{};
	};

	struct Entry
	{
		RE::AlchemyItem* item = nullptr;
		Category         category = Category::kUnusable;

		// Contributions from effects with no conditions - these always apply.
		Effects::Contribution unconditional{};

		// Contributions that have to be re-tested per press.
		std::vector<ConditionalEffect> conditional{};

		// Resolved once at catalogue build but checked per press against the Item Source setting, so
		// the catalogue stays a plain description of the load order and a setting change takes effect
		// on the next press without a rebuild.
		Source source = Source::kOther;

		// Unconditional plus every conditional effect, assumed to pass. The flags below derive from
		// it, and the catalogue dump needs it: on a large load order `unconditional` alone is zero for
		// more than half the entries, so a Sweet Roll carrying 4 rads conditionally prints as rads=0.0.
		Effects::Contribution total{};

		// Non-zero addiction chance on the ALCH record.
		bool addictive = false;

		// Whether this item can mend a crippled limb at all, counting conditional effects whether or
		// not their conditions currently pass. Not whether the effect would fire right now: the vanilla
		// Stimpak's six RestoreCondition effects are gated on HC_Rule_NoLimbConditionHeal == 1, so
		// outside Survival a stimpak mends limbs by a mechanism no effect describes. The narrow
		// question would leave the reserved crippled-limb slot with no candidate on Normal.
		bool canMendLimb = false;

		// Whether this item does anything beyond health, radiation, and limbs: a stat buff, a Fortify,
		// an AP restore, a script. From the full effect list, conditional entries included, because
		// being special is a property of the item rather than of the player's current perks.
		bool hasOtherEffects = false;

		// Whether the record asks for a use animation, via the PlayAnimOnItemUse keyword. Log only:
		// routing is by category, because an animation framework animates far more than the keyword
		// covers. Across Fallout4.esm and all six DLC exactly four aid records carry it: Stimpak,
		// Curie's Healthpak, Stealth Boy, and the Railroad Stealth Boy.
		bool animatesOnUse = false;

		// The strongest base, beneficial, non-perk-gated SPECIAL buff on the record, and the shelf
		// that follows from it. Only alcohol is filtered on this, but it is computed for every
		// entry: it costs one pass over effects already being walked, and it is worth having in the
		// catalogue dump when a player asks why a drink was or was not spent.
		float specialBuff = 0.0F;
		Shelf shelf = Shelf::kMiddleBottom;

		[[nodiscard]] bool AddsRads() const { return total.rads > 0.0F; }

		// Restores health at all. Only alcohol asks: vanilla alcohol heals nothing, but modded
		// drinks routinely do - one tested load order had 62 of them, up to 1500 HP - and those are
		// exactly the drinks a player may want held back.
		[[nodiscard]] bool RestoresHealth() const
		{
			return total.hpFlat > 0.0F || total.hpPercent > 0.0F;
		}
	};

	namespace detail
	{
		inline std::unordered_map<RE::TESFormID, Entry> g_entries;
		inline bool                                     g_built = false;

		// The base game and its six official DLC, by filename. Matched on the name rather than the
		// load-order index, which depends on what else the player has installed.
		inline constexpr std::array kBaseFiles{
			"Fallout4.esm"sv,
			"DLCRobot.esm"sv,        // Automatron
			"DLCworkshop01.esm"sv,   // Wasteland Workshop
			"DLCCoast.esm"sv,        // Far Harbor
			"DLCworkshop02.esm"sv,   // Contraptions Workshop
			"DLCworkshop03.esm"sv,   // Vault-Tec Workshop
			"DLCNukaWorld.esm"sv     // Nuka-World
		};

		// Filenames are compared without case: "DLCCoast.esm" and "dlccoast.esm" are the same file
		// on a Windows disk.
		[[nodiscard]] inline bool EqualsNoCase(std::string_view a_lhs, std::string_view a_rhs)
		{
			return a_lhs.size() == a_rhs.size() &&
			       std::ranges::equal(a_lhs, a_rhs, [](char a_a, char a_b) {
				       return std::tolower(static_cast<unsigned char>(a_a)) ==
				              std::tolower(static_cast<unsigned char>(a_b));
			       });
		}

		// TESForm::GetFile(0) is the plugin that introduced the form; GetFile(-1), the default, is the
		// last to override it. Filtering by origin rather than last editor keeps a balance mod's
		// override of Fallout4.esm's Mirelurk Queen Steak in the catalogue at Base with the modded
		// numbers. Creation Club is matched on the cc filename prefix, which also catches a mod-added
		// plugin named that way: the cost of a prefix test over a list that goes stale.
		[[nodiscard]] inline Source SourceOf(const RE::TESForm& a_form)
		{
			const auto file = a_form.GetFile(0);
			if (!file) {
				// No source file at all means a runtime-created form. Treated as a mod's, so at
				// Base it is excluded rather than silently allowed.
				return Source::kOther;
			}

			const auto filename = file->GetFilename();
			for (const auto& base : kBaseFiles) {
				if (EqualsNoCase(filename, base)) {
					return Source::kBaseGame;
				}
			}
			if (filename.size() >= 2 && EqualsNoCase(filename.substr(0, 2), "cc"sv)) {
				return Source::kCreationClub;
			}
			return Source::kOther;
		}

		// Category comes from the item's effects first and its flags second, only two flags being
		// consulted: the ObjectTypeAlcohol keyword and Medicine. IsFood() is not one of them; see the
		// `heals` branch below.
		[[nodiscard]] inline Category Classify(RE::AlchemyItem& a_item, const Effects::Contribution& a_all)
		{
			if (a_item.HasKeywordString("ObjectTypeAlcohol"sv)) {
				return Category::kAlcohol;
			}

			const bool medicine = a_item.IsMedicine();
			const bool heals = a_all.hpFlat > 0.0F || a_all.hpPercent > 0.0F;

			// What makes something a stimpak rather than just medicine. Mending a limb is the primary
			// marker, and it is a property of the effects rather than the name: only the vanilla Stimpak
			// restores limb condition actor values, so testing behaviour is what makes modded variants
			// work. ObjectTypeStimpak is the second marker, earning its place on one record, Curie's
			// Healing Pack healing a flat 35 with an ordinary ValueModifier and mending nothing.
			//
			// The kStimpak archetype is not a marker: it means "heals a percentage of maximum health",
			// which describes a heal rather than a kind of item, and would drag Glowing Blood Pack and the
			// MS19 Cure in. Everything else Medicine-flagged that heals is Aid, so a modded stimpak that
			// neither mends a limb nor carries the keyword lands there, one tier earlier than it should.
			if (a_all.mendsLimb || (medicine && heals && a_item.HasKeywordString("ObjectTypeStimpak"sv))) {
				return Category::kStimpak;
			}
			if (medicine && heals) {
				return Category::kAid;
			}

			// Radaway means medicine whose only job is removing radiation. The Medicine flag is
			// load-bearing: without it Nuka-Cola Xtreme (500 rads removed, no healing, a drink) lands
			// here, so rationing RadAway would also stop the player scrubbing rads with drinks.
			// RadAway carries no ObjectType keyword in vanilla, so the flag is what is left; across
			// the base game and all six DLC this admits exactly two records.
			if (medicine && a_all.rads < 0.0F) {
				return Category::kRadaway;
			}
			// Not `food && heals`. Anything that heals and has not claimed to be medicine takes the
			// food path, whether or not its ENIT FOOD flag is set: Bethesda leaves that flag off four
			// vanilla records, and a mod overriding a record can clear it on any other.
			//
			// So the food path is the default and Stimpak has to be earned, by mending a limb or by
			// the record saying Medicine. The two ways of being wrong are not symmetrical: a modded
			// stimpak miscalled food is spent under a toggle the player left on, while food miscalled
			// a stimpak escapes the irradiated and special gates entirely.
			//
			// `heals || removes radiation`, not just `heals`: a drink whose only effect is scrubbing
			// radiation belongs under the food settings rather than the chem one.
			if (heals || a_all.rads < 0.0F) {
				// Special beats irradiated. A Nuka-Cola is both (it heals, buffs AP and carries 5
				// rads), and what makes it worth saving is the buff; the rads are already priced
				// by the solver on their own axis. Allowed() then makes anything carrying
				// radiation clear the irradiated gate as well, so turning Include Irradiated Food
				// off still keeps every Nuka-Cola out of the plan.
				if (a_all.otherEffect) {
					return Category::kSpecialFood;
				}
				return a_all.rads > 0.0F ? Category::kIrradiatedFood : Category::kFood;
			}
			return Category::kUnusable;
		}
	}

	// Built once at kGameLoaded from the loaded form list, and never rebuilt for gameplay reasons,
	// since an ALCH record's effects cannot change at runtime. Retried on pause-menu close in case
	// TESDataHandler or the ActorValue singleton was not usable at kGameLoaded.
	inline void Build()
	{
		if (detail::g_built) {
			return;
		}

		if (!Effects::Resolve()) {
			return;  // retried on the next pause-menu close
		}

		const auto handler = RE::TESDataHandler::GetSingleton();
		if (!handler) {
			REX::WARN("Smart Heal Button: TESDataHandler unavailable - item catalogue not built"sv);
			return;
		}

		const auto& alchemyItems = handler->GetFormArray<RE::AlchemyItem>();
		detail::g_entries.clear();

		for (const auto item : alchemyItems) {
			if (!item) {
				continue;
			}

			Entry entry{};
			entry.item = item;

			// `all` is the unconditional total plus every conditional effect, used only to pick a
			// category. A category has to be stable across presses, so it assumes every condition
			// passes rather than depending on the player's current perks.
			Effects::Contribution all{};
			for (const auto effect : item->listOfEffects) {
				if (!effect) {
					continue;
				}
				const auto contribution = Effects::Reduce(*effect);
				// Read before the skip below. A Fortify Agility contributes nothing to any of the
				// three axes, so IsMeaningful() is false and the effect is dropped from the
				// arithmetic, but it is what makes a Deathclaw Steak special. Accumulating into
				// `all` after the skip would find nothing.
				all.otherEffect = all.otherEffect || contribution.otherEffect;
				// Read here for the same reason: a Fortify Strength is not meaningful on any of the
				// three axes, so the skip below drops it, and reading the buff after that point would
				// put every drink on the bottom shelf.
				entry.specialBuff = std::max(entry.specialBuff, Effects::SpecialBuff(*effect));
				if (!Effects::IsMeaningful(contribution)) {
					continue;
				}
				Effects::Accumulate(all, contribution);
				// Conditions live in two places and both count; see Effects::IsConditioned.
				if (Effects::IsConditioned(*effect)) {
					entry.conditional.emplace_back(effect, contribution);
				} else {
					Effects::Accumulate(entry.unconditional, contribution);
				}
			}

			entry.total = all;
			entry.source = detail::SourceOf(*item);
			entry.addictive = item->data.addictionChance > 0.0F || item->data.addictionItem != nullptr;
			entry.canMendLimb = all.mendsLimb;  // `all` includes conditional effects - see the member's comment
			entry.hasOtherEffects = all.otherEffect;
			entry.animatesOnUse = item->HasKeywordString("PlayAnimOnItemUse"sv);
			entry.shelf = ShelfFor(entry.specialBuff);
			entry.category = detail::Classify(*item, all);

			if (entry.category == Category::kUnusable) {
				continue;
			}

			// A healing chem carrying an addiction risk is not spent on someone who only asked to
			// be topped up. Alcohol is the exception, because choosing it is an explicit opt-in on
			// its own MCM dropdown.
			if (entry.addictive && entry.category != Category::kAlcohol) {
				continue;
			}

			detail::g_entries.emplace(item->GetFormID(), std::move(entry));
		}

		detail::g_built = true;

		// The per-source breakdown is printed unconditionally rather than behind bDebugLog: it is
		// how a player who set Item Source to Base and found nothing to eat tells a working filter
		// from one that ate the whole inventory.
		std::array<std::size_t, 3> bySource{};
		std::size_t                animating = 0;
		std::size_t                alcohol = 0;
		std::size_t                topShelf = 0;
		for (const auto& [formID, entry] : detail::g_entries) {
			++bySource[static_cast<std::size_t>(entry.source)];
			animating += entry.animatesOnUse ? 1u : 0u;
			if (entry.category == Category::kAlcohol) {
				++alcohol;
				topShelf += entry.shelf == Shelf::kTop ? 1u : 0u;
			}
		}
		// The top-shelf count is the cheapest check that the perk-gate exclusion is working. Every
		// vanilla drink carries a perk-gated Luck +1.5, so if those were being counted this would
		// read "28 alcohol, 28 top shelf". Vanilla plus all six DLC is 28 and 2.
		REX::INFO("Smart Heal Button: item catalogue built - {} usable of {} ALCH records "
		          "({} base game, {} Creation Club, {} mod-added; {} animate on use; "
		          "{} alcohol, {} top shelf)"sv,
			detail::g_entries.size(), alchemyItems.size(), bySource[0], bySource[1], bySource[2],
			animating, alcohol, topShelf);

		if (Settings::debugLog) {
			for (const auto& [formID, entry] : detail::g_entries) {
				// food/med/alc are what Classify branches on. A heavily modded load order can
				// override an ALCH's ENIT flags, and without these three a surprising category
				// looks like a bug here rather than an unexpected record. `total` is what Classify
				// branched on; `uncond` is what always applies.
				REX::DEBUG("Smart Heal Button: [CATALOG] {:08X} src={:<4} cat={:<15} "
				          "total(hp={:>7.1f} hp%={:>6.1f} rads={:>7.1f}) "
				          "uncond(hp={:>7.1f} rads={:>7.1f}) "
				          "canMendLimb={} special={} anim={} cond={} food={} med={} alc={} "
				          "buff={:.1f} shelf={} name={}"sv,
					formID, SourceName(entry.source), CategoryName(entry.category),
					entry.total.hpFlat, entry.total.hpPercent, entry.total.rads,
					entry.unconditional.hpFlat, entry.unconditional.rads,
					entry.canMendLimb, entry.hasOtherEffects, entry.animatesOnUse,
					entry.conditional.size(),
					entry.item->IsFood(), entry.item->IsMedicine(),
					entry.item->HasKeywordString("ObjectTypeAlcohol"sv),
					entry.specialBuff, ShelfName(entry.shelf),
					entry.item->GetFullName());
			}
		}
	}

	[[nodiscard]] inline const Entry* Find(const RE::TESBoundObject* a_object)
	{
		if (!a_object) {
			return nullptr;
		}
		const auto it = detail::g_entries.find(a_object->GetFormID());
		return it != detail::g_entries.end() ? &it->second : nullptr;
	}

	// Resolves an entry's value for the player as they are right now: the unconditional
	// contributions plus whichever conditional ones currently pass their CTDA.
	//
	// TESCondition::IsTrue(actionRef, targetRef) is the engine's own evaluator, so perk-gated and
	// difficulty-gated variants resolve as they would if the player ate the item by hand.
	[[nodiscard]] inline Effects::Contribution Evaluate(const Entry& a_entry, RE::TESObjectREFR* a_player)
	{
		Effects::Contribution out = a_entry.unconditional;
		for (const auto& conditional : a_entry.conditional) {
			if (conditional.effect && Effects::ConditionsPass(*conditional.effect, a_player)) {
				Effects::Accumulate(out, conditional.contribution);
			}
		}
		return out;
	}

	// "Alcohol Cooldown" (Settings::fAlcoholCooldown, minutes). Counts active gameplay only:
	// TickCooldown adds nothing while a pausing menu is up, so time in the Pip-Boy or a container
	// does not burn it down.
	//
	// Real time rather than game time, because the setting is in minutes and game time runs ~20x,
	// so a 3 would expire in nine real seconds. Not persisted.
	inline std::int64_t                          g_activeMs = 0;
	inline std::int64_t                          g_alcoholUsedAtMs = 0;
	inline bool                                  g_alcoholUsed = false;
	inline std::chrono::steady_clock::time_point g_lastTick{};
	inline bool                                  g_tickStarted = false;

	// Called every frame from the input hook.
	inline void TickCooldown()
	{
		const auto now = std::chrono::steady_clock::now();
		if (!g_tickStarted) {
			g_lastTick = now;
			g_tickStarted = true;
			return;
		}

		const auto delta = std::chrono::duration_cast<std::chrono::milliseconds>(now - g_lastTick).count();
		g_lastTick = now;

		const auto ui = RE::UI::GetSingleton();
		if (ui && ui->menuMode != 0) {
			return;  // paused or in a menu - do not count this span
		}

		// Clamped so a stall (alt-tab, load) cannot dump a large block of gameplay time into one
		// frame.
		g_activeMs += std::clamp<std::int64_t>(delta, 0, 250);
	}

	inline void MarkAlcoholUsed()
	{
		g_alcoholUsedAtMs = g_activeMs;
		g_alcoholUsed = true;
	}

	[[nodiscard]] inline bool AlcoholCooldownActive()
	{
		if (Settings::fAlcoholCooldown <= 0.0f || !g_alcoholUsed) {
			return false;
		}
		const auto cooldownMs = static_cast<std::int64_t>(Settings::fAlcoholCooldown * 60000.0f);
		return (g_activeMs - g_alcoholUsedAtMs) < cooldownMs;
	}

	// Whether the Alcohol dropdown's condition is met right now. Read per press, so stepping into
	// combat changes the answer without anything being rebuilt. In combat OR weapon drawn, as the
	// label says: a drawn weapon outside combat counts.
	[[nodiscard]] inline bool AlcoholModeSatisfied()
	{
		switch (Settings::alcohol) {
		case Settings::Alcohol::kOff:
			return false;
		case Settings::Alcohol::kCombatOnly: {
			const auto player = RE::PlayerCharacter::GetSingleton();
			return player && (player->IsInCombat() || player->GetWeaponMagicDrawn());
		}
		default:
			return true;
		}
	}

	// Whether the player's MCM item toggles allow this category at all. Kept here rather than in
	// the solver so the toggle-to-category mapping lives in one place.
	[[nodiscard]] inline bool CategoryEnabled(Category a_category)
	{
		switch (a_category) {
		case Category::kStimpak:        return Settings::bUseStimpak;
		case Category::kAid:            return Settings::bUseAid;
		case Category::kRadaway:        return Settings::bUseRadaway && Settings::bHealRadiation;
		// bUseFood is the master switch for all three food categories.
		case Category::kFood:          return Settings::bUseFood;
		case Category::kSpecialFood:    return Settings::bUseFood &&
		                                       Settings::foodSpecial != Settings::SpecialFood::kOff;
		case Category::kIrradiatedFood: return Settings::bUseFood &&
		                                       Settings::foodIrradiated != Settings::IrradiatedFood::kOff;
		case Category::kAlcohol:        return AlcoholModeSatisfied() && !AlcoholCooldownActive();
		default:                        return false;
		}
	}

	// Whether the Item Source setting admits items from this plugin.
	[[nodiscard]] inline bool SourceAllowed(Source a_source)
	{
		switch (Settings::itemSource) {
		case Settings::ItemSource::kBase:      return a_source == Source::kBaseGame;
		case Settings::ItemSource::kBaseAndCC: return a_source != Source::kOther;
		default:                               return true;
		}
	}

	// The three alcohol include-dropdowns, applied to one drink, as the three classes a drink can
	// belong to rather than as an Entry. Engine-free so the test stub can copy it verbatim and the
	// composition rules can be checked without a game.
	//
	// Every class the drink belongs to has to be included, so a healing, irradiated, top-shelf beer
	// needs all three set to something other than Off. That is the conservative direction: each
	// dropdown is the player saying "this kind of drink is fair game", and a drink that is three
	// kinds at once needs all three said.
	[[nodiscard]] inline bool AlcoholIncludedFor(bool a_heals, bool a_topShelf, bool a_addsRads)
	{
		using Inc = Settings::AlcoholInclude;

		return !(a_heals && Settings::alcoholHealing == Inc::kOff) &&
		       !(a_topShelf && Settings::alcoholTopShelf == Inc::kOff) &&
		       !(a_addsRads && Settings::alcoholIrradiated == Inc::kOff);
	}

	// How readily this drink is reached for, lowest first, among the drinks AlcoholIncludedFor has
	// already admitted. Solver::detail::PickReservedAlcohol compares this ahead of need-fit, so a
	// rank-2 drink is chosen only when nothing at rank 0 or 1 is carried.
	//
	//   0  in a class the player set to Preferred
	//   1  in none of the three classes: an ordinary drink, the baseline
	//   2  in a class the player set to Last Resort
	//
	// A drink in more than one class takes the most reluctant of them, so Preferred-healing and
	// Last-Resort-irradiated is a last resort. Same reasoning as the AND above: the dropdowns are
	// permissions, and the stingiest one the player gave is the one that binds.
	inline constexpr std::int32_t kAlcoholRankPreferred = 0;
	inline constexpr std::int32_t kAlcoholRankPlain = 1;
	inline constexpr std::int32_t kAlcoholRankLastResort = 2;

	[[nodiscard]] inline std::int32_t AlcoholRankFor(bool a_heals, bool a_topShelf, bool a_addsRads)
	{
		using Inc = Settings::AlcoholInclude;

		auto rank = kAlcoholRankPlain;
		auto inClass = false;

		const auto consider = [&](bool a_belongs, Inc a_setting) {
			// Off is AlcoholIncludedFor's business. Reaching it here would mean an excluded drink
			// is being ranked, so the only two values that can matter are the two that admit one.
			if (!a_belongs || a_setting == Inc::kOff) {
				return;
			}
			const auto classRank = a_setting == Inc::kLastResort ? kAlcoholRankLastResort :
			                                                       kAlcoholRankPreferred;
			rank = inClass ? std::max(rank, classRank) : classRank;
			inClass = true;
		};

		consider(a_heals, Settings::alcoholHealing);
		consider(a_topShelf, Settings::alcoholTopShelf);
		consider(a_addsRads, Settings::alcoholIrradiated);

		return rank;
	}

	// Non-alcohol is never filtered or ranked: it is admitted, and it sits at the neutral middle
	// rank that nothing outside PickReservedAlcohol ever reads.
	[[nodiscard]] inline bool AlcoholIncluded(const Entry& a_entry)
	{
		return a_entry.category != Category::kAlcohol ||
		       AlcoholIncludedFor(a_entry.RestoresHealth(), a_entry.shelf == Shelf::kTop,
		           a_entry.AddsRads());
	}

	[[nodiscard]] inline std::int32_t AlcoholRank(const Entry& a_entry)
	{
		return a_entry.category != Category::kAlcohol ?
		           kAlcoholRankPlain :
		           AlcoholRankFor(a_entry.RestoresHealth(), a_entry.shelf == Shelf::kTop,
		               a_entry.AddsRads());
	}

	// Whether this specific item may be spent at all: CategoryEnabled plus the rules a category on
	// its own cannot express.
	//
	// Special food that also carries radiation has to clear both gates. Eight vanilla items land
	// here, every Nuka-Cola among them: they heal, restore AP, and carry 5 rads. Classify() files
	// them under Special because the buff is what makes them worth saving, but a player who turned
	// Include Irradiated Food off did so to stop radiation arriving in their body.
	[[nodiscard]] inline bool Allowed(const Entry& a_entry)
	{
		if (!CategoryEnabled(a_entry.category)) {
			return false;
		}
		if (!AlcoholIncluded(a_entry)) {
			return false;
		}
		if (!SourceAllowed(a_entry.source)) {
			return false;
		}
		// Nothing that adds radiation is spent unless the player opted into it, and which setting
		// grants that depends on the category. Alcohol is exempt here because AlcoholIncluded above
		// already asked with its own toggle; gating it here as well would make Include Irradiated
		// Alcohol do nothing while Include Irradiated Food is Off.
		//
		// Everything else needs Include Irradiated Food, so a modded stimpak that irradiates does
		// too. A surprising exclusion is recoverable in one setting; a surprising dose of radiation
		// is not.
		if (a_entry.category != Category::kAlcohol && a_entry.AddsRads() &&
			Settings::foodIrradiated == Settings::IrradiatedFood::kOff) {
			return false;
		}
		return true;
	}
}
