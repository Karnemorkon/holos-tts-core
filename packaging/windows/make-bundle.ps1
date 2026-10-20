param(
    [Parameter(Mandatory = $true)][string]$Prefix,
    [string]$OrtBin = ""
)

$ErrorActionPreference = "Stop"
$Here = Split-Path -Parent $MyInvocation.MyCommand.Path

foreach ($exe in @("holos-server.exe", "holos-cli.exe")) {
    if (-not (Test-Path (Join-Path $Prefix "bin\$exe"))) {
        throw "missing $Prefix\bin\$exe"
    }
}

Copy-Item "$Here\holos-tts.bat" $Prefix
Copy-Item "$Here\open-ui.ps1" $Prefix
Copy-Item "$Here\download-models.ps1" $Prefix
Copy-Item "$Here\README.txt" $Prefix
New-Item -ItemType Directory -Force -Path (Join-Path $Prefix "models"), (Join-Path $Prefix "speakers") | Out-Null

if ($OrtBin) {
    $dll = Get-ChildItem -Path $OrtBin -Filter "onnxruntime*.dll" -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if (-not $dll) { throw "onnxruntime dll not found in $OrtBin" }
    Copy-Item $dll.FullName (Join-Path $Prefix "bin")
}

Write-Host "bundle ready: $Prefix"
