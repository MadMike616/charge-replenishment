# Hook Inventory — Repair Charge Replenishment

## Plugin metadata

| Field | Value |
|---|---|
| Plugin name | Repair Charge Replenishment |
| Plugin ID | repair-charge-replenishment |
| Release version | 1.0.0 |
| Inventory updated | 2026-11-27 |
| Source revision | repair-v1.0.0 |
| D2R build(s) audited | 3.4.95032; durability repair and charge replenishment confirmed in game |
| D2RLoader SDK / Plugin ABI | 0.4.0 / 4 |
| Audit status | Complete |

## Scope and method

Inspected every file in `src/`, build definitions, resources, and tests. Checked inline detours, CALL/JMP redirects, byte/NOP patches, vtable/shared-slot writes, MinHook, and Win32 input/message hooks. SDK lifecycle listeners, queued UI/game callbacks, item enumeration, and synchronous native item editing do not install or replace native targets.

## Hook sites

| Site ID | Type | Target | Address / identifier | Operation | Guard / validation | Source reference | Notes |
|---|---|---|---|---|---|---|---|

No direct hook sites found in the audit.

## Known collision overlaps

None identified in the current inventory; this is not a compatibility guarantee.

| Other plugin | Shared target | Relationship | Evidence / notes |
|---|---|---|---|

## Audit notes

This fork retains the original Charge Replenishment native helper profile and installs no hook sites. It reads built-in stat 252 instead of a custom stat and mutates only charged-skill stat 204 through the native charge setter and notification path. Durability fields and the repair-rate stat are never written. Helper calls and read-only byte/layout witnesses are documented in `NATIVE_CONTRACT.md`; existing D2RCore thunks are validated, never replaced.

The plugin ID, DLL name, console command, and resources are independent of Charge Replenishment. There is no installer, loose-file mutation, configuration resource, or installation token; the shared mod-install lock is unnecessary because no setup file operations occur. No original plugin files are changed.

Both plugins can affect the same charge pool if an item has both `rep-dur` and the original `rep-charges` property. This is an additive gameplay interaction, not an overlapping hook site. Sources unavailable elsewhere in the workspace remain unknown.

Release 1.0.0 refresh: no hook-site changes from the initial fork audit. Both durability repair and charge replenishment were confirmed in game on 2026-11-27 with the globally installed Release DLL. No file setup or configuration was required.
