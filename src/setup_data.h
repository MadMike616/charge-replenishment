#pragma once
#include "../third_party/nlohmann/json.hpp"
#include <algorithm>
#include <charconv>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace Recharge::Setup::Data {
using Json = nlohmann::ordered_json;
using Row = std::map<std::string, std::string>;
inline constexpr auto Stat = "item_replenish_charges";
inline constexpr auto Property = "rep-charges";
inline constexpr auto StringKey = "ModChargeReplenishmentRate";

[[noreturn]] inline void Fail(const std::string& reason) { throw std::runtime_error(reason); }
inline auto Trim(std::string_view text) -> std::string_view {
    const auto first = text.find_first_not_of(" \t\r\n");
    return first == text.npos ? std::string_view{} : text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
}
inline auto WithoutBom(std::string_view text) -> std::string_view {
    return text.starts_with("\xEF\xBB\xBF") ? text.substr(3) : text;
}
inline auto Number(std::string_view text) -> int {
    int value{};
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (text.empty() || result.ec != std::errc{} || result.ptr != text.data() + text.size()) Fail("invalid numeric table/configuration value");
    return value;
}
inline auto Newline(std::string_view text) -> std::string { return text.find("\r\n") == text.npos ? "\n" : "\r\n"; }

struct Table {
    std::vector<std::string> header;
    std::vector<Row> rows;
    std::string key;
    Table(std::string_view input, std::string keyColumn) : key(std::move(keyColumn)) {
        const auto text = WithoutBom(input);
        std::vector<std::vector<std::string>> records;
        std::vector<std::string> record;
        std::string value;
        bool quoted{}, closed{}, cellStart{true};
        for (std::size_t i = 0; i < text.size(); ++i) {
            const char ch = text[i];
            if (quoted) {
                if (ch != '"') value += ch;
                else if (i + 1 < text.size() && text[i + 1] == '"') { value += '"'; ++i; }
                else { quoted = false; closed = true; }
                continue;
            }
            if (ch == '\t' || ch == '\r' || ch == '\n') {
                record.push_back(std::move(value)); value.clear(); closed = false; cellStart = true;
                if (ch != '\t') {
                    records.push_back(std::move(record)); record.clear();
                    if (ch == '\r' && i + 1 < text.size() && text[i + 1] == '\n') ++i;
                }
            } else {
                if (closed) Fail("unexpected text after a quoted TSV cell");
                if (cellStart && ch == '"') quoted = true;
                else value += ch;
                cellStart = false;
            }
        }
        if (quoted) Fail("unterminated quoted TSV cell");
        if (!record.empty() || !value.empty() || closed) { record.push_back(std::move(value)); records.push_back(std::move(record)); }
        if (records.empty()) Fail("empty table");
        header = std::move(records.front());
        std::set<std::string> names;
        for (const auto& column : header) if (column.empty() || !names.insert(column).second) Fail("table column names must be unique and nonempty");
        if (!names.contains(key)) Fail("table is missing required key column " + key);
        std::set<std::string> keys;
        for (std::size_t i = 1; i < records.size(); ++i) {
            auto& values = records[i];
            bool any{}; for (const auto& cell : values) any |= !cell.empty();
            if (!any) continue;
            if (values.size() > header.size()) Fail("table row has too many columns");
            Row row;
            for (std::size_t j = 0; j < header.size(); ++j) row[header[j]] = j < values.size() ? values[j] : "";
            if (row.at(key).empty() || !keys.insert(row.at(key)).second) Fail("empty or duplicate table key");
            rows.push_back(std::move(row));
        }
    }
    auto Find(const std::string& name) const -> const Row* {
        for (const auto& row : rows) if (row.at(key) == name) return &row;
        return nullptr;
    }
    auto NextId(const std::string& column, int limit) const -> int {
        if (std::find(header.begin(), header.end(), column) == header.end()) Fail("missing table ID column " + column);
        int count{};
        for (const auto& row : rows) {
            if (key == "code" && row.at(key) == "Expansion" && row.at(column).empty()) continue;
            if (Number(row.at(column)) != count++) Fail("table IDs must match physical row order; existing rows will not be renumbered");
        }
        if (count > limit) Fail("no safe appended table ID remains");
        return count;
    }
    auto Add(std::string_view original, const Row& source) const -> std::string {
        for (const auto& [name, value] : source) {
            if (!value.empty() && name != "*Creator" && std::find(header.begin(), header.end(), name) == header.end())
                Fail("missing column with a nonempty source value: " + name);
        }
        if (const auto* existing = Find(source.at(key))) {
            for (const auto& [name, value] : *existing) {
                if (name.starts_with('*')) continue;
                const auto found = source.find(name);
                if ((found != source.end() && value != found->second) || (found == source.end() && value != "" && value != "0"))
                    Fail("existing " + source.at(key) + " has incompatible behavior");
            }
            return std::string(original);
        }
        auto output = std::string(original);
        const auto newline = Newline(original);
        if (!output.empty() && output.back() != '\r' && output.back() != '\n') output += newline;
        bool first{true};
        for (const auto& column : header) {
            if (!first) output += '\t'; first = false;
            const auto found = source.find(column);
            const auto value = column == "*Creator" ? std::string("MadMike") : found == source.end() ? std::string{} : found->second;
            if (value.find_first_of("\t\r\n\"") == value.npos) output += value;
            else { output += '"'; for (const char ch : value) { output += ch; if (ch == '"') output += ch; } output += '"'; }
        }
        return output + newline;
    }
};

