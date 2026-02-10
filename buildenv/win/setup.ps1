# =============================================================================
# zlink Development Environment Setup Script (Windows)
# =============================================================================
# Sets up the complete development environment for all zlink bindings:
#   Core C/C++ (VS 2022 + CMake), .NET 8, Java 22, Node.js 20+, Python 3.9+
#
# Usage:
#   .\buildenv\win\setup.ps1                        # Detect only (no changes)
#   .\buildenv\win\setup.ps1 -Install               # Install missing tools
#   .\buildenv\win\setup.ps1 -Install -BuildCore    # Install + build core library
#   .\buildenv\win\setup.ps1 -Bindings java,node    # Check specific bindings only
#   . .\buildenv\win\env.ps1                        # Activate dev environment
# =============================================================================

param(
    [string[]]$Bindings = @("all"),
    [switch]$Install,
    [switch]$BuildCore,
    [switch]$NoBuildCore
)

$ErrorActionPreference = "Stop"

# ---------------------------------------------------------------------------
# Resolve paths
# ---------------------------------------------------------------------------
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot  = (Resolve-Path (Join-Path $ScriptDir "..\..")).Path
$EnvFile   = Join-Path $ScriptDir "env.ps1"

# Normalize bindings: split comma-separated values and flatten
$AllBindings = @("cpp", "dotnet", "java", "node", "python")
$Bindings = $Bindings | ForEach-Object { $_ -split "," } | ForEach-Object { $_.Trim().ToLower() } | Where-Object { $_ }
if ($Bindings -contains "all") { $Bindings = $AllBindings }

# Track detected values for env.ps1 generation
$script:EnvVars = @{}

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------
function Write-Banner {
    Write-Host ""
    Write-Host "====================================" -ForegroundColor Cyan
    Write-Host "  zlink Development Environment"      -ForegroundColor Cyan
    Write-Host "====================================" -ForegroundColor Cyan
    Write-Host "  Repository : $RepoRoot"
    Write-Host "  Bindings   : $($Bindings -join ', ')"
    Write-Host "  Mode       : $(if ($Install) { 'Install' } else { 'Detect only' })"
    Write-Host "====================================" -ForegroundColor Cyan
    Write-Host ""
}

function Write-Status {
    param(
        [string]$Tool,
        [string]$Status,   # OK, MISSING, WRONG_VERSION, OPTIONAL
        [string]$Detail = ""
    )
    $color = switch ($Status) {
        "OK"            { "Green" }
        "MISSING"       { "Red" }
        "WRONG_VERSION" { "Yellow" }
        "OPTIONAL"      { "DarkYellow" }
        default         { "White" }
    }
    $icon = switch ($Status) {
        "OK"            { "[OK]" }
        "MISSING"       { "[--]" }
        "WRONG_VERSION" { "[!!]" }
        "OPTIONAL"      { "[??]" }
        default         { "[  ]" }
    }
    $line = "  {0,-8} {1,-22} {2}" -f $icon, $Tool, $Detail
    Write-Host $line -ForegroundColor $color
}

# ---------------------------------------------------------------------------
# Detection functions
# ---------------------------------------------------------------------------

