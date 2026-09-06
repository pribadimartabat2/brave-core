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

Brave's canonical README confirms `brave-core` contains the desktop build tooling, must be checked out under `<project>/src/brave`, `pnpm run init` fetches Chromium, and `pnpm run build Release` performs the release compile.

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
- on-disk key format `PBK1` + 32 bytes = 36 bytes;
- `CREATE_NEW` prevents race overwrite;
- malformed existing key is rejected;
- file buffers are flushed before success;
- temporary raw-key buffers are zeroed;
- initialization state marker is stored beside the key as `Portable Encryption Key.state`;
- once the state marker exists, a missing key fails closed instead of generating a replacement key that would strand existing `brp1` ciphertext;
- an older PoC key without a state marker can backfill the marker only while the valid key is still present.

### Legacy providers

In portable mode:

- DPAPI v10 remains available for legacy decryption but not new encryption;
- App-Bound v20 remains available for legacy decryption but not new encryption;
- outside portable mode upstream behavior remains unchanged.

Provider precedence:

- v10 DPAPI: 10;
- v20 App-Bound: 15;
- `brp1`: 20.

Chromium `153.0.8010.28` confirms both `DPAPIKeyProvider` and `AppBoundEncryptionProviderWin` remain subclassable and expose virtual `UseForEncryption()`, so the wrapper design is structurally compatible with the pinned API.

### Chromium integration

A minimal patch inserts `BRAVE_BROWSER_PROCESS_IMPL_ADD_PORTABLE_OSCRYPT_PROVIDER` after the App-Bound provider. Brave keeps the implementation in `chromium_src/chrome/browser/browser_process_impl.cc`; no copy of `BrowserProcessImpl::PreMainMessageLoopRun()` is maintained.

Build wiring adds Windows-only dependency `//brave/browser/os_crypt:portable_key_provider` from `//brave/browser:core`. The referenced upstream GN targets exist at the pinned Chromium tag.

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

The harness never claims build PASS from preflight alone. Normal hosted CI only parses the harness and runs the focused helper contract; it is not counted as a full Chromium/Brave build.

## EVIDENCE / TESTS

Targeted Windows helper evidence covers:

- first portable-key creation;
- subsequent load returns the identical key;
- PBK1 header and exact size;
- malformed existing key rejected without overwrite;
- initialized-profile key loss fails closed and does not regenerate;
- Chromium hook patch applies against pinned Chromium `153.0.8010.28`;
- provider/wrapper/build contract assertions;
- governed Windows build harness parses successfully.

TDD evidence for missing-key hardening:

- RED commit: `acd6de95a602aeea8b96bff16e2f6ebf3636007b`;
- expected failure: `std::filesystem::exists(state_path)` because no state marker existed yet;
- GREEN implementation: `fcb5dcd3a580c95235b751bec95f506ac274abd6`;
- observed Windows CI: helper compile PASS, helper runtime PASS, Chromium patch check PASS, integration contract PASS.

Fork CI noise was also isolated: the upstream `Compare Chromium versions` job previously failed after proving both sides were `153.0.8010.28`, only because it attempted to remove a label that did not exist. The branch makes that cleanup idempotent; this is not browser-runtime evidence.

## LAUNCHER INTEGRATION AUTHORITY

`pribadimartabat2/brave-portable` treats this `brp1` engine as the authority for new encryption.

Its existing `internal/portablecrypto` DPAPI vault remains only a bounded v10 legacy bridge. It is not the authority for new writes and does not solve v20 by itself.

After Brave exits, launcher postflight requires:

- `Local State` present;
- `Portable Encryption Key` present;
- key path is a regular file;
- key size is exactly 36 bytes.

Final launcher postflight must also be extended to require the portable state marker before release packaging.

If postflight fails, launcher must report `NO-GO` and avoid claiming portability. This catches accidental packaging of stock Brave because stock Brave does not create the PBK1 key expected from the patched engine.

## SECURITY CONTRACT

The portable key intentionally travels with the profile, so an unlocked portable drive contains security-sensitive key material.

Production posture:

- use encrypted removable storage/full-volume encryption;
- never log/export/upload portable key bytes;
- never include key bytes in diagnostics;
- a passphrase-wrapped key may be added later, after basic portability is proven.

The current `.state` marker protects against silent regeneration after key loss. It is an initialization-safety marker, not yet a cryptographic fingerprint of the key file; key-file integrity hardening remains a follow-up before release.

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
- The portable state marker does not yet fingerprint the 32-byte key, so valid-size/key-header corruption is not yet independently detected.

## RELEASE GATES

Status: `NO-GO`.

Required before merge/release:

1. targeted workflow PASS on current branch head;
2. full Windows `Release` compile PASS;
3. packaged launcher consumes patched binary, never stock Brave;
4. fresh profile creates PBK1 `Portable Encryption Key` plus valid state marker and starts normally;
5. launcher postflight PASS;
6. Chrome Web Store extension non-regression PASS;
7. controlled test sessions survive PC A -> PC B;
8. return PC B -> PC A remains valid;
9. existing-profile migration test on original decrypt-capable PC then PC B;
10. corrupt/missing-key behavior is explicit and non-destructive;
11. no cookie/password/key contents appear in logs or diagnostics;
12. release artifact/checksums only after all gates pass.

## WHAT-NEXT

1. Add key-file fingerprint validation so same-size PBK1 corruption cannot be accepted silently.
2. Extend launcher postflight to require the state marker.
3. Use `tools/pamungkas/windows-build.ps1 -CheckOnly` on a real Windows build workspace with sufficient disk.
4. Initialize Chromium with `-Initialize` only after preflight PASS and run the first full patched Brave `Release` build.
5. Rewire `brave-portable` packaging to consume that patched binary with a hard anti-stock gate.
6. Execute PC A -> PC B -> PC A runtime matrix.
7. Only then prepare release artifacts and move PR out of draft.
