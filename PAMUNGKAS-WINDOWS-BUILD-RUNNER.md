# PAMUNGKAS Brave Portable — Windows Build Runner Contract

Status: `BUILD-INFRASTRUCTURE / NO-GO UNTIL EVIDENCE`

## Purpose

This machine exists only to compile the patched Brave Core branch and produce candidate build/dist evidence. It must not be used as proof that browser sessions are portable; that requires the separate PC A -> PC B -> PC A runtime matrix.

## Required workspace

Canonical layout:

```text
<project>\
  src\
    brave\   <- pribadimartabat2/brave-core, branch pamungkas/portable-oscrypt-poc
```

The governed entry point is:

```powershell
src\brave\tools\pamungkas\windows-build.ps1
```

## Minimum build-machine contract

- Windows x64 supported by the current Brave/Chromium toolchain.
- At least 120 GB free disk before the governed harness is allowed to start; more is recommended for Chromium source, output, caches, and dist artifacts.
- Git, Node, pnpm, and Python available in PATH.
- Network access sufficient for Brave/Chromium initialization.
- No personal browser profiles, login cookies, private SSH keys, cloud credentials, or unrelated secrets stored on the build machine.

## Security posture

`pribadimartabat2/brave-core` is a public fork. Do not attach a normal borrowed/personal computer as a long-lived self-hosted runner.

Preferred build environment:

1. disposable Windows VM / cloud build VM;
2. dedicated only to this build;
3. no unrelated credentials;
4. destroyed or reset after candidate artifacts/evidence are copied out.

If GitHub self-hosted Actions is used, use an ephemeral/dedicated machine with the custom `brave-build` label and remove the runner immediately after the job. The self-hosted workflow is manual-only and is not a release approval.

## Preflight only

From `<project>\src\brave`:

```powershell
.\tools\pamungkas\windows-build.ps1 -CheckOnly -CreateDist -Configuration Release -MinimumFreeGB 120
```

PASS must create:

```text
<project>\.pamungkas\evidence\windows-build-preflight.json
```

A preflight PASS is not a compile PASS.

## First initialization + full candidate build

Only after preflight PASS:

```powershell
.\tools\pamungkas\windows-build.ps1 -Initialize -CreateDist -Configuration Release -MinimumFreeGB 120
```

Subsequent runs on an already initialized clean workspace can omit `-Initialize`.

## Required successful evidence

A genuine successful build/dist run must produce all of:

```text
<project>\.pamungkas\evidence\windows-build-preflight.json
<project>\.pamungkas\evidence\windows-build-result.json
<project>\.pamungkas\evidence\windows-dist-result.json
<project>\src\out\Release\brave_installer.exe
<project>\src\out\Release\dist\*.zip
```

`windows-dist-result.json` must contain SHA-256 values calculated from the actual produced installer and distribution ZIPs.

## Candidate handling

The outputs are candidate artifacts, not final releases.

Do not configure `brave-portable` to use a candidate until:

- build result is `BUILD_PASS`;
- dist result is `DIST_PASS`;
- candidate installer is published only through the governed `pribadimartabat2/brave-core` candidate release path;
- the published asset SHA-256 is identical to the evidence SHA-256.

`brave-portable` deliberately blocks stock Brave and any candidate whose URL/SHA authority is not configured.

## Runtime gate after packaging

Even a successful compile/package remains `NO-GO` until a fresh portable profile proves:

- PBK1 key exists;
- PBS2 state exists and engine accepts it;
- launcher postflight passes;
- Chrome Web Store extension survives;
- controlled own-account sessions survive PC A -> PC B -> PC A;
- no cookie/password/key/fingerprint values leak into logs or diagnostics.
