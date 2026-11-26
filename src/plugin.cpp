#include "native.h"
#include "recharge_policy.h"
#include "setup.h"

#include <array>
#include <atomic>
#include <charconv>
#include <cstring>
#include <map>
#include <string_view>
#include <utility>
#include <vector>
#include <windows.h>

namespace Recharge {
namespace {
constexpr D2RL::PluginInfo Info{
    .infoSize = D2RL::PluginInfoSize,
    .abiVersion = D2RL_PLUGIN_ABI_VERSION,
    .id = "charge-replenishment",
    .name = "Charge Replenishment",
    .version = "1.0.0",
    .author = "MadMike",
    .description = "Replenishes an item's skill charges at the rate set by its rep-charges property.",
    .flags = D2RL::PluginFlags::ModScopedOnly | D2RL::PluginFlags::Shared | D2RL::PluginFlags::NativeHooks,
};
const D2RL::PluginContext* Context{};
const D2RL::ItemService* Items{};
const D2RL::InventoryService* Inventory{};
const D2RL::ThreadService* Threads{};
const D2RL::DataTableService* Tables{};
std::atomic<bool> Active{}, InGame{}, Pending{}, Ready{}, UiQueued{};
std::atomic<D2RL::PlayerHandle> Player{};
std::atomic<std::uint64_t> Generation{};
std::atomic<std::uint64_t> Restored{}, Eligible{};
std::uint64_t NextPoll{}; // UI-thread only.
std::uint64_t GameGeneration{}; // Remaining mutable state is game-thread only.
std::uint64_t Scan{};
std::array<bool, 4> ValidBank{};
std::int32_t StatId{-1};
bool IncludeInventory{true};
struct Entry { Timer timer; std::uint64_t seen{}; };
using Key = std::pair<D2RL::ItemHandle, std::uint32_t>;
std::map<Key, Entry> Timers;

auto HasItemDetails() noexcept -> bool {
    return D2RL::HasItemServiceField(Items, D2RL::ItemServiceDetailsFieldEnd) && Items->getItemDetails;
}
template<class Service>
auto RequireService(const Service** service, const char* name, std::uint32_t minimumSize) noexcept -> bool {
    const auto result = Context->QueryService(service);
    if (result == D2RL::ServiceQueryResult::Success && *service
        && (*service)->serviceVersion == Service::AbiVersion && (*service)->serviceSize >= minimumSize) return true;
    D2RL::LogErrorF(Context, "Charge Replenishment: %s service unavailable or incomplete (query=%u, size=%u, required=%u).", name,
        static_cast<unsigned>(result), *service ? (*service)->serviceSize : 0U, minimumSize);
    return false;
}

auto Trim(std::string_view text) noexcept -> std::string_view {
    const auto first = text.find_first_not_of(" \t\r");
    return first == text.npos ? std::string_view{} : text.substr(first, text.find_last_not_of(" \t\r") - first + 1);
}
auto Configuration() noexcept -> bool {
    std::array<char, 65536> buffer{};
    std::uint32_t needed{};
    if (!Context->ReadConfig(buffer.data(), static_cast<std::uint32_t>(buffer.size()), &needed)) return false;
    std::string_view input(buffer.data());
    if (input.starts_with("\xEF\xBB\xBF")) input.remove_prefix(3);
    std::string_view section;
    unsigned seen{};
    bool enabled{true};
    while (!input.empty()) {
        const auto end = input.find('\n');
        auto line = input.substr(0, end);
        input = end == input.npos ? std::string_view{} : input.substr(end + 1);
        line = Trim(line.substr(0, line.find('#')));
        if (line.empty()) continue;
        if (line.front() == '[' && line.back() == ']') { section = line; continue; }
        if (section != "[charge-replenishment]") continue;
        const auto equal = line.find('=');
        if (equal == line.npos) return false;
        const auto name = Trim(line.substr(0, equal));
        const auto value = Trim(line.substr(equal + 1));
        unsigned bit{};
        if (name == "stat_id") {
            bit = 1;
            const auto result = std::from_chars(value.data(), value.data() + value.size(), StatId);
            if (result.ec != std::errc{} || result.ptr != value.data() + value.size()) return false;
        } else if (name == "enabled" || name == "include_inventory") {
            bit = name == "enabled" ? 2U : 4U;
            if (value != "true" && value != "false") return false;
            (name == "enabled" ? enabled : IncludeInventory) = value == "true";
        } else return false;
        if (seen & bit) return false;
        seen |= bit;
    }
    if (!enabled) { Context->LogInfo("Charge Replenishment is disabled in configuration."); return true; }
    if (!(seen & 1) || StatId < 398 || StatId > 510) {
        Context->LogError("Charge Replenishment: automatic setup has no valid stat ID. Check the setup error in the plugin log and restart D2RLoader after supplying any missing base files.");
        return false;
    }
    Active.store(true);
    return true;
}

template<class T> auto Field(const std::uint8_t* bytes, std::size_t offset) noexcept -> T {
    T output{}; std::memcpy(&output, bytes + offset, sizeof(output)); return output;
}
auto CheckBank(std::uint32_t bank) noexcept -> bool {
    D2RL::DataTables::TableView table{.structSize = D2RL::DataTables::TableViewSize};
    if (Tables->getTable(Context, static_cast<D2RL::DataTables::Bank>(bank), D2RL::DataTables::TableId::ItemStatCost, &table)
        != D2RL::DataTables::Result::Success || !table.rows || table.rowSize != 0x144
        || table.rowCount <= static_cast<std::uint32_t>(StatId)) return false;
    const auto* rows = static_cast<const std::uint8_t*>(table.rows);
    const auto* marker = rows + static_cast<std::size_t>(StatId) * table.rowSize;
    const auto* charged = rows + ChargedSkillStat * table.rowSize;
    // Validate the compiled schema too: a stale BIN must never bind to another stat.
    return Field<std::uint16_t>(marker, 0) == StatId
        && marker[0x08] == 10 && marker[0x09] == 0 && marker[0x14] == 0 && marker[0x15] == 10
        && Field<std::uint32_t>(marker, 0x18) == 0 && Field<std::uint32_t>(marker, 0x20) == 0
        && marker[0x50] == 0 && (Field<std::uint32_t>(marker, 4) & ((1U << 1) | (1U << 10))) == 0
        && Field<std::uint16_t>(charged, 0) == ChargedSkillStat && charged[0x15] == 16
        && Field<std::uint32_t>(charged, 0x20) == 16 && (Field<std::uint32_t>(charged, 4) & (1U << 10)) != 0;
}

void __cdecl OnTables(const D2RL::PluginContext*, const D2RL::Lifecycle::DataTablesLoadedEvent*, void*) noexcept {
    Ready.store(false);
    Timers.clear();
    ValidBank = {};
    if (!Active.load() || !Native::Initialize(Context, !HasItemDetails())) return;
    bool any{};
    for (std::uint32_t bank = 1; bank <= 3; ++bank) { ValidBank[bank] = CheckBank(bank); any |= ValidBank[bank]; }
    if (!any) Context->LogError("Charge Replenishment: no loaded data bank has the installed stat schema. Recompile the mod's BIN files after installing rep-charges.");
    Ready.store(any);
}

struct EditWork {
    D2RL::ItemHandle handle{};
    const std::vector<D2RL::Items::ItemStatValue>* stats{};
    std::int32_t rate{};
    std::vector<std::pair<std::uint32_t, std::int32_t>> changed;
    bool failed{};
};
void __cdecl Edit(const D2RL::PluginContext*, void* item, void* data) noexcept {
    auto& work = *static_cast<EditWork*>(data);
    Native::ItemState native{};
    if (!Native::Inspect(item, native) || native.bank >= ValidBank.size() || !ValidBank[native.bank]) return;
    try {
        for (const auto& stat : *work.stats) {
            if (stat.statId != ChargedSkillStat || stat.layer > 0xFFFF || (stat.layer & native.mask) == 0) continue;
            // An ambiguous duplicate cannot be safely mapped to one charge pool.
            unsigned duplicates{};
            for (const auto& other : *work.stats) duplicates += other.statId == ChargedSkillStat && other.layer == stat.layer;
            if (duplicates != 1) continue;
            const auto before = Charges::Decode(stat.value);
            if (!before.Valid()) continue;
            auto& timer = Timers[{work.handle, stat.layer}];
            timer.seen = Scan;
            const auto after = timer.timer.Advance(native.frame, work.rate, before);
            if (after.current == before.current) continue;
            if (!Native::Write(item, native, static_cast<std::uint16_t>(stat.layer), after.Pack())) { work.failed = true; return; }
            work.changed.emplace_back(stat.layer, after.Pack());
            Restored.fetch_add(after.current - before.current);
        }
    } catch (...) { work.failed = true; }
}

struct ReadWork { std::vector<D2RL::Items::ItemStatValue>* stats; bool success{}; };
void __cdecl ReadNativeStats(const D2RL::PluginContext*, void* item, void* data) noexcept {
    auto& work = *static_cast<ReadWork*>(data);
    Native::ItemState native{};
    if (!Native::Inspect(item, native) || native.bank >= ValidBank.size() || !ValidBank[native.bank]) return;
    work.success = Native::ReadStats(item, StatId, *work.stats);
}
auto Details(D2RL::ItemHandle handle, std::vector<D2RL::Items::ItemStatValue>& stats) -> bool {
    if (!HasItemDetails()) {
        ReadWork work{&stats};
        const auto result = Items->editNativeItem(Context, handle, ReadNativeStats, &work);
        return result == D2RL::Items::Result::Success && work.success;
    }
    D2RL::Items::ItemDetails details{.structSize = D2RL::Items::ItemDetailsSize};
    auto result = Items->getItemDetails(Context, handle, &details, nullptr, 0);
    if ((result != D2RL::Items::Result::Success && result != D2RL::Items::Result::InvalidArgument)
        || details.statCount == 0 || details.statCount > 4096) return false;
    stats.resize(details.statCount);
    result = Items->getItemDetails(Context, handle, &details, stats.data(), static_cast<std::uint32_t>(stats.size()));
    if (result != D2RL::Items::Result::Success || details.statCount > stats.size()) return false;
    stats.resize(details.statCount);
    return true;
}
void StopAfterError() noexcept {
    if (Ready.exchange(false)) Context->LogError("Charge Replenishment: a native update or charge verification failed; further replenishment is disabled until the next data load.");
}
auto __cdecl Visit(const D2RL::PluginContext*, const D2RL::Items::ItemInfo* item, void*) noexcept -> D2RL::Inventory::IterationAction {
    if (!Ready.load()) return D2RL::Inventory::IterationAction::Stop;
    if (!item || item->structSize < D2RL::Items::ItemInfoRequiredSize
        || (item->container != D2RL::Items::ItemContainer::Equipment
        && !(IncludeInventory && item->container == D2RL::Items::ItemContainer::Inventory))) return D2RL::Inventory::IterationAction::Continue;
    try {
        std::vector<D2RL::Items::ItemStatValue> stats;
        if (!Details(item->handle, stats)) return D2RL::Inventory::IterationAction::Continue;
        std::int64_t rate{};
        for (const auto& stat : stats) if (stat.statId == static_cast<std::uint32_t>(StatId) && stat.layer == 0) rate += stat.value;
        if (rate <= 0 || rate > MaximumRate) return D2RL::Inventory::IterationAction::Continue;
        Eligible.fetch_add(1);
        EditWork work{.handle = item->handle, .stats = &stats, .rate = static_cast<std::int32_t>(rate)};
        const auto result = Items->editNativeItem(Context, item->handle, Edit, &work);
        if (result != D2RL::Items::Result::Success) {
            if (result != D2RL::Items::Result::StaleHandle && result != D2RL::Items::Result::NotAuthoritative
                && result != D2RL::Items::Result::Unavailable) StopAfterError();
            return D2RL::Inventory::IterationAction::Continue;
        }
        if (work.failed) { StopAfterError(); return D2RL::Inventory::IterationAction::Stop; }
        if (!work.changed.empty()) {
            std::vector<D2RL::Items::ItemStatValue> actual;
            if (!Details(item->handle, actual)) { StopAfterError(); return D2RL::Inventory::IterationAction::Stop; }
            for (const auto& [layer, value] : work.changed) {
                unsigned matches{};
                for (const auto& stat : actual) matches += stat.statId == ChargedSkillStat && stat.layer == layer && stat.value == value;
                if (matches != 1) { StopAfterError(); return D2RL::Inventory::IterationAction::Stop; }
            }
        }
    } catch (...) { StopAfterError(); }
    return D2RL::Inventory::IterationAction::Continue;
}

void __cdecl GamePulse(const D2RL::PluginContext*, void*) noexcept {
    if (Active.load() && InGame.load() && Ready.load()) {
        const auto generation = Generation.load();
        if (GameGeneration != generation) { Timers.clear(); GameGeneration = generation; }
        ++Scan;
        Eligible.store(0);
        const D2RL::Inventory::ItemFilter filter{.structSize = D2RL::Inventory::ItemFilterSize,
            .containerMask = D2RL::Items::ContainerBit(D2RL::Items::ItemContainer::Equipment)
                | (IncludeInventory ? D2RL::Items::ContainerBit(D2RL::Items::ItemContainer::Inventory) : 0)};
        if (Inventory->forEachInventoryItem(Context, Player.load(), &filter, Visit, nullptr) == D2RL::Inventory::Result::Success) {
            std::erase_if(Timers, [](const auto& pair) { return pair.second.seen != Scan; });
        } else Timers.clear();
    }
    Pending.store(false);
}
void __cdecl UiPulse(const D2RL::PluginContext*, void*) noexcept {
    UiQueued.store(false);
    if (!Active.load() || !InGame.load()) return;
    // The SDK queues this for a later root-widget update, not an immediate recursive call.
    UiQueued.store(true);
    if (Threads->runOnUiThread(Context, UiPulse, nullptr) != D2RL::Threads::Result::Success) {
        UiQueued.store(false);
        Context->LogError("Charge Replenishment: UI scheduling failed; replenishment is suspended until a new game.");
        return;
    }
    const auto now = GetTickCount64();
    if (now < NextPoll || !InGame.load() || !Ready.load() || Pending.load()) return;
    NextPoll = now + 100;
    D2RL::PlayerHandle player{};
    if (Inventory->getLocalPlayer(Context, &player) != D2RL::Inventory::Result::Success || !player) return;
    Player.store(player);
    Pending.store(true);
    if (Threads->runOnGameThread(Context, GamePulse, nullptr) != D2RL::Threads::Result::Success) Pending.store(false);
}
void __cdecl Gameplay(const D2RL::PluginContext*, const D2RL::Lifecycle::GameplayEvent* event, void*) noexcept {
    if (!event || event->structSize < D2RL::Lifecycle::GameplayEventRequiredSize) return;
    Generation.store(event->sessionGeneration);
    if (event->kind == D2RL::Lifecycle::GameplayEventKind::GameLeft) {
        InGame.store(false); Player.store(0); Pending.store(false); Eligible.store(0);
    } else if (event->kind == D2RL::Lifecycle::GameplayEventKind::LocalPlayerReady) {
        InGame.store(true); Pending.store(false); NextPoll = 0;
        // Start only after activation, from this guaranteed UI-thread lifecycle callback.
        // No thread task is submitted while the plugin owner is still loading.
        if (Active.load() && !UiQueued.exchange(true)
            && Threads->runOnUiThread(Context, UiPulse, nullptr) != D2RL::Threads::Result::Success) {
            UiQueued.store(false);
            Context->LogError("Charge Replenishment: could not start UI scheduling for this game.");
        }
    }
}
auto __cdecl Status(D2R::Game::Client*, const D2RL::ConsoleCommandContext*, void*) noexcept -> D2RL::ConsoleCommandResult {
    char text[256]{};
    std::snprintf(text, sizeof(text), "Charge Replenishment 1.0.0: stat=%d, ready=%s, eligible items=%llu, charges restored=%llu. Interval: 100 / property value seconds.",
        StatId, Ready.load() ? "yes" : "no", static_cast<unsigned long long>(Eligible.load()), static_cast<unsigned long long>(Restored.load()));
    Context->WriteConsoleMessage(text);
    return D2RL::ConsoleCommandResult::Handled;
}
}
}

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept -> const D2RL::PluginInfo* { return &Recharge::Info; }
D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(const D2RL::PluginContext* context) noexcept -> bool {
    using namespace Recharge;
    if (!D2RL::HasContext(context)) return false;
    Context = context;
    if (!Setup::Ensure(context) || !Configuration()) return false;
    if (!Active.load()) return true;
    const D2RL::LifecycleService* lifecycle{};
    if (!RequireService(&Items, "Item", D2RL::ItemServiceRequiredSize)
        || !RequireService(&Inventory, "Inventory", D2RL::InventoryServiceRequiredSize)
        || !RequireService(&Threads, "Thread", D2RL::ThreadServiceRequiredSize)
        || !RequireService(&Tables, "DataTable", D2RL::DataTableServiceRequiredSize)
        || !RequireService(&lifecycle, "Lifecycle", D2RL::LifecycleServiceRequiredSize)) return false;
    if (!Items->editNativeItem || !Inventory->getLocalPlayer || !Inventory->forEachInventoryItem
        || !Threads->runOnUiThread || !Threads->runOnGameThread || !Tables->getTable
        || !lifecycle->registerDataTablesLoadedListener || !lifecycle->registerGameplayEventListener) {
        context->LogError("Charge Replenishment: a required SDK function pointer is missing."); return false;
    }
    D2RL::Lifecycle::ListenerHandle handle{};
    const D2RL::Lifecycle::DataTablesLoadedListener tableListener{.structSize = D2RL::Lifecycle::DataTablesLoadedListenerSize, .callback = OnTables};
    if (lifecycle->registerDataTablesLoadedListener(context, &tableListener, &handle) != D2RL::Lifecycle::Result::Success) return false;
    for (const auto kind : {D2RL::Lifecycle::GameplayEventKind::LocalPlayerReady, D2RL::Lifecycle::GameplayEventKind::GameLeft}) {
        const D2RL::Lifecycle::GameplayEventListener listener{.structSize = D2RL::Lifecycle::GameplayEventListenerSize, .kind = kind, .callback = Gameplay};
        if (lifecycle->registerGameplayEventListener(context, &listener, &handle) != D2RL::Lifecycle::Result::Success) return false;
    }
    if (!context->RegisterConsoleCommand("charge-replenishment", Status, "Show charge replenishment status.")) return false;
    context->LogInfo(HasItemDetails()
        ? "Charge Replenishment 1.0.0 loaded using SDK item details; waiting for validated tables."
        : "Charge Replenishment 1.0.0 loaded using compatible native item inspection; waiting for validated tables.");
    return true;
}
D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
    Recharge::Active.store(false);
    Recharge::Ready.store(false);
    // The SDK discards queued tasks and unregisters this plugin's listeners on unload.
}
