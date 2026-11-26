"""Merge this plugin's data by column name, then install a Release DLL. Python 3.11+."""
from __future__ import annotations

import argparse
import csv
from datetime import datetime, timezone
import hashlib
import io
import json
from pathlib import Path
import re
import sys
import tomllib

ROOT = Path(__file__).resolve().parents[1]
PLUGIN_ID = "charge-replenishment"
STAT = "item_replenish_charges"
PROPERTY = "rep-charges"
LANGUAGES = {"enUS", "zhTW", "deDE", "esES", "frFR", "itIT", "koKR", "plPL", "esMX", "jaJP", "ptBR", "ruRU", "zhCN"}


def load_json(text: str):
    # D2R localization sources also use // comments and trailing commas.
    # Strip syntax only for reading; no source file is normalized or rewritten.
    cleaned = []
    i = 0
    quoted = escaped = False
    while i < len(text):
        char = text[i]
        if quoted:
            cleaned.append(char)
            if escaped:
                escaped = False
            elif char == "\\":
                escaped = True
            elif char == '"':
                quoted = False
        elif char == '"':
            quoted = True
            cleaned.append(char)
        elif text.startswith("//", i):
            end = text.find("\n", i + 2)
            if end < 0:
                break
            cleaned.append("\n")
            i = end + 1
            continue
        elif text.startswith("/*", i):
            end = text.find("*/", i + 2)
            if end < 0:
                raise ValueError("Unterminated JSON comment.")
            cleaned.append(" ")
            i = end + 2
            continue
        else:
            cleaned.append(char)
        i += 1
    text = "".join(cleaned)
    output = []
    quoted = escaped = False
    for i, char in enumerate(text):
        if quoted:
            output.append(char)
            if escaped:
                escaped = False
            elif char == "\\":
                escaped = True
            elif char == '"':
                quoted = False
        elif char == '"':
            quoted = True
            output.append(char)
        elif char == ",":
            following = i + 1
            while following < len(text) and text[following].isspace():
                following += 1
            if following == len(text) or text[following] not in "}]":
                output.append(char)
        else:
            output.append(char)
    return json.loads("".join(output))


def decode(data: bytes) -> tuple[str, str, str]:
    return data.decode("utf-8-sig"), "utf-8-sig" if data.startswith(b"\xef\xbb\xbf") else "utf-8", "\r\n" if b"\r\n" in data else "\n"


def table(data: bytes, key: str) -> tuple[list[str], list[dict[str, str]]]:
    text, _, _ = decode(data)
    reader = csv.reader(io.StringIO(text), delimiter="\t")
    header = next(reader)
    if not all(header) or len(header) != len(set(header)) or key not in header:
        raise ValueError(f"Invalid {key} table header: columns must be unique/nonempty and contain {key}.")
    rows = []
    names = set()
    for values in reader:
        if not values or not any(values):
            continue
        if len(values) > len(header):
            raise ValueError(f"Too many values in {key} table row.")
        row = dict(zip(header, values + [""] * (len(header) - len(values))))
        if not row[key] or row[key] in names:
            raise ValueError(f"Empty or duplicate {key}: {row[key]!r}.")
        names.add(row[key])
        rows.append(row)
    return header, rows


def add_row(data: bytes, key: str, values: dict[str, str]) -> tuple[bytes, bool]:
    header, rows = table(data, key)
    missing = {k for k, v in values.items() if v and k not in header and k != "*Creator"}
    if missing:
        raise ValueError(f"Missing columns with nonempty source values: {sorted(missing)}")
    expected = {k: values.get(k, "") for k in header}
    if "*Creator" in header:
        expected["*Creator"] = "MadMike"
    existing = next((r for r in rows if r[key] == values[key]), None)
    if existing:
        # Only the owned semantic fields are constrained; comments can differ.
        if any(existing[k] != v for k, v in values.items() if k in header and not k.startswith("*")):
            raise ValueError(f"Existing {values[key]} has a different definition; refusing a collision.")
        if any(v not in ("", "0") for k, v in existing.items() if k not in values and not k.startswith("*")):
            raise ValueError(f"Existing {values[key]} has additional behavior; refusing a collision.")
        return data, False
    text, encoding, newline = decode(data)
    output = io.StringIO(newline="")
    writer = csv.writer(output, delimiter="\t", lineterminator=newline)
    writer.writerow([expected[k] for k in header])
    prefix = "" if not text or text.endswith(("\r", "\n")) else newline
    return (text + prefix + output.getvalue()).encode(encoding), True


