param(
    [string]$SourceDir = (Join-Path $PSScriptRoot ".." "core"),
    [string]$BuildDir = (Join-Path $PSScriptRoot ".." "core" "build"),
    [string]$Config = "Release",
    [switch]$NoTests,
    [switch]$DisableZmq
)

if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    Write-Warning "CMake not found. Install CMake and retry."
    exit 1
}

if (-not (Test-Path $SourceDir)) {
    Write-Warning "Source directory not found: $SourceDir"
    exit 1
}

$buildTests = if ($NoTests) { "OFF" } else { "ON" }
$enableZmq = if ($DisableZmq) { "OFF" } else { "ON" }

Write-Host "Configuring core build..."
$configureArgs = @(
    "-S", $SourceDir,
    "-B", $BuildDir,
    "-DNEUROFOCUS_BUILD_TESTS=$buildTests",
    "-DNEUROFOCUS_ENABLE_ZMQ=$enableZmq"
)
& cmake @configureArgs
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

Write-Host "Building neurofocus_engine ($Config)..."
$buildArgs = @(
    "--build", $BuildDir,
    "--target", "neurofocus_engine",
    "--config", $Config
)
& cmake @buildArgs
exit $LASTEXITCODE