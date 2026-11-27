#include "../src/native_stats.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); std::exit(1); } } while (false)
using namespace Recharge::Native;
int Rate{10}, Count{2}, Copies{};
std::array<StatRecord, 2> Records{{{3457, 204, 0x1400}, {3970, 204, 0x0A03}}};
auto __fastcall RateRead(void*, std::int32_t stat, std::uint32_t layer) noexcept -> std::int32_t {
    CHECK(stat == 252 && layer == 0); return Rate;
}
auto __fastcall ObservedCopy(void*, std::int32_t stat, StatRecord* out, std::int32_t capacity) noexcept -> std::int32_t {
    CHECK(stat == 204 && capacity == 128);
    // Exact bytes read from the authoritative test Gavel: layer 4232, stat 204,
    // packed value 770 (2 current / 3 maximum). Do not build this fixture from
    // StatRecord: its purpose is to catch an incorrect record declaration.
    constexpr std::array<unsigned char, 16> observed{0x88,0x10,0,0,0xCC,0,0,0,0x02,0x03,0,0,0,0,0,0};
    std::memcpy(out, observed.data(), observed.size());
    return 1;
}
auto __fastcall CopyRead(void*, std::int32_t stat, StatRecord* out, std::int32_t capacity) noexcept -> std::int32_t {
    CHECK(stat == 204 && capacity == 128); ++Copies;
    std::copy(Records.begin(), Records.end(), out); return Count;
}
int main() {
    auto* item = reinterpret_cast<void*>(1);
    std::vector<D2RL::Items::ItemStatValue> stats;
    CHECK(CollectStats(item, 252, RateRead, ObservedCopy, stats));
    CHECK(stats.size() == 2 && stats[1].layer == 4232 && stats[1].statId == 204 && stats[1].value == 770);
    CHECK(CollectStats(item, 252, RateRead, CopyRead, stats));
    CHECK(stats.size() == 3 && stats[0].value == 10);
    CHECK(stats[1].layer == 3457 && stats[1].value == 0x1400); // Zero charges retain their maximum.
    CHECK(stats[2].layer == 3970 && stats[2].value == 0x0A03);
    Rate = 0; const auto before = Copies;
    CHECK(CollectStats(item, 252, RateRead, CopyRead, stats)); CHECK(stats.size() == 1 && Copies == before);
    Rate = 1024; CHECK(CollectStats(item, 252, RateRead, CopyRead, stats)); CHECK(Copies == before);
    Rate = 10; Count = -1; CHECK(!CollectStats(item, 252, RateRead, CopyRead, stats));
    Count = 128; CHECK(!CollectStats(item, 252, RateRead, CopyRead, stats));
    Count = 2; Records[1].stat = 205; CHECK(!CollectStats(item, 252, RateRead, CopyRead, stats));
    Records[1].stat = 204; CHECK(CollectStats(item, 252, RateRead, CopyRead, stats));
    Records[1].value = 0x100000000LL; CHECK(!CollectStats(item, 252, RateRead, CopyRead, stats));
    Records[1].value = 0x0A03; Records[1].layer = 0x10000; CHECK(!CollectStats(item, 252, RateRead, CopyRead, stats));
    Count = 0; Records[1].layer = 3970;
    CHECK(CollectStats(item, 252, RateRead, CopyRead, stats)); CHECK(stats.size() == 1);
    CHECK(!CollectStats(item, 399, RateRead, CopyRead, stats));
    CHECK(!CollectStats(item, 253, RateRead, CopyRead, stats));
    CHECK(!CollectStats(item, 204, RateRead, CopyRead, stats));
    CHECK(!CollectStats(nullptr, 252, RateRead, CopyRead, stats));
    std::puts("Native stat reader tests passed.");
}
