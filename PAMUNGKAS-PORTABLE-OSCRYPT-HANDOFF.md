# PAMUNGKAS Portable OSCrypt — Compact Handoff

## START-HERE

Lifecycle: `EXISTING_PRODUCT_MAINTENANCE`

Repositories:
- launcher/integration: `pribadimartabat2/brave-portable`
- Brave engine/source/build tools: `pribadimartabat2/brave-core`

Working branch: `pamungkas/portable-oscrypt-poc`
Draft PR: `pribadimartabat2/brave-core#1`

Do not merge into `master` until the full Windows build and the real PC A -> PC B -> PC A portability gates pass.

## CURRENT-SNAPSHOT

Baseline Brave Core: `1.97.8`.
Pinned Chromium: `153.0.8010.28`.
Latest lightweight portable-key/provider contract CI: PASS.
Hosted targeted Windows compile gate is now active but has not yet produced a compile PASS.

Status: `NO-GO` for release.

## ROOT CAUSE

Portapps already sends `--user-data-dir`, `--disable-machine-id`, and historical `--disable-encryption-win`.

Modern Brave/Chromium uses OSCryptAsync. The old synchronous Windows OSCrypt implementation that made `--disable-encryption-win` effective was removed, while the switch remained available to launchers. A portable profile can therefore move correctly while new secret material is still protected by a machine-bound provider.

## CANONICAL POC

### Portable provider

`browser/os_crypt/brave_portable_key_provider_win.{h,cc}`:

- custom ciphertext prefix `brp1`;
- AES-256-GCM through OSCryptAsync `Encryptor::Key`;
- enabled only for explicit `--disable-encryption-win` mode;
- key path is inside explicit `--user-data-dir`;
- missing/corrupt key returns unavailable instead of silently replacing corruption;
- provider-local raw key is wiped after transfer into `Encryptor::Key`.

### Portable key + state

`browser/os_crypt/portable_key_file_win.{h,cc}`:

- key: `PBK1` + 32-byte CSPRNG key = 36 bytes;
- state: `PBS2` + 8-byte fingerprint = 12 bytes;
- `CREATE_NEW` prevents silent race overwrite;
- file buffers are flushed before success;
- temporary raw-key buffers are zeroed;
- malformed existing key is rejected;
- once initialized, a missing key fails closed;
- same-size PBK1 mutation/replacement is rejected because the PBS2 fingerprint no longer matches;
- legacy PBS1 presence-only state can upgrade to PBS2 only while the valid key is present.

### Legacy providers

In portable mode:
- DPAPI v10 remains available for legacy decryption but not new encryption;
- App-Bound v20 remains available for legacy decryption but not new encryption;
- outside portable mode upstream behavior remains unchanged.

Provider precedence:
- v10 DPAPI: 10;
- v20 App-Bound: 15;
- `brp1`: 20.

Chromium `153.0.8010.28` confirms the legacy provider classes remain subclassable and expose the virtual `UseForEncryption()` behavior used by the wrappers.

### Fail-closed encryption semantics

Exact Chromium `153.0.8010.28` OSCryptAsync behavior was re-audited:

- providers are evaluated by precedence and only a provider that successfully returns a key and whose `UseForEncryption()` is true becomes the default encryption provider;
- a `kTemporarilyUnavailable` portable key is retained as an unavailable key-ring entry for possible recovery/decryption semantics, but is not selected for new encryption;
- in portable mode the DPAPI and App-Bound wrappers both report `UseForEncryption() == false`;
- therefore a missing/corrupt portable key leaves no default encryption provider;
- Chromium `Encryptor::EncryptString()` returns failure when no default provider/key is available instead of silently falling back to a lower machine-bound provider.

This confirms the intended NO-GO/fail-closed behavior for missing portable-key material.

### Chromium integration

A minimal patch inserts `BRAVE_BROWSER_PROCESS_IMPL_ADD_PORTABLE_OSCRYPT_PROVIDER` after App-Bound. Brave keeps implementation in `chromium_src/chrome/browser/browser_process_impl.cc`; no copy of Chromium's full `BrowserProcessImpl::PreMainMessageLoopRun()` is maintained.