function Test-VisualStudio {
    $result = @{ Found = $false; Version = ""; Path = ""; HasVCTools = $false }
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) { return $result }

    # 1. Try with individual VC component (more reliable than full workload check)
    $vsPath = & $vswhere -version "[17.0,18.0)" -products * `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
        -property installationPath 2>$null | Select-Object -First 1

    if ($vsPath) {
        $result.HasVCTools = $true
    } else {
        # 2. Fallback: check for workload
        $vsPath = & $vswhere -version "[17.0,18.0)" -products * `
            -requires Microsoft.VisualStudio.Workload.VCTools `
            -property installationPath 2>$null | Select-Object -First 1
        if ($vsPath) { $result.HasVCTools = $true }
    }

    if (-not $vsPath) {
        # 3. Last resort: any VS 2022 installation (without C++ tools)
        $vsPath = & $vswhere -version "[17.0,18.0)" -products * `
            -property installationPath 2>$null | Select-Object -First 1
    }

    if ($vsPath) {
        $vsVersion = & $vswhere -version "[17.0,18.0)" -products * `
            -property installationVersion 2>$null | Select-Object -First 1
        $result.Found   = $true
        $result.Version = $vsVersion
        $result.Path    = $vsPath
    }
    return $result
}

function Test-CMake {
    $result = @{ Found = $false; Version = ""; MinVersion = "3.23"; Exe = "cmake" }

    # 1. Check PATH
    try {
        $out = cmake --version 2>$null | Select-Object -First 1
        if ($out -match "(\d+\.\d+\.\d+)") {
            $result.Version = $matches[1]
            if ([version]$matches[1] -ge [version]$result.MinVersion) {
                $result.Found = $true
                return $result
            }
        }
    } catch {}

    # 2. Check VS 2022 bundled CMake
    $vsCmakePaths = @(
        "C:\Program Files\Microsoft Visual Studio\2022\Community",
        "C:\Program Files\Microsoft Visual Studio\2022\Professional",
        "C:\Program Files\Microsoft Visual Studio\2022\Enterprise",
        "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"
    )
    foreach ($vsBase in $vsCmakePaths) {
        $cmakeExe = Join-Path $vsBase "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
        if (Test-Path $cmakeExe) {
            try {
                $out = & $cmakeExe --version 2>$null | Select-Object -First 1
                if ($out -match "(\d+\.\d+\.\d+)") {
                    $result.Version = $matches[1]
                    $result.Exe = $cmakeExe
                    if ([version]$matches[1] -ge [version]$result.MinVersion) {
                        $result.Found = $true
                        return $result
                    }
                }
            } catch {}
        }
    }
    return $result
}

function Test-Ninja {
    $result = @{ Found = $false; Version = ""; Exe = "ninja" }
    try {
        $cmd = Get-Command ninja -ErrorAction SilentlyContinue
        $ninjaExe = if ($cmd) { $cmd.Source } else { $null }
        if ($ninjaExe -and (Test-Path $ninjaExe)) {
            $out = & $ninjaExe --version 2>$null
            if ($out -match "(\d+\.\d+)") {
                $result.Found   = $true
                $result.Version = $out.Trim()
                $result.Exe     = $ninjaExe
                return $result
            }
        }
    } catch {}

    # Fallback: VS bundled Ninja
    $vsNinjaPaths = @(
        "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe",
        "C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe",
        "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe",
        "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
    )
    foreach ($ninjaExe in $vsNinjaPaths) {
        if (-not (Test-Path $ninjaExe)) { continue }
        try {
            $out = & $ninjaExe --version 2>$null
            if ($out -match "(\d+\.\d+)") {
                $result.Found   = $true
                $result.Version = $out.Trim()
                $result.Exe     = $ninjaExe
                return $result
            }
        } catch {}
    }
    return $result
}

function Test-DotNetSDK {
    $result = @{ Found = $false; Version = ""; MinVersion = "8.0"; Exe = "dotnet" }
    $dotnetCandidates = @()
    try {
        $cmd = Get-Command dotnet -ErrorAction SilentlyContinue
        if ($cmd -and $cmd.Source -and (Test-Path $cmd.Source)) {
            $dotnetCandidates += $cmd.Source
        }
    } catch {}

    $dotnetCandidates += "C:\Program Files\dotnet\dotnet.exe"
    $dotnetCandidates += "C:\Program Files (x86)\dotnet\dotnet.exe"
    $dotnetCandidates = $dotnetCandidates | Select-Object -Unique

    foreach ($dotnetExe in $dotnetCandidates) {
        if (-not (Test-Path $dotnetExe)) { continue }
        try {
            $out = & $dotnetExe --version 2>$null
            if ($out -match "(\d+\.\d+\.\d+)") {
                $result.Version = $matches[1]
                $result.Exe = $dotnetExe
                if ([version]$matches[1] -ge [version]"$($result.MinVersion).0") {
                    $result.Found = $true
                    return $result
                }
            }
        } catch {}
    }
    return $result
}

function Test-JDK22 {
    $result = @{ Found = $false; Version = ""; Path = "" }

    # 1. Check JDK22_HOME env var
    if ($env:JDK22_HOME -and (Test-Path (Join-Path $env:JDK22_HOME "bin\java.exe"))) {
        $result.Path = $env:JDK22_HOME
    }
    # 2. Check JAVA_HOME
    elseif ($env:JAVA_HOME -and (Test-Path (Join-Path $env:JAVA_HOME "bin\java.exe"))) {
        try {
            $oldPref = $ErrorActionPreference; $ErrorActionPreference = "Continue"
            $javaOut = & "$env:JAVA_HOME\bin\java.exe" -version 2>&1 | Out-String
            $ErrorActionPreference = $oldPref
            if ($javaOut -match '"?22[\.\d]*"?') {
                $result.Path = $env:JAVA_HOME
            }
        } catch {}
    }

    # 3. Scan Adoptium install directory
    if (-not $result.Path) {
        $adoptiumDirs = @(
            "C:\Program Files\Eclipse Adoptium\jdk-22*",
            "C:\Program Files\Java\jdk-22*"
        )
        foreach ($pattern in $adoptiumDirs) {
            $dirs = Get-Item $pattern -ErrorAction SilentlyContinue | Sort-Object Name -Descending
            if ($dirs) {
                $result.Path = $dirs[0].FullName
                break
            }
        }
    }

    # Validate the found path
    if ($result.Path) {
        $javaExe = Join-Path $result.Path "bin\java.exe"
        if (Test-Path $javaExe) {
            try {
                $oldPref = $ErrorActionPreference; $ErrorActionPreference = "Continue"
                $javaOut = & $javaExe -version 2>&1 | Out-String
                $ErrorActionPreference = $oldPref
                if ($javaOut -match "(\d+[\.\d]*)") {
                    $result.Version = $matches[1]
                    if ($result.Version -match "^22") {
                        $result.Found = $true
                    }
                }
            } catch {}
        }
    }
    return $result
}

function Test-NodeJS {
    $result = @{ Found = $false; Version = ""; MinVersion = "20.0.0" }
    try {
        $out = node --version 2>$null
        if ($out -match "v(\d+\.\d+\.\d+)") {
            $result.Version = $matches[1]
            if ([version]$matches[1] -ge [version]$result.MinVersion) {
                $result.Found = $true
            }
        }
    } catch {}
    return $result
}

function Test-Python {
    $result = @{
        Found = $false
        Version = ""
        MinVersion = "3.9"
        Exe = ""
        Path = ""
        ScriptsPath = ""
    }
    $minVer = [version]"$($result.MinVersion).0"

    # Try python first, then python3
    foreach ($cmd in @("python", "python3")) {
        try {
            # Skip Windows Store stub (AppInstallerPythonRedirector)
            $exePath = (Get-Command $cmd -ErrorAction SilentlyContinue).Source
            if ($exePath -and $exePath -like "*\WindowsApps\*") { continue }
            if (-not $exePath -or -not (Test-Path $exePath)) { continue }

            $oldPref = $ErrorActionPreference; $ErrorActionPreference = "Continue"
            $out = & $exePath --version 2>&1 | Out-String
            $ErrorActionPreference = $oldPref
            if ($out -match "Python\s+(\d+\.\d+\.\d+)") {
                $ver = [version]$matches[1]
                if ($ver -ge $minVer) {
                    $result.Found   = $true
                    $result.Version = $matches[1]
                    $result.Exe     = $exePath
                    $result.Path    = Split-Path -Parent $exePath
                    $result.ScriptsPath = Join-Path $result.Path "Scripts"
                    return $result
                }
            }
        } catch {}
    }

    # Try py launcher
    try {
        $pyLauncher = (Get-Command py -ErrorAction SilentlyContinue).Source
        if ($pyLauncher -and (Test-Path $pyLauncher)) {
            $oldPref = $ErrorActionPreference; $ErrorActionPreference = "Continue"
            $out = & $pyLauncher -3 --version 2>&1 | Out-String
            $ErrorActionPreference = $oldPref
            if ($out -match "Python\s+(\d+\.\d+\.\d+)") {
                $ver = [version]$matches[1]
                if ($ver -ge $minVer) {
                    $oldPref = $ErrorActionPreference; $ErrorActionPreference = "Continue"
                    $exeFromLauncher = & $pyLauncher -3 -c "import sys; print(sys.executable)" 2>&1 | Out-String
                    $ErrorActionPreference = $oldPref
                    $exeFromLauncher = $exeFromLauncher.Trim()
                    if ($exeFromLauncher -and (Test-Path $exeFromLauncher)) {
                        $result.Found   = $true
                        $result.Version = $matches[1]
                        $result.Exe     = $exeFromLauncher
                        $result.Path    = Split-Path -Parent $exeFromLauncher
                        $result.ScriptsPath = Join-Path $result.Path "Scripts"
                        return $result
                    }
                }
            }
        }
    } catch {}

    # Fallback: scan common install directories even when PATH is hijacked by WindowsApps.
    try {
        $candidateDirs = @()
        $localPythonRoot = Join-Path $env:LocalAppData "Programs\Python"
        if (Test-Path $localPythonRoot) {
            $candidateDirs += Get-ChildItem (Join-Path $localPythonRoot "Python*") -Directory -ErrorAction SilentlyContinue
        }
        $candidateDirs += Get-ChildItem "C:\Program Files\Python*" -Directory -ErrorAction SilentlyContinue
        $candidateDirs += Get-ChildItem "C:\Python*" -Directory -ErrorAction SilentlyContinue

        $candidateDirs = $candidateDirs | Sort-Object FullName -Descending
        foreach ($dir in $candidateDirs) {
            $exePath = Join-Path $dir.FullName "python.exe"
            if (-not (Test-Path $exePath)) { continue }

            $oldPref = $ErrorActionPreference; $ErrorActionPreference = "Continue"
            $out = & $exePath --version 2>&1 | Out-String
            $ErrorActionPreference = $oldPref
            if ($out -match "Python\s+(\d+\.\d+\.\d+)") {
                $ver = [version]$matches[1]
                if ($ver -ge $minVer) {
                    $result.Found   = $true
                    $result.Version = $matches[1]
                    $result.Exe     = $exePath
                    $result.Path    = Split-Path -Parent $exePath
                    $result.ScriptsPath = Join-Path $result.Path "Scripts"
                    return $result
                }
            }
        }
    } catch {}

    return $result
}

function Test-OpenSSLDlls {
    $result = @{ Found = $false; Path = "" }
    $dllName = "libcrypto-3-x64.dll"

    $searchPaths = @()

    # 1. ZLINK_OPENSSL_BIN env var
    if ($env:ZLINK_OPENSSL_BIN) { $searchPaths += $env:ZLINK_OPENSSL_BIN }

    # 2. OPENSSL_BIN env var
    if ($env:OPENSSL_BIN) { $searchPaths += $env:OPENSSL_BIN }

    # 3. Core dist directory (may contain copies)
    $searchPaths += Join-Path $RepoRoot "core\dist\windows-x64"

    # 4. Well-known paths
    $searchPaths += "C:\Program Files\OpenSSL-Win64\bin"
    $searchPaths += "C:\Program Files\Git\mingw64\bin"

    # 5. VS 2022 bundled Git
    $vsGitPaths = @(
        "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\TeamFoundation\Team Explorer\Git\mingw64\bin",
        "C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\IDE\CommonExtensions\Microsoft\TeamFoundation\Team Explorer\Git\mingw64\bin",
        "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\CommonExtensions\Microsoft\TeamFoundation\Team Explorer\Git\mingw64\bin"
    )
    $searchPaths += $vsGitPaths

    # 6. PATH directories
    $searchPaths += ($env:PATH -split ";")

    foreach ($dir in $searchPaths) {
        if (-not $dir) { continue }
        $dll = Join-Path $dir $dllName
        if (Test-Path $dll) {
            $result.Found   = $true
            $result.Path  = $dir
            return $result
        }
    }
    return $result
}

function Test-CoreBuild {
    $result = @{ Found = $false; DllPath = ""; LibPath = "" }
    $dllPath = Join-Path $RepoRoot "core\dist\windows-x64\libzlink.dll"
    $libPath = Join-Path $RepoRoot "core\dist\windows-x64\libzlink.lib"

    if (Test-Path $dllPath) {
        $result.Found   = $true
        $result.DllPath = $dllPath
    }
    if (Test-Path $libPath) {
        $result.LibPath = $libPath
    }
    return $result
}

function Test-NodeGyp {
    $result = @{ Found = $false; Version = "" }
    try {
        $oldPref = $ErrorActionPreference; $ErrorActionPreference = "Continue"
        $out = npx node-gyp --version 2>&1 | Out-String
        $ErrorActionPreference = $oldPref
        if ($out -match "v?(\d+\.\d+\.\d+)") {
            $result.Found   = $true
            $result.Version = $matches[1]
        }
    } catch {}
    return $result
}

function Test-VSCode {
    $result = @{ Found = $false; Version = "" }
    try {
        $out = code --version 2>$null | Select-Object -First 1
        if ($out -match "(\d+\.\d+\.\d+)") {
            $result.Found   = $true
            $result.Version = $matches[1]
        }
    } catch {}
    return $result
}

function Test-VSCodeExtensions {
    $result = @{ Recommended = @(); Installed = @(); Missing = @() }
    $extFile = Join-Path $RepoRoot ".vscode\extensions.json"
    if (-not (Test-Path $extFile)) {
        $extFile = Join-Path $ScriptDir "vscode\extensions.json"
    }
    if (-not (Test-Path $extFile)) { return $result }

    $extJson = Get-Content $extFile -Raw | ConvertFrom-Json
    $result.Recommended = $extJson.recommendations

    $installedRaw = code --list-extensions 2>$null
    if ($installedRaw) {
        $installedLower = $installedRaw | ForEach-Object { $_.ToLower() }
    } else {
        $installedLower = @()
    }
    $result.Installed = $result.Recommended | Where-Object { $installedLower -contains $_.ToLower() }
    $result.Missing   = $result.Recommended | Where-Object { $installedLower -notcontains $_.ToLower() }
    return $result
}

# ---------------------------------------------------------------------------
# Installation functions
# ---------------------------------------------------------------------------

function Test-WingetAvailable {
    try {
        $null = Get-Command winget -ErrorAction Stop
        return $true
    } catch {
        return $false
    }
}

function Test-WingetPackageInstalled {
    param([string]$PackageId)

    if (-not (Test-WingetAvailable)) { return $false }

    try {
        $oldPref = $ErrorActionPreference; $ErrorActionPreference = "Continue"
        $out = winget list --id $PackageId -e --accept-source-agreements 2>&1 | Out-String
        $exitCode = $LASTEXITCODE
        $ErrorActionPreference = $oldPref
        if ($exitCode -ne 0) { return $false }
        return ($out -match [regex]::Escape($PackageId))
    } catch {
        return $false
    }
}

function Invoke-WingetInstall {
    param([string]$PackageId, [string]$DisplayName)

    if (-not (Test-WingetAvailable)) {
        Write-Host "  winget is not available. Please install $DisplayName manually." -ForegroundColor Yellow
        return $false
    }

    if (Test-WingetPackageInstalled -PackageId $PackageId) {
        Write-Host "  $DisplayName is already installed." -ForegroundColor Green
        return $true
    }

    Write-Host "  Installing $DisplayName ($PackageId) via winget..." -ForegroundColor Cyan
    winget install --id $PackageId -e --accept-package-agreements --accept-source-agreements
    if ($LASTEXITCODE -eq 0) {
        Write-Host "  $DisplayName installed successfully." -ForegroundColor Green
        return $true
    } else {
        if (Test-WingetPackageInstalled -PackageId $PackageId) {
            Write-Host "  $DisplayName is already installed." -ForegroundColor Green
            return $true
        }
        Write-Host "  Failed to install $DisplayName. Please install manually." -ForegroundColor Yellow
        return $false
    }
}

function Install-VisualStudio {
    $setupScript = Join-Path $RepoRoot "core\build-scripts\windows\setup-vs2022.ps1"
    if (Test-Path $setupScript) {
        $isAdmin = ([Security.Principal.WindowsPrincipal] [Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
            [Security.Principal.WindowsBuiltInRole]::Administrator)
        if (-not $isAdmin) {
            Write-Host "  VS 2022 Build Tools requires Administrator privileges." -ForegroundColor Yellow
            Write-Host "  Run as Administrator, or install manually:" -ForegroundColor Yellow
            Write-Host "    powershell -ExecutionPolicy Bypass -File `"$setupScript`"" -ForegroundColor White
            return
        }
        Write-Host "  Delegating to existing setup script..." -ForegroundColor Cyan
        & $setupScript
    } else {
        Invoke-WingetInstall "Microsoft.VisualStudio.2022.BuildTools" "VS 2022 Build Tools"
    }
}

