Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Start-Role {
  param([string] $Name)
  Start-Process -FilePath "node" -ArgumentList @((Join-Path $PSScriptRoot "dist/Server/main.js"), "--role", $Name) -PassThru -RedirectStandardOutput (Join-Path $script:LogDir "$Name.log") -RedirectStandardError (Join-Path $script:LogDir "$Name.err.log")
}

function Wait-Http {
  param([string] $Url)
  for ($i = 0; $i -lt 300; $i++) {
    try {
      Invoke-RestMethod "$Url/health" | Out-Null
      return
    } catch {
      Start-Sleep -Milliseconds 100
    }
  }
  throw "Timed out waiting for $Url"
}

function Wait-Topology {
  Wait-Http -Url $env:SHOPPINGMALL_API_A_HTTP
  Wait-Http -Url $env:SHOPPINGMALL_API_B_HTTP
}

Push-Location $PSScriptRoot
try {
  npm run build | Out-Null
  $runDir = if ($env:SHOPPINGMALL_RUN_DIR) { $env:SHOPPINGMALL_RUN_DIR } else { Join-Path ([System.IO.Path]::GetTempPath()) ([System.Guid]::NewGuid().ToString("N")) }
  $script:LogDir = Join-Path $runDir "logs"
  $workDir = Join-Path $runDir "work"
  New-Item -ItemType Directory -Force -Path $script:LogDir, $workDir | Out-Null
  $env:SHOPPINGMALL_WORK_DIR = $workDir
  $env:SHOPPINGMALL_API_A_HTTP = "http://127.0.0.1:31401"
  $env:SHOPPINGMALL_API_B_HTTP = "http://127.0.0.1:31402"
  $env:SHOPPINGMALL_WORKFLOW_A_ENDPOINT = "tcp://127.0.0.1:31403"
  $env:SHOPPINGMALL_WORKFLOW_B_ENDPOINT = "tcp://127.0.0.1:31404"

  $roles = @("api-a", "api-b") | ForEach-Object { Start-Role $_ }
  try {
    Wait-Topology
    node (Join-Path $PSScriptRoot "dist/Client/main.js")
  } finally {
    $roles | ForEach-Object { Stop-Process -Id $_.Id -ErrorAction SilentlyContinue }
  }
} finally {
  Pop-Location
}