Build wiring adds Windows-only dependency `//brave/browser/os_crypt:portable_key_provider` from `//brave/browser:core`.

## WINDOWS BUILD + DIST CONTRACT

`tools/pamungkas/windows-build.ps1` is the governed build entry.

Preflight requires:
- layout `<project>\src\brave`;
- branch `pamungkas/portable-oscrypt-poc`;
- clean working tree;
- git/node/pnpm/python;
- pinned Chromium tag;
- configurable free disk floor, default 120 GB.

Modes:
- `-CheckOnly`: evidence/preflight only;
- `-Initialize`: runs Brave/Chromium initialization;
- `-CreateDist`: Release-only distribution phase;
- default build configuration: `Release`.

With `-CreateDist`, the harness uses Brave's official `create_dist` target with `--skip_signing`. Brave's own GN signing template explicitly turns signing into a file copy when `skip_signing=true`, allowing an unsigned PoC candidate without Brave signing keys.

Required dist outputs:
- `src/out/Release/brave_installer.exe`;
- one or more ZIPs under `src/out/Release/dist/`.

Evidence under `<project>/.pamungkas/evidence/`:
- `windows-build-preflight.json`;
- `windows-build-result.json`;
- `windows-dist-result.json`.

`windows-dist-result.json` records installer/distribution paths, byte sizes, and SHA-256 hashes. Dist PASS is impossible unless installer + ZIP outputs actually exist.

## HOSTED TARGETED WINDOWS COMPILE

`.github/workflows/pamungkas-targeted-windows-compile.yml` is a compile gate only. It is not release evidence.

Purpose:
- exercise the real Brave/Chromium Windows GN/Ninja toolchain against `brave/browser/os_crypt:portable_key_provider` without building the whole browser;
- expose actual C++/GN integration failures before consuming a full self-hosted build.

Environment blockers already fixed:
- Corepack rejected Brave's `devEngines.packageManager.version: ">=11.11.0"`; workflow now installs exact `pnpm 11.11.0` without Corepack project-range parsing;
- hosted runner Node 22 was below Brave's required `>=24.16.0 <25`; workflow now pins Node `24.16.0` through pinned `actions/setup-node`.

Optimization commit:
- `b4cca1d24c6841faad6d342a66374525b6bb9a78` changes initialization to Brave's own `sync.ts` with `--init --no-history --target_os win --target_arch x64` after a frozen-lockfile install;
- this preserves the official Brave sync/patch/hooks path while avoiding Chromium history that is unnecessary for a compile gate.

Current targeted run after this optimization:
- run `34017325645`;
- status at latest checkpoint: in progress during checkout/initialization path;
- no targeted C++ compile PASS has been claimed yet.

## FULL-BUILD WORKFLOW

`.github/workflows/pamungkas-full-windows-build.yml` is manual-only (`workflow_dispatch`) and deliberately uses:

`[self-hosted, Windows, X64, brave-build]`

A normal hosted runner is not counted as full Chromium/Brave build evidence.

The full workflow:
1. checks out the branch at canonical `src/brave` layout;
2. runs governed preflight;
3. optionally initializes Chromium;
4. builds patched Brave Release;
5. runs unsigned `create_dist`;
6. uploads build/dist evidence;
7. uploads `brave_installer.exe` + dist ZIP as **candidate artifacts only**.

It does not publish a GitHub Release automatically.

## EVIDENCE / TESTS

### Missing-key hardening
- RED: `acd6de95a602aeea8b96bff16e2f6ebf3636007b`;
- GREEN: `fcb5dcd3a580c95235b751bec95f506ac274abd6`.

### Same-size key replacement hardening
The first mutation helper was rejected as evidence because a normal stream did not reliably mutate the Windows hidden key file.