function Install-CMake {
    Invoke-WingetInstall "Kitware.CMake" "CMake"
}

function Install-Ninja {
    Invoke-WingetInstall "Ninja-build.Ninja" "Ninja"
}

function Install-DotNetSDK {
    Invoke-WingetInstall "Microsoft.DotNet.SDK.8" ".NET SDK 8"
}

function Install-JDK22 {
    $installed = Invoke-WingetInstall "EclipseAdoptium.Temurin.22.JDK" "Adoptium JDK 22"
    if ($installed) {
        # Try to locate and set JDK22_HOME
        $dirs = Get-Item "C:\Program Files\Eclipse Adoptium\jdk-22*" -ErrorAction SilentlyContinue |
            Sort-Object Name -Descending
        if ($dirs) {
            $jdkPath = $dirs[0].FullName
            Write-Host "  Setting JDK22_HOME = $jdkPath" -ForegroundColor Cyan
            [Environment]::SetEnvironmentVariable("JDK22_HOME", $jdkPath, "User")
            [Environment]::SetEnvironmentVariable("JAVA_HOME", $jdkPath, "User")
            $env:JDK22_HOME = $jdkPath
            $env:JAVA_HOME  = $jdkPath
        }
    }
}

function Install-NodeJS {
    Invoke-WingetInstall "OpenJS.NodeJS.LTS" "Node.js LTS"
}

