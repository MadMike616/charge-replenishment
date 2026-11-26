# Charge Replenishment

A standalone D2RLoader plugin by **MadMike**. Adds `rep-charges`, an item property that automatically restores the item's skill charges. Version **1.0.0**, MIT license.

[Download the latest release](https://github.com/MadMike616/charge-replenishment/releases/latest)

## Recharge speed

The property value controls the rate using the self-repair convention: **100 / value seconds per charge**.

| Property value | Time for one charge |
|---|---|
| 1 | 100 seconds |
| 5 | 20 seconds |
| 10 | 10 seconds |
| 25 | 4 seconds |
| 100 | 1 second |
| 200 | 0.5 seconds |

Values **1–1023** are supported. Each charged skill replenishes independently up to its original maximum. The property must be on the item whose charges should replenish.

Equipped items, alternate weapon slots, and backpack items are eligible. Set `include_inventory = false` to restrict replenishment to equipment. Items in the stash, Cube, trade, on the ground, on the cursor, on corpses, and on hirelings are excluded.

Timing follows game frames, so pausing the game pauses replenishment. Full pools do not bank extra charges. Partial timers reset when an item leaves an eligible container, its rate changes, or a new game begins. Restored charges persist through normal item saving; partial timers are not saved.

## Requirements and compatibility

- Windows x64 and D2RLoader providing **SDK 0.4.0 / Plugin ABI 4** lifecycle, inventory, native item access, thread, and data-table services. The newer item-detail function is optional.
- A loose-file mod with `itemstatcost.txt`, `properties.txt`, and `item-modifiers.json`. Extract missing base files with **Ladiks Casc Viewer**.
- **Python 3.11+** for installation only.

Supported scope is the local player's items in single-player and the hosting player's items in a locally hosted game. Remote players, dedicated servers, and official online play are not supported. Single-player replenishment, recovery from zero charges, casting a restored charge, save/rejoin persistence, and stash/Cube suspension and resumption were confirmed on **2026-11-26**. Multiple charged skills and locally hosted multiplayer have automated or source-level coverage but have not been confirmed in game.

The native profile was audited against **D2R 3.4.95032**. Required function bytes, D2RCore-owned thunks, stat layouts, and charge packing are validated before use. Unknown layouts are left untouched. No native hook or patch is installed, and no explicit game-version gate is used. See [HOOK_INVENTORY.md](HOOK_INVENTORY.md) and [NATIVE_CONTRACT.md](NATIVE_CONTRACT.md).

## Install

Close the game and extract `charge-replenishment-1.0.0-windows-x64.zip`. Run from the extracted folder:

```powershell
python tools/install.py --mod-root "<game>/mods/<mod name>" --dll "d2rl-charge-replenishment.dll"
```

Add `--dry-run` to preview changes. If base files are needed, extract them with Ladiks Casc Viewer into `<mod>/d2rloader/plugins/basefiles-and-backups/basefiles`, then add `--basefiles "<that folder>"`. The folder must contain `data/global/excel/...` and `data/local/lng/strings/...`, or the contents of that `data` directory.

The installer:

- Adds `item_replenish_charges` to ItemStatCost, `rep-charges` to Properties, and a modifier translation in all **13 D2R languages**.
- Allocates unused IDs, preserves unrelated data and existing settings, and rejects conflicting definitions.
- Installs the DLL at `<mod>/d2rloader/plugins/d2rl-charge-replenishment.dll` and binds its stat ID in `<mod>/d2rloader/config/charge-replenishment.toml`.
- Saves backups and a restore manifest under `<mod>/backups/charge-replenishment-<timestamp>/`. Setup is marked complete only after all writes succeed; failed writes roll back, and repeated installation is safe.

If the optional `<game>/d2rloader/I Hate TOMLs` configuration mirror already exists, the installer also updates this plugin's corresponding mod configuration there.

Affected stale BIN files are backed up and invalidated. **Rebuild the affected BIN files with your normal mod workflow before playing.** If compiled tables do not contain the expected stat, the plugin reports the problem and changes no charges.

## Add the property to items

Installation adds the property definition; it does not change item drops or retrofit saved items. Add the following to an item definition, affix, runeword, or Cube recipe using its normal property columns:

| Field | Value |
|---|---|
| Property / mod code | `rep-charges` |
| Parameter | blank |
| Minimum | `10` |
| Maximum | `10` |

This restores one charge every ten seconds. Min `5`, Max `10` rolls a rate within that range. The English tooltip for value 10 is **“Replenishes 10 Charges per 100 Seconds”**. Generate a new item to receive changed item properties.

Keep the installed custom ItemStatCost row when loading saved items that carry it. Disable replenishment through configuration or remove the DLL; removing or renumbering the stat row can invalidate those items.

## Configuration and status

```toml
[charge-replenishment]
enabled = true
stat_id = -1
include_inventory = true
```

The installer replaces `-1` with the allocated stat ID. Preserve that value and restart after configuration changes. Do not copy IDs between mods.

Run `charge-replenishment` in the D2RLoader console to see readiness, the latest eligible-item count, and total charges restored during the plugin load. The command does not create or edit items.

## Build and tests

Use the official [PluginSDK 0.4.0](https://github.com/D2RLoader/PluginSDK), an x64 MSVC toolchain with Windows SDK, and C++20. Keep generated output outside the source tree:

```powershell
./tools/build.ps1 -BuildDirectory "../build-charge-replenishment" -SdkDirectory "<PluginSDK>/SDK-0.4.0" -VsDevCmd "<Visual Studio>/Common7/Tools/VsDevCmd.bat"
python -B -m unittest discover -s tests -p "test_*.py" -v
```

The direct build uses Release flags `/O2 /DNDEBUG /MT`, builds version resources, and runs the C++ tests. CMake 3.29+ is also supported with `find_package(D2RLPlugin 0.4.0 EXACT)`; select Release and an external build directory. Enable `CHARGE_REPLENISHMENT_TESTS` for C++ tests.

Automated coverage includes rate arithmetic, zero-charge recovery, pauses, independent pools, caps, container/session changes, both item-inspection paths, legacy Item service layouts, native stat record validation, rejected writes, post-write verification, installer ID allocation, reordered columns, localization preservation, optional configuration mirrors, repeat installation, and rollback. Public release packages include verified loader/Windows metadata and a SHA-256 checksum.
