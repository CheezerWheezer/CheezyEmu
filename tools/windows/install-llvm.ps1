# SPDX-License-Identifier: GPL-3.0-or-later

# Installs the LLVM release that the Windows CI jobs compile with (clang-cl, lld-link, llvm-lib)
# and exports its location as LLVM_ROOT for later steps. The installer is pinned by checksum so a
# runner image update never changes the compiler.

$ErrorActionPreference = 'Stop'

$version = '22.1.8'
$sha256 = '16e5709785fef73c854646241c4a92c5cd574318d1b33c63330dd7721903e55c'

$installer = Join-Path $env:RUNNER_TEMP 'llvm.exe'
# GitHub's release downloads time out now and then (504): retry.
foreach ($attempt in 1..6) {
    try {
        Invoke-WebRequest -OutFile $installer "https://github.com/llvm/llvm-project/releases/download/llvmorg-$version/LLVM-$version-win64.exe"
        break
    } catch {
        if ($attempt -eq 6) { throw }
        Write-Host "LLVM download attempt $attempt failed: $($_.Exception.Message)"
        Start-Sleep -Seconds (15 * $attempt)
    }
}
if ((Get-FileHash -Algorithm SHA256 $installer).Hash -ne $sha256) {
    throw 'LLVM installer checksum mismatch'
}
$root = Join-Path $env:RUNNER_TEMP 'llvm'
Start-Process -Wait -FilePath $installer -ArgumentList '/S', "/D=$root"
$reported = & "$root/bin/clang-cl.exe" --version
$reported
if (-not ($reported -match "clang version $([regex]::Escape($version))")) {
    throw "Unexpected clang-cl: $reported"
}
"LLVM_ROOT=$($root.Replace('\', '/'))" | Out-File -Append -Encoding utf8 $env:GITHUB_ENV