function Install-Python {
    Invoke-WingetInstall "Python.Python.3.12" "Python 3.12"
}

function Install-NodeGyp {
    Write-Host "  Installing node-gyp globally..." -ForegroundColor Cyan
    npm install -g node-gyp 2>&1 | Out-Null
    if ($LASTEXITCODE -eq 0) {
        Write-Host "  node-gyp installed successfully." -ForegroundColor Green
    } else {
        Write-Host "  Failed to install node-gyp." -ForegroundColor Yellow
    }
}

function Install-PythonPackages {
    param([string]$PythonExe = "python")
    Write-Host "  Installing Python packages (setuptools, wheel, pytest)..." -ForegroundColor Cyan
    $oldPref = $ErrorActionPreference; $ErrorActionPreference = "Continue"
    & $PythonExe -m pip install --disable-pip-version-check --no-warn-script-location setuptools wheel pytest 2>&1 | Out-Null
    $exitCode = $LASTEXITCODE
    $ErrorActionPreference = $oldPref
    if ($exitCode -eq 0) {
        Write-Host "  Python packages installed." -ForegroundColor Green
    } else {
        Write-Host "  Failed to install Python packages." -ForegroundColor Yellow
    }
}

function Install-VSCodeExtensions {
    param($Missing)
    if (-not $Missing -or $Missing.Count -eq 0) {
        Write-Host "  All recommended extensions already installed." -ForegroundColor Green
        return
    }
    foreach ($ext in $Missing) {
        Write-Host "  Installing: $ext ..." -ForegroundColor Cyan
        $oldPref = $ErrorActionPreference; $ErrorActionPreference = "Continue"
        code --install-extension $ext --force 2>&1 | Out-Null
        $exitCode = $LASTEXITCODE
        $ErrorActionPreference = $oldPref
        if ($exitCode -eq 0) {
            Write-Status "Extension" "OK" $ext
        } else {
            Write-Status "Extension" "MISSING" "$ext (install failed)"
        }
    }
}

# ---------------------------------------------------------------------------
# Binding setup functions
# ---------------------------------------------------------------------------

function Setup-DotNetBinding {
    Write-Host ""
    Write-Host "  Setting up .NET binding..." -ForegroundColor Cyan
    $csproj = Join-Path $RepoRoot "bindings\dotnet\src\Zlink\Zlink.csproj"
    if (Test-Path $csproj) {
        $oldPref = $ErrorActionPreference; $ErrorActionPreference = "Continue"
        dotnet restore $csproj 2>&1 | Out-Null
        $exitCode = $LASTEXITCODE
        $ErrorActionPreference = $oldPref
        if ($exitCode -eq 0) {
            Write-Host "  .NET binding: dotnet restore OK" -ForegroundColor Green
        } else {
            Write-Host "  .NET binding: dotnet restore failed" -ForegroundColor Yellow
        }
    }
}

