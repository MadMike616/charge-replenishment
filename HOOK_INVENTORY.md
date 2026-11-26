# Hook Inventory â€” Charge Replenishment

## Plugin metadata

| Field | Value |
|---|---|
| Plugin name | Charge Replenishment |
| Plugin ID | charge-replenishment |
| Release version | 1.0.0 |
| Inventory updated | 2026-11-26 |
| Source revision | v1.0.0 |
| D2R build(s) audited | 3.4.95032; single-player validation confirmed |
| D2RLoader SDK / Plugin ABI | 0.4.0 / 4 |
| Audit status | Complete |

## Scope and method

Inspected every file in `src/`, the default TOML, build definitions, and `tools/install.py`. Searched for inline detours, CALL/JMP redirects, byte/NOP patches, vtable/shared-slot writes, MinHook, and Win32 input/message hooks. Lifecycle events, queued SDK UI/game tasks, item enumeration and native item editing do not install or replace native targets.

## Hook sites

| Site ID | Type | Target | Address / identifier | Operation | Guard / validation | Source reference | Notes |
|---|---|---|---|---|---|---|---|

No direct hook sites found in the audit.

## Known collision overlaps

None identified in the current inventory; this is not a compatibility guarantee.

| Other plugin | Shared target | Relationship | Evidence / notes |
|---|---|---|---|

## Audit notes

Audited the initial public 1.0.0 release source. No hook-site changes from the tested working tree. No direct native hook/patch, vtable, shared-slot, or OS input/message target is installed. The plugin ID, DLL/config names and `rep-charges` / `item_replenish_charges` data names are distinct in the source audit. The installer allocates free IDs and refuses incompatible existing definitions.

Native helper calls and build-specific layout witnesses are recorded in `NATIVE_CONTRACT.md`; they are not counted as hook sites. The existing D2RCore entry thunks are read and validated, never replaced. Another plugin intercepting those helpers may cause admission to fail closed. Unknown/binary-only plugins in the workspace aggregate remain unknown. Single-player replenishment, zero-charge recovery, save/rejoin persistence, and stash/Cube suspension and resumption were confirmed on 2026-11-26; no compatibility guarantee is inferred from a no-hooks audit.
