[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$scriptRoot = Split-Path -Parent $PSCommandPath
$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $scriptRoot '..'))
$inputs = @(
    (Join-Path $projectRoot 'Source'),
    (Join-Path $projectRoot 'Config'),
    (Join-Path $projectRoot 'Content'),
    (Join-Path $projectRoot 'Plugins'),
    (Join-Path $projectRoot 'BuildTools\Version'),
    (Join-Path $projectRoot 'SourceArt\Deploy\PropHuntGateway'),
    (Join-Path $projectRoot 'SourceArt\Deploy\NOVA\Patches'),
    (Join-Path $projectRoot 'PropHunt.uproject')
)

$files = foreach ($inputPath in $inputs) {
    if (Test-Path -LiteralPath $inputPath -PathType Leaf) {
        Get-Item -LiteralPath $inputPath
    } elseif (Test-Path -LiteralPath $inputPath -PathType Container) {
        Get-ChildItem -LiteralPath $inputPath -Recurse -File | Where-Object {
            $_.FullName -notmatch '[\\/](Binaries|Intermediate|Saved|DerivedDataCache)[\\/]'
        }
    } else {
        throw "Entrée de release absente: $inputPath"
    }
}

$builder = [System.Text.StringBuilder]::new()
foreach ($file in @($files | Sort-Object FullName)) {
    $relative = $file.FullName.Substring($projectRoot.Length).TrimStart('\', '/').Replace('\', '/').ToLowerInvariant()
    [void]$builder.Append($relative)
    [void]$builder.Append('|')
    [void]$builder.Append($file.Length)
    [void]$builder.Append('|')
    [void]$builder.Append($file.LastWriteTimeUtc.Ticks)
    [void]$builder.Append("`n")
}

$sha = [System.Security.Cryptography.SHA256]::Create()
try {
    $bytes = [System.Text.Encoding]::UTF8.GetBytes($builder.ToString())
    $hash = ([BitConverter]::ToString($sha.ComputeHash($bytes)) -replace '-', '').ToLowerInvariant()
} finally {
    $sha.Dispose()
}
Write-Output $hash