def next_id(rows: list[dict[str, str]], column: str, limit: int) -> int:
    # The vanilla Properties table contains a non-record Expansion separator.
    records = [r for r in rows if not (column == "*Id" and r.get("code") == "Expansion" and not r[column])]
    identifiers = [int(r[column]) for r in records]
    if len(identifiers) != len(set(identifiers)):
        raise ValueError(f"Duplicate {column} values.")
    # D2 assigns these IDs by physical row ordinal. Never fill a gap by inserting a row.
    if identifiers != list(range(len(records))):
        raise ValueError(f"{column} values must match physical row order; existing rows will not be renumbered.")
    result = len(records)
    if result > limit:
        raise ValueError("No safe free ID remains; this installer does not change the item save codec.")
    return result


def append_localization(data: bytes, entry: dict) -> bytes:
    text, encoding, newline = decode(data)
    entries = load_json(text)
    if not isinstance(entries, list):
        raise ValueError("The localization file must contain a JSON array.")
    existing = [x for x in entries if x.get("Key") == entry["Key"]]
    if existing:
        if len(existing) != 1:
            raise ValueError("The localization key is already present with different content.")
        if existing[0] == entry:
            return data
        if {k: v for k, v in existing[0].items() if k != "id"} != {k: v for k, v in entry.items() if k != "id"}:
            raise ValueError("The localization key is already present with different content.")
        # Correct only our own previously generated object if a reserved ID is
        # discovered later. Unrelated entries retain their exact original bytes.
        def formatted(obj):
            return newline.join("  " + line for line in json.dumps(obj, ensure_ascii=False, indent=2).splitlines())
        previous = formatted(existing[0])
        if text.count(previous) != 1:
            raise ValueError("Cannot safely locate the owned localization object for ID rebinding.")
        return text.replace(previous, formatted(entry), 1).encode(encoding)
    # Insert one object; leave all existing object bytes and their ordering intact.
    last = text.rfind("]")
    if last < 0 or text[last + 1:].strip():
        raise ValueError("Invalid localization array terminator.")
    prefix = text[:last].rstrip()
    obj = json.dumps(entry, ensure_ascii=False, indent=2)
    indented = newline.join("  " + line for line in obj.splitlines())
    trailing_comma = prefix.endswith(",")
    result = prefix + ("," if entries and not trailing_comma else "") + newline + indented + ("," if trailing_comma else "") + newline + text[last:]
    if load_json(result) != entries + [entry]:
        raise ValueError("Localization insertion did not round trip.")
    return result.encode(encoding)


def upsert(text: str, section: str, key: str, value: str) -> str:
    newline = "\r\n" if "\r\n" in text else "\n"
    lines = text.splitlines(keepends=True)
    start = next((i for i, line in enumerate(lines) if line.strip() == f"[{section}]"), None)
    if start is None:
        return text.rstrip() + newline * 2 + f"[{section}]" + newline + f"{key} = {value}" + newline
    end = next((i for i in range(start + 1, len(lines)) if lines[i].lstrip().startswith("[")), len(lines))
    matches = [i for i in range(start + 1, end) if re.match(rf"\s*{re.escape(key)}\s*=", lines[i])]
    if len(matches) > 1:
        raise ValueError(f"Duplicate {section}.{key}.")
    if matches:
        old_comment = lines[matches[0]].split("#", 1)
        comment = " #" + old_comment[1].rstrip("\r\n") if len(old_comment) == 2 else ""
        lines[matches[0]] = f"{key} = {value}{comment}{newline}"
    else:
        lines.insert(end, f"{key} = {value}{newline}")
    return "".join(lines)


