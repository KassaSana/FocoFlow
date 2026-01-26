param(
    [string]$BackendUrl = "http://localhost:8080",
    [string]$SessionId = "live-session",
    [string]$SessionGoal = "",
    [string]$ZmqEndpoint = "tcp://127.0.0.1:5560",
    [string]$EnginePath = "",
    [string]$LogPath = "",
    [int]$ReplaySleepMs = 0,
    [int]$ReplayStartupDelayMs = 100,
    [switch]$NoBackend,
    [switch]$NoInference,
    [switch]$NoFrontend
)

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
$defaultEngine = Join-Path $root "core/build/bin/neurofocus_engine.exe"

Write-Host "Starting Neural Focus stack..."

if (-not $NoBackend) {
    $backendDir = Join-Path $root "backend"
    $mvnWrapper = Join-Path $backendDir "mvnw.cmd"
    if (Test-Path $mvnWrapper) {
        Start-Process -FilePath $mvnWrapper -WorkingDirectory $backendDir -ArgumentList "spring-boot:run" | Out-Null
        Write-Host "Started backend (mvnw.cmd spring-boot:run)"
    } else {
        $mvnPath = $null
        $mvnCmd = Get-Command mvn -ErrorAction SilentlyContinue
        if ($mvnCmd) {
            $mvnPath = $mvnCmd.Source
            if (-not $mvnPath) {
                $mvnPath = $mvnCmd.Path
            }
        }
        if (-not $mvnPath -and $env:M2_HOME) {
            $candidate = Join-Path $env:M2_HOME "bin\\mvn.cmd"
            if (Test-Path $candidate) {
                $mvnPath = $candidate
            }
        }
        if (-not $mvnPath) {
            $chocoRoot = $env:ChocolateyInstall
            if (-not $chocoRoot) {
                $chocoRoot = "C:\\ProgramData\\chocolatey"
            }
            $candidate = Join-Path $chocoRoot "bin\\mvn.cmd"
            if (Test-Path $candidate) {
                $mvnPath = $candidate
            }
        }

        if ($mvnPath) {
            Start-Process -FilePath $mvnPath -WorkingDirectory $backendDir -ArgumentList "spring-boot:run" | Out-Null
            Write-Host "Started backend (mvn spring-boot:run)"
        } elseif (Test-Path (Join-Path $root "docker-compose.yml")) {
            $composePath = Join-Path $root "docker-compose.yml"
            if (Get-Command docker -ErrorAction SilentlyContinue) {
                & docker compose -f $composePath up -d backend | Out-Null
                Write-Host "Started backend (docker compose up -d backend)"
            } elseif (Get-Command docker-compose -ErrorAction SilentlyContinue) {
                & docker-compose -f $composePath up -d backend | Out-Null
                Write-Host "Started backend (docker-compose up -d backend)"
            } else {
                Write-Warning "Maven not found and Docker is unavailable. Start backend manually."
            }
        } else {
            Write-Warning "Maven not found. Start backend manually or use docker compose."
        }
    }
}

if (-not $NoInference) {
    $args = @(
        "-m",
        "ml.inference_server",
        "--backend-url",
        $BackendUrl,
        "--session-id",
        $SessionId,
        "--zmq-endpoint",
        $ZmqEndpoint
    )
    if ($SessionGoal -ne "") {
        $args += @("--session-goal", $SessionGoal)
    }
    Start-Process -FilePath python -WorkingDirectory $root -ArgumentList $args | Out-Null
    Write-Host "Started inference bridge"
}

if ($EnginePath -ne "") {
    if (Test-Path $EnginePath) {
        Start-Process -FilePath $EnginePath -WorkingDirectory $root | Out-Null
        Write-Host "Started event engine: $EnginePath"
    } else {
        Write-Warning "Engine path not found: $EnginePath"
    }
} elseif ($LogPath -eq "" -and (Test-Path $defaultEngine)) {
    Start-Process -FilePath $defaultEngine -WorkingDirectory $root | Out-Null
    Write-Host "Started event engine: $defaultEngine"
}

if ($LogPath -ne "") {
    if (Test-Path $LogPath) {
        $args = @(
            "-m",
            "ml.event_replay",
            "--log-path",
            $LogPath,
            "--endpoint",
            $ZmqEndpoint,
            "--startup-delay-ms",
            $ReplayStartupDelayMs
        )
        if ($ReplaySleepMs -gt 0) {
            $args += @("--sleep-ms", $ReplaySleepMs)
        }
        Start-Process -FilePath python -WorkingDirectory $root -ArgumentList $args | Out-Null
        Write-Host "Started event replay: $LogPath"
    } else {
        Write-Warning "Log path not found: $LogPath"
    }
}

if (-not $NoFrontend) {
    if (Test-Path (Join-Path $root "frontend/package.json")) {
        Start-Process -FilePath npm -WorkingDirectory (Join-Path $root "frontend") -ArgumentList "run dev" | Out-Null
        Write-Host "Started frontend (npm run dev)"
    } else {
        Write-Warning "Frontend package.json missing; start frontend manually."
    }
}

Write-Host "Done. Use -NoFrontend/-NoBackend/-NoInference to skip components."
