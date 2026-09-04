param(
    [string]$OutputDir = "$PSScriptRoot\..\assets\stories\fox_forest\fox_forest\audio"
)

Add-Type -AssemblyName System.Speech

New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null

$format = [System.Speech.AudioFormat.SpeechAudioFormatInfo]::new(
    16000,
    [System.Speech.AudioFormat.AudioBitsPerSample]::Sixteen,
    [System.Speech.AudioFormat.AudioChannel]::Mono)
$synth = New-Object System.Speech.Synthesis.SpeechSynthesizer
$synth.SelectVoice('Microsoft Huihui Desktop')
$synth.Rate = -1
$synth.Volume = 90

$manifestPath = Join-Path $PSScriptRoot '..\assets\stories\fox_forest\fox_forest\story.json'
$manifestText = [System.IO.File]::ReadAllText($manifestPath, [System.Text.Encoding]::UTF8)
$manifest = $manifestText | ConvertFrom-Json
$scenes = @($manifest.scenes | Sort-Object index)

try {
    for ($i = 0; $i -lt $scenes.Count; $i++) {
        $path = Join-Path $OutputDir ('{0:D3}.wav' -f $i)
        $synth.SetOutputToWaveFile($path, $format)
        $synth.Speak([string]$scenes[$i].narration)
        $synth.SetOutputToNull()
        Write-Host ("scene {0}: {1}" -f $i, $path)
    }
}
finally {
    $synth.Dispose()
}
