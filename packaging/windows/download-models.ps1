# Download the default HolosTTS model set (multi-stage holos-onnx bundle,
# ~0.8 GB) from Hugging Face.
# Already present files are skipped.
$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

$Dir = Split-Path -Parent $MyInvocation.MyCommand.Path
$Out = if ($env:HOLOS_MODELS_DIR) { $env:HOLOS_MODELS_DIR } else { Join-Path $Dir "models" }
$Repo = if ($env:HOLOS_HF_REPO) { $env:HOLOS_HF_REPO } else { "Morkon/holos-native-models" }
$Base = "https://huggingface.co/$Repo/resolve/main"

$Files = @(
    @("holos-onnx-int8/holos_onnx.json", "holos-onnx/holos_onnx.json"),
    @("holos-onnx-int8/encoder.onnx", "holos-onnx/encoder.onnx"),
    @("holos-onnx-int8/prosody.onnx", "holos-onnx/prosody.onnx"),
    @("holos-onnx-int8/decoder.onnx", "holos-onnx/decoder.onnx"),
    @("holos-onnx-int8/decoder_window.onnx", "holos-onnx/decoder_window.onnx"),
    @("holos-onnx-int8/source.onnx", "holos-onnx/source.onnx"),
    @("holos-onnx-int8/voices.npz", "holos-onnx/voices.npz"),
    @("stress/stress.trie", "stress.trie"),
    @("accentor/accentor-lite.onnx", "accentor-lite.onnx"),
    @("tts/voice_encoder.onnx", "voice_encoder.onnx"),
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
