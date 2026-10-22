#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <tuple>
#include <vector>

#include "Inventory.h"
#include "PlayerState.h"
#include "Settings.h"

// Picks what to consume.
//
// The objective, in strict priority order:
//
//   1. Land inside the acceptable band. A plan that does not is only chosen when nothing does:
//      falling short of a target is never a reason to refuse to heal at all.
//   2. Least waste. How far the result sits from the target, on both axes, each normalised to a
//      percentage of its own scale so health and radiation are comparable.
//   3. Cheapest tier. Food before stimpaks. This sits above item count, and it is
//      the one step Item Order Preference moves: Prefer/Avoid Stimpak shift where the stimpak
//      sits in the tier order. See detail::Tier.
//   4. Fewest items.
//   5. Lowest inventory value.
//
// Reserved slots (crippled-limb stimpak, the one alcohol) come out of the budget first, and
// their healing and radiation count toward the targets: a Medic 4 stimpak spent on a crippled
// arm may finish the health job on its own.
namespace SH::Solver
{
	// Which axis a pick was chosen for. The consume path re-checks that axis before each item and
	// drops the rest of the picks that serve it once it is satisfied.
	enum class Reason : std::uint8_t
	{
		kReserved,   // crippled-limb stimpak or the one alcohol: consumed unconditionally
		kRadiation,
		kHealth
	};

	struct Pick
	{
		const Inventory::Available* item = nullptr;
		std::uint32_t               count = 0;
		Reason                      reason = Reason::kHealth;
	};

	struct Plan
	{
		std::vector<Pick> picks{};

		float finalHP = 0.0F;
		float finalRads = 0.0F;
		float finalCeiling = 0.0F;

		bool          hpSatisfied = false;
		bool          radSatisfied = false;
		std::uint32_t totalItems = 0;

		[[nodiscard]] bool Empty() const { return picks.empty(); }
	};

	// -------------------------------------------------------------------------------------------
	// Bands
	// -------------------------------------------------------------------------------------------
	//
	// A target T, a tolerance H, and which side of T a plan is allowed to land on.
	//
	//   Over   prefer the smallest final >= T; accept down to T - H only if nothing reaches T.
	//   Under  prefer the largest  final <= T; accept up   to T + H only if nothing reaches T.
	//
	// A band never decides whether to act. That falls out of the scoring, the empty plan being a
	// candidate like any other and winning whenever nothing scores better. An irradiated build set
	// to Over at 10% and sitting at 90% is still RadAway'd down toward 10% for the same reason.
	struct Band
	{
		float                target = 0.0F;
		float                tolerance = 0.0F;
		Settings::Direction  direction = Settings::Direction::kOver;
		float                scale = 1.0F;  // denominator that makes waste comparable across axes

		[[nodiscard]] bool Acceptable(float a_value) const
		{
			return direction == Settings::Direction::kOver ? a_value >= target - tolerance :
			                                                 a_value <= target + tolerance;
		}

		// The hysteresis slider says how close to the target counts as close enough. Inside the dead
		// zone everything scores zero waste, so a plan spending another item to close the last few
		// points ties on waste and loses on item count. At 0 the dead zone is the target itself.
		//
		//   Over  dead zone [T - H, T]
		//   Under dead zone [T, T + H]
		[[nodiscard]] bool GoodEnough(float a_value) const
		{
			return direction == Settings::Direction::kOver ?
			           (a_value >= target - tolerance && a_value <= target) :
			           (a_value >= target && a_value <= target + tolerance);
		}

		// Outside the dead zone, waste is the distance from the target, scaled up on the tolerated side
		// so a plan that lands short is beaten by a modest overshoot but still beats an absurd one.
		// Without that weighting, five stimpaks would be spent to avoid finishing one point short.
		[[nodiscard]] float Waste(float a_value) const
		{
			if (GoodEnough(a_value)) {
				return 0.0F;
			}
			const bool preferred = direction == Settings::Direction::kOver ? a_value >= target :
			                                                                 a_value <= target;
			const auto distance = std::abs(a_value - target);
			const auto weighted = preferred ? distance : distance * kToleratedSideWeight;
			return scale > 0.0F ? (weighted / scale) * 100.0F : weighted;
		}

		static constexpr float kToleratedSideWeight = 4.0F;
	};

