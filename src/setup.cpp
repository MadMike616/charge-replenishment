#include "setup.h"
#include "setup_data.h"
#include "setup_resources.h"
#include <windows.h>
#include <array>
#include <filesystem>
#include <fstream>
#include <optional>
#include <system_error>

namespace Recharge::Setup {
namespace {
namespace fs = std::filesystem;
using namespace Data;
constexpr auto BaseFiles = L"d2rloader/plugins/basefiles-and-backups/basefiles";
constexpr auto BackupFiles = L"d2rloader/plugins/basefiles-and-backups/backups/charge-replenishment";
constexpr auto ConfigFile = L"d2rloader/config/charge-replenishment.toml";
constexpr auto Missing = INVALID_FILE_ATTRIBUTES;

[[noreturn]] void WindowsError(const char* operation) {
    throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), operation);
}
auto Extended(fs::path path) -> fs::path {
    const auto name = path.make_preferred().native();
    if (name.starts_with(L"\\\\?\\")) return path;
    if (name.starts_with(L"\\\\")) return L"\\\\?\\UNC\\" + name.substr(2);
    return L"\\\\?\\" + name;
}
auto Attributes(const fs::path& path) -> DWORD {
    const auto result = GetFileAttributesW(Extended(path).c_str());
    if (result != Missing || GetLastError() == ERROR_FILE_NOT_FOUND || GetLastError() == ERROR_PATH_NOT_FOUND) return result;
    WindowsError("checking setup path");
}
auto Checked(const fs::path& root, const fs::path& relative) -> DWORD {
    if (relative.empty() || relative.has_root_path()) Fail("invalid relative setup path");
    auto path = root;
    for (auto it = relative.begin(); it != relative.end(); ++it) {
        if (*it == L"..") Fail("setup path leaves its root");
        path /= *it;
        const auto attributes = Attributes(path);
        if (attributes == Missing) return Missing;
        if (attributes & FILE_ATTRIBUTE_REPARSE_POINT) Fail("setup paths cannot use links or junctions");
        if (std::next(it) == relative.end()) return attributes;
        if (!(attributes & FILE_ATTRIBUTE_DIRECTORY)) Fail("setup directory is occupied by a file");
    }
    Fail("empty setup path");
}
auto Read(const fs::path& root, const fs::path& relative) -> std::optional<std::string> {
    const auto attributes = Checked(root, relative);
    if (attributes == Missing) return {};
    if (attributes & FILE_ATTRIBUTE_DIRECTORY) Fail("setup file path is occupied by a directory");
    std::ifstream stream(Extended(root / relative), std::ios::binary | std::ios::ate);
    const auto size = stream.tellg();
    if (!stream || size < 0 || size > 64 * 1024 * 1024) Fail("cannot read setup file or file exceeds 64 MiB");
    std::string text(static_cast<std::size_t>(size), '\0');
    stream.seekg(0);
    if (!stream.read(text.data(), static_cast<std::streamsize>(text.size()))) Fail("cannot read complete setup file");
    return text;
}
void Write(const fs::path& root, const fs::path& relative, const std::optional<std::string>& text) {
    Checked(root, relative);
    const auto target = Extended(root / relative);
    if (!text) { if (!DeleteFileW(target.c_str())) WindowsError("invalidating stale BIN file"); return; }
    fs::create_directories(target.parent_path());
    // A private sibling directory provides a new, nonshared staging filename.
    const auto staging = target.parent_path() / (L".charge-replenishment-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
    if (!CreateDirectoryW(staging.c_str(), nullptr)) WindowsError("creating setup staging directory");
    const auto temporary = staging / L"write.tmp";
    try {
        std::ofstream out(temporary, std::ios::binary);
        out.write(text->data(), static_cast<std::streamsize>(text->size())); out.close();
        if (!out) Fail("cannot write setup staging file");
        if (!MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) WindowsError("committing setup file");
    } catch (...) { DeleteFileW(temporary.c_str()); RemoveDirectoryW(staging.c_str()); throw; }
    RemoveDirectoryW(staging.c_str());
    if (Read(root, relative) != text) Fail("setup file verification failed");
}
auto Same(const fs::path& left, const fs::path& right) -> bool {
    return CompareStringOrdinal(left.c_str(), -1, right.c_str(), -1, TRUE) == CSTR_EQUAL;
}
struct Location { fs::path root, data; };
auto Locate(const D2RL::PluginContext& context) -> Location {
    if (context.loadScope != D2RL::LoadScope::Mod || !context.activeMod) Fail("automatic setup requires an active mod");
    const std::string_view name(context.activeMod);
    const fs::path mod(std::u8string(name.begin(), name.end()));
    if (name.empty() || mod.has_root_path() || mod.has_parent_path() || mod == L"." || mod == L".."
        || mod.native().find_first_of(L"<>:\"/\\|?*") != std::wstring::npos
        || mod.native().back() == L'.' || mod.native().back() == L' ') Fail("invalid active mod name");
    const fs::path package = mod.native() + L".mpq";
    for (const auto* directory : {context.modDirectory, context.scopeRootDirectory}) {
        if (!directory || !*directory) continue;
        auto path = fs::path(directory).lexically_normal();
        if (!path.is_absolute()) continue;
        if (path.filename().empty()) path = path.parent_path();
        if (Same(path.filename(), package)) path = path.parent_path();
        for (const auto& candidate : {path, path / mod, path / L"mods" / mod}) {
            if (!Same(candidate.filename(), mod)) continue;
            const auto attributes = Attributes(candidate);
            if (attributes == Missing || !(attributes & FILE_ATTRIBUTE_DIRECTORY) || (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) continue;
            const auto root = fs::canonical(candidate);
            const auto archive = Checked(root, package);
            if (archive != Missing) {
                if (!(archive & FILE_ATTRIBUTE_DIRECTORY)) Fail("packed MPQ mods require a loose mod folder for automatic setup");
                return {root, package / L"data"};
            }
            const auto data = Checked(root, L"data");
            if (data != Missing && (data & FILE_ATTRIBUTE_DIRECTORY)) return {root, L"data"};
        }
    }
    Fail("cannot locate the active mod's loose data folder");
}
class InstallLock {
    HANDLE handle{};
public:
    InstallLock() {
        handle = CreateMutexW(nullptr, FALSE, L"Local\\D2RLoader.ModFirstRunInstall.v1");
        if (!handle) WindowsError("opening shared first-run setup lock");
        const auto result = WaitForSingleObject(handle, 10'000);
        if (result == WAIT_OBJECT_0 || result == WAIT_ABANDONED) return;
        CloseHandle(handle); Fail("another mod setup is busy; restart D2RLoader to retry");
    }
    ~InstallLock() { ReleaseMutex(handle); CloseHandle(handle); }
    InstallLock(const InstallLock&) = delete;
    InstallLock& operator=(const InstallLock&) = delete;
};
auto Resource(int id) -> std::string_view {
    static const char anchor{};
    HMODULE module{};
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&anchor), &module)) WindowsError("locating bundled setup resources");
    const auto resource = FindResourceW(module, MAKEINTRESOURCEW(id), RT_RCDATA);
    if (!resource) Fail("bundled setup resource is missing");
    const auto* bytes = static_cast<const char*>(LockResource(LoadResource(module, resource)));
    const auto size = SizeofResource(module, resource);
    if (!bytes || !size) Fail("cannot read bundled setup resource");
    return {bytes, size};
}
auto Configuration(const D2RL::PluginContext& context) -> std::string {
    std::vector<char> bytes(65536); std::uint32_t required{};
    if (!context.ReadConfig(bytes.data(), static_cast<std::uint32_t>(bytes.size()), &required) || required > bytes.size()) Fail("cannot read plugin configuration");
    bytes.back() = '\0';
    return std::string(bytes.data());
}
auto Complete(const std::string& config) -> bool {
    const auto token = ConfigValue(config, "installation", "complete");
    if (token && *token != "true" && *token != "false") Fail("installation.complete must be true or false");
    if (!token || *token != "true") return false;
    const auto schema = ConfigValue(config, "installation", "schema_version");
    const auto stat = ConfigValue(config, "charge-replenishment", "stat_id");
    if (!schema || *schema != "1" || !stat || Number(*stat) < 398 || Number(*stat) > 510)
        Fail("invalid completed installation binding; restore the correct configuration or set installation.complete = false to retry setup");
    return true;
}
struct Edit { fs::path root, relative; std::optional<std::string> before, after; };
void Add(std::vector<Edit>& edits, const fs::path& root, const fs::path& relative, std::optional<std::string> after) {
    auto before = Read(root, relative);
    if (before != after) edits.push_back({root, relative, std::move(before), std::move(after)});
}
auto Source(const Location& location, const fs::path& relative) -> std::string {
    if (auto text = Read(location.root, location.data / relative)) return *text;
    for (const auto& prefix : {fs::path(BaseFiles) / L"data", fs::path(BaseFiles)})
        if (auto text = Read(location.root, prefix / relative)) return *text;
    const auto name = relative.filename().u8string();
    Fail("missing " + std::string(name.begin(), name.end()) + "; extract it with Ladiks Casc Viewer into this mod's d2rloader/plugins/basefiles-and-backups/basefiles folder, then restart D2RLoader");
}
void StringIds(const fs::path& root, const fs::path& directory, std::set<int>& ids,
               std::vector<std::pair<fs::path, Json>>& owned, bool active) {
    if (Checked(root, directory) == Missing) return;
    for (const auto& file : fs::directory_iterator(Extended(root / directory))) {
        if (!Same(file.path().extension(), L".json")) continue;
        const auto relative = directory / file.path().filename();
        const auto text = Read(root, relative);
        if (!text) Fail("localization file disappeared during setup");
        for (const auto& entry : Array(*text)) {
            if (entry.value("Key", "") == StringKey) { if (active) owned.emplace_back(relative, entry); }
            else if (entry.contains("id")) {
                if (!entry["id"].is_number_integer()) Fail("localization ID is not an integer");
                ids.insert(entry["id"].get<int>());
            }
        }
    }
}
auto Plan(const Location& location, const std::string& config) -> std::vector<Edit> {
    std::vector<Edit> edits;
    const auto statPath = fs::path(L"global/excel/itemstatcost.txt");
    const auto propertyPath = fs::path(L"global/excel/properties.txt");
    const auto stringPath = fs::path(L"local/lng/strings/item-modifiers.json");
    const auto statBytes = Source(location, statPath);
    const Table stats(statBytes, "Stat");
    const auto nextStat = stats.NextId("*ID", 511);
    const auto* charged = stats.Find("item_charged_skill");
    if (!charged || charged->at("*ID") != "204" || charged->at("Save Bits") != "16") Fail("required vanilla charged-skill stat schema is missing");
    const auto* existing = stats.Find(Stat);
    const auto id = existing ? Number(existing->at("*ID")) : nextStat;
    if (id < 398 || id > 510) Fail("no supported appended custom stat ID remains");
    const Row marker{{"Stat", Stat}, {"*ID", std::to_string(id)}, {"Signed", "0"}, {"Send Bits", "10"},
        {"Save Bits", "10"}, {"Save Add", "0"}, {"descpriority", "159"}, {"descfunc", "19"}, {"descval", "0"},
        {"descstrpos", StringKey}, {"descstrneg", StringKey}, {"*eol", "0"}};
    const auto newStat = stats.Add(statBytes, marker);
    const auto propertyBytes = Source(location, propertyPath);
    const Table properties(propertyBytes, "code");
    const auto nextProperty = properties.NextId("*Id", 65535);
    existing = properties.Find(Property);
    const auto propertyId = existing ? Number(existing->at("*Id")) : nextProperty;
    if (propertyId > 65534) Fail("no appended property ID remains");
    const Row property{{"code", Property}, {"*Id", std::to_string(propertyId)}, {"*Enabled", "1"}, {"func1", "1"}, {"stat1", Stat},
        {"uiRangeType", "7"}, {"*Tooltip", "%d Charges Replenished per 100 Seconds"}, {"*Parameter", "Unused"},
        {"*Min", "Minimum recharge rate (1-1023)"}, {"*Max", "Maximum recharge rate (1-1023)"},
        {"*Notes", "MadMike: 100 / value seconds per charge; requires Charge Replenishment."}, {"*eol", "0"}};
    const auto newProperty = properties.Add(propertyBytes, property);
    const auto translations = ParseJson(Resource(CR_SETUP_TRANSLATIONS));
    const std::set<std::string> languages{"enUS", "zhTW", "deDE", "esES", "frFR", "itIT", "koKR", "plPL", "esMX", "jaJP", "ptBR", "ruRU", "zhCN"};
    std::set<std::string> supplied;
    for (const auto& [key, value] : translations.items()) if (key != "Key") { if (!value.is_string()) Fail("invalid bundled translation"); supplied.insert(key); }
    if (supplied != languages || translations.value("Key", "") != StringKey) Fail("bundled translations must cover all 13 languages");
    const auto stringBytes = Source(location, stringPath);
    std::set<int> ids;
    const auto catalog = ParseJson(Resource(CR_SETUP_RESERVED_IDS));
    for (const auto& range : catalog.at("inclusive_ranges"))
        for (auto i = range.at(0).get<int>(); i <= range.at(1).get<int>(); ++i) ids.insert(i);
    std::vector<std::pair<fs::path, Json>> owned;
    StringIds(location.root, location.data / L"local/lng/strings", ids, owned, true);
    for (const auto& base : {fs::path(BaseFiles) / L"data/local/lng/strings", fs::path(BaseFiles) / L"local/lng/strings"})
        StringIds(location.root, base, ids, owned, false);
    if (!Read(location.root, location.data / stringPath)) {
        for (const auto& entry : Array(stringBytes)) {
            if (entry.value("Key", "") == StringKey) owned.emplace_back(location.data / stringPath, entry);
            else if (entry.contains("id")) ids.insert(entry["id"].get<int>());
        }
    }
    int stringId{};
    if (!owned.empty()) {
        if (owned.size() != 1 || owned.front().first != location.data / stringPath) Fail("localization key collision in another string file");
        stringId = owned.front().second.at("id").get<int>();
    }
    if (owned.empty() || stringId < 0 || stringId >= 65535 || ids.contains(stringId)) {
        for (stringId = 10000; stringId < 65535 && ids.contains(stringId); ++stringId) {}
        if (stringId == 65535) Fail("no free localization ID remains");
    }
    Json entry{{"id", stringId}}; entry.update(translations);
    const auto newStrings = Localize(stringBytes, entry);
    Add(edits, location.root, location.data / statPath, newStat);
    Add(edits, location.root, location.data / propertyPath, newProperty);
    Add(edits, location.root, location.data / stringPath, newStrings);
    const bool rebound = !owned.empty() && owned.front().second.at("id").get<int>() != stringId;
    for (const auto& path : {statPath, propertyPath}) {
        const bool changed = path == statPath ? statBytes != newStat || rebound : propertyBytes != newProperty;
        if (changed || !Read(location.root, location.data / path)) {
            auto bin = location.data / path; bin.replace_extension(L".bin");
            Add(edits, location.root, bin, std::nullopt);
        }
    }
    auto updated = Upsert(config, "charge-replenishment", "stat_id", std::to_string(id));
    updated = Upsert(updated, "installation", "schema_version", "1");
    updated = Upsert(updated, "installation", "complete", "true");
    // This optional configuration mirror is updated only if its integration exists.
    const auto gameRoot = location.root.parent_path().parent_path();
    if (Same(location.root.parent_path().filename(), L"mods") && Checked(gameRoot, L"d2rloader/I Hate TOMLs") != Missing) {
        const auto relative = fs::path(L"d2rloader/I Hate TOMLs") / location.root.filename() / L"charge-replenishment.toml";
        auto mirrored = Read(gameRoot, relative).value_or(config);
        for (const auto key : {"enabled", "stat_id", "include_inventory"}) {
            const auto value = ConfigValue(updated, "charge-replenishment", key);
            if (value) mirrored = Upsert(mirrored, "charge-replenishment", key, *value);
        }
        mirrored = Upsert(mirrored, "installation", "schema_version", "1");
        mirrored = Upsert(mirrored, "installation", "complete", "true");
        Add(edits, gameRoot, relative, mirrored);
    }
    // The active TOML, containing the completion token, is always committed last.
    Add(edits, location.root, ConfigFile, updated);
    return edits;
}
void Apply(const D2RL::PluginContext& context, const Location& location, const std::vector<Edit>& edits, const std::string& originalConfig) {
    if (edits.empty()) return;
    const auto backup = fs::path(BackupFiles) / (std::to_wstring(GetTickCount64()) + L"-" + std::to_wstring(GetCurrentProcessId()));
    Json manifest = Json::array();
    for (std::size_t i = 0; i < edits.size(); ++i) {
        const auto& edit = edits[i];
        const auto name = std::to_wstring(i) + L"-" + edit.relative.filename().native();
        if (edit.before) Write(location.root, backup / name, edit.before);
        const auto fileName = fs::path(name).generic_u8string();
        const auto original = (edit.root / edit.relative).generic_u8string();
        manifest.push_back({{"original", std::string(original.begin(), original.end())},
            {"backup", edit.before ? Json(std::string(fileName.begin(), fileName.end())) : Json(nullptr)}});
    }
    Write(location.root, backup / L"manifest.json", manifest.dump(2) + "\n");
    std::size_t applied{}; bool configAttempted{};
    try {
        for (const auto& edit : edits) {
            if (Read(edit.root, edit.relative) != edit.before) Fail("a setup file changed while installation was being prepared");
            ++applied; // Include partial write failures in rollback.
            if (edit.root == location.root && edit.relative == ConfigFile) {
                configAttempted = true;
                if (!context.WriteConfig(edit.after->c_str())) Fail("loader rejected the completed setup configuration");
                if (Configuration(context) != *edit.after || Read(edit.root, edit.relative) != edit.after) Fail("configuration commit verification failed");
            } else Write(edit.root, edit.relative, edit.after);
        }
    } catch (...) {
        bool restored{true};
        if (configAttempted && !context.WriteConfig(originalConfig.c_str())) restored = false;
        while (applied) {
            const auto& edit = edits[--applied];
            try {
                const auto current = Read(edit.root, edit.relative);
                if (current == edit.before) continue;
                if (current != edit.after) { restored = false; continue; }
                Write(edit.root, edit.relative, edit.before);
            } catch (...) { restored = false; }
        }
        if (!restored) context.LogError("Charge Replenishment: setup rollback was incomplete; restore files from this plugin's basefiles-and-backups/backups/charge-replenishment folder before retrying.");
        throw;
    }
}
}

