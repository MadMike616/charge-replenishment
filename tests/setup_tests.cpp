// Exercise the real installer and the same embedded resources shipped in the DLL.
#include "../src/setup.cpp"
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <atomic>
#include <thread>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n%s\n", __LINE__, #x, Tests::Error.c_str()); std::exit(1); } } while (false)

namespace Tests {
namespace fs = std::filesystem;
using namespace Recharge::Setup::Data;
std::string Error;
unsigned Writes{}, FailWrite{}, Infos{};
bool VerifySharedLock{};
unsigned SharedLockChecks{};
HANDLE ConfigReadEvent{};
std::atomic<unsigned> ConfigReads{};
// Probe from a different process: Win32 mutex ownership is recursive on the
// owning thread, so checking it on the installer thread would prove nothing.
auto ProbeSharedLock() -> DWORD {
    std::array<wchar_t, 32768> executable{};
    CHECK(GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size())));
    auto command = L"\"" + std::wstring(executable.data()) + L"\" --probe-shared-install-lock";
    STARTUPINFOW startup{.cb = sizeof(STARTUPINFOW)};
    PROCESS_INFORMATION process{};
    CHECK(CreateProcessW(executable.data(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
        nullptr, nullptr, &startup, &process));
    const auto wait = WaitForSingleObject(process.hProcess, 5000);
    if (wait != WAIT_OBJECT_0) TerminateProcess(process.hProcess, 99);
    DWORD exitCode{}; CHECK(GetExitCodeProcess(process.hProcess, &exitCode));
    CloseHandle(process.hThread); CloseHandle(process.hProcess);
    CHECK(wait == WAIT_OBJECT_0);
    return exitCode;
}
auto Bytes(const fs::path& path) -> std::string {
    std::ifstream stream(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}
void Save(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary); out << text; out.close(); CHECK(out.good());
}
auto __cdecl ReadConfig(const D2RL::PluginContext* ctx, char* output, std::uint32_t capacity, std::uint32_t* required) noexcept -> bool {
    try {
        const auto text = Bytes(ctx->pluginConfigPath);
        ++ConfigReads;
        if (ConfigReadEvent) SetEvent(ConfigReadEvent);
        *required = static_cast<std::uint32_t>(text.size() + 1);
        if (capacity < *required) return false;
        std::memcpy(output, text.c_str(), *required); return true;
    } catch (...) { return false; }
}
auto __cdecl WriteConfig(const D2RL::PluginContext* ctx, const char* text) noexcept -> bool {
    if (VerifySharedLock) { CHECK(ProbeSharedLock() == 10); ++SharedLockChecks; }
    if (++Writes == FailWrite) return false;
    try { Save(ctx->pluginConfigPath, text); return true; } catch (...) { return false; }
}
void __cdecl LogError(const D2RL::PluginContext*, const char* message) noexcept { Error = message; }
void __cdecl LogInfo(const D2RL::PluginContext*, const char*) noexcept { ++Infos; }
D2RL::PluginApi Api{.apiSize = D2RL::PluginApiSize, .logInfo = LogInfo, .logError = LogError,
    .readConfig = ReadConfig, .writeConfig = WriteConfig};