	// The two bands, built from the current settings. Exposed so Consume can re-check an axis
	// between items without re-running the whole solve.
	[[nodiscard]] inline Band HPBand(float a_ceiling)
	{
		Band band{};
		band.target = PlayerState::HPTargetValue(a_ceiling);
		band.tolerance = PlayerState::HPToleranceValue(a_ceiling);
		band.direction = Settings::hpDirection;
		band.scale = std::max(a_ceiling, 1.0F);
		return band;
	}

	[[nodiscard]] inline Band RadBand(const PlayerState::State& a_state)
	{
		Band band{};
		band.target = PlayerState::RadTargetValue(a_state);
		band.tolerance = PlayerState::RadToleranceValue(a_state);
		band.direction = Settings::radDirection;
		band.scale = std::max(a_state.radScale, 1.0F);
		return band;
	}

	namespace detail
	{
		// ---------------------------------------------------------------------------------------
		// Scoring
		// ---------------------------------------------------------------------------------------
		struct Score
		{
			int          outsideBand = 0;   // how many axes ended outside their band (0, 1 or 2)
			float        waste = 0.0F;      // summed, both axes, as a percentage of their own scale
			std::int64_t tierKey = 0;       // packed per-tier item counts, see TierKey()
			std::int32_t items = 0;
			std::int64_t gold = 0;

			[[nodiscard]] bool BetterThan(const Score& a_other) const
			{
				// Waste is compared with a small epsilon so that two plans differing by float
				// noise fall through to the tier comparison rather than being decided by it.
				if (outsideBand != a_other.outsideBand) {
					return outsideBand < a_other.outsideBand;
				}
				if (std::abs(waste - a_other.waste) > 0.01F) {
					return waste < a_other.waste;
				}
				return std::tie(tierKey, items, gold) <
				       std::tie(a_other.tierKey, a_other.items, a_other.gold);
			}
		};

		// Item Order Preference moves the stimpak tier and nothing else. Aid has its own toggle.
		//
		// kPreferStimpak is conditional. The health phase clamps its result at the ceiling, so at
		// the default HP Target of 100 an overshoot scores no waste at all: one stimpak and one
		// apple both reach the target, and the tier is the only thing that picks the apple.
		// Promoting the stimpak unconditionally would therefore spend one on a single point of
		// damage. The half-stimpak rule promotes it only when the deficit is at least half of what
		// one copy heals.
		//
		// The deficit is the one measured at the start of the phase, not what is left part-way
		// through a plan. That asks "is this player hurt enough to reach for a stimpak at all",
		// which is the question the setting is for; a per-position test is not expressible in the
		// packed tier key anyway.
		//
		// Shared with Preferred healing alcohol; see EffectiveAlcoholRank.
		inline constexpr float kPreferStimpakUsefulFraction = 0.5F;

		[[nodiscard]] inline bool WorthReachingFor(float a_healEach, float a_deficit)
		{
			return a_healEach <= 0.0F || a_deficit >= a_healEach * kPreferStimpakUsefulFraction;
		}

		// How readily a category is spent, lowest first. This is what makes the mod eat ten pieces of
		// food rather than break out a stimpak:
		//
		//   0  irradiated or special food, when the player set it to Preferred
		//      stimpaks, under Prefer Stimpak, when the half-stimpak rule passes
		//   1  plain food, RadAway
		//   2  aid - blood packs and other medicine that is not a stimpak
		//   3  stimpaks
		//   4  irradiated food, when set to Last Resort
		//   5  special food, when set to Last Resort
		//   6  anything that fell through; stimpaks under Avoid Stimpak
		//
		// Last-resort special food sits above stimpaks and above last-resort irradiated food, a stimpak being
		// replaceable where a Deathclaw Steak's Fortify Agility is not. It has to sit in the tier order
		// rather than be scored, because waste is measured in health and radiation and a lost buff
		// shows up in neither. a_healEach and a_deficit are read only for stimpaks under Prefer
		// Stimpak; the radiation phase passes 0 for both, a stimpak never being a candidate there.
		[[nodiscard]] inline std::int32_t Tier(ItemCatalog::Category a_category, float a_healEach,
			float a_deficit)
		{
			switch (a_category) {
			case ItemCatalog::Category::kIrradiatedFood:
				// Preferred puts it ahead of everything, an irradiated build wanting the radiation it
				// brings. Last Resort puts it behind stimpaks, reached for only when nothing clean will do.
				return Settings::foodIrradiated == Settings::IrradiatedFood::kPreferred ? 0 : 4;
			case ItemCatalog::Category::kSpecialFood:
				return Settings::foodSpecial == Settings::SpecialFood::kPreferred ? 0 : 5;
			case ItemCatalog::Category::kFood:    return 1;
			case ItemCatalog::Category::kRadaway: return 1;
			// Aid sits below stimpaks: a blood pack is plentiful and a stimpak is not.
			case ItemCatalog::Category::kAid:     return 2;
			case ItemCatalog::Category::kStimpak:
				switch (Settings::itemOrder) {
				case Settings::ItemOrder::kPreferStimpak:
					return WorthReachingFor(a_healEach, a_deficit) ? 0 : 3;
				// Above last-resort special food, so everything else is spent first. Still spent when nothing
				// else reaches the target, waste outranking tier.
				case Settings::ItemOrder::kAvoidStimpak: return 6;
				default:                                 return 3;
				}
			default: return 6;
			}
		}