// Mask comments and trailing commas for reading, retaining byte offsets so only
// the plugin-owned object is inserted/rebound in the original localization file.
inline auto CleanJson(std::string_view text, bool removeTrailingCommas = true) -> std::string {
    auto clean = std::string(text);
    bool quoted{};
    std::size_t last = std::string::npos;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char ch = text[i];
        if (quoted) {
            if (ch == '\\' && i + 1 < text.size()) ++i;
            else if (ch == '"') { quoted = false; last = i; }
            continue;
        }
        if (ch == '/' && i + 1 < text.size() && (text[i + 1] == '/' || text[i + 1] == '*')) {
            const bool line = text[i + 1] == '/';
            auto end = line ? text.find_first_of("\r\n", i + 2) : text.find("*/", i + 2);
            if (end == text.npos) { if (!line) Fail("unterminated JSON comment"); end = text.size(); }
            else if (!line) end += 2;
            for (auto j = i; j < end; ++j) if (clean[j] != '\r' && clean[j] != '\n') clean[j] = ' ';
            i = end - 1; continue;
        }
        if (removeTrailingCommas && (ch == '}' || ch == ']') && last != text.npos && clean[last] == ',') clean[last] = ' ';
        if (ch == '"') quoted = true;
        if (ch != ' ' && ch != '\t' && ch != '\r' && ch != '\n') last = i;
    }
    return clean;
}
inline auto ParseJson(std::string_view text) -> Json { return Json::parse(CleanJson(WithoutBom(text))); }
inline auto Array(std::string_view text) -> Json {
    auto array = ParseJson(text);
    if (!array.is_array()) Fail("localization file must contain a JSON array");
    for (const auto& entry : array) if (!entry.is_object()) Fail("localization array contains a non-object");
    return array;
}
inline auto Formatted(const Json& entry, const std::string& newline) -> std::string {
    std::string output = "  ";
    for (const auto ch : entry.dump(2, ' ', false)) output += ch == '\n' ? newline + "  " : std::string(1, ch);
    return output;
}
inline auto Localize(std::string_view original, const Json& entry) -> std::string {
    const auto entries = Array(original);
    const auto clean = CleanJson(original, false);
    const auto newline = Newline(original);
    auto output = std::string(original);
    for (const auto& existing : entries) if (existing.value("Key", "") == StringKey) {
        auto withoutId = existing; withoutId.erase("id");
        auto expected = entry; expected.erase("id");
        if (withoutId != expected) Fail("existing localization key has different translations");
        if (existing == entry) return output;
        // Find the matching top-level object's exact span, preserving all others.
        unsigned depth{}; bool quoted{}; std::size_t start{};
        for (std::size_t i = 0; i < clean.size(); ++i) {
            if (quoted) { if (clean[i] == '\\') ++i; else if (clean[i] == '"') quoted = false; continue; }
            if (clean[i] == '"') { quoted = true; continue; }
            if (clean[i] == '{' && depth++ == 0) start = i;
            if (clean[i] == '}' && --depth == 0 && ParseJson(original.substr(start, i - start + 1)) == existing) {
                auto replacement = entry.dump(2, ' ', false);
                std::string indented;
                for (const char ch : replacement) indented += ch == '\n' ? newline + "  " : std::string(1, ch);
                output.replace(start, i - start + 1, indented);
                return output;
            }
        }
        Fail("cannot locate owned localization object");
    }
    const auto close = clean.find_last_not_of(" \t\r\n");
    if (close == clean.npos || clean[close] != ']') Fail("invalid localization array terminator");
    const auto previous = clean.find_last_not_of(" \t\r\n", close - 1);
    const bool trailingComma = clean[previous] == ',';
    auto insert = newline + Formatted(entry, newline) + (trailingComma ? "," : "") + newline;
    output.insert(close, insert);
    if (!entries.empty() && !trailingComma) output.insert(previous + 1, ",");
    auto expected = entries; expected.push_back(entry);
    if (Array(output) != expected) Fail("localization insertion did not round trip");
    return output;
}

