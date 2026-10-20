$ErrorActionPreference = "SilentlyContinue"

$hostName = if ($env:HOLOS_HOST) { $env:HOLOS_HOST } else { "127.0.0.1" }
$port = if ($env:HOLOS_PORT) { $env:HOLOS_PORT } else { "8004" }
$url = "http://${hostName}:${port}/"

for ($i = 0; $i -lt 120; $i++) {
    try {
        Invoke-WebRequest -UseBasicParsing -TimeoutSec 2 -Uri "${url}healthz" | Out-Null
        break
    } catch {
        Start-Sleep -Seconds 1
    }
}

Start-Process $url