		// Packs "how many items of each tier" into one integer that sorts correctly. A plain sum of
		// tier weights grows with the number of items, so ten pieces of food (10 x 1) would score worse
		// than one stimpak plus one food (3 + 1). Counting per tier and comparing from the most
		// precious tier downward spends as few stimpaks as possible and only then cares how much food
		// it took. Six bits per tier covers the item budget's cap of 50; seven tiers is 42 of 63 bits.
		inline constexpr std::int32_t kTierCount = 7;
		inline constexpr std::int32_t kTierBits = 6;

		[[nodiscard]] inline std::int64_t TierKey(std::int32_t a_tier, std::uint32_t a_items)
		{
			const auto clamped = std::min<std::int64_t>(a_items, (1LL << kTierBits) - 1);
			return clamped << (kTierBits * std::clamp(a_tier, 0, kTierCount - 1));
		}

		// ---------------------------------------------------------------------------------------
		// Bounded knapsack over (progress on one axis) x (items spent)
		// ---------------------------------------------------------------------------------------
		//
		// Item count is part of the state rather than the cost, because Max Allowed Items Per Use is a
		// hard cap: a plan needing eleven items at a budget of ten is not a plan. That also leaves the
		// cost carrying only tier and value. Counts are binary-split (1, 2, 4, ... copies) so a stack
		// of forty stimpaks costs six pseudo-items rather than forty.
		struct Chunk
		{
			const Inventory::Available* item = nullptr;
			std::uint32_t               copies = 0;
			std::int32_t                axisStep = 0;  // quantised progress, positive
			std::int64_t                tierKey = 0;   // packed tier count for these copies
			std::int64_t                gold = 0;
			float                       rads = 0.0F;   // side effect on the other axis
			float                       exact = 0.0F;  // unquantised progress, for the final totals
		};

		struct Cell
		{
			bool         reachable = false;
			std::int64_t tierKey = 0;
			std::int64_t gold = 0;
			float        rads = 0.0F;
			float        exact = 0.0F;
			std::int32_t fromChunk = -1;
		};

		// Quantised so the table stays small whatever the character's maximum health is. 512 buckets
		// keeps a modded 1500-HP character to about three health points per bucket.
		inline constexpr std::int32_t kAxisBuckets = 512;

		class Knapsack
		{
		public:
			// a_axisRange is the largest amount of progress worth considering on this axis; going
			// past it can only add waste.
			Knapsack(const std::vector<Chunk>& a_chunks, float a_axisRange, std::int32_t a_budget, float a_quantum) :
				_chunks(a_chunks),
				_budget(std::max(a_budget, 0)),
				_quantum(a_quantum),
				_axisMax(static_cast<std::int32_t>(std::ceil(a_axisRange / a_quantum)))
			{
				_axisMax = std::clamp(_axisMax, 0, kAxisBuckets);
				_stride = _budget + 1;
				_cells.assign(static_cast<std::size_t>(_axisMax + 1) * _stride, Cell{});
				At(0, 0).reachable = true;
				Run();
			}

			[[nodiscard]] std::int32_t AxisMax() const { return _axisMax; }
			[[nodiscard]] std::int32_t Budget() const { return _budget; }
			[[nodiscard]] const Cell&  At(std::int32_t a_axis, std::int32_t a_items) const
			{
				return _cells[static_cast<std::size_t>(a_axis) * _stride + a_items];
			}

