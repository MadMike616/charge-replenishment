#include "native.h"
#include "recharge_policy.h"
#include "native_stats.h"

#include <array>
#include <cstring>
#include <windows.h>

namespace Recharge::Native {
namespace {
std::uintptr_t Base{};
bool StatReaderValidated{};
using PointerFn = void* (__fastcall*)(void*) noexcept;
using OwnerFn = std::uint32_t (__fastcall*)(void*) noexcept;
using BankFn = std::uint8_t (__fastcall*)(void*) noexcept;
using TablesFn = void* (__fastcall*)(std::uint8_t) noexcept;
using FindFn = void* (__fastcall*)(void*, std::int32_t, std::uint32_t) noexcept;
using ChargeFn = std::int32_t (__fastcall*)(void*, std::int32_t, std::int32_t, std::int32_t) noexcept;
// The loader's admitted packet adapter reads a full DWORD for the final layer.
using NotifyFn = void (__fastcall*)(void*, void*, bool, std::int32_t, std::int32_t, std::uint32_t) noexcept;

template<class T> auto Function(std::uintptr_t rva) noexcept -> T {
    return reinterpret_cast<T>(Base + rva);
}
auto Read(const void* address, void* output, std::size_t size) noexcept -> bool {
    MEMORY_BASIC_INFORMATION region{};
    if (!address || VirtualQuery(address, &region, sizeof(region)) != sizeof(region)
        || region.State != MEM_COMMIT || (region.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return false;
    const auto at = reinterpret_cast<std::uintptr_t>(address);
    const auto end = reinterpret_cast<std::uintptr_t>(region.BaseAddress) + region.RegionSize;
    if (at > end || size > end - at) return false;
    std::memcpy(output, address, size);
    return true;
}
template<class T> auto ReadAt(const void* address, std::size_t offset, T& output) noexcept -> bool {
    return Read(static_cast<const std::uint8_t*>(address) + offset, &output, sizeof(output));
}
template<std::size_t N> auto Check(const D2RL::PluginContext* ctx, std::uintptr_t rva,
                                  const std::array<std::uint8_t, N>& bytes) noexcept -> bool {
    const auto matched = ctx->CheckExpectedBytes(rva, bytes.data(), static_cast<std::uint32_t>(N));
    if (!matched) D2RL::LogErrorF(ctx, "Repair Charge Replenishment: helper/layout signature differs at RVA 0x%llX; native access disabled.", static_cast<unsigned long long>(rva));
    return matched;
}
// D2RLoader replaces these two native entries with RIP-indirect jumps into D2RCore.
// Admit only that known owner and the untouched native body, never an unknown detour.
auto CoreThunkTarget(std::uintptr_t rva) noexcept -> void* {
    std::array<std::uint8_t, 6> entry{};
    if (!Read(reinterpret_cast<void*>(Base + rva), entry.data(), entry.size())
        || entry[0] != 0xFF || entry[1] != 0x25) return nullptr;
    std::int32_t displacement{};
    std::memcpy(&displacement, entry.data() + 2, sizeof(displacement));
    const auto slot = static_cast<std::intptr_t>(Base + rva + 6) + displacement;
    void* target{};
    if (!Read(reinterpret_cast<void*>(slot), &target, sizeof(target))) return nullptr;
    HMODULE owner{};
    const bool knownOwner = target && GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
        | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(target), &owner)
        && owner == GetModuleHandleW(L"D2RCore.dll");
    return knownOwner ? target : nullptr;
}
auto RequireCoreThunk(const D2RL::PluginContext* ctx, std::uintptr_t rva) noexcept -> bool {
    if (CoreThunkTarget(rva)) return true;
    D2RL::LogErrorF(ctx, "Repair Charge Replenishment: expected a D2RCore-owned native thunk at RVA 0x%llX; native access disabled.", static_cast<unsigned long long>(rva));
    return false;
}
template<std::size_t N>
auto TargetBytes(void* target, std::size_t offset, const std::array<std::uint8_t, N>& expected) noexcept -> bool {
    std::array<std::uint8_t, N> actual{};
    return target && Read(static_cast<const std::uint8_t*>(target) + offset, actual.data(), N) && actual == expected;
}
auto ValidateWideStatCopy(const D2RL::PluginContext* ctx) noexcept -> bool {
    auto* target = CoreThunkTarget(0x2FA060);
    // Check the actual implementation reached by the native copy helper. Its
    // original game body is bypassed and cannot establish the output ABI.
    const bool valid = TargetBytes(target, 0, std::to_array<std::uint8_t>({0x41,0x57,0x41,0x56,0x41,0x54,0x56,0x57,0x53,0x31,0xC0,0x45,0x85,0xC9,0x0F,0x8E,0xD4,0x00,0x00,0x00}))
        && TargetBytes(target, 0x7F, std::to_array<std::uint8_t>({0x49,0xC1,0xE4,0x04,0x4A,0x39,0x3C,0x23}))
        && TargetBytes(target, 0xC2, std::to_array<std::uint8_t>({0x0F,0x10,0x07,0x41,0x0F,0x11,0x00,0x4A,0x3B,0x5C,0x11,0x08,0x73,0x15,0x48,0xFF,0xC0,0x48,0x83,0xC6,0x10,0x49,0x83,0xC0,0x10}));
    if (!valid) ctx->LogError("Repair Charge Replenishment: the active stat-copy implementation does not match the validated 16-byte record layout; native stat inspection is disabled.");
    return valid;
}
}

auto Initialize(const D2RL::PluginContext* ctx, bool requireStatReader) noexcept -> bool {
    Base = ctx->exeBase;
    StatReaderValidated = false;
    bool valid = Base
        && Check(ctx, 0x48FF00, std::to_array<std::uint8_t>({0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,0xD9,0x48,0x85,0xC9,0x75,0x20,0x88,0x4C}))
        && Check(ctx, 0x371D00, std::to_array<std::uint8_t>({0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,0xD9,0x48,0x85,0xC9,0x75,0x13,0x88,0x4C}))
        && Check(ctx, 0x48FE80, std::to_array<std::uint8_t>({0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x18,0x57,0x48,0x83,0xEC,0x20,0x41}))
        && Check(ctx, 0x48FDE0, std::to_array<std::uint8_t>({0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,0xD9,0x48,0x85,0xC9,0x74,0x1E,0xE8,0xDD}))
        && Check(ctx, 0x34A0E0, std::to_array<std::uint8_t>({0x48,0x83,0xEC,0x28,0x48,0x85,0xC9,0x75,0x1A,0x88,0x4C,0x24,0x30}))
        && Check(ctx, 0x300A90, std::to_array<std::uint8_t>({0x48,0x83,0xEC,0x28,0x0F,0xB6,0xC1,0x48,0x89,0x44,0x24,0x38,0x48,0x83,0xF8,0x04}))
        && RequireCoreThunk(ctx, 0x3D4C10)
        && Check(ctx, 0x3D4C16, std::to_array<std::uint8_t>({0x41,0x54,0x41,0x56,0x48,0x83,0xEC,0x40,0x45,0x8B,0xE1,0x41,0x8B,0xE8,0x44,0x8B,0xF2,0x48,0x8B,0xF1}))
        && RequireCoreThunk(ctx, 0x47A190)
        && Check(ctx, 0x47A197, std::to_array<std::uint8_t>({0x48,0x81,0xEC,0x80,0x00,0x00,0x00,0x48,0x8B,0x05,0x23,0x11,0x55,0x02}))
        && Check(ctx, 0x42E615, std::to_array<std::uint8_t>({0x44,0x8B,0x89,0x70,0x01,0x00,0x00,0x48,0x8B,0xDA,0x89,0x44,0x24,0x28,0x41,0xFF,0xC1}))
        && Check(ctx, 0x3D4C84, std::to_array<std::uint8_t>({0x8B,0x88,0xD0,0x14,0x00,0x00,0xD3,0xE7,0x8B,0x88,0xD4,0x14,0x00,0x00}));
    if (valid && requireStatReader) {
        valid = RequireCoreThunk(ctx, 0x2F5020)
            && Check(ctx, 0x2F502A, std::to_array<std::uint8_t>({0x48,0x89,0x74,0x24,0x20,0x57,0x48,0x83,0xEC,0x20,0x41,0x0F,0xB7,0xE8,0x8B,0xFA,0x48,0x8B,0xD9}))
            && Check(ctx, 0x2F84B0, std::to_array<std::uint8_t>({0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57,0x48,0x83,0xEC,0x30}))
            && Check(ctx, 0x2F8504, std::to_array<std::uint8_t>({0xE8,0x57,0x1B,0x00,0x00}))
            && ValidateWideStatCopy(ctx);
        StatReaderValidated = valid;
    }
    if (!valid) ctx->LogError("Repair Charge Replenishment: native helper or layout validation failed; charges will not be changed.");
    return valid;
}

auto ReadStats(void* item, std::int32_t rateStat, std::vector<D2RL::Items::ItemStatValue>& stats) noexcept -> bool {
    stats.clear();
    if (!StatReaderValidated || !item || rateStat != static_cast<std::int32_t>(RepairRateStat)) return false;
    return CollectStats(item, rateStat, Function<GetStatFn>(0x2F5020), Function<CopyStatFn>(0x2F84B0), stats);
}

auto Inspect(void* item, ItemState& state) noexcept -> bool {
    void* game = Function<PointerFn>(0x48FF00)(item);
    if (!game) return false;
    const auto owner = Function<OwnerFn>(0x371D00)(item);
    if (owner == UINT32_MAX) return false;
    void* player = Function<FindFn>(0x48FE80)(game, 0, owner);
    if (!player) return false;
    state.client = Function<PointerFn>(0x48FDE0)(player);
    state.bank = Function<BankFn>(0x34A0E0)(item);
    if (!state.client || state.bank < 1 || state.bank > 3) return false;
    const auto* tables = Function<TablesFn>(0x300A90)(static_cast<std::uint8_t>(state.bank));
    return tables && ReadAt(game, 0x170, state.frame)
        && ReadAt(tables, 0x14D0, state.shift) && ReadAt(tables, 0x14D4, state.mask)
        && state.shift >= 1 && state.shift <= 8 && state.mask == ((1U << state.shift) - 1U);
}

auto Write(void* item, const ItemState& state, std::uint16_t layer, std::int32_t packed) noexcept -> bool {
    const auto charges = Charges::Decode(packed);
    if (!charges.Valid() || !state.client) return false;
    const auto skill = static_cast<std::int32_t>(layer >> state.shift);
    const auto level = static_cast<std::int32_t>(layer & state.mask);
    if (skill <= 0 || level <= 0) return false;
    if (!Function<ChargeFn>(0x3D4C10)(item, skill, level, static_cast<std::int32_t>(charges.current))) return false;
    // Identical stat/layer/value publication to the native vendor recharge path.
    Function<NotifyFn>(0x47A190)(state.client, item, true, ChargedSkillStat, packed, layer);
    return true;
}
}