function Setup-JavaBinding {
    Write-Host ""
    Write-Host "  Setting up Java binding..." -ForegroundColor Cyan
    $gradlew = Join-Path $RepoRoot "bindings\java\gradlew.bat"
    if (Test-Path $gradlew) {
        Push-Location (Join-Path $RepoRoot "bindings\java")
        try {
            $oldPref = $ErrorActionPreference; $ErrorActionPreference = "Continue"
            & .\gradlew.bat --version 2>&1 | Out-Null
            $exitCode = $LASTEXITCODE
            $ErrorActionPreference = $oldPref
            if ($exitCode -eq 0) {
                Write-Host "  Java binding: Gradle wrapper OK (downloads Gradle 9.3.1 if needed)" -ForegroundColor Green
            } else {
                Write-Host "  Java binding: Gradle wrapper failed" -ForegroundColor Yellow
            }
        } catch {
            Write-Host "  Java binding: Gradle wrapper error: $_" -ForegroundColor Yellow
        } finally {
            Pop-Location
        }
    }
}

function Setup-NodeBinding {
    param(
        [string]$LibPath,
        [string]$PythonExe = ""
    )
    Write-Host ""
    Write-Host "  Setting up Node.js binding..." -ForegroundColor Cyan

    if ($LibPath) {
        $nodeLibPath = $LibPath -replace '\\', '/'
        $env:ZLINK_LIB_PATH = $nodeLibPath
        Write-Host "  ZLINK_LIB_PATH = $nodeLibPath" -ForegroundColor DarkGray
    }
    if ($PythonExe -and (Test-Path $PythonExe)) {
        $env:PYTHON = $PythonExe
        Write-Host "  PYTHON = $PythonExe" -ForegroundColor DarkGray
    }

    $bindingDir = Join-Path $RepoRoot "bindings\node"
    Push-Location $bindingDir
    try {
        Write-Host "  Running node-gyp rebuild..." -ForegroundColor Cyan
        $oldPref = $ErrorActionPreference; $ErrorActionPreference = "Continue"
        npx node-gyp rebuild 2>&1
        $exitCode = $LASTEXITCODE
        $ErrorActionPreference = $oldPref
        if ($exitCode -eq 0) {
            Write-Host "  Node.js binding: native addon built OK" -ForegroundColor Green
        } else {
            Write-Host "  Node.js binding: node-gyp rebuild failed" -ForegroundColor Yellow
            Write-Host "  Ensure VS 2022 Build Tools and core library are built first." -ForegroundColor Yellow
        }
    } catch {
        Write-Host "  Node.js binding: error: $_" -ForegroundColor Yellow
    } finally {
        Pop-Location
    }
}

function Setup-PythonBinding {
    param([string]$PythonExe = "python")
    Write-Host ""
    Write-Host "  Setting up Python binding..." -ForegroundColor Cyan

    $bindingDir = Join-Path $RepoRoot "bindings\python"
    $oldPref = $ErrorActionPreference; $ErrorActionPreference = "Continue"
    & $PythonExe -m pip install -e $bindingDir 2>&1 | Out-Null
    $exitCode = $LASTEXITCODE
    $ErrorActionPreference = $oldPref
    if ($exitCode -eq 0) {
        Write-Host "  Python binding: editable install OK" -ForegroundColor Green
    } else {
        Write-Host "  Python binding: pip install -e failed" -ForegroundColor Yellow
    }
}

# ---------------------------------------------------------------------------
# Environment script generation
# ---------------------------------------------------------------------------

function Export-EnvScript {
    $lines = @()
    $lines += "# zlink Development Environment"
    $lines += "# Auto-generated by setup.ps1 on $(Get-Date -Format 'yyyy-MM-dd HH:mm')"
    $lines += "# Usage: . .\buildenv\win\env.ps1"
    $lines += ""
    $lines += "`$RepoRoot = `"$RepoRoot`""
    $lines += ""

    # Core library paths
    $dllPath = Join-Path $RepoRoot "core\dist\windows-x64\libzlink.dll"
    $libPath = Join-Path $RepoRoot "core\dist\windows-x64\libzlink.lib"
    $lines += "# Core library"
    $lines += "`$env:ZLINK_LIBRARY_PATH = `"$dllPath`""
    $lines += "`$env:ZLINK_LIB_PATH     = `"$libPath`""
    $lines += ""

    # OpenSSL
    if ($script:EnvVars["ZLINK_OPENSSL_BIN"]) {
        $lines += "# OpenSSL runtime"
        $lines += "`$env:ZLINK_OPENSSL_BIN = `"$($script:EnvVars["ZLINK_OPENSSL_BIN"])`""
        $lines += ""
    }

    # Java
    if ($script:EnvVars["JDK22_HOME"]) {
        $lines += "# Java 22"
        $lines += "`$env:JDK22_HOME = `"$($script:EnvVars["JDK22_HOME"])`""
        $lines += "`$env:JAVA_HOME  = `$env:JDK22_HOME"
        $lines += ""
    }

    # Python
    $lines += "# Python"
    $pyPath = Join-Path $RepoRoot "bindings\python\src"
    $lines += "`$env:PYTHONPATH = `"$pyPath`""
    $lines += ""

    # PATH (with dedup)
    $distDir = Join-Path $RepoRoot "core\dist\windows-x64"
    $lines += "# PATH (duplicates prevented)"
    $lines += "`$_pathsToAdd = @(`"$distDir`""
    if ($script:EnvVars["PYTHON_DIR"]) {
        $lines += "    , `"$($script:EnvVars["PYTHON_DIR"])`""
    }
    if ($script:EnvVars["PYTHON_SCRIPTS_DIR"]) {
        $lines += "    , `"$($script:EnvVars["PYTHON_SCRIPTS_DIR"])`""
    }
    if ($script:EnvVars["ZLINK_OPENSSL_BIN"]) {
        $lines += "    , `"$($script:EnvVars["ZLINK_OPENSSL_BIN"])`""
    }
    if ($script:EnvVars["CMAKE_DIR"]) {
        $lines += "    , `"$($script:EnvVars["CMAKE_DIR"])`""
    }
    if ($script:EnvVars["DOTNET_DIR"]) {
        $lines += "    , `"$($script:EnvVars["DOTNET_DIR"])`""
    }
    if ($script:EnvVars["NINJA_DIR"]) {
        $lines += "    , `"$($script:EnvVars["NINJA_DIR"])`""
    }
    $lines += ") | Where-Object { `$_ -and (`$env:PATH -split ';') -notcontains `$_ }"
    $lines += "if (`$_pathsToAdd) { `$env:PATH = (`$_pathsToAdd -join ';') + ';' + `$env:PATH }"
    $lines += ""
    $lines += "Write-Host 'zlink dev environment activated.' -ForegroundColor Green"

    $content = $lines -join "`r`n"
    Set-Content -Path $EnvFile -Value $content -Encoding UTF8
    Write-Host "  Generated: $EnvFile" -ForegroundColor Green
}

