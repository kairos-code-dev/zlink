Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$RootDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$CoreLib = Join-Path $RootDir "../../../core/build/lib/libzlink.so"
if (Test-Path $CoreLib) {
    $env:ZLINK_LIBRARY_PATH = $CoreLib
}

Set-Location $RootDir

function Invoke-Checked {
    param([string]$FilePath, [string[]]$Arguments)
    & $FilePath @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed: $FilePath $($Arguments -join ' ')"
    }
}

function Invoke-SampleWithRetry {
    param([string]$ScriptPath)
    $output = New-TemporaryFile
    try {
        for ($attempt = 1; $attempt -le 3; $attempt++) {
            & pwsh -NoProfile -ExecutionPolicy Bypass -File $ScriptPath *> $output
            if ($LASTEXITCODE -eq 0) {
                Get-Content $output
                return
            }
            $text = Get-Content $output -Raw
            if ($text -notmatch "ZlinkBindException|Timed out waiting") {
                Write-Error $text
                throw "Sample failed: $ScriptPath"
            }
            if ($attempt -eq 3) {
                Write-Error $text
                throw "Sample failed after retries: $ScriptPath"
            }
            Write-Error "sample transient port bind failure; retrying $ScriptPath ($attempt/3)"
        }
    } finally {
        Remove-Item -Force -ErrorAction SilentlyContinue $output
    }
}

$Gradle = if ($IsWindows) { Join-Path $RootDir "gradlew.bat" } else { Join-Path $RootDir "gradlew" }
Invoke-Checked $Gradle @(
    "-Pzlink.useLocalBindings=true",
    "--no-daemon",
    ":zlink-framework-testkit:contractTest",
    "--tests",
    "*SampleReleaseGateContractTest*"
)
Invoke-Checked $Gradle @(
    "-Pzlink.useLocalBindings=true",
    "--no-daemon",
    "--no-parallel",
    ":zlink-framework-testkit:fakeBackendTest",
    "--tests",
    "systems.zlink.framework.testkit.ActorRuntimeFakeBackendTest.entrySpotDestroyActorRemovesEntryOwnedActorWithoutLeftCallback"
)
Write-Output "java actor lifecycle sample gate completed"

foreach ($sample in @("TicTacToe", "Bingo")) {
    Invoke-SampleWithRetry (Join-Path $RootDir "java/$sample/run_sample.ps1")
}

foreach ($sample in @("TicTacToe", "Bingo")) {
    Invoke-SampleWithRetry (Join-Path $RootDir "kotlin/$sample/run_sample.ps1")
}

$forbiddenSamplePattern = "systems\.zlink\.(runtime|internal)"
$forbiddenSamplePattern += "|systems\.zlink\.contracts\.core\.RoutingId\."
$forbiddenSamplePattern += "|RouteStore|MetadataStore|RemoteAddressResolver"
$forbiddenSamplePattern += "|Thread\.sleep|sleep\(|CountDownLatch|toCompletableFuture\(\)"
$forbiddenSamplePattern += "|ZLinkFrameworkRuntime\.start"
$forbiddenSamplePattern += "|Fake[A-Za-z0-9_]*|Recording[A-Za-z0-9_]*|Catalog"
$forbiddenSamplePattern += "|UnsupportedOperationException\(""not needed by sample""\)"

$sources = Get-ChildItem -Path $RootDir -Recurse -Include *.java,*.kt -File |
    Where-Object { $_.FullName -notmatch "[/\\](build|bin)[/\\]" }
$offenders = $sources | Select-String -Pattern $forbiddenSamplePattern
if ($offenders) {
    $offenders | ForEach-Object { Write-Error $_.ToString() }
    throw "sample gate failed: forbidden sample pattern found"
}

Write-Output "All Java/Kotlin samples passed"
