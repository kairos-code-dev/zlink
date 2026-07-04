Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Start-Server {
  param([string] $Path, [string] $Out, [string] $Err)
  Start-Process -FilePath "node" -ArgumentList @($Path) -PassThru -RedirectStandardOutput $Out -RedirectStandardError $Err
}

function Wait-Port {
  param([string] $Url)
  for ($i = 0; $i -lt 100; $i++) {
    try {
      Invoke-RestMethod "$Url/health" | Out-Null
      return
    } catch {
      Start-Sleep -Milliseconds 100
    }
  }
  throw "Timed out waiting for $Url"
}

Push-Location $PSScriptRoot
try {
  npm run build | Out-Null
  $runDir = if ($env:SUPPORTCHAT_RUN_DIR) { $env:SUPPORTCHAT_RUN_DIR } else { Join-Path ([System.IO.Path]::GetTempPath()) ([System.Guid]::NewGuid().ToString("N")) }
  $logDir = Join-Path $runDir "logs"
  $workDir = Join-Path $runDir "work"
  New-Item -ItemType Directory -Force -Path $logDir, $workDir | Out-Null
  $env:SUPPORTCHAT_WORK_DIR = $workDir
  $env:SUPPORTCHAT_STATE_FILE = Join-Path $workDir "supportchat-state.json"
  $env:SUPPORTCHAT_API_HTTP = "http://127.0.0.1:31180"

  $api = Start-Server -Path (Join-Path $PSScriptRoot "dist/Server/Api/main.js") -Out (Join-Path $logDir "api.log") -Err (Join-Path $logDir "api.err.log")
  try {
    Wait-Port -Url $env:SUPPORTCHAT_API_HTTP
    node (Join-Path $PSScriptRoot "dist/Server/Probe/main.js")
    node (Join-Path $PSScriptRoot "dist/Client/main.js")
  } finally {
    Stop-Process -Id $api.Id -ErrorAction SilentlyContinue
  }
} finally {
  Pop-Location
}
