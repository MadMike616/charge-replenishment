#include "../src/recharge_policy.h"
#include <cstdio>
#include <cstdlib>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); std::exit(1); } } while (false)
using Recharge::Charges;
using Recharge::Timer;
int main() {
    Timer timer;
    CHECK(timer.Advance(0, 10, {0, 20}).current == 0);
    CHECK(timer.Advance(249, 10, {0, 20}).current == 0);
    CHECK(timer.Advance(250, 10, {0, 20}).current == 1);
    CHECK(timer.Advance(250, 10, {1, 20}).current == 1); // Paused game: no elapsed frames.
    CHECK(timer.Advance(750, 10, {1, 20}).current == 3);
    Timer slow;
    CHECK(slow.Advance(0, 5, {4, 10}).current == 4);
    CHECK(slow.Advance(499, 5, {4, 10}).current == 4);
    CHECK(slow.Advance(500, 5, {4, 10}).current == 5);
    Timer fractional;
    fractional.Advance(0, 3, {0, 10});
    CHECK(fractional.Advance(833, 3, {0, 10}).current == 0);
    CHECK(fractional.Advance(834, 3, {0, 10}).current == 1);
    CHECK(fractional.Advance(1667, 3, {1, 10}).current == 2);
    CHECK(fractional.Advance(2500, 3, {2, 10}).current == 3); // No truncation drift.
    Timer full;
    CHECK(full.Advance(0, 100, {10, 10}).current == 10);
    CHECK(full.Advance(10000, 100, {9, 10}).current == 9); // Full items bank no credit.
    CHECK(full.Advance(10025, 100, {9, 10}).current == 10);
    CHECK(!full.running);
    Timer changed;
    changed.Advance(100, 5, {0, 10});
    CHECK(changed.Advance(599, 10, {0, 10}).current == 0); // Changing rate starts a fresh interval.
    CHECK(changed.Advance(849, 10, {0, 10}).current == 1);
    CHECK(changed.Advance(9999, 0, {1, 10}).current == 1);
    CHECK(!changed.running);
    CHECK(changed.Advance(10000, 1024, {1, 10}).current == 1);
    CHECK(!changed.running);
    Timer fast;
    fast.Advance(0, 1023, {0, 255});
    CHECK(fast.Advance(25000, 1023, {0, 255}).current == 255); // Catch up, cap at maximum.
    Timer wrapped;
    wrapped.Advance(0xFFFFFFF0, 100, {0, 10});
    CHECK(wrapped.Advance(9, 100, {0, 10}).current == 1);
    Timer reset;
    reset.Advance(1000, 100, {0, 10});
    CHECK(reset.Advance(100, 100, {0, 10}).current == 0);
    CHECK(!Charges::Decode(-1).Valid());
    CHECK(!Charges::Decode(0x10000).Valid());
    CHECK((!Charges{11, 10}.Valid()));
    CHECK(Charges::Decode(0x1400).Valid());
    CHECK(Charges::Decode(0x1400).maximum == 20);
    CHECK(Charges::Decode(0x1400).current == 0);
    CHECK(Charges::Decode(0x1405).Pack() == 0x1405);
    std::puts("Recharge policy tests passed.");
}
