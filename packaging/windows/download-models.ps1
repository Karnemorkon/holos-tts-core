# Download the default HolosTTS model set (~0.6 GB) from Hugging Face.
# Already present files are skipped.
$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

$Dir = Split-Path -Parent $MyInvocation.MyCommand.Path
$Out = if ($env:HOLOS_MODELS_DIR) { $env:HOLOS_MODELS_DIR } else { Join-Path $Dir "models" }
$Repo = if ($env:HOLOS_HF_REPO) { $env:HOLOS_HF_REPO } else { "Morkon/holos-native-models" }
$Base = "https://huggingface.co/$Repo/resolve/main"

$Files = @(
    @("stress/stress.trie", "stress.trie"),
    @("tts/vocab.json", "vocab.json"),
    @("tts/voices.json", "voices.json"),
    @("tts/holos_cpu_int8.onnx", "holos_cpu_int8.onnx"),
    @("tts/voice_encoder.onnx", "voice_encoder.onnx"),
    @("accentor/accentor-lite.onnx", "accentor-lite.onnx"),
    @("verbalizer/gemma-int8/model.onnx", "verbalizer/gemma-int8/model.onnx"),
    @("verbalizer/gemma-int8/tokenizer.model", "verbalizer/gemma-int8/tokenizer.model"),
    @("verbalizer/gemma-int8/config.json", "verbalizer/gemma-int8/config.json"),
    @("verbalizer/gemma-int8/GEMMA-NOTICE.txt", "verbalizer/gemma-int8/GEMMA-NOTICE.txt")
)

New-Item -ItemType Directory -Force -Path $Out | Out-Null

foreach ($f in $Files) {
    $src = $f[0]
    $dst = $f[1]
    $path = Join-Path $Out $dst

    if ((Test-Path $path) -and ((Get-Item $path).Length -gt 0)) {
        Write-Host "  skip  $dst"
        continue
    }

    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $path) | Out-Null
    Write-Host "  fetch $dst"
    Invoke-WebRequest -UseBasicParsing -Uri "$Base/$src" -OutFile "$path.part"
    Move-Item -Force "$path.part" $path
}

Write-Host ""
Write-Host "Done. Run holos-tts.bat"