function Export-VSCodeSettings {
    $vscodeDir   = Join-Path $RepoRoot ".vscode"
    $templateDir = Join-Path $ScriptDir "vscode"

    if (-not (Test-Path $templateDir)) {
        Write-Host "  Template directory not found: $templateDir" -ForegroundColor Yellow
        return
    }

    if (-not (Test-Path $vscodeDir)) {
        New-Item -ItemType Directory -Path $vscodeDir | Out-Null
    }

    $files = @("settings.json", "extensions.json", "tasks.json")
    foreach ($file in $files) {
        $src = Join-Path $templateDir $file
        $dst = Join-Path $vscodeDir $file
        if (-not (Test-Path $src)) { continue }
        if (-not (Test-Path $dst)) {
            Copy-Item $src $dst
            Write-Status "VSCode" "OK" "Created .vscode/$file"
        } else {
            Write-Status "VSCode" "OK" ".vscode/$file already exists (skipped)"
        }
    }
}

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

Write-Banner

# ===== DETECTION PHASE =====
Write-Host "Detecting installed tools..." -ForegroundColor Cyan
Write-Host ""

# --- Core tools (always checked) ---
Write-Host "  Core Tools" -ForegroundColor White
Write-Host "  ----------" -ForegroundColor DarkGray

$vsResult = Test-VisualStudio
if ($vsResult.Found -and $vsResult.HasVCTools) {
    Write-Status "VS 2022" "OK" "v$($vsResult.Version) @ $($vsResult.Path)"
} elseif ($vsResult.Found) {
    Write-Status "VS 2022" "WRONG_VERSION" "v$($vsResult.Version) found but C++ Build Tools missing"
} else {
    Write-Status "VS 2022" "MISSING" "Visual Studio 2022 with C++ Build Tools required"
}

$cmakeResult = Test-CMake
if ($cmakeResult.Found) {
    $cmakeDetail = "v$($cmakeResult.Version)"
    if ($cmakeResult.Exe -ne "cmake") {
        $cmakeDetail += " (VS bundled)"
        $script:EnvVars["CMAKE_DIR"] = Split-Path $cmakeResult.Exe
    }
    Write-Status "CMake" "OK" $cmakeDetail
} elseif ($cmakeResult.Version) {
    Write-Status "CMake" "WRONG_VERSION" "v$($cmakeResult.Version) (need >= $($cmakeResult.MinVersion))"
} else {
    Write-Status "CMake" "MISSING" "CMake >= $($cmakeResult.MinVersion) required"
}

$ninjaResult = Test-Ninja
if ($ninjaResult.Found) {
    $ninjaDetail = "v$($ninjaResult.Version)"
    if ($ninjaResult.Exe -and $ninjaResult.Exe -ne "ninja") {
        $ninjaDetail += " (VS bundled)"
        $script:EnvVars["NINJA_DIR"] = Split-Path $ninjaResult.Exe
    }
    Write-Status "Ninja" "OK" $ninjaDetail
} else {
    Write-Status "Ninja" "OPTIONAL" "Not found (optional, used by VS Code tasks)"
}

$opensslResult = Test-OpenSSLDlls
if ($opensslResult.Found) {
    Write-Status "OpenSSL" "OK" "DLLs found @ $($opensslResult.Path)"
    $script:EnvVars["ZLINK_OPENSSL_BIN"] = $opensslResult.Path
} else {
    Write-Status "OpenSSL" "MISSING" "libcrypto-3-x64.dll / libssl-3-x64.dll not found"
}

$coreBuildResult = Test-CoreBuild
if ($coreBuildResult.Found) {
    Write-Status "Core Build" "OK" $coreBuildResult.DllPath
} else {
    Write-Status "Core Build" "MISSING" "Run with -BuildCore to build, or build manually"
}

$vscodeResult = Test-VSCode
if ($vscodeResult.Found) {
    Write-Status "VS Code" "OK" "v$($vscodeResult.Version)"
    $extResult = Test-VSCodeExtensions
    $total   = $extResult.Recommended.Count
    $ok      = $extResult.Installed.Count
    $missing = $extResult.Missing.Count
    if ($missing -eq 0) {
        Write-Status "Extensions" "OK" "$total/$total installed"
    } else {
        Write-Status "Extensions" "WRONG_VERSION" "$ok/$total installed ($missing missing)"
    }
} else {
    Write-Status "VS Code" "OPTIONAL" "Not found (optional)"
}

Write-Host ""

# --- Binding-specific tools ---
if ($Bindings -contains "dotnet") {
    Write-Host "  .NET Binding" -ForegroundColor White
    Write-Host "  ------------" -ForegroundColor DarkGray
    $dotnetResult = Test-DotNetSDK
    if ($dotnetResult.Found) {
        $dotnetDetail = "v$($dotnetResult.Version) (>= $($dotnetResult.MinVersion))"
        if ($dotnetResult.Exe -and $dotnetResult.Exe -ne "dotnet") {
            $dotnetDetail += " (custom path)"
            $script:EnvVars["DOTNET_DIR"] = Split-Path $dotnetResult.Exe
        }
        Write-Status ".NET SDK" "OK" $dotnetDetail
    } elseif ($dotnetResult.Version) {
        Write-Status ".NET SDK" "WRONG_VERSION" "v$($dotnetResult.Version) (need >= $($dotnetResult.MinVersion))"
    } else {
        Write-Status ".NET SDK" "MISSING" ".NET SDK >= $($dotnetResult.MinVersion) required"
    }
    Write-Host ""
}

if ($Bindings -contains "java") {
    Write-Host "  Java Binding" -ForegroundColor White
    Write-Host "  ------------" -ForegroundColor DarkGray
    $jdkResult = Test-JDK22
    if ($jdkResult.Found) {
        Write-Status "JDK 22" "OK" "v$($jdkResult.Version) @ $($jdkResult.Path)"
        $script:EnvVars["JDK22_HOME"] = $jdkResult.Path
    } else {
        Write-Status "JDK 22" "MISSING" "Adoptium JDK 22 required (build.gradle toolchain)"
    }
    Write-Host ""
}