			// Walks the back-pointers and merges repeated picks of the same item into one entry.
			[[nodiscard]] std::vector<Pick> Reconstruct(std::int32_t a_axis, std::int32_t a_items) const
			{
				std::vector<Pick> picks;
				auto              axis = a_axis;
				auto              items = a_items;

				while (axis > 0 || items > 0) {
					const auto& cell = At(axis, items);
					if (cell.fromChunk < 0) {
						break;
					}
					const auto& chunk = _chunks[static_cast<std::size_t>(cell.fromChunk)];
					if (const auto existing = std::ranges::find_if(picks,
							[&](const Pick& a_pick) { return a_pick.item == chunk.item; });
						existing != picks.end()) {
						existing->count += chunk.copies;
					} else {
						picks.push_back(Pick{ chunk.item, chunk.copies });
					}
					axis -= chunk.axisStep;
					items -= static_cast<std::int32_t>(chunk.copies);
				}
				return picks;
			}

		private:
			Cell& At(std::int32_t a_axis, std::int32_t a_items)
			{
				return _cells[static_cast<std::size_t>(a_axis) * _stride + a_items];
			}

			void Run()
			{
				for (std::size_t index = 0; index < _chunks.size(); ++index) {
					const auto& chunk = _chunks[index];
					const auto  copies = static_cast<std::int32_t>(chunk.copies);
					if (chunk.axisStep <= 0 || copies > _budget) {
						continue;
					}
					// Descending on both dimensions is what keeps this 0/1 per chunk: a cell can
					// never be reached twice through the same chunk in one pass.
					for (std::int32_t axis = _axisMax; axis >= chunk.axisStep; --axis) {
						for (std::int32_t items = _budget; items >= copies; --items) {
							const auto& from = At(axis - chunk.axisStep, items - copies);
							if (!from.reachable) {
								continue;
							}
							Cell candidate{};
							candidate.reachable = true;
							candidate.tierKey = from.tierKey + chunk.tierKey;
							candidate.gold = from.gold + chunk.gold;
							candidate.rads = from.rads + chunk.rads;
							candidate.exact = from.exact + chunk.exact;
							candidate.fromChunk = static_cast<std::int32_t>(index);

							auto& target = At(axis, items);
							if (!target.reachable ||
								std::tie(candidate.tierKey, candidate.gold) < std::tie(target.tierKey, target.gold)) {
								target = candidate;
							}
						}
					}
				}
			}

			const std::vector<Chunk>& _chunks;
			std::int32_t              _budget = 0;
			float                     _quantum = 1.0F;
			std::int32_t              _axisMax = 0;
			std::int32_t              _stride = 1;
			std::vector<Cell>         _cells;
		};

		// Splits a stack into 1, 2, 4, ... copies so the knapsack sees log(count) pseudo-items.
		// a_deficit is the shortfall on the axis being solved, forwarded to Tier() for the
		// half-stimpak rule. The radiation phase passes 0; see Tier().
		inline void AddChunks(std::vector<Chunk>& a_out, const Inventory::Available& a_item,
			float a_progressEach, float a_quantum, std::int32_t a_budget, std::uint32_t a_available,
			float a_deficit)
		{
			const auto step = static_cast<std::int32_t>(std::lround(a_progressEach / a_quantum));
			if (step <= 0) {
				return;  // too small to register on this axis at this resolution
			}

			const auto tier = Tier(a_item.Category(), a_item.hp, a_deficit);
			auto       remaining = std::min<std::uint32_t>(a_available, static_cast<std::uint32_t>(std::max(a_budget, 0)));

			for (std::uint32_t copies = 1; remaining > 0; copies *= 2) {
				const auto take = std::min(copies, remaining);
				a_out.push_back(Chunk{
					&a_item,
					take,
					step * static_cast<std::int32_t>(take),
					TierKey(tier, take),
					static_cast<std::int64_t>(a_item.goldValue) * take,
					a_item.rads * static_cast<float>(take),
					a_progressEach * static_cast<float>(take) });
				remaining -= take;
			}
		}

		[[nodiscard]] inline float Quantum(float a_range)
		{
			return std::max(a_range / static_cast<float>(kAxisBuckets), 1.0F);
		}

		// The single item for a reserved slot: least wasteful that qualifies, then cheapest. A reserved
		// stimpak still has to mend the limb, so among those that can, the one healing least wastes least.
		[[nodiscard]] inline std::size_t PickReserved(
			const std::vector<Inventory::Available>& a_available,
			const std::vector<std::uint32_t>&        a_remaining,
			ItemCatalog::Category                    a_category,
			bool                                     a_mustMendLimb)
		{
			auto best = a_available.size();
			for (std::size_t index = 0; index < a_available.size(); ++index) {
				const auto& candidate = a_available[index];
				if (a_remaining[index] == 0 || candidate.Category() != a_category) {
					continue;
				}
				if (a_mustMendLimb && !candidate.mendsLimb) {
					continue;
				}
				if (best == a_available.size() ||
					std::tie(candidate.hp, candidate.goldValue) <
						std::tie(a_available[best].hp, a_available[best].goldValue)) {
					best = index;
				}
			}
			return best;
		}

