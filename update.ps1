# =======================================================================
# MemLLVM SDK - One-Click Auto Updater
# Fetches the latest release from GitHub and updates the local SDK in-place
# =======================================================================

param(
    [switch]$Force,
    [switch]$Help
)

$ErrorActionPreference = 'Stop'

if ($Help) {
    Write-Host "Usage: update.bat [-Force]"
    Write-Host "  -Force : Automatically proceed with update without prompting"
    exit 0
}

$currentDir = $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($currentDir)) {
    $currentDir = (Get-Item .).FullName
}
$currentDir = $currentDir.TrimEnd('\')

Write-Host "=======================================================================" -ForegroundColor Cyan
Write-Host "              MemLLVM SDK - One-Click Auto Updater                     " -ForegroundColor Cyan
Write-Host "=======================================================================" -ForegroundColor Cyan
Write-Host ""
Write-Host ("[+] Local SDK Directory : " + $currentDir) -ForegroundColor Green

$repo = "Laster-dev/MemLLVM"
$latestReleaseApi = "https://api.github.com/repos/$repo/releases/latest"
$directDownloadUrl = "https://github.com/$repo/releases/latest/download/MemLLVM-SDK-windows-x64.zip"

Write-Host "[*] Checking for the latest release on GitHub..." -ForegroundColor Yellow

$latestTag = "latest"
$releaseTitle = ""
$releaseDate = ""

try {
    $headers = @{
        'User-Agent' = 'MemLLVM-SDK-Updater'
        'Accept'     = 'application/vnd.github.v3+json'
    }
    $releaseInfo = Invoke-RestMethod -Uri $latestReleaseApi -Headers $headers -TimeoutSec 10 -ErrorAction Stop
    if ($releaseInfo) {
        $latestTag = $releaseInfo.tag_name
        $releaseTitle = $releaseInfo.name
        $releaseDate = $releaseInfo.published_at
    }
} catch {
    # If API hits unauthenticated rate limit or timeout, fallback to query via HTTP redirect
    Write-Host "[!] Note: GitHub API query rate-limited or timed out. Falling back to direct release endpoint." -ForegroundColor Gray
    try {
        $wr = [System.Net.WebRequest]::Create("https://github.com/$repo/releases/latest")
        $wr.AllowAutoRedirect = $false
        $resp = $wr.GetResponse()
        $loc = $resp.GetResponseHeader("Location")
        $resp.Close()
        if ($loc -match 'releases/tag/([^/]+)') {
            $latestTag = $matches[1]
        }
    } catch {
        # Proceed with 'latest' default
    }
}

Write-Host ("[+] Latest Version Found: " + $latestTag) -ForegroundColor Green
if ($releaseTitle) {
    Write-Host ("[+] Release Title      : " + $releaseTitle) -ForegroundColor Green
}
if ($releaseDate) {
    Write-Host ("[+] Published Date     : " + $releaseDate) -ForegroundColor Gray
}
Write-Host ""

if (-not $Force -and [Environment]::UserInteractive) {
    $confirm = Read-Host "Proceed to download and overwrite local SDK with $latestTag? [Y/n]"
    if (-not [string]::IsNullOrWhiteSpace($confirm) -and ($confirm -match '^[nN]')) {
        Write-Host "[-] Update cancelled by user." -ForegroundColor Yellow
        exit 0
    }
}

$tempBase = [System.IO.Path]::GetTempPath()
$randId = [System.Guid]::NewGuid().ToString().Substring(0, 8)
$tempZip = Join-Path $tempBase "MemLLVM-SDK-update-$randId.zip"
$tempExtractDir = Join-Path $tempBase "MemLLVM-SDK-unpack-$randId"

try {
    Write-Host ""
    Write-Host "[*] Downloading latest MemLLVM-SDK package from GitHub..." -ForegroundColor Cyan
    Write-Host ("    URL: " + $directDownloadUrl) -ForegroundColor Gray

    $hasCurl = $false
    $cmd = Get-Command curl.exe -ErrorAction SilentlyContinue
    if ($cmd) {
        $curlPath = $cmd.Source
        $hasCurl = $true
    }

    if ($hasCurl) {
        & $curlPath -f -L --progress-bar -o $tempZip $directDownloadUrl
        if ($LASTEXITCODE -ne 0 -or -not (Test-Path $tempZip)) {
            throw "curl download failed with exit code $LASTEXITCODE"
        }
    } else {
        [System.Net.ServicePointManager]::SecurityProtocol = [System.Net.SecurityProtocolType]::Tls12 -bor [System.Net.SecurityProtocolType]::Tls13
        $webClient = New-Object System.Net.WebClient
        $webClient.Headers.Add("User-Agent", "MemLLVM-SDK-Updater")
        $webClient.DownloadFile($directDownloadUrl, $tempZip)
    }

    $zipSize = (Get-Item $tempZip).Length / 1MB
    Write-Host ("[+] Download completed successfully! ($([math]::Round($zipSize, 2)) MB)") -ForegroundColor Green

    Write-Host "[*] Extracting package..." -ForegroundColor Cyan
    if (Test-Path $tempExtractDir) { Remove-Item -Recurse -Force $tempExtractDir }
    New-Item -ItemType Directory -Path $tempExtractDir -Force | Out-Null
    Expand-Archive -Path $tempZip -DestinationPath $tempExtractDir -Force

    # If the zip has a single root subdirectory, navigate into it
    $subDirs = Get-ChildItem -Path $tempExtractDir -Directory
    $files = Get-ChildItem -Path $tempExtractDir -File
    $sourceDir = $tempExtractDir
    if ($subDirs.Count -eq 1 -and $files.Count -eq 0) {
        $sourceDir = $subDirs[0].FullName
    }

    Write-Host "[*] Overwriting local SDK directory with updated files..." -ForegroundColor Cyan

    # Copy / update all files from sourceDir to currentDir
    $items = Get-ChildItem -Path $sourceDir
    foreach ($item in $items) {
        $destPath = Join-Path $currentDir $item.Name
        if ($item.PSIsContainer) {
            # Directory: copy recursively
            Copy-Item -Path $item.FullName -Destination $currentDir -Recurse -Force
        } else {
            # File: overwrite
            Copy-Item -Path $item.FullName -Destination $destPath -Force
        }
    }

    Write-Host "[+] Local SDK files updated successfully!" -ForegroundColor Green

    # Re-run environment setup to ensure paths and LLVMInstallDir are properly synchronized
    $setupScript = Join-Path $currentDir "setup_env.ps1"
    if (Test-Path $setupScript) {
        Write-Host ""
        Write-Host "[*] Synchronizing environment variables with setup_env.ps1..." -ForegroundColor Cyan
        & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $setupScript
    }

    Write-Host ""
    Write-Host "=======================================================================" -ForegroundColor Cyan
    Write-Host ("[SUCCESS] MemLLVM SDK successfully updated to version: " + $latestTag) -ForegroundColor Green
    Write-Host "=======================================================================" -ForegroundColor Cyan
} catch {
    Write-Host ""
    Write-Host ("[-] Update failed: " + $_.Exception.Message) -ForegroundColor Red
    exit 1
} finally {
    # Clean up temporary zip and unpack folder
    if (Test-Path $tempZip) {
        Remove-Item -Force $tempZip -ErrorAction SilentlyContinue
    }
    if (Test-Path $tempExtractDir) {
        Remove-Item -Recurse -Force $tempExtractDir -ErrorAction SilentlyContinue
    }
}

Write-Host ""
if ([Environment]::UserInteractive) {
    Read-Host "Press Enter to exit..."
}