if ($Bindings -contains "node") {
    Write-Host "  Node.js Binding" -ForegroundColor White
    Write-Host "  ---------------" -ForegroundColor DarkGray
    $nodeResult = Test-NodeJS
    if ($nodeResult.Found) {
        Write-Status "Node.js" "OK" "v$($nodeResult.Version) (>= $($nodeResult.MinVersion))"
    } elseif ($nodeResult.Version) {
        Write-Status "Node.js" "WRONG_VERSION" "v$($nodeResult.Version) (need >= $($nodeResult.MinVersion))"
    } else {
        Write-Status "Node.js" "MISSING" "Node.js >= $($nodeResult.MinVersion) required"
    }

    $nodegypResult = Test-NodeGyp
    if ($nodegypResult.Found) {
        Write-Status "node-gyp" "OK" "v$($nodegypResult.Version)"
    } else {
        Write-Status "node-gyp" "MISSING" "Required to build native addon"
    }
    Write-Host ""
}

# Python: detect if python or node binding is selected (node-gyp requires Python)
if (($Bindings -contains "python") -or ($Bindings -contains "node")) {
    Write-Host "  Python $(if ($Bindings -contains 'python') { 'Binding' } else { '(node-gyp dependency)' })" -ForegroundColor White
    Write-Host "  --------------" -ForegroundColor DarkGray
    $pythonResult = Test-Python
    if ($pythonResult.Found) {
        Write-Status "Python" "OK" "v$($pythonResult.Version) ($($pythonResult.Exe))"
        if ($pythonResult.Path) {
            $script:EnvVars["PYTHON_DIR"] = $pythonResult.Path
        }
        if ($pythonResult.ScriptsPath -and (Test-Path $pythonResult.ScriptsPath)) {
            $script:EnvVars["PYTHON_SCRIPTS_DIR"] = $pythonResult.ScriptsPath
        }
    } elseif ($pythonResult.Version) {
        Write-Status "Python" "WRONG_VERSION" "v$($pythonResult.Version) (need >= $($pythonResult.MinVersion))"
    } else {
        Write-Status "Python" "MISSING" "Python >= $($pythonResult.MinVersion) required"
    }
    Write-Host ""
}

# ===== INSTALLATION PHASE =====
if ($Install) {
    Write-Host "====================================" -ForegroundColor Cyan
    Write-Host "  Installing missing tools"           -ForegroundColor Cyan
    Write-Host "====================================" -ForegroundColor Cyan
    Write-Host ""

    # Core tools
    if (-not $vsResult.Found -or -not $vsResult.HasVCTools) { Install-VisualStudio }
    if (-not $cmakeResult.Found) { Install-CMake }
    if (-not $ninjaResult.Found) { Install-Ninja }

    # Binding tools
    if (($Bindings -contains "dotnet") -and -not $dotnetResult.Found) {
        Install-DotNetSDK
    }
    if (($Bindings -contains "java") -and -not $jdkResult.Found) {
        Install-JDK22
    }
    if (($Bindings -contains "node") -and -not $nodeResult.Found) {
        Install-NodeJS
    }
    # Python: install if python or node binding needs it
    if ((($Bindings -contains "python") -or ($Bindings -contains "node")) -and -not $pythonResult.Found) {
        Install-Python
    }

    # Post-install: node-gyp (needs Node.js)
    if (($Bindings -contains "node") -and -not $nodegypResult.Found) {
        # Re-check Node.js after potential install
        $nodeCheck = Test-NodeJS
        if ($nodeCheck.Found) { Install-NodeGyp }
    }

    # Post-install: Python packages
    if (($Bindings -contains "python") -or ($Bindings -contains "node")) {
        $pyCheck = Test-Python
        if ($pyCheck.Found) {
            if ($pyCheck.Path) {
                $script:EnvVars["PYTHON_DIR"] = $pyCheck.Path
            }
            if ($pyCheck.ScriptsPath -and (Test-Path $pyCheck.ScriptsPath)) {
                $script:EnvVars["PYTHON_SCRIPTS_DIR"] = $pyCheck.ScriptsPath
            }
            Install-PythonPackages -PythonExe $pyCheck.Exe
        }
    }

    # VSCode extensions
    if ($vscodeResult.Found) {
        Write-Host ""
        Write-Host "  VSCode Extensions" -ForegroundColor White
        Write-Host "  -----------------" -ForegroundColor DarkGray
        Install-VSCodeExtensions -Missing $extResult.Missing
    }

    Write-Host ""

    # Re-run detection after installations
    Write-Host "Re-checking after installation..." -ForegroundColor Cyan
    $vsResult       = Test-VisualStudio
    $cmakeResult    = Test-CMake
    $ninjaResult    = Test-Ninja
    $opensslResult  = Test-OpenSSLDlls
    if ($cmakeResult.Found -and $cmakeResult.Exe -and $cmakeResult.Exe -ne "cmake") {
        $script:EnvVars["CMAKE_DIR"] = Split-Path $cmakeResult.Exe
    }
    if ($ninjaResult.Found -and $ninjaResult.Exe -and $ninjaResult.Exe -ne "ninja") {
        $script:EnvVars["NINJA_DIR"] = Split-Path $ninjaResult.Exe
    }
    if ($opensslResult.Found) { $script:EnvVars["ZLINK_OPENSSL_BIN"] = $opensslResult.Path }
    if ($Bindings -contains "dotnet") {
        $dotnetResult = Test-DotNetSDK
        if ($dotnetResult.Found -and $dotnetResult.Exe -and $dotnetResult.Exe -ne "dotnet") {
            $script:EnvVars["DOTNET_DIR"] = Split-Path $dotnetResult.Exe
        }
    }
    if ($Bindings -contains "java") {
        $jdkResult = Test-JDK22
        if ($jdkResult.Found) { $script:EnvVars["JDK22_HOME"] = $jdkResult.Path }
    }
    if ($Bindings -contains "node")   { $nodeResult = Test-NodeJS; $nodegypResult = Test-NodeGyp }
    if (($Bindings -contains "python") -or ($Bindings -contains "node")) {
        $pythonResult = Test-Python
        if ($pythonResult.Found) {
            if ($pythonResult.Path) {
                $script:EnvVars["PYTHON_DIR"] = $pythonResult.Path
            }
            if ($pythonResult.ScriptsPath -and (Test-Path $pythonResult.ScriptsPath)) {
                $script:EnvVars["PYTHON_SCRIPTS_DIR"] = $pythonResult.ScriptsPath
            }
        }
    }
    Write-Host ""
}