		// Where one drink sits against the others, once the deficit is known. The rank itself comes
		// from the Include dropdowns and is settled at snapshot time (ItemCatalog::AlcoholRankFor);
		// the only thing decided here is the guard on Preferred.
		//
		// A drink that restores health is always in the healing class, so its rank is never the
		// plain middle - it is Preferred or Last Resort. Preferred, unguarded, therefore means
		// "always drink the healing one first", which at full health spends a 1500 HP beer to get a
		// +1 Strength buff. That is the Prefer Stimpak hazard exactly, and it takes the same
		// answer: the promotion only holds when the shortfall is at least half of what the drink
		// heals. Demoted, it drops to the plain rank rather than out of the pool - still ahead of
		// any last-resort drink, just no longer ahead of an ordinary beer.
		//
		// The guard tests the healing, not which class earned the promotion. A top-shelf drink that
		// also heals is subject to it for the same reason: what is being wasted is the healing.
		[[nodiscard]] inline std::int32_t EffectiveAlcoholRank(
			const Inventory::Available& a_candidate, float a_deficit)
		{
			if (a_candidate.alcoholRank != ItemCatalog::kAlcoholRankPreferred ||
				a_candidate.hp <= 0.0F || WorthReachingFor(a_candidate.hp, a_deficit)) {
				return a_candidate.alcoholRank;
			}
			return ItemCatalog::kAlcoholRankPlain;
		}

		// The alcohol reserved slot: the one drink a press may spend. Which drinks are eligible has
		// already been settled by ItemCatalog::Allowed, so the only question is which one.
		//
		// Rank first, then the drink whose healing best fits the shortfall, then the cheapest.
		// Need-fit under rank rather than over it is what makes the dropdowns mean anything: on
		// distance alone a top-shelf whiskey wins any press where its healing happens to fit better,
		// however the player set it. Unhurt, the deficit is 0 and the drink closest to no healing
		// wins, saving the healing ones. Hurt, commitReserved folds the chosen drink's healing into
		// the running total, so the health phase plans for what is actually left.
		//
		// Alcohol is not a candidate in either knapsack. "Always 1" is a hard cap of one drink per
		// press, and a bounded knapsack cannot express "at most one item from this whole group": it
		// would have to cap each stack at one copy and could still take one from each of several
		// drinks.
		[[nodiscard]] inline std::size_t PickReservedAlcohol(
			const std::vector<Inventory::Available>& a_available,
			const std::vector<std::uint32_t>&        a_remaining,
			float                                    a_deficit)
		{
			auto         best = a_available.size();
			std::int32_t bestRank = 0;
			float        bestDistance = 0.0F;

			for (std::size_t index = 0; index < a_available.size(); ++index) {
				const auto& candidate = a_available[index];
				if (a_remaining[index] == 0 ||
					candidate.Category() != ItemCatalog::Category::kAlcohol) {
					continue;
				}
				const auto rank = EffectiveAlcoholRank(candidate, a_deficit);
				const auto distance = std::abs(candidate.hp - a_deficit);
				if (best == a_available.size() ||
					std::tie(rank, distance, candidate.goldValue) <
						std::tie(bestRank, bestDistance, a_available[best].goldValue)) {
					best = index;
					bestRank = rank;
					bestDistance = distance;
				}
			}
			return best;
		}

		// Merges only within one reason. An item that is both the reserved limb stimpak and part of the
		// health plan stays two entries: the reserved one is unconditional, the other is dropped if
		// health is already satisfied by the time its turn comes.
		inline void AddPick(std::vector<Pick>& a_picks, const Inventory::Available* a_item,
			std::uint32_t a_count, Reason a_reason)
		{
			if (!a_item || a_count == 0) {
				return;
			}
			if (const auto existing = std::ranges::find_if(a_picks,
					[&](const Pick& a_pick) { return a_pick.item == a_item && a_pick.reason == a_reason; });
				existing != a_picks.end()) {
				existing->count += a_count;
			} else {
				a_picks.push_back(Pick{ a_item, a_count, a_reason });
			}
		}

		// Index of an item within the availability vector, used to decrement its remaining count.
		[[nodiscard]] inline std::size_t IndexOf(
			const std::vector<Inventory::Available>& a_available, const Inventory::Available* a_item)
		{
			return static_cast<std::size_t>(a_item - a_available.data());
		}
	}

