# Native charge update contract

The SDK is the API/ABI authority. Ordinary discovery and all scheduling use SDK 0.4.0 / ABI 4 services. `ItemService::editNativeItem` is the documented raw-pointer escape hatch and requires `PluginFlags::NativeHooks` even though this plugin installs no hooks. Its pointer is used only in that synchronous authoritative game-thread callback.

The following are **helper calls, not hook sites**. They were traced in a loader-hosted 3.4.95032 `.text` capture; source-level audit does not substitute for live validation.

| Helper / witness | RVA | Purpose |
|---|---|---|
| Unit game | `0x48FF00` | Resolve the item's game without retaining a game pointer |
| Item owner ID | `0x371D00` | Resolve the item's player owner |
| Find server unit | `0x48FE80` | Resolve the owner in that same game |
| Player client | `0x48FDE0` | Obtain the owning client's notification endpoint |
| Unit data bank | `0x34A0E0` | Resolve the item's active compiled data bank |
| Data tables | `0x300A90` | Resolve that bank's charge shift and mask |
| Unit stat getter (fallback only) | `0x2F5020` | Read the item's own total replenishment rate at layer zero |
| Copy entries for stat (fallback only) | `0x2F84B0` | Copy charged-skill records using the same helper as vendor recharge |
| Copy implementation ABI witness (fallback only) | `0x2FA060` | Validate the D2RCore-owned implementation actually reached by the copy helper, including 16-byte stride and stores |
| Set item charges | `0x3D4C10` | Change one skill/layer's current charges in its existing magic stat list |
| Send item stat | `0x47A190` | Publish stat 204 with its original layer and updated packed value |
| Game frame layout witness | `0x42E615` | Validate the frame counter at game `+0x170` |
| Charge encoding layout witness | `0x3D4C84` | Validate bank `+0x14D0` shift and `+0x14D4` mask |

The vendor path at RVA `0x46F310` enumerates stat 204, obtains the current/max values from low/high bytes, calls `0x3D4C10(item, skill, level, current)`, and sends the packed result with `0x47A190(client, item, true, 204, value, layer)`. This plugin follows that sequence for an incremental charge count. The setter operates on the existing charged-skill stat list and clamps to its original maximum; it does not create a base-stat replacement. The native charge callback updates the skill node and normal saving preserves the changed item stat.

D2RLoader owns RIP-indirect entry thunks at the setter and sender. Admission requires both known untouched native-body witnesses and a live jump destination owned by `D2RCore.dll`. Unknown inline detours or different owners are rejected. All remaining helper entries require their expected byte signatures. No private D2RCore export is called.

The base Item service can be 88 bytes: native access is present at byte 48, while the optional `ItemService::getItemDetails` function begins at byte 88. Startup checks every required base field. The optional details function is used only when the service size includes it and its pointer is set.

Without item details, stat snapshots are read inside separate synchronous `editNativeItem` callbacks. The reader validates the D2RCore-owned stat-getter thunk and its untouched body, the stat-copy entry, and the CALL at game RVA `0x2F8504` that reaches game RVA `0x2FA060`. It then resolves that entry's D2RCore-owned target and checks its actual prologue, 16-byte stride, and 16-byte copy instructions. Checking the bypassed original game body alone does not establish the ABI of the loader's implementation.

Copied records use `{uint32 layer, uint32 stat, int64 value}` with size 16. Read-only inspection of the active implementation and authoritative item stat arrays confirmed this layout. The regression fixture independently encodes observed bytes for layer 4232, stat 204, and packed value 770 (two of three charges). It does not construct these bytes from the plugin record type.

The reader rejects unexpected stat IDs, layers or packed charges outside the validated 16-bit save schema, negative counts, and a completely filled 128-entry buffer, which might be truncated. Charge updates and verification use the same snapshot path. The notification adapter's final layer argument is passed as a full 32-bit value, matching its DWORD stack read; the packed charge value stays within 16 bits. No pointer escapes these callbacks.

The 0x144-byte compiled ItemStatCost rows must retain built-in IDs 252 (item_replenish_durability) and 204 (item_charged_skill). The repair stat must retain its stock signed flag, 5 send bits, 6 save bits, zero shift/add/parameter, no callback, and no stat operation. The charged-skill row must retain 16-bit saved values and parameters and its callback flag. Native table shift must be in 1..8 and the mask must equal `(1 << shift) - 1`. Frame and table reads are checked with VirtualQuery after code witnesses match.

The native stat reader accepts only rate stat 252. This value is read from the item's own total stat list. Charge eligibility requires a valid existing charged-skill pool, and positive rate values above the inherited safety bound of 1023 are skipped. No custom stat or game file is installed. No API reads or writes a configuration file, and no active-mod or mod-directory context is needed.

Every charge update preserves the original high-byte maximum and skill/layer. Item snapshots are reread after an update and must contain the exact result. A failed native update or verification disables replenishment until another table load. Ambiguous duplicate charged-skill layers are skipped. The native charge setter and stat-204 notification do not edit durability, maximum durability, item quantity, or repair-rate stat 252; the game's own durability repair remains active.

SDK LoadScope::Global with null mod/configuration paths and LoadScope::Mod are covered by the same integration tests. Native helper addresses and byte guards are unchanged from the original audited profile; both durability repair and charge replenishment were confirmed in game on 2026-11-27 with the DLL installed globally. No explicit game-version check is used. Future native layouts require a fresh audit.
