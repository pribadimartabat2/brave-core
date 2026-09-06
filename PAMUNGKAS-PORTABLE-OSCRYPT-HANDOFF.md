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

Brave's canonical README confirms `brave-browser` is not needed to build the browser. `brave-core` contains the build tools, must be checked out under `<project>/src/brave`, `pnpm run init` fetches Chromium, and `pnpm run build Release` performs the release compile.

Status: `NO-GO`.

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

### Portable key file

`browser/os_crypt/portable_key_file_win.{h,cc}`:

- 32-byte CSPRNG key;
- on-disk format `PBK1` + 32 bytes = 36 bytes;
- `CREATE_NEW` prevents race overwrite;
- malformed existing key is rejected;
- file buffers are flushed before success;
- temporary raw-key buffers are zeroed.

### Legacy providers

In portable mode:

- DPAPI v10 remains available for legacy decryption but not new encryption;
- App-Bound v20 remains available for legacy decryption but not new encryption;
- outside portable mode upstream behavior remains unchanged.

Provider precedence:

- v10 DPAPI: 10;
- v20 App-Bound: 15;
- `brp1`: 20.

### Chromium integration

A minimal patch inserts `BRAVE_BROWSER_PROCESS_IMPL_ADD_PORTABLE_OSCRYPT_PROVIDER` after the App-Bound provider. Brave keeps the implementation in `chromium_src/chrome/browser/browser_process_impl.cc`; no copy of `BrowserProcessImpl::PreMainMessageLoopRun()` is maintained.

Build wiring adds Windows-only dependency `//brave/browser/os_crypt:portable_key_provider` from `//brave/browser:core`.

## WINDOWS BUILD HARNESS

`tools/pamungkas/windows-build.ps1` is the governed build entry for this PoC.

It fail-fast checks:

- checkout layout is exactly `<project>\src\brave`;
- current branch is `pamungkas/portable-oscrypt-poc`;
- working tree is clean;
- `git`, `node`, `pnpm`, and `python` exist;
- pinned Chromium tag can be read from `package.json`;
- free disk satisfies the configurable safety floor (default 120 GB).

Modes:

- `-CheckOnly`: preflight/evidence only, no init/build;
- `-Initialize`: allows `pnpm run init` to fetch/sync Chromium;
- default configuration: `Release`;
- supported configurations: `Component`, `Release`, `Static`, `Debug`.

Evidence files are written outside the source repo under `<project>/.pamungkas/evidence/`:

- `windows-build-preflight.json`;
- `windows-build-result.json` only after a successful compile.

The harness never claims build PASS from preflight alone.

The PoC GitHub workflow now includes a PowerShell parser gate for this harness, but does not run the huge Chromium initialization/compile on a normal hosted runner.

## EVIDENCE / TESTS

Targeted Windows helper evidence covers:

- first portable-key creation;
- subsequent load returns the identical key;
- PBK1 header and exact size;
- malformed existing key rejected without overwrite;
- Chromium hook patch applies against pinned Chromium `153.0.8010.28`;
- provider/wrapper/build contract assertions.

Last fully observed targeted workflow PASS before later hardening: commit `da93b76c860748e6cc400eccc625c054f7b1fbca`.

Later branch work includes:

- idempotent fork workflow label cleanup;
- raw-key zeroization hardening;
- governed Windows build harness;
- PowerShell syntax gate for the harness.

Do not claim those later commits PASS until their workflow result is actually observed.

## LAUNCHER INTEGRATION AUTHORITY

`pribadimartabat2/brave-portable` now treats this `brp1` engine as the authority for new encryption.

Its existing `internal/portablecrypto` DPAPI vault remains only a bounded v10 legacy bridge. It is not the authority for new writes and does not solve v20 by itself.

After Brave exits, the launcher postflight requires:

- `Local State` present;
- `Portable Encryption Key` present;
- key path is a regular file;
- key size is exactly 36 bytes.

If that postflight fails, launcher logs `NO-GO` and skips legacy key capture. This catches accidental packaging of stock Brave, because stock Brave does not create the PBK1 key expected from the patched engine.

## SECURITY CONTRACT

The portable key intentionally travels with the profile, so an unlocked portable drive contains security-sensitive key material.

Production posture:

- use encrypted removable storage/full-volume encryption;
- never log/export/upload portable key bytes;
- never include key bytes in diagnostics;
- a passphrase-wrapped key may be added later, after basic portability is proven.

## MIGRATION CONTRACT

Fresh profile:
- new encrypted secrets are expected to use `brp1` immediately.

Existing profile:
- v10/v20 remain available for decryption on a context that can still unlock old data;
- store-wide re-encryption to `brp1` must be proven for cookies/passwords, not assumed;
- already machine-bound secrets cannot be promised recoverable once the original decrypt-capable context is unavailable.

## KNOWN-ISSUES

- Full Brave Windows compile has not yet been proven.
- Real browser runtime has not yet been tested with the patched engine.
- Real PC A -> PC B -> PC A session retention has not yet been proven.
- Portapps `build.properties` still downloads the official stock Brave installer; final packaging must be switched to the patched binary with an anti-stock gate.
- Existing-profile store-wide migration remains unproven.

## RELEASE GATES

Status: `NO-GO`.

Required before merge/release:

1. targeted workflow PASS on current branch head;
2. full Windows `Release` compile PASS;
3. packaged launcher consumes patched binary, never stock Brave;
4. fresh profile creates PBK1 `Portable Encryption Key` and starts normally;
5. launcher postflight PASS;
6. Chrome Web Store extension non-regression PASS;
7. controlled test sessions survive PC A -> PC B;
8. return PC B -> PC A remains valid;
9. existing-profile migration test on original decrypt-capable PC then PC B;
10. corrupt/missing-key behavior is explicit and non-destructive;
11. no cookie/password/key contents appear in logs or diagnostics;
12. release artifact/checksums only after all gates pass.

## WHAT-NEXT

1. Use `tools/pamungkas/windows-build.ps1 -CheckOnly` on a real Windows build workspace.
2. Initialize the Chromium workspace with `-Initialize` only after preflight PASS.
3. Run the first full patched Brave `Release` build.
4. Rewire `brave-portable` packaging to consume that patched binary with a hard anti-stock gate.
5. Execute PC A -> PC B -> PC A runtime matrix.
6. Only then prepare release artifacts and move PR out of draft.