	// -----------------------------------------------------------------------------------------
	// Solve
	// -----------------------------------------------------------------------------------------
	[[nodiscard]] inline std::optional<Plan> Solve(
		const PlayerState::State&                a_state,
		const std::vector<Inventory::Available>& a_available)
	{
		using namespace detail;

		if (!a_state.valid) {
			return std::nullopt;
		}

		const auto radScale = std::max(a_state.radScale, 1.0F);
		const auto radBand = RadBand(a_state);

		// How many of each stack are still unspent. Every phase decrements this, so a single
		// stimpak reserved for a crippled limb is not also counted by the health phase.
		std::vector<std::uint32_t> remaining;
		remaining.reserve(a_available.size());
		for (const auto& candidate : a_available) {
			remaining.push_back(candidate.count);
		}

		// Projected values, never raw current ones; see PlayerState::State.
		float rads = a_state.ProjectedRads();
		float hp = a_state.curHP + a_state.pendingHP;

		std::vector<Pick> picks;
		auto              budget = Settings::iMaxItems;

		const auto commitReserved = [&](std::size_t a_index) {
			if (a_index >= a_available.size()) {
				return;
			}
			const auto& item = a_available[a_index];
			AddPick(picks, &item, 1, Reason::kReserved);
			--remaining[a_index];
			hp += item.hp;
			rads += item.rads;
			--budget;
		};

		const auto reserve = [&](ItemCatalog::Category a_category, bool a_mustMendLimb) {
			if (budget <= 0) {
				return;
			}
			commitReserved(PickReserved(a_available, remaining, a_category, a_mustMendLimb));
		};

		// -- Reserved slots, before anything else, so the smallest budget still honours them.
		if (Settings::bStimpakOnCrippled && a_state.anyLimbCrippled &&
			ItemCatalog::CategoryEnabled(ItemCatalog::Category::kStimpak)) {
			const auto before = picks.size();
			reserve(ItemCatalog::Category::kStimpak, true);
			if (picks.size() == before) {
				REX::WARN("Smart Heal Button: limb crippled and Always Use Stimpak is ON, but nothing in "
				          "the inventory can mend a limb - no stimpak reserved"sv);
			}
		}
		// One drink per press, when the dropdown's condition holds. CategoryEnabled has already
		// answered that - including the combat check and the cooldown - by keeping alcohol out of
		// a_available entirely, so reaching this with an eligible drink present means it is wanted.
		// Carrying nothing eligible is diagnosed in Inventory::Snapshot, the only place that can
		// tell "no alcohol at all" from "alcohol, but all of it excluded by the Include settings".
		if (Settings::alcohol != Settings::Alcohol::kOff && budget > 0) {
			// The shortfall as it stands before the radiation and health phases run, which is an
			// estimate: clearing radiation later raises the ceiling and so the target. It only
			// decides which drink to reach for.
			const auto reserveCeiling = a_state.maxHP * (1.0F - (rads / radScale));
			const auto reserveDeficit =
				std::max(HPBand(reserveCeiling).target - std::min(hp, reserveCeiling), 0.0F);
			commitReserved(PickReservedAlcohol(a_available, remaining, reserveDeficit));
		}

		// -- Radiation phase, settled first because clearing radiation raises the health ceiling and
		//    the HP target is a percentage of that ceiling. Any item that removes radiation qualifies,
		//    not just RadAway: Mutant Hound Chops remove 50 rads and heal 60, and several mod-added
		//    foods do the same. Whatever healing they carry is folded into `hp` below, so the health
		//    phase sees a smaller deficit rather than double-counting them.
		if (Settings::bHealRadiation && budget > 0) {
			// Alcohol is the reserved slot's alone; see PickReservedAlcohol.
			const auto removesRads = [&](std::size_t a_index) {
				return a_available[a_index].rads < 0.0F && remaining[a_index] > 0 &&
				       a_available[a_index].Category() != ItemCatalog::Category::kAlcohol;
			};

			float maxRemoval = 0.0F;
			float largestRemoval = 0.0F;
			for (std::size_t index = 0; index < a_available.size(); ++index) {
				const auto& candidate = a_available[index];
				if (!removesRads(index)) {
					continue;
				}
				const auto usable = std::min<std::uint32_t>(remaining[index], static_cast<std::uint32_t>(budget));
				maxRemoval += -candidate.rads * static_cast<float>(usable);
				largestRemoval = std::max(largestRemoval, -candidate.rads);
			}

			// The axis stretches one item past the amount needed, or the last RadAway that would take the
			// player cleanly to zero is not representable: 500 rads with two 300-rad RadAways available
			// would otherwise produce one RadAway and 200 rads left over.
			const auto usefulRange = std::min(maxRemoval, rads + radBand.tolerance + largestRemoval);

			if (usefulRange > 0.0F) {
				const auto         quantum = Quantum(usefulRange);
				std::vector<Chunk> chunks;
				for (std::size_t index = 0; index < a_available.size(); ++index) {
					const auto& candidate = a_available[index];
					if (!removesRads(index)) {
						continue;
					}
					AddChunks(chunks, candidate, -candidate.rads, quantum, budget, remaining[index], 0.0F);
				}

				if (!chunks.empty()) {
					const Knapsack knapsack{ chunks, usefulRange, budget, quantum };

					// The empty plan is a candidate like any other, which is what decides whether
					// radiation is worth spending anything on at all.
					Score        best{};
					best.outsideBand = radBand.Acceptable(rads) ? 0 : 1;
					best.waste = radBand.Waste(rads);
					std::int32_t bestAxis = 0;
					std::int32_t bestItems = 0;

					for (std::int32_t axis = 0; axis <= knapsack.AxisMax(); ++axis) {
						for (std::int32_t items = 0; items <= knapsack.Budget(); ++items) {
							const auto& cell = knapsack.At(axis, items);
							if (!cell.reachable) {
								continue;
							}
							const auto result = std::max(rads - cell.exact, 0.0F);
							Score      score{};
							score.outsideBand = radBand.Acceptable(result) ? 0 : 1;
							score.waste = radBand.Waste(result);
							score.tierKey = cell.tierKey;
							score.items = items;
							score.gold = cell.gold;
							if (score.BetterThan(best)) {
								best = score;
								bestAxis = axis;
								bestItems = items;
							}
						}
					}

					if (bestItems > 0) {
						rads = std::max(rads - knapsack.At(bestAxis, bestItems).exact, 0.0F);
						for (const auto& pick : knapsack.Reconstruct(bestAxis, bestItems)) {
							AddPick(picks, pick.item, pick.count, Reason::kRadiation);
							const auto index = IndexOf(a_available, pick.item);
							remaining[index] -= std::min(remaining[index], pick.count);
							// Whatever these happened to heal counts too, so the health phase
							// plans for the deficit that is actually left.
							hp += pick.item->hp * static_cast<float>(pick.count);
						}
						budget -= bestItems;
					}
				}
			}
		}

		// -- Health phase, against the ceiling the radiation phase just produced.
		const auto ceiling = a_state.maxHP * (1.0F - (rads / radScale));
		const auto hpBand = HPBand(ceiling);
		const auto hpDeficit = std::max(hpBand.target - std::min(hp, ceiling), 0.0F);

		if (budget > 0 && hpDeficit > 0.0F) {
			// Two runs. An item that heals and adds radiation moves both axes at once, so the solve runs
			// with those items and without, and the full score, which prices radiation, picks the winner.
			// The test is "does it add radiation", not "is it irradiated food": every Nuka-Cola heals,
			// buffs and carries 5 rads, and Classify files those under Special.
			const auto addsRads = [&](std::size_t a_index) { return a_available[a_index].rads > 0.0F; };

			const auto usable = [&](std::size_t a_index, bool a_withRadAdders) {
				const auto& candidate = a_available[a_index];
				if (candidate.hp <= 0.0F || remaining[a_index] == 0) {
					return false;
				}
				const auto category = candidate.Category();
				if (category == ItemCatalog::Category::kAlcohol) {
					// Reserved slot only; see PickReservedAlcohol. Its healing is already in `hp`.
					return false;
				}
				// Inventory has already applied this; repeated here so the solver decides for
				// itself what it is allowed to spend.
				if (!ItemCatalog::CategoryEnabled(category)) {
					return false;
				}
				if (!a_withRadAdders && addsRads(a_index)) {
					return false;
				}
				return true;
			};

			bool anyRadAdder = false;
			for (std::size_t index = 0; index < a_available.size(); ++index) {
				if (usable(index, true) && addsRads(index)) {
					anyRadAdder = true;
					break;
				}
			}

			Score best{};
			best.outsideBand = (hpBand.Acceptable(hp) ? 0 : 1) + (radBand.Acceptable(rads) ? 0 : 1);
			best.waste = hpBand.Waste(hp) + radBand.Waste(rads);
			std::vector<Pick> bestPicks;
			float             bestHP = hp;
			float             bestRads = rads;

			for (int pass = 0; pass < (anyRadAdder ? 2 : 1); ++pass) {
				const bool withIrradiated = (pass == 0);

				float largestHeal = 0.0F;
				for (std::size_t index = 0; index < a_available.size(); ++index) {
					if (usable(index, withIrradiated)) {
						largestHeal = std::max(largestHeal, a_available[index].hp);
					}
				}

				// Overshooting by more than one item's worth is never the least-wasteful answer.
				const auto usefulRange = hpDeficit + largestHeal;
				if (usefulRange <= 0.0F) {
					continue;
				}
				const auto quantum = Quantum(usefulRange);

				std::vector<Chunk> chunks;
				for (std::size_t index = 0; index < a_available.size(); ++index) {
					if (usable(index, withIrradiated)) {
						AddChunks(chunks, a_available[index], a_available[index].hp, quantum, budget,
							remaining[index], hpDeficit);
					}
				}
				if (chunks.empty()) {
					continue;
				}

				const Knapsack knapsack{ chunks, usefulRange, budget, quantum };

				for (std::int32_t axis = 0; axis <= knapsack.AxisMax(); ++axis) {
					for (std::int32_t items = 1; items <= knapsack.Budget(); ++items) {
						const auto& cell = knapsack.At(axis, items);
						if (!cell.reachable) {
							continue;
						}
						const auto resultRads = std::clamp(rads + cell.rads, 0.0F, radScale);
						// Radiation the plan itself adds lowers the ceiling it is healing toward.
						const auto resultCeiling = a_state.maxHP * (1.0F - (resultRads / radScale));
						const auto resultHP = std::min(hp + cell.exact, resultCeiling);

						Score score{};
						score.outsideBand = (hpBand.Acceptable(resultHP) ? 0 : 1) +
						                    (radBand.Acceptable(resultRads) ? 0 : 1);
						score.waste = hpBand.Waste(resultHP) + radBand.Waste(resultRads);
						score.tierKey = cell.tierKey;
						score.items = items;
						score.gold = cell.gold;

						if (score.BetterThan(best)) {
							best = score;
							bestPicks = knapsack.Reconstruct(axis, items);
							bestHP = resultHP;
							bestRads = resultRads;
						}
					}
				}
			}

			for (const auto& pick : bestPicks) {
				AddPick(picks, pick.item, pick.count, Reason::kHealth);
				const auto index = IndexOf(a_available, pick.item);
				remaining[index] -= std::min(remaining[index], pick.count);
			}
			hp = bestHP;
			rads = bestRads;
		}

		if (picks.empty()) {
			if (Settings::debugLog) {
				REX::DEBUG("Smart Heal Button: [SOLVER] nothing worth spending - hp={:.1f} rads={:.1f}"sv, hp, rads);
			}
			return std::nullopt;
		}

		Plan plan{};
		plan.picks = std::move(picks);
		plan.finalRads = rads;
		plan.finalCeiling = a_state.maxHP * (1.0F - (rads / radScale));
		plan.finalHP = std::min(hp, plan.finalCeiling);
		plan.hpSatisfied = hpBand.Acceptable(plan.finalHP);
		plan.radSatisfied = radBand.Acceptable(plan.finalRads);
		for (const auto& pick : plan.picks) {
			plan.totalItems += pick.count;
		}

		if (Settings::debugLog) {
			REX::DEBUG("Smart Heal Button: [SOLVER] {} item(s) -> hp={:.1f}/{:.1f} (target {:.1f}, {}) "
			          "rads={:.1f} (target {:.1f}, {})"sv,
				plan.totalItems, plan.finalHP, plan.finalCeiling, hpBand.target,
				plan.hpSatisfied ? "in band"sv : "OUT OF BAND"sv,
				plan.finalRads, radBand.target,
				plan.radSatisfied ? "in band"sv : "OUT OF BAND"sv);
			for (const auto& pick : plan.picks) {
				REX::DEBUG("Smart Heal Button: [SOLVER]   x{} {} (reason={} cat={} hp={:.1f} rads={:.1f})"sv,
					pick.count, pick.item->Name(),
					pick.reason == Reason::kReserved ? "reserved"sv :
						pick.reason == Reason::kRadiation ? "radiation"sv : "health"sv,
					ItemCatalog::CategoryName(pick.item->Category()),
					pick.item->hp, pick.item->rads);
			}
		}

		return plan;
	}
}