def plan_install(mod: Path, dll: Path | None, base: Path | None = None) -> tuple[dict[Path, bytes | None], dict]:
    mod = mod.resolve(strict=True)
    data_root = mod / (mod.name + ".mpq") / "data"
    excel = data_root / "global/excel"
    strings = data_root / "local/lng/strings"
    changes: dict[Path, bytes | None] = {}

    def source(path: Path) -> bytes:
        if path.is_file():
            return path.read_bytes()
        if base:
            relative = path.relative_to(data_root)
            for candidate in (base / "data" / relative, base / relative):
                if candidate.is_file():
                    return candidate.read_bytes()
        raise ValueError(f"Missing {path.name}. Extract the required base file with Ladiks Casc Viewer first.")

    stat_path = excel / "itemstatcost.txt"
    stat_bytes = source(stat_path)
    _, stats = table(stat_bytes, "Stat")
    if not any(r["Stat"] == "item_charged_skill" and r["*ID"] == "204" and r["Save Bits"] == "16" for r in stats):
        raise ValueError("The required vanilla charged-skill stat schema is missing.")
    existing_stat = next((r for r in stats if r["Stat"] == STAT), None)
    stat_id = int(existing_stat["*ID"]) if existing_stat else next_id(stats, "*ID", 510)
    if stat_id < 398 or stat_id > 510:
        raise ValueError("The custom stat ID is outside the supported unused item-stat domain.")
    stat_values = {
        "Stat": STAT, "*ID": str(stat_id), "Signed": "0", "Send Bits": "10",
        "Save Bits": "10", "Save Add": "0", "descpriority": "159", "descfunc": "19", "descval": "0",
        "descstrpos": "ModChargeReplenishmentRate", "descstrneg": "ModChargeReplenishmentRate", "*eol": "0",
    }
    new_stat, _ = add_row(stat_bytes, "Stat", stat_values)
    changes[stat_path] = new_stat

    prop_path = excel / "properties.txt"
    prop_bytes = source(prop_path)
    _, properties = table(prop_bytes, "code")
    existing_property = next((r for r in properties if r["code"] == PROPERTY), None)
    property_id = int(existing_property["*Id"]) if existing_property else next_id(properties, "*Id", 65534)
    prop_values = {
        "code": PROPERTY, "*Id": str(property_id), "*Enabled": "1", "func1": "1", "stat1": STAT,
        "uiRangeType": "7", "*Tooltip": "%d Charges Replenished per 100 Seconds", "*Parameter": "Unused",
        "*Min": "Minimum recharge rate (1-1023)", "*Max": "Maximum recharge rate (1-1023)",
        "*Notes": "MadMike: 100 / value seconds per charge; requires Charge Replenishment.", "*eol": "0",
    }
    new_prop, _ = add_row(prop_bytes, "code", prop_values)
    changes[prop_path] = new_prop

    localization = json.loads((ROOT / "data/localization.json").read_text(encoding="utf-8"))
    if set(localization) - {"Key"} != LANGUAGES:
        raise ValueError("The plugin must supply every supported language.")
    string_path = strings / "item-modifiers.json"
    string_bytes = source(string_path)
    string_files = {p: p.read_bytes() for p in strings.glob("*.json")}
    string_files[string_path] = string_bytes
    catalog = json.loads((ROOT / "data/vanilla-string-ids.json").read_text(encoding="utf-8"))
    ids = {number for first, last in catalog["inclusive_ranges"] for number in range(first, last + 1)}
    if base:
        for directory in (base / "data/local/lng/strings", base / "local/lng/strings"):
            for path in directory.glob("*.json"):
                ids.update(int(e["id"]) for e in load_json(path.read_bytes().decode("utf-8-sig")) if "id" in e)
    matches = []
    for path, content in string_files.items():
        entries = load_json(decode(content)[0])
        if not isinstance(entries, list):
            raise ValueError(f"Localization file {path.name} is not an array.")
        for entry in entries:
            if "id" in entry and entry.get("Key") != localization["Key"]:
                ids.add(int(entry["id"]))
            if entry.get("Key") == localization["Key"]:
                matches.append((path, entry))
    if matches:
        if len(matches) != 1 or matches[0][0] != string_path:
            raise ValueError("Localization key collision in another string file.")
        string_id = int(matches[0][1]["id"])
        if string_id in ids:
            string_id = next((i for i in range(10000, 65535) if i not in ids), None)
    else:
        string_id = next((i for i in range(10000, 65535) if i not in ids), None)
    if string_id is None:
        raise ValueError("No free localization ID remains.")
    entry = {"id": string_id, **localization}
    changes[string_path] = append_localization(string_bytes, entry)
    localization_rebound = bool(matches and int(matches[0][1]["id"]) != string_id)

    config = mod / f"d2rloader/config/{PLUGIN_ID}.toml"
    text = config.read_bytes().decode("utf-8-sig") if config.exists() else (ROOT / f"{PLUGIN_ID}.toml").read_text(encoding="utf-8")
    tomllib.loads(text)
    text = upsert(text, PLUGIN_ID, "stat_id", str(stat_id))
    # Written only in the final commit step, after all tables, strings and DLL succeed.
    text = upsert(text, "installation", "complete", "true")
    text = upsert(text, "installation", "schema_version", "1")
    parsed_config = tomllib.loads(text)
    if parsed_config[PLUGIN_ID]["stat_id"] != stat_id:
        raise ValueError("Configuration binding failed.")
    config_bytes = text.encode("utf-8")
    mirror_root = mod.parent.parent / "d2rloader/I Hate TOMLs"

    if dll:
        binary = dll.resolve(strict=True).read_bytes()
        if binary[:2] != b"MZ":
            raise ValueError("The DLL is not a Windows binary.")
        changes[mod / f"d2rloader/plugins/d2rl-{PLUGIN_ID}.dll"] = binary
    else:
        binary = b""
    for txt, original, updated in ((stat_path, stat_bytes, new_stat), (prop_path, prop_bytes, new_prop)):
        bin_path = txt.with_suffix(".bin")
        if (original != updated or not txt.exists() or (txt == stat_path and localization_rebound)) and bin_path.exists():
            changes[bin_path] = None
    # The active completion token is deliberately the final write in insertion order.
    if mirror_root.is_dir():
        changes[mirror_root / mod.name / config.name] = config_bytes
    changes[config] = config_bytes
    changes = {p: b for p, b in changes.items() if (p.exists() and b is None) or (b is not None and (not p.exists() or p.read_bytes() != b))}
    return changes, {"mod": mod.name, "stat": STAT, "stat_id": stat_id, "property": PROPERTY,
                     "property_id": property_id, "localization_id": string_id, "changed_files": len(changes),
                     "dll_sha256": hashlib.sha256(binary).hexdigest() if binary else None}


