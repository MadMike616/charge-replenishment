#pragma once

#include <D2RLPlugin/api.h>
#include <cstdint>
#include <vector>

namespace Recharge::Native {
struct ItemState {
    std::uint32_t frame{};
    std::uint32_t shift{};
    std::uint32_t mask{};
    std::uint32_t bank{};
    void* client{};
};
auto Initialize(const D2RL::PluginContext* context, bool requireStatReader) noexcept -> bool;
auto ReadStats(void* item, std::int32_t rateStat, std::vector<D2RL::Items::ItemStatValue>& stats) noexcept -> bool;
// All three operations are only called inside synchronous SDK editNativeItem callbacks.
auto Inspect(void* item, ItemState& state) noexcept -> bool;
auto Write(void* item, const ItemState& state, std::uint16_t layer, std::int32_t packed) noexcept -> bool;
}
