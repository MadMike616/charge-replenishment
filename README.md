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

Supported scope is the local player's items in single-player and the hosting player's items in a locally hosted game. Remote players, dedicated servers, and official online play are not supported. Single-player replenishment, recovery from zero charges, casting a restored charge, save/rejoin persistence, and stash/Cube suspension and resumption were confirmed on **2026-11-26**. First-run DLL setup, a second launch with setup skipped, and continued replenishment were confirmed on **2026-11-27**. Multiple charged skills and locally hosted multiplayer have automated or source-level coverage but have not been confirmed in game.

The native profile was audited against **D2R 3.4.95032**. Required function bytes, D2RCore-owned thunks, stat layouts, and charge packing are validated before use. Unknown layouts are left untouched. No native hook or patch is installed, and no explicit game-version gate is used. See [HOOK_INVENTORY.md](HOOK_INVENTORY.md) and [NATIVE_CONTRACT.md](NATIVE_CONTRACT.md).

## Install

Close the game, place **`d2rl-charge-replenishment.dll`** in:

```text
<mod>/d2rloader/plugins/
```

Launch the mod through D2RLoader. **The DLL performs first-run setup automatically; no Python or installer command is required.** D2RLoader creates the default configuration if needed.

On first launch, the plugin:

- Appends `item_replenish_charges` to ItemStatCost and `rep-charges` to Properties, mapping fields by exact column name and allocating unused IDs without renumbering existing rows.
- Adds the modifier translation in all **13 D2R languages**, preserving unrelated localization entries.
- Binds the allocated stat ID in `<mod>/d2rloader/config/charge-replenishment.toml`, preserving existing settings.
- Backs up affected files under `<mod>/d2rloader/plugins/basefiles-and-backups/backups/charge-replenishment/`, with a restore manifest.
- Writes `[installation] complete = true` only after every required operation succeeds. Later launches skip file setup; failed setup rolls back and remains retryable.

Setup coordinates with other plugin installers using the shared `Local\D2RLoader.ModFirstRunInstall.v1` mutex. It rechecks the completion token after acquiring the lock and holds it through backups, file changes, rollback, and token creation. If another installer remains busy, setup stops without writing and can retry on the next launch.

The plugin uses existing loose mod files first. If `itemstatcost.txt`, `properties.txt`, or `item-modifiers.json` is missing, extract the required original with **Ladiks Casc Viewer** into:

```text
<mod>/d2rloader/plugins/basefiles-and-backups/basefiles/
```

Preserve the extracted `data/global/excel/...` and `data/local/lng/strings/...` paths, or the paths inside `data`. The plugin copies only missing files from that folder, then merges its additions. Packed MPQ mods must first be made available as loose mod files.

First-time file changes may require exiting and restarting D2RLoader with the same mod. Affected stale BIN files are backed up and invalidated; let your normal mod workflow rebuild them. If the new compiled tables are not loaded, replenishment stays inactive and the log explains the required restart or BIN rebuild.

If the optional `<game>/d2rloader/I Hate TOMLs` configuration mirror exists, only this plugin's owned settings are synchronized there. Existing completed installations retain their IDs and settings.

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

First-run setup replaces `-1` with the allocated stat ID. Preserve that value and restart after configuration changes. Do not copy IDs between mods.

Run `charge-replenishment` in the D2RLoader console to see readiness, the latest eligible-item count, and total charges restored during the plugin load. The command does not create or edit items.

## Build and tests

Use the official [PluginSDK 0.4.0](https://github.com/D2RLoader/PluginSDK), an x64 MSVC toolchain with Windows SDK, and C++20. Keep generated output outside the source tree:

```powershell
./tools/build.ps1 -BuildDirectory "../build-charge-replenishment" -SdkDirectory "<PluginSDK>/SDK-0.4.0" -VsDevCmd "<Visual Studio>/Common7/Tools/VsDevCmd.bat"
```

The direct build uses Release flags `/O2 /DNDEBUG /MT`, builds version resources, and runs the C++ tests, including the real first-run installer with embedded resources. CMake 3.29+ is also supported with `find_package(D2RLPlugin 0.4.0 EXACT)`; select Release and an external build directory. Enable `CHARGE_REPLENISHMENT_TESTS` for C++ tests.

Automated coverage includes rate arithmetic, zero-charge recovery, pauses, independent pools, caps, container/session changes, both item-inspection paths, legacy Item service layouts, native stat record validation, rejected writes, post-write verification, first-run ID allocation, reordered columns, localization preservation, missing-base-file handling, optional configuration mirrors, completion-token skipping, conflicts, repeat installation, and rollback. Cross-process checks verify the shared install lock during token creation and rollback; a competing installer test verifies waiting without writes and rechecking the token after acquisition. Public release packages include verified loader/Windows metadata and a SHA-256 checksum.
