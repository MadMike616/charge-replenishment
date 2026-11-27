#pragma once

#include <algorithm>
#include <cstdint>

namespace Recharge {
inline constexpr std::uint32_t ChargedSkillStat = 204;
inline constexpr std::uint32_t RepairRateStat = 252;
inline constexpr std::uint32_t RateDenominator = 2500; // 25 game frames/s * 100 seconds.
inline constexpr std::int32_t MaximumRate = 1023;

struct Charges {
    std::uint32_t current{};
    std::uint32_t maximum{};
    static constexpr auto Decode(std::int32_t packed) noexcept -> Charges {
        if (packed < 0 || packed > 0xFFFF) return {};
        return {static_cast<std::uint32_t>(packed) & 255U,
                static_cast<std::uint32_t>(packed) >> 8U};
    }
    constexpr auto Valid() const noexcept -> bool {
        return maximum > 0 && maximum <= 255 && current <= maximum;
    }
    constexpr auto Pack() const noexcept -> std::int32_t {
        return static_cast<std::int32_t>((maximum << 8U) | current);
    }
};

// One timer per item identity and charged-skill layer. No game pointers are retained.
struct Timer {
    std::uint32_t frame{};
    std::uint32_t credit{};
    std::int32_t rate{};
    std::uint32_t maximum{};
    bool running{};

    auto Advance(std::uint32_t now, std::int32_t newRate, Charges charges) noexcept -> Charges {
        if (!charges.Valid() || newRate <= 0 || newRate > MaximumRate || charges.current == charges.maximum) {
            *this = {};
            return charges;
        }
        if (!running || rate != newRate || maximum != charges.maximum) {
            *this = {now, 0, newRate, charges.maximum, true};
            return charges;
        }
        const auto elapsed = static_cast<std::uint32_t>(now - frame);
        frame = now;
        // Unsigned subtraction admits normal frame wrap, but rejects a reset/backward clock.
        if (elapsed > 0x7FFFFFFFU) {
            credit = 0;
            return charges;
        }
        const auto total = static_cast<std::uint64_t>(credit)
            + static_cast<std::uint64_t>(elapsed) * static_cast<std::uint32_t>(rate);
        const auto restored = std::min<std::uint64_t>(total / RateDenominator,
            charges.maximum - charges.current);
        credit = static_cast<std::uint32_t>(total % RateDenominator);
        charges.current += static_cast<std::uint32_t>(restored);
        if (charges.current == charges.maximum) *this = {};
        return charges;
    }
};
}
