# Repair Charge Replenishment

The Repairs Durability variant of [Charge Replenishment](https://github.com/MadMike616/charge-replenishment) by **MadMike**, version **1.0.0**, MIT license.

[Download the latest release](https://github.com/MadMike616/charge-replenishment/releases/tag/repair-v1.0.0)

An item with the built-in **Repairs Durability** property also replenishes its skill charges. **The item's normal durability repair continues unchanged.** The fork reads the existing repair-rate stat and updates only charged-skill pools; it does not replace the game's repair logic.

Source branch: [`repair-charge-replenishment`](https://github.com/MadMike616/charge-replenishment/tree/repair-charge-replenishment). This variant shares the original repository and has its own DLL and release tag.

## Install

Close the game and copy **`d2rl-repair-charge-replenishment.dll`** into:

```text
<game>/d2rloader/plugins/
```

This loads the plugin globally, including when no mod is selected. It can also be installed for one mod in `<mod>/d2rloader/plugins/`; a mod copy of the same plugin ID takes precedence over the global copy.

**Only the DLL is needed to install.** No base files, loose TXT/JSON edits, custom stat, Python installer, companion archive, TOML, or first-run file setup is required. Documentation and license notices accompany packaged downloads. The plugin uses the game's existing property, tooltip, and translations.

## Behavior

The same item must have both a positive **Repairs Durability** rate (`rep-dur`, stat `item_replenish_durability`, ID 252) and at least one charged skill. Items without either condition are skipped. Replenish Quantity is a separate property and does not trigger this plugin.

One charge returns every **100 / repair-rate value seconds**, matching the repair property's timing convention:

| Repair-rate value | One charge returns every |
|---|---|
| 1 | 100 seconds |
| 5 | 20 seconds |
| 10 | 10 seconds |
| 25 | 4 seconds |

For example, an item displaying “Repairs 1 Durability in 20 Seconds” replenishes one skill charge every 20 unpaused seconds. Charge replenishment also works when the item's durability is already full.

Equipped items, alternate weapons, and backpack items are eligible. Stash, Cube, trade, ground, cursor, corpse, and hireling items are excluded. Every charged skill replenishes independently up to its existing maximum. Full charge pools do not bank extra charges. Partial timers reset when the rate changes, an item leaves an eligible container, or a new game begins. Restored charges persist through normal item saving; partial timers are not saved. Pausing the game pauses replenishment.

Run `repair-charge-replenishment` in the D2RLoader console to see readiness, eligible items, and restored charges. Disable this fork through D2RLoader or remove its DLL while the game is closed.

This fork has its own plugin ID and does not change the original Charge Replenishment plugin or its `rep-charges` property. If both plugins run, an item carrying both properties can be replenished by both plugins. Items carrying only Repairs Durability are handled by this fork independently.

## Requirements and compatibility

Windows x64 and D2RLoader with **SDK 0.4.0 / Plugin ABI 4** lifecycle, inventory, item native-access, thread, and data-table services. The newer item-detail function is optional. Required services and function pointers are checked at runtime.

The native profile is derived from Charge Replenishment's audited **D2R 3.4.95032** profile. Expected function bytes, D2RCore-owned entry thunks, the 16-byte native stat record layout, built-in stat schemas, and charge packing must validate before changes occur. Unknown layouts are left untouched. No direct hooks, patches, or explicit game-version gate are installed.

Supported scope is the local authoritative player's items in single-player and the hosting player's items in a locally hosted game. Remote players, dedicated servers, and official online play are not supported. Global and mod loading without config paths, repair-based timing, absent charge pools, durability/quantity preservation, both stat readers, and game transitions are covered by automated tests. Both normal durability repair and charge replenishment were confirmed in game on **2026-11-27**, with the DLL installed globally. Locally hosted multiplayer remains unverified in game.

## Build

Use the official D2RLoader PluginSDK 0.4.0, an x64 MSVC toolchain with Windows SDK, and C++20. Keep generated output outside the source tree:

```powershell
./tools/build.ps1 -BuildDirectory "../build-repair-charge-replenishment" -SdkDirectory "<PluginSDK>/SDK-0.4.0" -VsDevCmd "<Visual Studio>/Common7/Tools/VsDevCmd.bat"
```

The script selects optimized Release flags `/O2 /DNDEBUG /MT`, builds resources, and runs the C++ tests. CMake 3.29+ is also supported; select Release and enable `REPAIR_CHARGE_REPLENISHMENT_TESTS` for tests. No generated data headers or external runtime dependencies are needed.

See `HOOK_INVENTORY.md` and `NATIVE_CONTRACT.md` for the audited native boundaries.
