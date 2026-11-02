# =======================================================================
# MemLLVM SDK - Environment Variable Auto-Configuration & Toolchain Setup
# =======================================================================

param(
    [switch]$Uninstall,
    [switch]$Help
)

$currentDir = $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($currentDir)) {
    $currentDir = (Get-Item .).FullName
}
$currentDir = $currentDir.TrimEnd('\')

$isUninstall = $Uninstall.IsPresent -or (@($args) -match 'uninstall|/u|-u')

Write-Host "=======================================================================" -ForegroundColor Cyan
Write-Host "       MemLLVM SDK - Environment Variable Setup & Auto-Updater         " -ForegroundColor Cyan
Write-Host "=======================================================================" -ForegroundColor Cyan
Write-Host ""

$oldDir = [Environment]::GetEnvironmentVariable('MEMLLVM_DIR', 'User')
$oldLLVMDir = [Environment]::GetEnvironmentVariable('LLVMInstallDir', 'User')
$oldPathStr = [Environment]::GetEnvironmentVariable('PATH', 'User')
if ([string]::IsNullOrWhiteSpace($oldPathStr)) { $oldPathStr = '' }
$pathParts = @($oldPathStr -split ';' | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })

if ($isUninstall) {
    Write-Host "[*] Performing uninstall cleanup for MemLLVM environment variables..." -ForegroundColor Yellow
    [Environment]::SetEnvironmentVariable('MEMLLVM_DIR', $null, 'User')
    [Environment]::SetEnvironmentVariable('LLVMInstallDir', $null, 'User')

    $filteredPath = [System.Collections.Generic.List[string]]::new()
    foreach ($entry in $pathParts) {
        $p = $entry.TrimEnd('\')
        $isOld = ($oldDir -and ($p.StartsWith($oldDir, [StringComparison]::OrdinalIgnoreCase)))
        $isOldLLVM = ($oldLLVMDir -and ($p.StartsWith($oldLLVMDir, [StringComparison]::OrdinalIgnoreCase)))
        $isCurr = ($p.StartsWith($currentDir, [StringComparison]::OrdinalIgnoreCase))
        $isGenericMemLLVM = ($p -match '\\MemLLVM(-SDK.*)?\\(bin|Tools|toolchain\\bin)$')
        if (-not ($isOld -or $isOldLLVM -or $isCurr -or $isGenericMemLLVM)) {
            $filteredPath.Add($entry)
        }
    }
    $newPathStr = $filteredPath -join ';'
    [Environment]::SetEnvironmentVariable('PATH', $newPathStr, 'User')

    Write-Host "[+] Successfully removed MEMLLVM_DIR, LLVMInstallDir, and PATH entries from User Environment!" -ForegroundColor Green
    Write-Host "=======================================================================" -ForegroundColor Cyan
    Write-Host ""
    if ([Environment]::UserInteractive) {
        Read-Host "Press Enter to exit..."
    }
    exit 0
}

Write-Host ("[+] Detected SDK Directory : " + $currentDir) -ForegroundColor Green

if ($oldDir) {
    if ($oldDir.TrimEnd('\') -ieq $currentDir) {
        Write-Host "[*] MEMLLVM_DIR already matches current directory. Refreshing settings..." -ForegroundColor Gray
    } else {
        Write-Host ("[!] Detected previous SDK directory : " + $oldDir) -ForegroundColor Yellow
        Write-Host ("[!] Automatically migrating and updating to : " + $currentDir) -ForegroundColor Yellow
    }
} else {
    Write-Host "[*] Initializing MEMLLVM_DIR in User Environment..." -ForegroundColor Gray
}

# 1. Update MEMLLVM_DIR in User Environment
[Environment]::SetEnvironmentVariable('MEMLLVM_DIR', $currentDir, 'User')

# 2. Detect & configure bundled hermetic Clang toolchain
$newBin = Join-Path $currentDir 'bin'
$newTools = Join-Path $currentDir 'Tools'
$toolchainDir = Join-Path $currentDir 'toolchain'
$toolchainBin = Join-Path $toolchainDir 'bin'
$hasBundledToolchain = (Test-Path (Join-Path $toolchainBin 'clang-cl.exe'))

if (-not $hasBundledToolchain) {
    # Check if clang-cl is in bin/ directly
    if (Test-Path (Join-Path $newBin 'clang-cl.exe')) {
        $toolchainDir = $currentDir
        $toolchainBin = $newBin
        $hasBundledToolchain = $true
    } else {
        # Check if an archive exists to auto-extract
        $zipCandidates = @(
            (Join-Path $currentDir "clang-plugin-toolchain.zip"),
            (Join-Path $currentDir "release_artifacts\clang-plugin-toolchain.zip")
        )
        foreach ($zip in $zipCandidates) {
            if (Test-Path $zip) {
                Write-Host ("[+] Auto-extracting bundled clang toolchain from: " + $zip) -ForegroundColor Cyan
                Expand-Archive -Path $zip -DestinationPath $toolchainDir -Force
                if (Test-Path (Join-Path $toolchainBin 'clang-cl.exe')) {
                    $hasBundledToolchain = $true
                    Write-Host "[+] Bundled toolchain extracted successfully to toolchain/" -ForegroundColor Green
                }
                break
            }
        }
    }
}

if ($hasBundledToolchain) {
    [Environment]::SetEnvironmentVariable('LLVMInstallDir', $toolchainDir, 'User')
    Write-Host ("[+] Bound LLVMInstallDir to bundled toolchain: " + $toolchainDir) -ForegroundColor Green
}

# 3. Clean existing/old MemLLVM paths from PATH (prevents accumulating duplicates on repeated runs or directory moves)
$cleanedPath = [System.Collections.Generic.List[string]]::new()
foreach ($entry in $pathParts) {
    $p = $entry.TrimEnd('\')
    $isMatchOld = ($oldDir -and ($p.StartsWith($oldDir, [StringComparison]::OrdinalIgnoreCase)))
    $isMatchOldLLVM = ($oldLLVMDir -and ($p.StartsWith($oldLLVMDir, [StringComparison]::OrdinalIgnoreCase)))
    $isMatchNew = ($p.StartsWith($currentDir, [StringComparison]::OrdinalIgnoreCase))
    $isGenericMemLLVM = ($p -match '\\MemLLVM(-SDK.*)?\\(bin|Tools|toolchain\\bin)$')
    if (-not ($isMatchOld -or $isMatchOldLLVM -or $isMatchNew -or $isGenericMemLLVM)) {
        $cleanedPath.Add($entry)
    }
}

# 4. Mount current SDK bin, toolchain bin, and Tools directories to User PATH
if (Test-Path $newTools) { $cleanedPath.Insert(0, $newTools) }
if (Test-Path $newBin)   { $cleanedPath.Insert(0, $newBin) }
if ($hasBundledToolchain -and (Test-Path $toolchainBin)) {
    $cleanedPath.Insert(0, $toolchainBin)
}

$finalPathStr = $cleanedPath -join ';'
[Environment]::SetEnvironmentVariable('PATH', $finalPathStr, 'User')

Write-Host "-----------------------------------------------------------------------" -ForegroundColor DarkGray
Write-Host ("[OK] MEMLLVM_DIR    -> " + $currentDir) -ForegroundColor Green
if ($hasBundledToolchain) {
    Write-Host ("[OK] LLVMInstallDir -> " + $toolchainDir) -ForegroundColor Green
}
if (Test-Path $newBin)   { Write-Host ("[OK] PATH entry     -> " + $newBin) -ForegroundColor Green }
if ($hasBundledToolchain -and (Test-Path $toolchainBin)) {
    Write-Host ("[OK] PATH entry     -> " + $toolchainBin) -ForegroundColor Green
}
if (Test-Path $newTools) { Write-Host ("[OK] PATH entry     -> " + $newTools) -ForegroundColor Green }
Write-Host "=======================================================================" -ForegroundColor Cyan
Write-Host "[SUCCESS] Environment variables configured successfully!" -ForegroundColor Cyan
Write-Host "          1. Native Visual Studio (.vcxproj) projects can directly use `$(MEMLLVM_DIR)." -ForegroundColor White
if ($hasBundledToolchain) {
    Write-Host "          2. Visual Studio ClangCL automatically binds to bundled toolchain (`$(LLVMInstallDir))." -ForegroundColor White
}
Write-Host "          3. Moving the SDK to any new path and running this script will update all paths cleanly." -ForegroundColor White
Write-Host "          4. To uninstall, run: setup_env.bat --uninstall" -ForegroundColor Gray
Write-Host "=======================================================================" -ForegroundColor Cyan
Write-Host ""
if ([Environment]::UserInteractive) {
    Read-Host "Press Enter to exit..."
}