# ===== CORE BUILD PHASE =====
if ($BuildCore -and -not $NoBuildCore) {
    Write-Host "====================================" -ForegroundColor Cyan
    Write-Host "  Building core library"              -ForegroundColor Cyan
    Write-Host "====================================" -ForegroundColor Cyan
    Write-Host ""

    $buildScript = Join-Path $RepoRoot "core\build-scripts\windows\build.ps1"
    if (Test-Path $buildScript) {
        Write-Host "  Delegating to: $buildScript" -ForegroundColor DarkGray
        & $buildScript -Architecture x64 -RunTests OFF
        $coreBuildResult = Test-CoreBuild
        if ($coreBuildResult.Found) {
            Write-Host ""
            Write-Host "  Core library built successfully." -ForegroundColor Green
        } else {
            Write-Host ""
            Write-Host "  Core library build may have failed. Check output above." -ForegroundColor Yellow
        }
    } else {
        Write-Host "  Build script not found: $buildScript" -ForegroundColor Red
    }
    Write-Host ""
}

# ===== BINDING SETUP PHASE =====
$coreBuildResult = Test-CoreBuild
$hasCore = $coreBuildResult.Found

if ($hasCore) {
    # Set environment variables before binding setup
    if ($script:EnvVars["DOTNET_DIR"] -and (($env:PATH -split ';') -notcontains $script:EnvVars["DOTNET_DIR"])) {
        $env:PATH = "$($script:EnvVars["DOTNET_DIR"]);$env:PATH"
    }
    if ($script:EnvVars["NINJA_DIR"] -and (($env:PATH -split ';') -notcontains $script:EnvVars["NINJA_DIR"])) {
        $env:PATH = "$($script:EnvVars["NINJA_DIR"]);$env:PATH"
    }
    if ($script:EnvVars["PYTHON_DIR"] -and (($env:PATH -split ';') -notcontains $script:EnvVars["PYTHON_DIR"])) {
        $env:PATH = "$($script:EnvVars["PYTHON_DIR"]);$env:PATH"
    }
    if ($script:EnvVars["PYTHON_SCRIPTS_DIR"] -and (($env:PATH -split ';') -notcontains $script:EnvVars["PYTHON_SCRIPTS_DIR"])) {
        $env:PATH = "$($script:EnvVars["PYTHON_SCRIPTS_DIR"]);$env:PATH"
    }
    if ($script:EnvVars["JDK22_HOME"]) {
        $env:JDK22_HOME = $script:EnvVars["JDK22_HOME"]
        $env:JAVA_HOME  = $script:EnvVars["JDK22_HOME"]
    }
    if ($script:EnvVars["ZLINK_OPENSSL_BIN"]) {
        $env:ZLINK_OPENSSL_BIN = $script:EnvVars["ZLINK_OPENSSL_BIN"]
    }
    if ($pythonResult.Found -and $pythonResult.Exe) {
        $env:PYTHON = $pythonResult.Exe
    }
    $env:ZLINK_LIBRARY_PATH = $coreBuildResult.DllPath

    $setupAny = $false

    if (($Bindings -contains "dotnet") -and $dotnetResult.Found) {
        Setup-DotNetBinding
        $setupAny = $true
    }
    if (($Bindings -contains "java") -and $jdkResult.Found) {
        Setup-JavaBinding
        $setupAny = $true
    }
    if (($Bindings -contains "node") -and $nodeResult.Found) {
        if ($vsResult.HasVCTools -and $pythonResult.Found) {
            Setup-NodeBinding -LibPath $coreBuildResult.LibPath -PythonExe $pythonResult.Exe
        } else {
            Write-Host ""
            Write-Host "  Skipping Node.js native addon build (requires VS 2022 + Python)." -ForegroundColor DarkGray
        }
        $setupAny = $true
    }
    if (($Bindings -contains "python") -and $pythonResult.Found) {
        $pyExe = if ($pythonResult.Exe) { $pythonResult.Exe } else { "python" }
        Setup-PythonBinding -PythonExe $pyExe
        $setupAny = $true
    }

    if ($setupAny) { Write-Host "" }
} elseif (-not $NoBuildCore) {
    Write-Host "  Core library not built. Skipping binding setup." -ForegroundColor DarkGray
    Write-Host "  Run with -BuildCore to build the core library first." -ForegroundColor DarkGray
    Write-Host ""
}

# ===== VSCODE SETTINGS =====
Write-Host "====================================" -ForegroundColor Cyan
Write-Host "  VSCode Settings"                     -ForegroundColor Cyan
Write-Host "====================================" -ForegroundColor Cyan
Export-VSCodeSettings
Write-Host ""

# ===== GENERATE ENV SCRIPT =====
Write-Host "====================================" -ForegroundColor Cyan
Write-Host "  Generating environment script"      -ForegroundColor Cyan
Write-Host "====================================" -ForegroundColor Cyan
Export-EnvScript
Write-Host ""

# ===== FINAL SUMMARY =====
Write-Host "====================================" -ForegroundColor Cyan
Write-Host "  Setup Complete"                      -ForegroundColor Cyan
Write-Host "====================================" -ForegroundColor Cyan
Write-Host ""
Write-Host "  Next steps:" -ForegroundColor White
Write-Host ""
Write-Host "  1. Activate dev environment:" -ForegroundColor DarkGray
Write-Host "     . .\buildenv\win\env.ps1" -ForegroundColor White
Write-Host ""
if (-not $coreBuildResult.Found) {
    Write-Host "  2. Build core library:" -ForegroundColor DarkGray
    Write-Host "     .\buildenv\win\setup.ps1 -BuildCore" -ForegroundColor White
    Write-Host "     # or manually:" -ForegroundColor DarkGray
    Write-Host "     .\core\build-scripts\windows\build.ps1 -Architecture x64" -ForegroundColor White
    Write-Host ""
}
Write-Host "  Run tests:" -ForegroundColor DarkGray
if ($Bindings -contains "dotnet") {
    Write-Host "     dotnet test bindings\dotnet\tests\Zlink.Tests\Zlink.Tests.csproj" -ForegroundColor White
}
if ($Bindings -contains "java") {
    Write-Host "     cd bindings\java && .\gradlew.bat test" -ForegroundColor White
}
if ($Bindings -contains "node") {
    Write-Host "     cd bindings\node && node --test" -ForegroundColor White
}
if ($Bindings -contains "python") {
    Write-Host "     python -m pytest bindings\python\tests" -ForegroundColor White
}
Write-Host ""
Write-Host "====================================" -ForegroundColor Cyan