inline auto ConfigValue(std::string_view text, std::string_view section, std::string_view key) -> std::optional<std::string> {
    text = WithoutBom(text);
    std::string current;
    std::optional<std::string> found;
    while (!text.empty()) {
        const auto end = text.find('\n'); auto line = Trim(text.substr(0, end));
        text = end == text.npos ? std::string_view{} : text.substr(end + 1);
        line = Trim(line.substr(0, line.find('#')));
        if (line.starts_with('[') && line.ends_with(']')) current = std::string(line.substr(1, line.size() - 2));
        else if (current == section) {
            const auto equal = line.find('=');
            if (equal != line.npos && Trim(line.substr(0, equal)) == key) {
                if (found) Fail("duplicate owned configuration setting");
                found = std::string(Trim(line.substr(equal + 1)));
            }
        }
    }
    return found;
}
inline auto Upsert(std::string text, std::string_view section, std::string_view key, std::string_view value) -> std::string {
    if (text.starts_with("\xEF\xBB\xBF")) return std::string("\xEF\xBB\xBF") + Upsert(text.substr(3), section, key, value);
    const auto newline = Newline(text);
    std::string current;
    std::size_t insert = text.size(); bool inSection{}, seenSection{};
    for (std::size_t pos = 0; pos < text.size();) {
        const auto end = text.find('\n', pos);
        const auto length = end == text.npos ? text.size() - pos : end - pos + 1;
        const auto line = Trim(std::string_view(text).substr(pos, length));
        const auto sectionLine = Trim(line.substr(0, line.find('#')));
        if (sectionLine.starts_with('[') && sectionLine.ends_with(']')) {
            if (inSection) { insert = pos; break; }
            current = std::string(sectionLine.substr(1, sectionLine.size() - 2));
            inSection = current == section; seenSection |= inSection;
        } else if (inSection) {
            const auto equal = line.find('=');
            if (equal != line.npos && Trim(line.substr(0, equal)) == key) {
                const auto comment = line.find('#');
                text.replace(pos, length, std::string(key) + " = " + std::string(value)
                    + (comment == line.npos ? "" : " " + std::string(line.substr(comment))) + newline);
                return text;
            }
        }
        pos += length;
    }
    if (!seenSection) { text += newline + "[" + std::string(section) + "]" + newline; insert = text.size(); }
    else if (insert == text.size() && !text.empty() && text.back() != '\n') { text += newline; insert = text.size(); }
    text.insert(insert, std::string(key) + " = " + std::string(value) + newline);
    return text;
}
}
