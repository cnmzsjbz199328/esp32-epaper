[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string] $StoryDir,

    [Parameter(Mandatory = $true)]
    [string] $OutputDir
)

$ErrorActionPreference = "Stop"

$storyPath = [System.IO.Path]::GetFullPath($StoryDir)
$outputPath = [System.IO.Path]::GetFullPath($OutputDir)
$storyId = Split-Path -Leaf $storyPath
$storyRoot = Join-Path $storyPath $storyId
$metadataPath = Join-Path $storyRoot "story.json"
$fvidPath = Join-Path $storyPath "$storyId.fvid"

if (-not (Test-Path -LiteralPath $metadataPath)) {
    throw "Missing story metadata: $metadataPath"
}
if (-not (Test-Path -LiteralPath $fvidPath)) {
    throw "Missing FVID: $fvidPath"
}
if (Test-Path -LiteralPath $outputPath) {
    throw "Output directory already exists; choose an empty/new staging path: $outputPath"
}

$metadata = Get-Content -LiteralPath $metadataPath -Raw -Encoding UTF8 | ConvertFrom-Json
if ($metadata.audio_status -eq "pending") {
    throw "Story $storyId has audio_status=pending and cannot be staged as a formal SD package."
}

$checker = Join-Path (Get-Location) "tools/check_story_assets.py"
python $checker $storyPath
if ($LASTEXITCODE -ne 0) {
    throw "Asset checker failed for $storyId; package was not staged."
}

$videoPath = Join-Path $outputPath "video"
New-Item -ItemType Directory -Path $videoPath -Force | Out-Null
Copy-Item -LiteralPath $fvidPath -Destination $videoPath
Copy-Item -LiteralPath $storyRoot -Destination $videoPath -Recurse

Write-Output "STAGED story=$storyId destination=$videoPath"