auto Encode(const std::vector<std::string>& header, const Row& row) -> std::string {
    std::string text; bool first{true};
    for (const auto& column : header) { if (!first) text += '\t'; first = false; const auto it = row.find(column); text += it == row.end() ? "" : it->second; }
    return text + "\r\n";
}
struct Fixture {
    fs::path game, mod, data, config, mirror;
    D2RL::PluginContext context{};
    Fixture(const fs::path& root, const std::string& name, bool withMirror = true) {
        static unsigned sequence{};
        game = root / (name + "-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64()) + "-" + std::to_string(++sequence));
        mod = game / L"mods/TestMod"; data = mod / L"TestMod.mpq/data";
        config = mod / L"d2rloader/config/charge-replenishment.toml";
        mirror = game / L"d2rloader/I Hate TOMLs/TestMod/charge-replenishment.toml";
        std::vector<std::string> columns{"Stat", "*ID", "Signed", "Send Bits", "Save Bits", "Save Add", "descpriority", "descfunc", "descval", "descstrpos", "descstrneg", "*eol", "*Creator", "extra"};
        std::reverse(columns.begin(), columns.end());
        Row header; for (const auto& column : columns) header[column] = column;
        auto stats = Encode(columns, header);
        for (int id = 0; id < 399; ++id) stats += Encode(columns, {{"Stat", id == 204 ? "item_charged_skill" : "stat_" + std::to_string(id)},
            {"*ID", std::to_string(id)}, {"Save Bits", id == 204 ? "16" : ""}, {"*Creator", "Original"}, {"extra", "Keep"}});
        Save(data / L"global/excel/itemstatcost.txt", stats);
        columns = {"code", "*Id", "*Enabled", "func1", "stat1", "uiRangeType", "*Tooltip", "*Parameter", "*Min", "*Max", "*Notes", "*eol", "*Creator", "extra"};
        std::reverse(columns.begin(), columns.end()); header.clear(); for (const auto& column : columns) header[column] = column;
        Save(data / L"global/excel/properties.txt", Encode(columns, header) + Encode(columns, {{"code", "old"}, {"*Id", "0"}, {"extra", "Keep"}}) + Encode(columns, {{"code", "Expansion"}}));
        Save(data / L"local/lng/strings/item-modifiers.json", "[\r\n  {\"id\":10000,\"Key\":\"existing\",\"enUS\":\"Keep // ,} \\\"quote\\\"\",}, // trailing comment,\r\n]\r\n");
        Save(data / L"local/lng/strings/skills.json", "[{\"id\":10001,\"Key\":\"skill\"}]\n");
        Save(data / L"global/excel/itemstatcost.bin", "stale stat BIN");
        Save(data / L"global/excel/properties.bin", "stale property BIN");
        Save(config, "[charge-replenishment]\nenabled = true\nstat_id = -1 # allocated\ninclude_inventory = false\n\n[other-plugin]\nkeep = 17\n");
        if (withMirror) Save(mirror, Bytes(config));
        context = {.contextSize = D2RL::PluginContextSize, .abiVersion = 4, .api = &Api, .loadScope = D2RL::LoadScope::Mod,
            .activeMod = "TestMod", .modDirectory = mod.c_str(), .pluginConfigPath = config.c_str(), .scopeRootDirectory = mod.c_str()};
        Error.clear(); Writes = FailWrite = Infos = SharedLockChecks = 0; ConfigReads = 0;
    }
    auto Run() -> bool { return Recharge::Setup::Ensure(&context); }
};
auto Snapshot(const fs::path& root) -> std::map<fs::path, std::string> {
    std::map<fs::path, std::string> result;
    for (const auto& file : fs::recursive_directory_iterator(root)) {
        if (file.is_regular_file() && file.path().wstring().find(L"basefiles-and-backups\\backups") == std::wstring::npos)
            result[file.path().lexically_relative(root)] = Bytes(file.path());
    }
    return result;
}
}
int main(int argc, char** argv) {
    using namespace Tests;
    if (argc == 2 && std::string_view(argv[1]) == "--probe-shared-install-lock") {
        const auto lock = CreateMutexW(nullptr, FALSE, L"Local\\D2RLoader.ModFirstRunInstall.v1");
        if (!lock) return 20;
        const auto wait = WaitForSingleObject(lock, 0);
        if (wait == WAIT_OBJECT_0 || wait == WAIT_ABANDONED) ReleaseMutex(lock);
        CloseHandle(lock);
        return wait == WAIT_TIMEOUT ? 10 : wait == WAIT_OBJECT_0 || wait == WAIT_ABANDONED ? 0 : 20;
    }
    fs::path root;
    if (argc > 1) root = fs::absolute(argv[1]);
    else { std::array<wchar_t, 32768> path{}; CHECK(GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()))); root = fs::path(path.data()).parent_path() / L"setup-fixtures"; }
    fs::create_directories(root);
    {
        Fixture f(root, "fresh");
        const auto originalStat = Bytes(f.data / L"global/excel/itemstatcost.txt");
        const auto originalProp = Bytes(f.data / L"global/excel/properties.txt");
        VerifySharedLock = true;
        CHECK(f.Run()); CHECK(Error.empty()); CHECK(Writes == 1); CHECK(Infos == 1);
        VerifySharedLock = false;
        CHECK(SharedLockChecks == 1); CHECK(ProbeSharedLock() == 0);
        const auto config = Bytes(f.config);
        CHECK(ConfigValue(config, "charge-replenishment", "stat_id") == "399");
        CHECK(ConfigValue(config, "charge-replenishment", "include_inventory") == "false");
        CHECK(ConfigValue(config, "installation", "complete") == "true");
        CHECK(ConfigValue(config, "other-plugin", "keep") == "17"); CHECK(Bytes(f.mirror) == config);
        CHECK(Bytes(f.data / L"global/excel/itemstatcost.txt").starts_with(originalStat));
        CHECK(Bytes(f.data / L"global/excel/properties.txt").starts_with(originalProp));
        const Table stats(Bytes(f.data / L"global/excel/itemstatcost.txt"), "Stat");
        CHECK(stats.rows.back().at("Stat") == Stat); CHECK(stats.rows.back().at("*Creator") == "MadMike"); CHECK(stats.rows.back().at("extra").empty());
        const auto strings = Bytes(f.data / L"local/lng/strings/item-modifiers.json");
        CHECK(strings.find("// trailing comment,") != strings.npos);
        const auto array = Array(strings); CHECK(array.size() == 2); CHECK(array.back().size() == 15);
        CHECK(array.back()["enUS"] == "Replenishes %d Charges per 100 Seconds");
        const auto catalog = ParseJson(Recharge::Setup::Resource(CR_SETUP_RESERVED_IDS));
        const int stringId = array.back().at("id").get<int>();
        for (const auto& range : catalog.at("inclusive_ranges")) CHECK(stringId < range[0].get<int>() || stringId > range[1].get<int>());
        CHECK(!fs::exists(f.data / L"global/excel/itemstatcost.bin")); CHECK(!fs::exists(f.data / L"global/excel/properties.bin"));
        const auto before = Snapshot(f.game); const auto count = Writes;
        CHECK(f.Run()); CHECK(Writes == count); CHECK(Snapshot(f.game) == before);
        // A completed installation performs no table reads or merging.
        Save(f.data / L"global/excel/itemstatcost.txt", "deliberately unreadable table content");
        CHECK(f.Run()); CHECK(Writes == count);
    }
    {
        Fixture f(root, "missing"); fs::remove(f.data / L"global/excel/properties.txt");
        const auto before = Snapshot(f.game); CHECK(!f.Run()); CHECK(Error.find("properties.txt") != Error.npos);
        CHECK(Snapshot(f.game) == before); CHECK(Writes == 0);
    }
    {
        Fixture f(root, "basefiles", false);
        const auto base = f.mod / L"d2rloader/plugins/basefiles-and-backups/basefiles/data";
        for (const auto& relative : {fs::path(L"global/excel/itemstatcost.txt"), fs::path(L"global/excel/properties.txt"), fs::path(L"local/lng/strings/item-modifiers.json")}) {
            Save(base / relative, Bytes(f.data / relative)); fs::remove(f.data / relative);
        }
        CHECK(f.Run()); CHECK(!fs::exists(f.mirror));
        CHECK(fs::exists(f.data / L"global/excel/itemstatcost.txt"));
        CHECK(Array(Bytes(f.data / L"local/lng/strings/item-modifiers.json")).size() == 2);
    }
    {
        Fixture f(root, "rollback"); const auto before = Snapshot(f.game); FailWrite = 1;
        VerifySharedLock = true;
        CHECK(!f.Run()); CHECK(Error.find("loader rejected") != Error.npos); CHECK(Snapshot(f.game) == before);
        VerifySharedLock = false;
        CHECK(SharedLockChecks == 2); CHECK(ProbeSharedLock() == 0);
        FailWrite = 0; CHECK(f.Run()); CHECK(ConfigValue(Bytes(f.config), "installation", "complete") == "true");
    }
    {
        Fixture f(root, "lock-wait-token-recheck");
        // If setup plans edits after the wait without checking the token again,
        // this malformed table would fail. No mod files may change while waiting.
        Save(f.data / L"global/excel/itemstatcost.txt", "not a valid table");
        const auto before = Snapshot(f.game);
        const auto lock = CreateMutexW(nullptr, TRUE, L"Local\\D2RLoader.ModFirstRunInstall.v1"); CHECK(lock);
        ConfigReadEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr); CHECK(ConfigReadEvent);
        bool result{};
        std::thread installer([&] { result = f.Run(); });
        const auto read = WaitForSingleObject(ConfigReadEvent, 5000);
        CHECK(read == WAIT_OBJECT_0); CHECK(Snapshot(f.game) == before); CHECK(Writes == 0);
        auto completed = Upsert(Bytes(f.config), "charge-replenishment", "stat_id", "399");
        completed = Upsert(completed, "installation", "schema_version", "1");
        completed = Upsert(completed, "installation", "complete", "true");
        Save(f.config, completed);
        const auto afterOtherInstaller = Snapshot(f.game);
        CHECK(ReleaseMutex(lock)); installer.join();
        CloseHandle(lock); CloseHandle(ConfigReadEvent); ConfigReadEvent = nullptr;
        CHECK(result); CHECK(ConfigReads >= 2); CHECK(Writes == 0); CHECK(Infos == 0);
        CHECK(Snapshot(f.game) == afterOtherInstaller); CHECK(ProbeSharedLock() == 0);
    }
    {
        Fixture f(root, "bom-rollback"); Save(f.config, std::string("\xEF\xBB\xBF") + Bytes(f.config));
        const auto before = Snapshot(f.game); FailWrite = 1;
        CHECK(!f.Run()); CHECK(Snapshot(f.game) == before);
        FailWrite = 0; CHECK(f.Run()); CHECK(Bytes(f.config).starts_with("\xEF\xBB\xBF")); CHECK(f.Run());
    }
    {
        Fixture f(root, "existing"); CHECK(f.Run());
        auto config = Upsert(Bytes(f.config), "installation", "complete", "false"); Save(f.config, config);
        const auto stat = Bytes(f.data / L"global/excel/itemstatcost.txt");
        const auto prop = Bytes(f.data / L"global/excel/properties.txt");
        const auto strings = Bytes(f.data / L"local/lng/strings/item-modifiers.json");
        CHECK(f.Run()); CHECK(Bytes(f.data / L"global/excel/itemstatcost.txt") == stat);
        CHECK(Bytes(f.data / L"global/excel/properties.txt") == prop); CHECK(Bytes(f.data / L"local/lng/strings/item-modifiers.json") == strings);
        const auto ownedId = Array(strings).back().at("id").get<int>();
        Save(f.data / L"local/lng/strings/another.json", Json::array({Json{{"id", ownedId}, {"Key", "another"}}}).dump());
        Save(f.config, Upsert(Bytes(f.config), "installation", "complete", "false"));
        CHECK(f.Run()); CHECK(Array(Bytes(f.data / L"local/lng/strings/item-modifiers.json")).back().at("id") != ownedId);
    }
    {
        Fixture f(root, "collision"); CHECK(f.Run());
        // Reject a semantic property conflict without changing any file.
        auto prop = Bytes(f.data / L"global/excel/properties.txt");
        const auto offset = prop.rfind("item_replenish_charges"); CHECK(offset != prop.npos);
        prop.replace(offset, std::strlen("item_replenish_charges"), "other_stat"); Save(f.data / L"global/excel/properties.txt", prop);
        Save(f.config, Upsert(Bytes(f.config), "installation", "complete", "false"));
        const auto before = Snapshot(f.game); CHECK(!f.Run()); CHECK(Snapshot(f.game) == before);
    }
    {
        Fixture f(root, "packed"); fs::rename(f.mod / L"TestMod.mpq", f.mod / L"saved-loose-data"); Save(f.mod / L"TestMod.mpq", "MPQ");
        const auto before = Snapshot(f.game); CHECK(!f.Run()); CHECK(Error.find("packed MPQ") != Error.npos); CHECK(Snapshot(f.game) == before);
    }
    {
        Fixture f(root, "disabled"); Save(f.config, Upsert(Bytes(f.config), "charge-replenishment", "enabled", "false"));
        const auto before = Snapshot(f.game); CHECK(f.Run()); CHECK(Snapshot(f.game) == before); CHECK(Writes == 0);
    }
    {
        Fixture f(root / L"Unicode-\u00E5\u6D4B\u8BD5", "mirror-preservation");
        Save(f.mirror, Bytes(f.mirror) + "\n[mirror-only]\nkeep = 23\n");
        CHECK(f.Run()); CHECK(ConfigValue(Bytes(f.mirror), "mirror-only", "keep") == "23");
        CHECK(ConfigValue(Bytes(f.mirror), "charge-replenishment", "stat_id") == "399");
        // Inline section comments are retained without creating duplicate sections.
        const auto text = Upsert("[installation] # keep\ncomplete = false\n", "installation", "complete", "true");
        CHECK(ConfigValue(text, "installation", "complete") == "true"); CHECK(text.find("# keep") != text.npos);
    }
    {
        Json entry{{"id", 10038}, {"Key", StringKey}, {"enUS", "Charges %d"}};
        for (const auto& text : {"[]", "[{}] // a comma, in a comment", "[{\"id\":1,}, /* comma, */]", "[\n// comment,\n]"}) {
            const auto added = Localize(text, entry); CHECK(Array(added).back() == entry);
        }
        bool rejected{};
        try { Localize("[{\"id\":3,\"Key\":\"ModChargeReplenishmentRate\",\"enUS\":\"Different\"}]", entry); } catch (...) { rejected = true; }
        CHECK(rejected);
    }
    {
        bool rejected{};
        try { Table("code\tcode\n", "code"); } catch (...) { rejected = true; } CHECK(rejected);
        rejected = false; try { Table("code\t\n", "code"); } catch (...) { rejected = true; } CHECK(rejected);
        rejected = false; try { Table("code\n", "code").Add("code\n", {{"code", "new"}, {"missing", "value"}}); } catch (...) { rejected = true; } CHECK(rejected);
        CHECK(Table("code\n", "code").Add("code\n", {{"code", "new"}, {"*Creator", "MadMike"}}) == "code\nnew\n");
        rejected = false; try { Table("code\t*Id\none\t0\ntwo\t2\n", "code").NextId("*Id", 65534); } catch (...) { rejected = true; } CHECK(rejected);
    }
    std::puts("Native first-run setup tests passed.");
}
