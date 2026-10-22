#pragma once

#include <format>
#include <string>

#include "Consume.h"
#include "Inventory.h"
#include "ItemCatalog.h"
#include "Notify.h"
#include "PlayerState.h"
#include "Settings.h"
#include "Solver.h"

// The whole feature in one call: read the player, read the inventory, work out the plan, use it,
// say what happened. InputHook.h calls Trigger() and nothing else.
namespace SH::SmartHeal
{
	inline void Trigger()
	{
		const auto state = PlayerState::Read();
		if (!state.valid) {
			REX::WARN("Smart Heal Button: player state unreadable - press ignored"sv);
			return;
		}

		// The snapshot is a local, and Solver::Plan holds pointers into it, so the plan cannot
		// outlive this function. Consume::Begin copies out the only thing it needs (the
		// AlchemyItem forms, which are permanent) before that happens.
		const auto available = Inventory::Snapshot(state);

		const auto plan = Solver::Solve(state, available);
		if (!plan) {
			Notify::Show(available.empty() ? Notify::NoItems() : Notify::Nothing());
			return;
		}

		// Consumption is spread over frames and re-checked between items, so the message reporting
		// what was used is emitted by Consume when the queue drains rather than here: the
		// re-check can end up using less than was planned.
		Consume::Begin(*plan, state);
	}
}
