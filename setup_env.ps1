# =======================================================================
# MemLLVM SDK - Environment Variable Auto-Configuration & Updater
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
$oldPathStr = [Environment]::GetEnvironmentVariable('PATH', 'User')
if ([string]::IsNullOrWhiteSpace($oldPathStr)) { $oldPathStr = '' }
$pathParts = @($oldPathStr -split ';' | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })

if ($isUninstall) {
    Write-Host "[*] Performing uninstall cleanup for MemLLVM environment variables..." -ForegroundColor Yellow
    [Environment]::SetEnvironmentVariable('MEMLLVM_DIR', $null, 'User')

    $filteredPath = [System.Collections.Generic.List[string]]::new()
    foreach ($entry in $pathParts) {
        $p = $entry.TrimEnd('\')
        $isOld = ($oldDir -and ($p.StartsWith($oldDir, [StringComparison]::OrdinalIgnoreCase)))
        $isCurr = ($p.StartsWith($currentDir, [StringComparison]::OrdinalIgnoreCase))
        $isGenericMemLLVM = ($p -match '\\MemLLVM(-SDK.*)?\\(bin|Tools)$')
        if (-not ($isOld -or $isCurr -or $isGenericMemLLVM)) {
            $filteredPath.Add($entry)
        }
    }
    $newPathStr = $filteredPath -join ';'
    [Environment]::SetEnvironmentVariable('PATH', $newPathStr, 'User')

    Write-Host "[+] Successfully removed MEMLLVM_DIR and PATH entries from User Environment!" -ForegroundColor Green
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
        Write-Host "[*] MEMLLVM_DIR already matches current directory. Refreshing PATH entries..." -ForegroundColor Gray
    } else {
        Write-Host ("[!] Detected previous SDK directory : " + $oldDir) -ForegroundColor Yellow
        Write-Host ("[!] Automatically migrating and updating to : " + $currentDir) -ForegroundColor Yellow
    }
} else {
    Write-Host "[*] Initializing MEMLLVM_DIR in User Environment..." -ForegroundColor Gray
}

# 1. Update MEMLLVM_DIR in User Environment
[Environment]::SetEnvironmentVariable('MEMLLVM_DIR', $currentDir, 'User')

# 2. Clean existing/old MemLLVM paths from PATH (prevents accumulating duplicates on repeated runs or directory moves)
$newBin = Join-Path $currentDir 'bin'
$newTools = Join-Path $currentDir 'Tools'

$cleanedPath = [System.Collections.Generic.List[string]]::new()
foreach ($entry in $pathParts) {
    $p = $entry.TrimEnd('\')
    $isMatchOld = ($oldDir -and ($p.StartsWith($oldDir, [StringComparison]::OrdinalIgnoreCase)))
    $isMatchNew = ($p.StartsWith($currentDir, [StringComparison]::OrdinalIgnoreCase))
    $isGenericMemLLVM = ($p -match '\\MemLLVM(-SDK.*)?\\(bin|Tools)$')
    if (-not ($isMatchOld -or $isMatchNew -or $isGenericMemLLVM)) {
        $cleanedPath.Add($entry)
    }
}

# 3. Mount current SDK bin and Tools directories to User PATH
if (Test-Path $newTools) { $cleanedPath.Insert(0, $newTools) }
if (Test-Path $newBin)   { $cleanedPath.Insert(0, $newBin) }

$finalPathStr = $cleanedPath -join ';'
[Environment]::SetEnvironmentVariable('PATH', $finalPathStr, 'User')

Write-Host "-----------------------------------------------------------------------" -ForegroundColor DarkGray
Write-Host ("[OK] MEMLLVM_DIR -> " + $currentDir) -ForegroundColor Green
if (Test-Path $newBin)   { Write-Host ("[OK] PATH entry  -> " + $newBin) -ForegroundColor Green }
if (Test-Path $newTools) { Write-Host ("[OK] PATH entry  -> " + $newTools) -ForegroundColor Green }
Write-Host "=======================================================================" -ForegroundColor Cyan
Write-Host "[SUCCESS] Environment variables configured successfully!" -ForegroundColor Cyan
Write-Host "          1. Native Visual Studio (.vcxproj) projects can directly use `$(MEMLLVM_DIR)." -ForegroundColor White
Write-Host "          2. Moving the SDK to any new path and running this script will update all paths cleanly." -ForegroundColor White
Write-Host "          3. To uninstall, run: setup_env.bat --uninstall" -ForegroundColor Gray
Write-Host "=======================================================================" -ForegroundColor Cyan
Write-Host ""
if ([Environment]::UserInteractive) {
    Read-Host "Press Enter to exit..."
}
