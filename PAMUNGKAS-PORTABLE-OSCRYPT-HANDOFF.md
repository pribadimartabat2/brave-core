# PAMUNGKAS Portable OSCrypt — Compact Handoff

## START-HERE

Lifecycle: `EXISTING_PRODUCT_MAINTENANCE`

Repositories:
- launcher/integration: `pribadimartabat2/brave-portable`
- Brave engine: `pribadimartabat2/brave-core`

Working branch: `pamungkas/portable-oscrypt-poc`

Do not merge this branch into `master` until the Windows build and the real PC-A -> PC-B -> PC-A portability gate pass.

## CURRENT-SNAPSHOT

Brave `master` at the start of this work reports Brave Core `1.97.8` and pins Chromium `153.0.8010.28`.

Portapps already launches Brave with `--user-data-dir`, `--disable-machine-id`, and `--disable-encryption-win`. The profile directory is portable, but the old Windows OSCrypt implementation that made `--disable-encryption-win` effective was removed when Chromium/Brave migrated to OSCrypt Async. Current Windows OSCrypt Async uses DPAPI (`v10`, precedence 10) and App-Bound encryption (`v20`, precedence 15).

## ROOT CAUSE

The launcher still sends a historical portable-encryption switch, while the current Chromium encryption path no longer routes that switch through the old synchronous OSCrypt implementation. Moving the same profile to another Windows installation can therefore leave cookies/session secrets protected by a machine-bound provider.

The fix must not replace v10 or v20 outright: existing data may still need those providers to decrypt on the original machine before migration.

## IMPLEMENTED POC

1. `browser/os_crypt/portable_key_file_win.{h,cc}`
   - creates/loads a 32-byte random portable key;
   - on-disk format: `PBK1` + 32 bytes;
   - `CREATE_NEW` prevents race overwrite;
   - malformed existing key is rejected, never silently replaced;
   - file buffers are flushed before success.

2. `browser/os_crypt/brave_portable_key_provider_win.{h,cc}`
   - custom ciphertext prefix: `brp1`;
   - AES-256-GCM through OSCryptAsync `Encryptor::Key`;
   - active only when `--disable-encryption-win` is explicit;
   - key lives inside the explicit `--user-data-dir`;
   - missing/corrupt key returns temporarily unavailable instead of regenerating over an existing file.

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

A successful targeted helper workflow is necessary but not sufficient for release. Full Brave Windows compile is still required after the provider integration.

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

## RELEASE GATES

Status: `NO-GO`.

Required before merge/release:
1. targeted workflow PASS;
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

1. Resolve any targeted workflow failure.
2. Run a full Windows Brave compile with this branch.
3. Build a portable Brave package that consumes the patched binary rather than the stock Brave installer.
4. Execute the real two-computer test matrix.
5. Only after evidence passes, prepare release artifact/checksums and move the PR out of draft.
