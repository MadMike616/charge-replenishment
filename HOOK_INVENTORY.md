# Hook Inventory â€” Charge Replenishment

## Plugin metadata

| Field | Value |
|---|---|
| Plugin name | Charge Replenishment |
| Plugin ID | charge-replenishment |
| Release version | 1.0.0 |
| Inventory updated | 2026-11-27 |
| Source revision | v1.0.0 |
| D2R build(s) audited | 3.4.95032; single-player validation confirmed |
| D2RLoader SDK / Plugin ABI | 0.4.0 / 4 |
| Audit status | Complete |

## Scope and method

Inspected every file in `src/`, the default TOML, build definitions, including `src/setup.cpp` and `src/setup_data.h`. Searched for inline detours, CALL/JMP redirects, byte/NOP patches, vtable/shared-slot writes, MinHook, and Win32 input/message hooks. Lifecycle events, queued SDK UI/game tasks, item enumeration and native item editing do not install or replace native targets.

## Hook sites

| Site ID | Type | Target | Address / identifier | Operation | Guard / validation | Source reference | Notes |
|---|---|---|---|---|---|---|---|

No direct hook sites found in the audit.

## Known collision overlaps

None identified in the current inventory; this is not a compatibility guarantee.

| Other plugin | Shared target | Relationship | Evidence / notes |
|---|---|---|---|

## Audit notes

Audited the corrected 1.0.0 source with native first-run file setup. No hook-site changes from the previously tested recharge implementation. No direct native hook/patch, vtable, shared-slot, or OS input/message target is installed. The plugin ID, DLL/config names and `rep-charges` / `item_replenish_charges` data names are distinct in the source audit. The installer allocates free IDs and refuses incompatible existing definitions.

Native helper calls and build-specific layout witnesses are recorded in `NATIVE_CONTRACT.md`; they are not counted as hook sites. The existing D2RCore entry thunks are read and validated, never replaced. Another plugin intercepting those helpers may cause admission to fail closed. Unknown/binary-only plugins in the workspace aggregate remain unknown. Single-player replenishment, zero-charge recovery, save/rejoin persistence, and stash/Cube suspension and resumption were confirmed on 2026-11-26; no compatibility guarantee is inferred from a no-hooks audit.

First-run setup uses `Local\D2RLoader.ModFirstRunInstall.v1`, matching the audited Character Resistance Display and Random Set Piece installers. It acquires this shared lock before planning or backing up changes, rechecks the completion token under the lock, and retains ownership through file writes, rollback, and token creation. Cross-process tests confirm exclusion during token creation and rollback; a competing-installer test confirms waiting without writes and skipping setup when the token becomes complete during the wait. This is synchronization, not an input/message hook or a native target replacement. Embedded setup resources are DLL-local. No new hook targets are introduced.

First-run DLL setup and a second launch with replenishment still working were confirmed on 2026-11-27. The completion token is true, the configuration mirror matches, and the existing stat/property/localization files retain their pre-test hashes.

2026-11-27 release packaging/documentation review: no hook-site changes; the published DLL is unchanged.
