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
    $dlls = @(Get-ChildItem -Path $OrtBin -Filter "onnxruntime*.dll" -ErrorAction SilentlyContinue)
    if (-not $dlls) { throw "onnxruntime dll not found in $OrtBin" }
    if (-not ($dlls | Where-Object { $_.Name -eq "onnxruntime.dll" })) {
        throw "onnxruntime.dll not found in $OrtBin"
    }
    $dlls | Copy-Item -Destination (Join-Path $Prefix "bin")
}

Write-Host "bundle ready: $Prefix"
