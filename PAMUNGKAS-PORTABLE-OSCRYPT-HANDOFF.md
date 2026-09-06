# PAMUNGKAS Portable OSCrypt — Compact Handoff

## START-HERE

Lifecycle: `EXISTING_PRODUCT_MAINTENANCE`

Repositories:
- launcher/integration: `pribadimartabat2/brave-portable`
- Brave engine/source/build tools: `pribadimartabat2/brave-core`

Working branch: `pamungkas/portable-oscrypt-poc`
Draft PR: `pribadimartabat2/brave-core#1`

Do not merge this branch into `master` until full Windows build and real PC A -> PC B -> PC A portability gates pass.

## CURRENT-SNAPSHOT

Baseline Brave Core: `1.97.8`.
Pinned Chromium: `153.0.8010.28`.
Current proven core head: `ffb211964006540f9769e866d91589c2e87c30c5`.

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
- key and state are stored beside the portable profile;
- `CREATE_NEW` prevents silent race overwrite;
- file buffers are flushed before success;
- temporary raw-key buffers are zeroed;
- malformed existing key is rejected;
- once initialized, a missing key fails closed instead of silently generating a replacement;
- same-size PBK1 key mutation/replacement is rejected because PBS2 fingerprint no longer matches;
- legacy PBS1 presence-only state is upgraded to PBS2 only while the valid key is still available.

### Legacy providers

In portable mode:

- DPAPI v10 remains available for legacy decryption but not new encryption;
- App-Bound v20 remains available for legacy decryption but not new encryption;
- outside portable mode upstream behavior remains unchanged.

Provider precedence:
- v10 DPAPI: 10;
- v20 App-Bound: 15;
- `brp1`: 20.

Chromium `153.0.8010.28` confirms both legacy provider classes remain subclassable and expose virtual `UseForEncryption()` behavior needed by the wrappers.

### Chromium integration

A minimal patch inserts `BRAVE_BROWSER_PROCESS_IMPL_ADD_PORTABLE_OSCRYPT_PROVIDER` after App-Bound. Brave keeps implementation in `chromium_src/chrome/browser/browser_process_impl.cc`; it does not copy Chromium's full `BrowserProcessImpl::PreMainMessageLoopRun()`.

Build wiring adds Windows-only dependency `//brave/browser/os_crypt:portable_key_provider` from `//brave/browser:core`. Referenced upstream GN targets exist at the pinned Chromium tag.

## WINDOWS BUILD HARNESS

`tools/pamungkas/windows-build.ps1` is the governed full-build entry.

It checks:
- layout `<project>\src\brave`;
- branch `pamungkas/portable-oscrypt-poc`;
- clean working tree;
- git/node/pnpm/python;
- pinned Chromium tag;
- configurable free-disk floor, default 120 GB.

Modes:
- `-CheckOnly`: preflight only;
- `-Initialize`: allows `pnpm run init`;
- default full build: `Release`.

Evidence is written outside source under `<project>/.pamungkas/evidence/`. Hosted targeted CI is not counted as a full Brave/Chromium build.

## EVIDENCE / TESTS

### Missing-key hardening

- RED: `acd6de95a602aeea8b96bff16e2f6ebf3636007b` — expected state marker absent;
- GREEN: `fcb5dcd3a580c95235b751bec95f506ac274abd6` — initialized key loss fails closed.

### Same-size key replacement hardening

The first mutation helper used a normal stream against a Windows hidden file and did not reliably prove the mutation. That test evidence was rejected rather than counted.

Corrected evidence:
- test helper uses Win32 `CreateFileW`/`WriteFile`, asserts mutated bytes are actually present on disk;
- RED: `7c94d8150b83a8cd07c00a37657c93ba777ec78e` with presence-only PBS1 state;
- observed failure: `Assertion failed: !changed.has_value()` at the mutation test;
- GREEN restored implementation: `ffb211964006540f9769e866d91589c2e87c30c5`;
- current targeted workflow `pamungkas-portable-oscrypt`: PASS;
- `Compare Chromium versions`: PASS;
- label workflow: PASS.

Current targeted workflow also checks:
- portable helper C++ compile;
- helper runtime contract;
- Chromium patch application against pinned `153.0.8010.28`;
- provider/wrapper/build integration assertions;
- Windows build harness PowerShell parsing.

## LAUNCHER INTEGRATION

`pribadimartabat2/brave-portable` branch `pamungkas/portable-session-root-cause` now requires postflight metadata for:

- `Local State`;
- regular 36-byte `Portable Encryption Key`;
- regular 12-byte `Portable Encryption Key.state`.

Launcher does not read key/fingerprint contents; engine is authority for PBS2 fingerprint validation.

Launcher targeted CI on syntax-fixed commit `1e4259c5f52d08c741a491998ef3244119813148`: PASS.

## SECURITY CONTRACT

The portable key intentionally travels with the profile. Therefore possession of unlocked portable media materially increases risk compared with Windows machine-bound protection.

Required production posture:
- encrypted removable/full-volume storage;
- never log/export/upload key bytes;
- never expose fingerprint contents in diagnostics;
- passphrase wrapping may be considered later after basic portability is proven.

PBS2 is an integrity/identity check against accidental key loss/replacement. It is not a substitute for encrypted removable media.

## MIGRATION CONTRACT

Fresh profile:
- new encrypted secrets are expected to use `brp1` immediately.

Existing profile:
- v10/v20 remain available for decryption where the original Windows context can still unlock them;
- migration to `brp1` must be proven store-by-store, not assumed;
- already machine-bound secrets cannot be promised recoverable once the original decrypt-capable context is unavailable.

## KNOWN-ISSUES

- Full Brave Windows `Release` compile has not yet been proven.
- Real patched-browser runtime has not yet been tested.
- Real PC A -> PC B -> PC A retention has not yet been proven.
- Portapps packaging still downloads official stock Brave; final packaging must consume patched binary with an anti-stock gate.
- Existing-profile store-wide migration remains unproven.

## RELEASE GATES

Status: `NO-GO`.

Required before merge/release:
1. current-head targeted CI PASS — core PASS; launcher targeted PASS on latest code-bearing commit;
2. full Windows `Release` compile PASS;
3. packaged launcher consumes patched binary, never stock Brave;
4. fresh profile creates PBK1 + PBS2 and starts normally;
5. launcher postflight PASS;
6. Chrome Web Store extension non-regression PASS;
7. controlled own-account sessions survive PC A -> PC B;
8. return PC B -> PC A remains valid;
9. existing-profile migration test on original decrypt-capable PC then PC B;
10. corrupt/missing/replaced-key behavior is explicit and non-destructive;
11. no cookie/password/key/fingerprint contents appear in logs or diagnostics;
12. release artifacts/checksums only after all gates pass.

## WHAT-NEXT

1. Run `tools/pamungkas/windows-build.ps1 -CheckOnly` on a real Windows build workspace with sufficient disk.
2. After preflight PASS, initialize Chromium and run first full patched Brave `Release` build.
3. Rewire `brave-portable` packaging to consume that patched binary with hard anti-stock rejection.
4. Execute PC A -> PC B -> PC A runtime matrix.
5. Only after evidence passes, prepare release artifacts and move PRs out of draft.