Corrected evidence:
- Win32 `CreateFileW`/`WriteFile` test helper verifies changed bytes are really on disk;
- RED: `7c94d8150b83a8cd07c00a37657c93ba777ec78e` with PBS1 presence-only state;
- expected failure observed at `!changed.has_value()`;
- PBS2 fingerprint implementation restored and targeted CI PASS.

Latest lightweight workflow checks:
- portable helper C++ compile;
- portable helper runtime contract;
- PowerShell build harness parser;
- `CreateDist`/`--skip_signing`/SHA-256 dist contract;
- full-build workflow artifact contract;
- Chromium patch applies against pinned `153.0.8010.28`;
- provider/wrapper/build integration assertions.

## LAUNCHER INTEGRATION

`pribadimartabat2/brave-portable`, branch `pamungkas/portable-session-root-cause`, requires postflight metadata for:
- `Local State`;
- regular 36-byte PBK1 key;
- regular 12-byte PBS2 state.

Launcher does not read key/fingerprint contents; the engine is authority for PBS2 fingerprint validation.

The Portapps package build now has a hard anti-stock authority gate. Until the patched engine candidate is published through the governed `pribadimartabat2/brave-core` release path, the Portapps build must fail and the actual packaging job is skipped. This is intentional NO-GO, not a build regression.

## SECURITY CONTRACT

The portable key intentionally travels with the profile. Possession of unlocked portable media therefore materially increases risk compared with Windows machine-bound protection.

Required production posture:
- encrypted removable/full-volume storage;
- never log/export/upload key bytes;
- never expose fingerprint contents in diagnostics;
- passphrase wrapping may be considered later after basic portability is proven.

PBS2 is an identity/integrity check against accidental key loss/replacement. It is not a substitute for encrypted removable media.

## MIGRATION CONTRACT

Fresh profile:
- new encrypted secrets are expected to use `brp1` immediately.

Existing profile:
- v10/v20 remain available for decryption where the original Windows context can still unlock them;
- migration to `brp1` must be proven store-by-store;
- already machine-bound secrets cannot be promised recoverable once the original decrypt-capable context is unavailable.

## KNOWN-ISSUES

- Hosted targeted Windows compile has not yet reached/recorded a successful provider target compile.
- Full Brave Windows Release compile has not yet been executed on a 120GB+ capable Windows build environment.
- Candidate installer/dist artifacts therefore do not exist yet.
- Real patched-browser runtime has not yet been tested.
- Real PC A -> PC B -> PC A retention has not yet been proven.
- Existing-profile store-wide migration remains unproven.

## RELEASE GATES

Status: `NO-GO`.

Required before merge/release:
1. targeted CI PASS in both repos;
2. hosted real C++ target compile PASS for `portable_key_provider`;
3. full Windows `Release` build PASS;
4. `create_dist` PASS with installer + ZIP SHA-256 evidence;
5. Portapps packaging consumes governed patched installer, never stock Brave;
6. fresh profile creates PBK1 + PBS2 and starts normally;
7. launcher postflight PASS;
8. Chrome Web Store extension non-regression PASS;
9. controlled own-account sessions survive PC A -> PC B;
10. return PC B -> PC A remains valid;
11. existing-profile migration test on original decrypt-capable PC then PC B;
12. corrupt/missing/replaced-key behavior remains non-destructive;
13. no cookie/password/key/fingerprint contents appear in logs or diagnostics;
14. final release artifacts/checksums only after all gates pass.

## WHAT-NEXT

1. Let hosted targeted compile `34017325645` reach the actual `portable_key_provider` target.
2. If compile fails, patch only the concrete C++/GN error and rerun until this gate is green.
3. Attach a suitable Windows self-hosted/cloud build runner labeled `brave-build` with at least 120 GB free.
4. Run `pamungkas-full-windows-build` with `initialize=true` for the first workspace initialization.
5. Collect candidate installer/dist + SHA-256 evidence.
6. Configure `brave-portable` to the candidate patched installer via the governed URL+SHA helper/gate.
7. Build the first integrated portable candidate.
8. Execute PC A -> PC B -> PC A runtime matrix.
9. Only then prepare final release artifacts and move PRs out of draft.