auto Ensure(const D2RL::PluginContext* context) noexcept -> bool {
    if (!D2RL::HasContext(context)) return false;
    try {
        auto config = Configuration(*context);
        const auto enabled = ConfigValue(config, "charge-replenishment", "enabled");
        if (enabled && *enabled == "false") return true;
        if (enabled && *enabled != "true") Fail("enabled must be true or false");
        if (Complete(config)) return true;
        const InstallLock lock;
        config = Configuration(*context);
        if (Complete(config)) return true;
        const auto location = Locate(*context);
        if (!context->pluginConfigPath || !Same(fs::path(context->pluginConfigPath).lexically_normal(), (location.root / ConfigFile).lexically_normal()))
            Fail("plugin configuration is outside the active mod's configuration folder");
        if (!context->GetApi()->writeConfig) Fail("loader configuration writing is unavailable");
        const auto edits = Plan(location, config);
        Apply(*context, location, edits, config);
        context->LogInfo("Charge Replenishment: first-run setup completed. Exit and restart D2RLoader with this mod if the new tables are not loaded yet. Later launches skip setup.");
        return true;
    } catch (const std::exception& error) {
        D2RL::LogErrorF(context, "Charge Replenishment: automatic setup stopped: %s. Setup remains retryable; charges are unchanged.", error.what());
        return false;
    } catch (...) {
        context->LogError("Charge Replenishment: automatic setup failed; restart D2RLoader to retry after checking the plugin log.");
        return false;
    }
}
}
