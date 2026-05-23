# Copyright 2026 The openusd-fabric authors / V-Sekai contributors.
# SPDX-License-Identifier: Apache-2.0 OR MPL-2.0
#
# Smoke test: round-trip every USD fixture in openusd-fabric/tests/fixtures
# through libidtx_core's USD -> VRM -> USD path. Exits non-zero if any
# conversion fails (rc != 0) or any output file is missing.
#
# Run from the repo root:
#     pwsh tools\idtxcli\smoke_test.ps1

$ErrorActionPreference = 'Stop'

# Stage the runtime DLLs next to idtxcli.exe so its dependency chain
# (libidtx_core -> libidtx_usd -> usd_ms -> tbb12) resolves.
$cliDir = 'build\idtxcli'
if (-not (Test-Path "$cliDir\idtxcli.exe")) {
    Write-Error "build\idtxcli\idtxcli.exe not found — run 'scons -j8' first."
    exit 1
}
Copy-Item -Force build\idtx_core\libidtx_core.windows.x86_64.dll $cliDir
Copy-Item -Force thirdparty\openusd-25.11\lib\usd_ms.dll $cliDir
Copy-Item -Force thirdparty\openusd-25.11\bin\tbb12.dll $cliDir
Copy-Item -Force usd\libs\windows\libidtx_usd.dll $cliDir

$env:PXR_PLUGINPATH_NAME = (Resolve-Path 'thirdparty\openusd-25.11\lib\usd').Path

# usdchecker + usdcat live in the with-Python build because the no-
# Python USD build doesn't include the CLI tools. usdchecker gates
# ARKit-relaxed schema validity; usdcat --flatten lets us do a
# round-trip USD -> USD diff.
$usdchecker = Resolve-Path 'thirdparty\openusd-25.11-withPython\bin\usdchecker.exe' -ErrorAction SilentlyContinue
$usdcat     = Resolve-Path 'thirdparty\openusd-25.11-withPython\bin\usdcat.exe'     -ErrorAction SilentlyContinue
if ($usdchecker) {
    $env:PATH = (Split-Path $usdchecker.Path) + ';' + (Resolve-Path 'thirdparty\openusd-25.11-withPython\lib').Path + ';' + $env:PATH
}

$fixtures = @(
    'minimal_with_schemas',
    'rich_avatar',
    'macbeth_colorchecker',
    'compare_scss_mtoon',
    'mire_essence'
)

$failed = 0
foreach ($f in $fixtures) {
    $src  = "openusd-fabric\tests\fixtures\$f.usda"
    $vrm  = "$cliDir\$f.vrm"
    $back = "$cliDir\${f}_roundtrip.usda"
    if (-not (Test-Path $src)) {
        Write-Warning "skipping $f — fixture missing"
        continue
    }
    & "$cliDir\idtxcli.exe" usd-to-vrm $src $vrm > $null
    $rc1 = $LASTEXITCODE
    & "$cliDir\idtxcli.exe" vrm-to-usd $vrm $back > $null
    $rc2 = $LASTEXITCODE

    # usdchecker post-gate. ARKit relaxation is intentional — V-Sekai
    # avatars carry skinning + applied API schemas the ARKit profile
    # explicitly rejects.
    $rc3 = 0
    $checker_msg = ''
    if ($usdchecker -and (Test-Path $back)) {
        $out = & $usdchecker.Path --arkit=$false $back 2>&1
        $rc3 = $LASTEXITCODE
        if ($rc3 -ne 0) { $checker_msg = " usdchecker: " + ($out -join '; ') }
    }

    # USD -> USD round-trip diff via usdcat --flatten. Informational
    # only for now — the openusd-fabric/tests/fixtures set carries
    # USD-only features the libidtx_core handle model doesn't preserve
    # (composition arcs, material input connections beyond
    # UsdPreviewSurface's baseline, V-Sekai-namespace customData not
    # surfaced through the C ABI, etc). The CHI-251 acceptance
    # criterion's "empty diff" expectation applies to a CURATED
    # round-trip fixture corpus, not these authoring fixtures.
    #
    # The diff line count is reported so regressions are visible — a
    # fixture that previously diffed 100 lines suddenly diffing 500
    # signals something dropped on the floor.
    $diff_count = 0
    $diff_msg = ''
    if ($usdcat) {
        $usd_back = "$cliDir\${f}_usd_roundtrip.usda"
        & "$cliDir\idtxcli.exe" usd-to-usd $src $usd_back > $null
        if ($LASTEXITCODE -eq 0 -and (Test-Path $usd_back)) {
            $flat_src  = "$cliDir\${f}_src.flat.usda"
            $flat_back = "$cliDir\${f}_back.flat.usda"
            & $usdcat.Path --flatten -o $flat_src  $src      > $null 2>&1
            & $usdcat.Path --flatten -o $flat_back $usd_back > $null 2>&1
            if ((Test-Path $flat_src) -and (Test-Path $flat_back)) {
                $sa = (Get-Content $flat_src)  | Where-Object { $_ -notmatch '^\s*doc\s*=\s*"' }
                $sb = (Get-Content $flat_back) | Where-Object { $_ -notmatch '^\s*doc\s*=\s*"' }
                $diff = Compare-Object $sa $sb
                if ($diff) {
                    $diff_count = $diff.Count
                    $diff_msg = " usd-diff: $diff_count line(s) (informational)"
                }
            }
        }
    }

    $ok = ($rc1 -eq 0) -and ($rc2 -eq 0) -and ($rc3 -eq 0) `
        -and (Test-Path $vrm) -and (Test-Path $back)
    $status = if ($ok) { 'PASS' } else { 'FAIL'; $failed++ }
    Write-Host ("{0,-28} {1}  usd->vrm rc={2}  vrm->usd rc={3}  usdchecker rc={4}{5}{6}" -f `
        $f, $status, $rc1, $rc2, $rc3, $checker_msg, $diff_msg)
}

if ($failed -gt 0) {
    Write-Error "$failed fixture(s) failed round-trip"
    exit 1
}
Write-Host "all fixtures round-tripped successfully"