def commit(mod: Path, changes: dict[Path, bytes | None]) -> Path | None:
    if not changes:
        return None
    stamp = datetime.now(timezone.utc).strftime("%Y%m%d-%H%M%S-%f")
    backup = mod / f"backups/{PLUGIN_ID}-{stamp}"
    backup.mkdir(parents=True, exist_ok=False)
    previous = {p: p.read_bytes() if p.exists() else None for p in changes}
    manifest = []
    for i, (path, content) in enumerate(previous.items()):
        saved = backup / f"{i:02d}-{path.name}"
        if content is not None:
            saved.write_bytes(content)
        manifest.append({"original": str(path), "backup": saved.name if content is not None else None})
    (backup / "manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    written = []
    try:
        for path, content in changes.items():
            written.append(path)
            if content is None:
                path.unlink()
            else:
                path.parent.mkdir(parents=True, exist_ok=True)
                temporary = path.with_name(path.name + ".charge-replenishment.tmp")
                temporary.write_bytes(content)
                temporary.replace(path)
            if (path.exists() if content is None else path.read_bytes() != content):
                raise OSError(f"Verification failed: {path}")
    except Exception:
        for path in reversed(written):
            if previous[path] is None:
                path.unlink(missing_ok=True)
            else:
                path.write_bytes(previous[path])
            path.with_name(path.name + ".charge-replenishment.tmp").unlink(missing_ok=True)
        raise
    return backup


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mod-root", type=Path, required=True)
    parser.add_argument("--dll", type=Path)
    parser.add_argument("--basefiles", type=Path, help="Extracted base data used only for missing required files")
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()
    mod = args.mod_root.resolve(strict=True)
    changes, summary = plan_install(mod, args.dll, args.basefiles)
    if not args.dry_run:
        summary["backup"] = str(commit(mod, changes)) if changes else None
    summary["dry_run"] = args.dry_run
    print(json.dumps(summary, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, KeyError, StopIteration) as error:
        sys.exit(f"Installation stopped before completion: {error}")
