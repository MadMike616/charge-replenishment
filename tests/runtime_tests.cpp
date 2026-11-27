#include "../src/plugin.cpp"
#include "../src/native_stats.h"
#include <cstdio>
#include <cstdlib>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); std::exit(1); } } while (false)

namespace Fixture {
std::map<D2RL::ItemHandle, std::vector<D2RL::Items::ItemStatValue>> Stats;
std::vector<D2RL::Items::ItemInfo> ItemList;
std::uint32_t Frame{};
D2RL::ItemHandle Editing{};
bool RejectWrite{}, CorruptWrite{};
unsigned Writes{}, Errors{};
unsigned UiTasks{};
unsigned NativeReads{}, RegisteredTables{}, RegisteredGameplay{};
bool NativeReaderRequired{};
std::map<D2RL::ServiceId, const void*> Services;
auto __cdecl Query(const D2RL::PluginContext*, D2RL::ServiceId id, std::uint32_t version, const void** out) noexcept -> D2RL::ServiceQueryResult {
    const auto found = Services.find(id);
    *out = found == Services.end() ? nullptr : found->second;
    return version == 1 && *out ? D2RL::ServiceQueryResult::Success : D2RL::ServiceQueryResult::Unavailable;
}
auto __cdecl Console(const D2RL::PluginContext*, const D2RL::ConsoleCommandRegistration*) noexcept -> bool { return true; }
auto __cdecl RegisterTables(const D2RL::PluginContext*, const D2RL::Lifecycle::DataTablesLoadedListener*, D2RL::Lifecycle::ListenerHandle* handle) noexcept -> D2RL::Lifecycle::Result {
    *handle = ++RegisteredTables; return D2RL::Lifecycle::Result::Success;
}
auto __cdecl RegisterGameplay(const D2RL::PluginContext*, const D2RL::Lifecycle::GameplayEventListener*, D2RL::Lifecycle::ListenerHandle* handle) noexcept -> D2RL::Lifecycle::Result {
    *handle = ++RegisteredGameplay; return D2RL::Lifecycle::Result::Success;
}
auto __cdecl LocalPlayer(const D2RL::PluginContext*, D2RL::PlayerHandle* handle) noexcept -> D2RL::Inventory::Result {
    *handle = 1; return D2RL::Inventory::Result::Success;
}
auto __cdecl QueueUi(const D2RL::PluginContext*, D2RL::Threads::Callback, void*) noexcept -> D2RL::Threads::Result {
    ++UiTasks; return D2RL::Threads::Result::Success;
}
std::vector<std::uint8_t> Schema(400 * 0x144);
auto __cdecl Table(const D2RL::PluginContext*, D2RL::DataTables::Bank, D2RL::DataTables::TableId,
                   D2RL::DataTables::TableView* table) noexcept -> D2RL::DataTables::Result {
    table->rows = Schema.data(); table->rowCount = 400; table->rowSize = 0x144;
    return D2RL::DataTables::Result::Success;
}
auto __cdecl Details(const D2RL::PluginContext*, D2RL::ItemHandle item, D2RL::Items::ItemDetails* details,
                     D2RL::Items::ItemStatValue* output, std::uint32_t capacity) noexcept -> D2RL::Items::Result {
    const auto& stats = Stats[item];
    details->statCount = static_cast<std::uint32_t>(stats.size());
    if (!output || capacity == 0) return D2RL::Items::Result::Success;
    if (capacity < stats.size()) return D2RL::Items::Result::InvalidArgument;
    std::copy(stats.begin(), stats.end(), output);
    return D2RL::Items::Result::Success;
}
auto __cdecl Edit(const D2RL::PluginContext* ctx, D2RL::ItemHandle item, D2RL::Items::NativeItemEditCallback callback, void* data) noexcept -> D2RL::Items::Result {
    Editing = item;
    callback(ctx, reinterpret_cast<void*>(item), data);
    Editing = 0;
    return D2RL::Items::Result::Success;
}
auto __cdecl Each(const D2RL::PluginContext* ctx, D2RL::PlayerHandle, const D2RL::Inventory::ItemFilter* filter,
                  D2RL::Inventory::ItemCallback callback, void* data) noexcept -> D2RL::Inventory::Result {
    for (const auto& item : ItemList) if (filter->containerMask & D2RL::Items::ContainerBit(item.container)) {
        if (callback(ctx, &item, data) == D2RL::Inventory::IterationAction::Stop) break;
    }
    return D2RL::Inventory::Result::Success;
}
void __cdecl Error(const D2RL::PluginContext*, const char*) noexcept { ++Errors; }
auto __fastcall ReadRate(void*, std::int32_t id, std::uint32_t layer) noexcept -> std::int32_t {
    CHECK(layer == 0); int rate{};
    for (const auto& stat : Stats[Editing]) if (stat.statId == static_cast<std::uint32_t>(id) && stat.layer == 0) rate += stat.value;
    return rate;
}
auto __fastcall CopyWideStats(void*, std::int32_t id, Recharge::Native::StatRecord* output, std::int32_t capacity) noexcept -> std::int32_t {
    int count{};
    // Model the observed loader ABI independently of the plugin's record type.
    auto* bytes = reinterpret_cast<std::uint8_t*>(output);
    for (const auto& stat : Stats[Editing]) if (stat.statId == static_cast<std::uint32_t>(id)) {
        if (count >= capacity) return capacity;
        const auto offset = static_cast<std::size_t>(count) * 16;
        const std::int64_t value = stat.value;
        std::memcpy(bytes + offset, &stat.layer, 4);
        std::memcpy(bytes + offset + 4, &stat.statId, 4);
        std::memcpy(bytes + offset + 8, &value, 8);
        ++count;
    }
    return count;
}
auto Current(D2RL::ItemHandle item, std::uint32_t layer) -> std::uint32_t {
    for (const auto& stat : Stats[item]) if (stat.statId == 204 && stat.layer == layer) return Recharge::Charges::Decode(stat.value).current;
    return UINT32_MAX;
}
void Add(D2RL::ItemHandle handle, D2RL::Items::ItemContainer container, int rate, bool second = false) {
    ItemList.push_back({.structSize = D2RL::Items::ItemInfoSize, .handle = handle, .container = container});
    Stats[handle] = {{252, 0, rate, 0}, {204, 54 * 64 + 1, 0x1400, 0}};
    if (second) Stats[handle].push_back({204, 62 * 64 + 2, 0x0A00, 0});
}
}
namespace Recharge::Native {
auto Initialize(const D2RL::PluginContext*, bool requireStatReader) noexcept -> bool { Fixture::NativeReaderRequired = requireStatReader; return true; }
auto ReadStats(void*, std::int32_t rateStat, std::vector<D2RL::Items::ItemStatValue>& stats) noexcept -> bool {
    CHECK(rateStat == 252 && Fixture::Editing != 0);
    ++Fixture::NativeReads;
    return CollectStats(reinterpret_cast<void*>(Fixture::Editing), rateStat, Fixture::ReadRate, Fixture::CopyWideStats, stats);
}
auto Inspect(void*, ItemState& state) noexcept -> bool { state = {Fixture::Frame, 6, 63, 3, reinterpret_cast<void*>(1)}; return true; }
auto Write(void*, const ItemState&, std::uint16_t layer, std::int32_t packed) noexcept -> bool {
    if (Fixture::RejectWrite) return false;
    for (auto& stat : Fixture::Stats[Fixture::Editing]) if (stat.statId == 204 && stat.layer == layer) {
        stat.value = Fixture::CorruptWrite ? packed + 1 : packed;
        ++Fixture::Writes;
        return true;
    }
    return false;
}
}
int main() {
    using namespace Recharge;
    using D2RL::Items::ItemContainer;
    D2RL::PluginApi api{.apiSize = D2RL::PluginApiSize, .logError = Fixture::Error,
        .registerConsoleCommand = Fixture::Console, .queryService = Fixture::Query};
    D2RL::PluginContext ctx{.contextSize = sizeof(D2RL::PluginContext), .abiVersion = 4, .api = &api, .loadScope = D2RL::LoadScope::Global};
    D2RL::ItemService itemService{.serviceSize = D2RL::ItemServiceSize, .serviceVersion = 1,
        .editNativeItem = Fixture::Edit, .getItemDetails = Fixture::Details};
    // The installed loader exposes only the first 88 bytes of the Item service.
    // getItemDetails begins at byte 88, outside this object.
    alignas(D2RL::ItemService) std::array<std::byte, 88> legacyItem{};
    std::memcpy(legacyItem.data(), &itemService, legacyItem.size());
    const std::uint32_t legacySize = 88;
    std::memcpy(legacyItem.data(), &legacySize, sizeof(legacySize));
    D2RL::InventoryService inventoryService{.serviceSize = D2RL::InventoryServiceSize, .serviceVersion = 1,
        .getLocalPlayer = Fixture::LocalPlayer, .forEachInventoryItem = Fixture::Each};
    D2RL::DataTableService tableService{.serviceSize = D2RL::DataTableServiceSize, .serviceVersion = 1, .getTable = Fixture::Table};
    D2RL::ThreadService threadService{.serviceSize = D2RL::ThreadServiceSize, .serviceVersion = 1,
        .runOnUiThread = Fixture::QueueUi, .runOnGameThread = Fixture::QueueUi};
    D2RL::LifecycleService lifecycle{.serviceSize = D2RL::LifecycleServiceSize, .serviceVersion = 1,
        .registerDataTablesLoadedListener = Fixture::RegisterTables, .registerGameplayEventListener = Fixture::RegisterGameplay};
    Fixture::Services = {{D2RL::ServiceId::Item, legacyItem.data()}, {D2RL::ServiceId::Inventory, &inventoryService},
        {D2RL::ServiceId::DataTable, &tableService}, {D2RL::ServiceId::Thread, &threadService}, {D2RL::ServiceId::Lifecycle, &lifecycle}};
    CHECK(ctx.activeMod == nullptr && ctx.modDirectory == nullptr && ctx.pluginConfigPath == nullptr);
    CHECK(api.readConfig == nullptr && api.writeConfig == nullptr);
    CHECK(!D2RL::HasFlag(D2RLoaderGetPluginInfo()->flags, D2RL::PluginFlags::ModScopedOnly));
    CHECK(D2RLoaderLoadPlugin(&ctx)); CHECK(!HasItemDetails());
    CHECK(Fixture::RegisteredTables == 1 && Fixture::RegisteredGameplay == 2 && Fixture::UiTasks == 0);
    auto* marker = Fixture::Schema.data() + 252 * 0x144;
    auto* charged = Fixture::Schema.data() + 204 * 0x144;
    const std::uint16_t markerId = 252, chargedId = 204;
    const std::uint32_t callbackFlag = 1U << 10, parameterBits = 16;
    std::memcpy(marker, &markerId, sizeof(markerId));
    const std::uint32_t signedFlag = 1U << 1;
    std::memcpy(marker + 4, &signedFlag, sizeof(signedFlag)); marker[8] = 5; marker[0x15] = 6;
    std::memcpy(charged, &chargedId, sizeof(chargedId)); charged[0x15] = 16;
    std::memcpy(charged + 4, &callbackFlag, sizeof(callbackFlag));
    std::memcpy(charged + 0x20, &parameterBits, sizeof(parameterBits));
    CHECK(CheckBank(3));
    OnTables(&ctx, nullptr, nullptr); CHECK(Fixture::NativeReaderRequired && Ready.load());
    marker[0x15] = 5; CHECK(!CheckBank(3)); marker[0x15] = 6;
    std::memcpy(marker + 4, &callbackFlag, sizeof(callbackFlag)); CHECK(!CheckBank(3));
    std::memcpy(marker + 4, &signedFlag, sizeof(signedFlag));
    charged[0x20] = 0; CHECK(!CheckBank(3)); charged[0x20] = 16;
    CHECK(CheckBank(3));
    Active.store(true); Ready.store(true); InGame.store(true); ValidBank[3] = true;
    Generation.store(1); Player.store(1);
    Fixture::Add(1, ItemContainer::Equipment, 10, true);
    Fixture::Add(2, ItemContainer::Inventory, 5);
    Fixture::Add(3, ItemContainer::PersonalStash, 100);
    Fixture::Add(4, ItemContainer::Equipment, 0);
    // Both optional-SDK inspection and the legacy fallback report the same snapshot.
    std::vector<D2RL::Items::ItemStatValue> snapshot;
    Items = &itemService; CHECK(HasItemDetails()); CHECK(Details(1, snapshot));
    CHECK(snapshot.size() == 3 && Fixture::NativeReads == 0);
    Items = reinterpret_cast<const D2RL::ItemService*>(legacyItem.data());
    CHECK(!HasItemDetails()); CHECK(Details(1, snapshot)); CHECK(Fixture::NativeReads == 1 && snapshot.size() == 3);
    GamePulse(&ctx, nullptr);
    CHECK(Fixture::Writes == 0);
    Fixture::Frame = 250; GamePulse(&ctx, nullptr);
    CHECK(Fixture::Current(1, 3457) == 1);
    CHECK(Fixture::Current(1, 3970) == 1); // Both charged skills replenish independently.
    CHECK(Fixture::Current(2, 3457) == 0);
    CHECK(Fixture::Current(3, 3457) == 0);
    CHECK(Fixture::Current(4, 3457) == 0);
    CHECK(Charges::Decode(Fixture::Stats[1][1].value).maximum == 20);
    GamePulse(&ctx, nullptr); CHECK(Fixture::Writes == 2); // Pausing cannot add charges.
    Fixture::Frame = 500; GamePulse(&ctx, nullptr);
    CHECK(Fixture::Current(2, 3457) == 1);
    // Removing an item from the carried containers discards its partial timer.
    Fixture::ItemList[1].container = ItemContainer::Cube;
    Fixture::Frame = 600; GamePulse(&ctx, nullptr);
    Fixture::ItemList[1].container = ItemContainer::Inventory;
    Fixture::Frame = 900; GamePulse(&ctx, nullptr);
    CHECK(Fixture::Current(2, 3457) == 1);
    Fixture::Frame = 1252; GamePulse(&ctx, nullptr); CHECK(Fixture::Current(2, 3457) == 1);
    Fixture::Frame = 1400; GamePulse(&ctx, nullptr); CHECK(Fixture::Current(2, 3457) == 2);
    // Session generations cannot reuse old item-handle timers.
    Generation.store(2); Fixture::Frame = 100000; GamePulse(&ctx, nullptr);
    CHECK(Fixture::Current(2, 3457) == 2);
    // An ambiguous duplicate layer is not changed.
    Fixture::Stats[1].push_back(Fixture::Stats[1][1]);
    Fixture::Frame = 100500; const auto before = Fixture::Current(1, 3457); GamePulse(&ctx, nullptr);
    CHECK(Fixture::Current(1, 3457) == before);
    // Unknown native data bank: no mutation.
    ValidBank[3] = false;
    const auto writes = Fixture::Writes, reads = Fixture::NativeReads; Fixture::Frame += 1000; GamePulse(&ctx, nullptr);
    CHECK(Fixture::Writes == writes && Fixture::NativeReads == reads); ValidBank[3] = true;
    // A failed native write and a mismatched verification both disable further writes.
    Fixture::Stats[1].pop_back(); Fixture::RejectWrite = true;
    Fixture::Frame += 1000; GamePulse(&ctx, nullptr); Fixture::Frame += 1000; GamePulse(&ctx, nullptr);
    CHECK(!Ready.load()); CHECK(Fixture::Errors == 1);
    Fixture::RejectWrite = false; Fixture::CorruptWrite = true; Ready.store(true); Timers.clear();
    GamePulse(&ctx, nullptr); Fixture::Frame += 1000; GamePulse(&ctx, nullptr);
    CHECK(!Ready.load()); CHECK(Fixture::Errors == 2);
    // Exact observed Gavel state must pass through the legacy SDK fallback and
    // the real widened-record decoder, then replenish 2/3 -> 3/3 in 250 frames.
    Fixture::CorruptWrite = false; Fixture::ItemList.clear(); Fixture::Stats.clear();
    Timers.clear(); Ready.store(true); Generation.store(3); Fixture::Frame = 0;
    Fixture::Add(9, ItemContainer::Inventory, 10);
    Fixture::Stats[9] = {{252, 0, 10, 0}, {204, 4232, 770, 0}};
    GamePulse(&ctx, nullptr); CHECK(Eligible.load() == 1);
    Fixture::Frame = 249; GamePulse(&ctx, nullptr); CHECK(Fixture::Current(9, 4232) == 2);
    Fixture::Frame = 250; GamePulse(&ctx, nullptr); CHECK(Fixture::Current(9, 4232) == 3);
    CHECK(Fixture::Stats[9][1].value == 771 && Ready.load());
    Threads = &threadService;
    D2RL::Lifecycle::GameplayEvent event{.structSize = D2RL::Lifecycle::GameplayEventSize,
        .kind = D2RL::Lifecycle::GameplayEventKind::GameLeft, .sessionGeneration = 3};
    Gameplay(&ctx, &event, nullptr); CHECK(!InGame.load());
    event.kind = D2RL::Lifecycle::GameplayEventKind::LocalPlayerReady;
    Gameplay(&ctx, &event, nullptr); CHECK(InGame.load()); CHECK(Fixture::UiTasks == 1);
    Gameplay(&ctx, &event, nullptr); CHECK(Fixture::UiTasks == 1); // No duplicate recurring UI loops.
    // An item with self-repair but no charged skill has nothing to replenish.
    Fixture::ItemList.clear(); Fixture::Stats.clear(); Timers.clear(); Ready.store(true);
    Fixture::Add(10, ItemContainer::Inventory, 5);
    Fixture::Stats[10] = {{252, 0, 5, 0}, {72, 0, 7, 0}, {73, 0, 30, 0}};
    const auto noChargesWrites = Fixture::Writes;
    GamePulse(&ctx, nullptr); Fixture::Frame += 10000; GamePulse(&ctx, nullptr);
    CHECK(Fixture::Writes == noChargesWrites && Eligible.load() == 0 && Ready.load());
    // Charges without self-repair, and the original fork's custom marker alone,
    // do not qualify. Replenish Quantity is a different stat.
    Fixture::Stats[10] = {{399, 0, 10, 0}, {253, 0, 10, 0}, {204, 4232, 770, 0}};
    GamePulse(&ctx, nullptr); Fixture::Frame += 10000; GamePulse(&ctx, nullptr);
    CHECK(Fixture::Writes == noChargesWrites && Eligible.load() == 0);
    // Charge edits preserve current/max durability, the repair-rate property,
    // and quantity. The game retains ownership of normal durability repair.
    Fixture::Stats[10] = {{252, 0, 5, 0}, {72, 0, 7, 0}, {73, 0, 30, 0}, {70, 0, 12, 0}, {204, 4232, 770, 0}};
    Timers.clear(); GamePulse(&ctx, nullptr); Fixture::Frame += 500; GamePulse(&ctx, nullptr);
    CHECK(Fixture::Current(10, 4232) == 3);
    CHECK(Fixture::Stats[10][0].value == 5 && Fixture::Stats[10][1].value == 7);
    CHECK(Fixture::Stats[10][2].value == 30 && Fixture::Stats[10][3].value == 12);
    // The optional SDK-details path must obey the same eligibility and rate.
    Items = &itemService; Fixture::Stats[10].back().value = 770; Timers.clear();
    GamePulse(&ctx, nullptr); Fixture::Frame += 500; GamePulse(&ctx, nullptr);
    CHECK(Fixture::Current(10, 4232) == 3 && Fixture::Stats[10][1].value == 7);
    D2RLoaderUnloadPlugin(); CHECK(!Active.load() && !Ready.load());
    ctx.loadScope = D2RL::LoadScope::Mod; ctx.activeMod = "TestMod";
    CHECK(D2RLoaderLoadPlugin(&ctx)); CHECK(Active.load());
    CHECK(Fixture::RegisteredTables == 2 && Fixture::RegisteredGameplay == 4);
    D2RLoaderUnloadPlugin();
    std::puts("Global and mod SDK repair-charge integration tests passed.");
}
