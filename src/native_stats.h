#pragma once

#include "recharge_policy.h"
#include <D2RLPlugin/item.h>
#include <array>
#include <cstddef>
#include <vector>

namespace Recharge::Native {
// D2RCore's admitted stat-copy implementation widens the legacy game's records.
struct StatRecord { std::uint32_t layer, stat; std::int64_t value; };
static_assert(sizeof(StatRecord) == 16 && offsetof(StatRecord, stat) == 4 && offsetof(StatRecord, value) == 8);
using GetStatFn = std::int32_t (__fastcall*)(void*, std::int32_t, std::uint32_t) noexcept;
using CopyStatFn = std::int32_t (__fastcall*)(void*, std::int32_t, StatRecord*, std::int32_t) noexcept;

// GetStat/CopyStat must already be admitted against the active native profile.
inline auto CollectStats(void* item, std::int32_t rateStat, GetStatFn getStat, CopyStatFn copyStat,
                         std::vector<D2RL::Items::ItemStatValue>& stats) noexcept -> bool {
    stats.clear();
    if (!item || !getStat || !copyStat || rateStat < 398 || rateStat > 510) return false;
    try {
        const auto rate = getStat(item, rateStat, 0);
        stats.push_back({static_cast<std::uint32_t>(rateStat), 0, rate, 0});
        if (rate <= 0 || rate > MaximumRate) return true;
        std::array<StatRecord, 128> records{};
        const auto count = copyStat(item, ChargedSkillStat, records.data(), static_cast<std::int32_t>(records.size()));
        // A completely filled buffer might be truncated: reject rather than guess.
        if (count < 0 || count >= static_cast<std::int32_t>(records.size())) return false;
        for (std::int32_t i = 0; i < count; ++i) {
            const auto& record = records[static_cast<std::size_t>(i)];
            if (record.stat != ChargedSkillStat || record.layer > 0xFFFF || record.value < 0 || record.value > 0xFFFF) return false;
            stats.push_back({record.stat, record.layer, static_cast<std::int32_t>(record.value), 0});
        }
        return true;
    } catch (...) { stats.clear(); return false; }
}
}
