# PAMUNGKAS Portable OSCrypt — Compact Handoff

## START-HERE

Lifecycle: `EXISTING_PRODUCT_MAINTENANCE`

Repositories:
- launcher/integration: `pribadimartabat2/brave-portable`
- Brave engine: `pribadimartabat2/brave-core`

Working branch: `pamungkas/portable-oscrypt-poc`
Draft PR: `pribadimartabat2/brave-core#1`

Do not merge this branch into `master` until the Windows build and the real PC-A -> PC-B -> PC-A portability gate pass.

## CURRENT-SNAPSHOT

Brave `master` at the start of this work reports Brave Core `1.97.8` and pins Chromium `153.0.8010.28`.

Portapps already launches Brave with `--user-data-dir`, `--disable-machine-id`, and `--disable-encryption-win`. The profile directory is portable, but the old Windows OSCrypt implementation that made `--disable-encryption-win` effective was removed when Chromium/Brave migrated to OSCrypt Async. Current Windows OSCrypt Async uses DPAPI (`v10`, precedence 10) and App-Bound encryption (`v20`, precedence 15).

Current PoC branch includes:
- the portable provider implementation;
- Chromium hook/build wiring;
- targeted Windows helper CI;
- fork-CI fix making Chromium mismatch label removal idempotent;
- temporary raw-key buffer wiping hardening.

Status remains `NO-GO`.

## ROOT CAUSE

The launcher still sends a historical portable-encryption switch, while the current Chromium encryption path no longer routes that switch through the old synchronous OSCrypt implementation. Moving the same profile to another Windows installation can therefore leave cookies/session secrets protected by a machine-bound provider.

The fix must not replace v10 or v20 outright: existing data may still need those providers to decrypt on the original machine before migration.

## IMPLEMENTED POC

1. `browser/os_crypt/portable_key_file_win.{h,cc}`
   - creates/loads a 32-byte random portable key;
   - on-disk format: `PBK1` + 32 bytes;
   - `CREATE_NEW` prevents race overwrite;
   - malformed existing key is rejected, never silently replaced;
   - file buffers are flushed before success;
   - temporary raw-key byte buffers are wiped with `SecureZeroMemory` on scope exit/error paths.

2. `browser/os_crypt/brave_portable_key_provider_win.{h,cc}`
   - custom ciphertext prefix: `brp1`;
   - AES-256-GCM through OSCryptAsync `Encryptor::Key`;
   - active only when `--disable-encryption-win` is explicit;
   - key lives inside the explicit `--user-data-dir`;
   - missing/corrupt key returns temporarily unavailable instead of regenerating over an existing file;
   - provider-local raw key is wiped after `Encryptor::Key` copies it.

3. Legacy provider wrappers
   - DPAPI v10 remains available for decryption but is decryption-only in portable mode;
   - App-Bound v20 remains available for decryption but is decryption-only in portable mode;
   - outside portable mode upstream encryption behavior is preserved.

4. Provider precedence
   - v10 DPAPI: 10;
   - v20 App-Bound: 15;
   - `brp1` portable: 20.

5. Chromium integration
   - a minimal patch inserts only `BRAVE_BROWSER_PROCESS_IMPL_ADD_PORTABLE_OSCRYPT_PROVIDER` after the App-Bound provider;
   - implementation remains in Brave `chromium_src/chrome/browser/browser_process_impl.cc`;
   - no copy of `BrowserProcessImpl::PreMainMessageLoopRun()` was made.

6. Build wiring
   - `//brave/browser/os_crypt:portable_key_provider`;
   - Windows-only dependency from `//brave/browser:core`.

## EVIDENCE / TESTS

Targeted Windows helper contract covers:
- first creation succeeds;
- subsequent load returns the identical key;
- `PBK1` header and exact file size;
- malformed existing file is rejected and not overwritten.

GitHub workflow also checks that the Chromium hook patch applies to the exact Chromium tag read from Brave `package.json`, and checks the static provider/wrapper/build contract.

Last observed targeted workflow PASS: commit `da93b76c860748e6cc400eccc625c054f7b1fbca`.

Later commits:
- `1d8897a14fd7ae357113f3342082a3cdcabddcba` — fork workflow label-cleanup fix;
- `b10ff90494a49c52382c7557fc89c8a5022e7406` — temporary raw-key memory wiping hardening.

At the single post-push check, workflow runs for these later commits were not yet returned by the connector. Therefore they are **not** claimed PASS here.

Upstream contract audit against Chromium `153.0.8010.28`:
- `KeyProvider::UseForEncryption()` is virtual and matches the PoC override contract;
- DPAPI/App-Bound provider signatures match the wrappers;
- GN dependency labels used by the PoC exist at this Chromium pin;
- OSCryptAsync accepts variable-length provider tags, so `brp1` does not depend on the 3-byte `v10`/`v20` length;
- if `brp1` is temporarily unavailable, portable mode does not select DPAPI/App-Bound for new encryption, so encryption fails closed instead of falling back to a machine-bound provider.

A successful targeted helper workflow and source-contract audit are necessary but not sufficient for release. Full Brave Windows compile is still required after the provider integration.

## SECURITY CONTRACT

The cookie/password database remains encrypted, but the portable key is intentionally machine-independent and travels with the profile. Therefore possession of the portable media materially increases the sensitivity of that media. Production use should pair this with encrypted removable storage or a later passphrase-wrapped portable-key design.

Never log, export, upload, or include the portable key value in diagnostics.

## MIGRATION CONTRACT

Fresh portable profile:
- expected new encrypted secrets use `brp1` immediately.

Existing profile:
- v10/v20 data can only be migrated while running on a machine that can still decrypt that old data;
- OSCryptAsync marks data decrypted by a non-current provider for re-encryption, but a complete store-wide migration must be proven for cookies/passwords before claiming existing-profile portability;
- do not promise recovery of already machine-bound secrets after the original decryption context is unavailable.

## KNOWN-ISSUES

- Full Brave Windows compile has not yet been proven.
- Real browser runtime has not yet been tested with this patched engine.
- Real PC A -> PC B -> PC A session portability has not yet been proven.
- `pribadimartabat2/brave-browser` wrapper/build repository is not currently present, and the available connector has no fork/create-repository action. A wrapper repo or equivalent build environment is needed for the next full Windows build gate.
- Existing-profile store-wide migration remains unproven.

## RELEASE GATES

Status: `NO-GO`.

Required before merge/release:
1. targeted workflow PASS on the current branch head;
2. Brave Windows build PASS with Chromium `153.0.8010.28`;
3. fresh profile creates `Portable Encryption Key` and browser starts normally;
4. Chrome Web Store extension installs and remains enabled;
5. test accounts remain logged in after clean shutdown and move PC A -> PC B;
6. return PC B -> PC A remains valid;
7. existing-profile migration test on original PC, then move to PC B;
8. corrupt/missing-key behavior is explicit and does not silently fall back to machine-bound encryption;
9. no cookie/password/key content appears in logs or diagnostics;
10. launcher package is rebuilt against the patched Brave binary and tested as one release unit.

## WHAT-NEXT

1. Obtain/build a Brave Windows wrapper environment that consumes `pribadimartabat2/brave-core@pamungkas/portable-oscrypt-poc`.
2. Run a full Windows Brave compile with the PoC branch.
3. Build a portable Brave package that consumes the patched binary rather than the stock Brave installer.
4. Execute the real two-computer test matrix.
5. Only after evidence passes, prepare release artifact/checksums and move the PR out of draft.
