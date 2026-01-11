# PowerShell version definitions for Windows builds

param()

$script:LIBZMQ_VERSION = "4.3.5"
$script:LIBSODIUM_VERSION = "1.0.19"

# Export variables to caller scope
if ($MyInvocation.InvocationName -ne '.') {
    $global:LIBZMQ_VERSION = $script:LIBZMQ_VERSION
    $global:LIBSODIUM_VERSION = $script:LIBSODIUM_VERSION
}

Write-Host "==================================="
Write-Host "Build Configuration"
Write-Host "==================================="
Write-Host "libzmq version:    $script:LIBZMQ_VERSION"
Write-Host "libsodium version: $script:LIBSODIUM_VERSION"
Write-Host "==================================="
