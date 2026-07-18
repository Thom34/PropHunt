[CmdletBinding()]
param(
    [string]$PackageRoot,
    [string]$OutputDirectory,
    [switch]$IncludeDebugFiles
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$scriptDirectory = Split-Path -Parent $PSCommandPath
$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $scriptDirectory '..\..'))
$canonicalProject = Join-Path $projectRoot 'PropHunt.uproject'

if (-not (Test-Path -LiteralPath $canonicalProject -PathType Leaf)) {
    throw "Projet canonique introuvable : $canonicalProject"
}

if ([string]::IsNullOrWhiteSpace($PackageRoot)) {
    $PackageRoot = Join-Path $projectRoot 'package\goal-final-20260717\server-linux\LinuxServer'
}

if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path $projectRoot 'package\goal-final-20260717\vps-bundle'
}

$PackageRoot = [System.IO.Path]::GetFullPath($PackageRoot)
$OutputDirectory = [System.IO.Path]::GetFullPath($OutputDirectory)
$opsRoot = [System.IO.Path]::GetFullPath((Join-Path $scriptDirectory 'Linux'))
$projectPrefix = $projectRoot.TrimEnd('\', '/') + [System.IO.Path]::DirectorySeparatorChar

foreach ($pathCheck in @(
    @{ Label = 'Package Linux'; Path = $PackageRoot },
    @{ Label = 'Sortie bundle'; Path = $OutputDirectory },
    @{ Label = 'Kit exploitation'; Path = $opsRoot }
)) {
    if (-not $pathCheck.Path.StartsWith($projectPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "$($pathCheck.Label) doit rester dans le dépôt PropHunt : $($pathCheck.Path)"
    }
}

$requiredSourceFiles = @(
    (Join-Path $PackageRoot 'PropHuntServer.sh'),
    (Join-Path $PackageRoot 'PropHunt\Binaries\Linux\PropHuntServer'),
    (Join-Path $PackageRoot 'Engine\Binaries\ThirdParty\Steamworks\Steamv164\x86_64-unknown-linux-gnu\libsteam_api.so'),
    (Join-Path $opsRoot 'run-prophunt-server.sh'),
    (Join-Path $opsRoot 'preflight-prophunt-server.sh'),
    (Join-Path $opsRoot 'test-vps-bundle-local.sh'),
    (Join-Path $opsRoot 'prophunt-server.service'),
    (Join-Path $opsRoot 'prophunt-server.env.example'),
    (Join-Path $opsRoot 'README.md')
)

foreach ($requiredFile in $requiredSourceFiles) {
    if (-not (Test-Path -LiteralPath $requiredFile -PathType Leaf)) {
        throw "Fichier obligatoire absent : $requiredFile"
    }
}

$tarCommand = Get-Command 'tar.exe' -ErrorAction Stop
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null

$serverArchiveName = if ($IncludeDebugFiles) {
    'PropHuntServer-Linux-Protocol6-WithDebug.tar.gz'
}
else {
    'PropHuntServer-Linux-Protocol6-Runtime.tar.gz'
}
$serverArchive = Join-Path $OutputDirectory $serverArchiveName
$opsArchive = Join-Path $OutputDirectory 'PropHuntServer-Ops-Protocol6.tar.gz'
$manifestPath = Join-Path $OutputDirectory 'PropHuntServer-Protocol6-manifest.json'
$checksumPath = Join-Path $OutputDirectory 'SHA256SUMS'

foreach ($outputFile in @($serverArchive, $opsArchive, $manifestPath, $checksumPath)) {
    if (Test-Path -LiteralPath $outputFile) {
        throw "Refus d'écraser un bundle existant : $outputFile"
    }
}

function Invoke-TarArchive {
    param(
        [Parameter(Mandatory = $true)][string]$SourceRoot,
        [Parameter(Mandatory = $true)][string]$ArchivePath,
        [string[]]$ExcludePatterns = @()
    )

    $tarArguments = @('-czf', $ArchivePath)
    foreach ($excludePattern in $ExcludePatterns) {
        $tarArguments += "--exclude=$excludePattern"
    }
    $tarArguments += @('-C', $SourceRoot, '.')

    & $tarCommand.Source @tarArguments
    if ($LASTEXITCODE -ne 0) {
        throw "tar.exe a échoué pour $ArchivePath avec le code $LASTEXITCODE"
    }
}

function Get-TarEntries {
    param([Parameter(Mandatory = $true)][string]$ArchivePath)

    $entries = @(& $tarCommand.Source -tzf $ArchivePath)
    if ($LASTEXITCODE -ne 0) {
        throw "Lecture impossible de l'archive $ArchivePath"
    }
    return $entries
}

function Assert-ArchiveEntry {
    param(
        [Parameter(Mandatory = $true)][string[]]$Entries,
        [Parameter(Mandatory = $true)][string]$ExpectedEntry,
        [Parameter(Mandatory = $true)][string]$ArchiveLabel
    )

    $normalizedExpected = $ExpectedEntry.Replace('\', '/').TrimStart('.', '/')
    $found = $false
    foreach ($entry in $Entries) {
        if ($entry.Replace('\', '/').TrimStart('.', '/') -eq $normalizedExpected) {
            $found = $true
            break
        }
    }

    if (-not $found) {
        throw "$ArchiveLabel ne contient pas $ExpectedEntry"
    }
}

$serverExcludes = @()
if (-not $IncludeDebugFiles) {
    $serverExcludes = @(
        './PropHunt/Binaries/Linux/PropHuntServer.debug',
        './PropHunt/Binaries/Linux/PropHuntServer.sym',
        './Manifest_DebugFiles_Linux.txt'
    )
}

Invoke-TarArchive -SourceRoot $PackageRoot -ArchivePath $serverArchive -ExcludePatterns $serverExcludes
Invoke-TarArchive -SourceRoot $opsRoot -ArchivePath $opsArchive

$serverEntries = Get-TarEntries -ArchivePath $serverArchive
$opsEntries = Get-TarEntries -ArchivePath $opsArchive

Assert-ArchiveEntry -Entries $serverEntries -ExpectedEntry 'PropHuntServer.sh' -ArchiveLabel 'Archive serveur'
Assert-ArchiveEntry -Entries $serverEntries -ExpectedEntry 'PropHunt/Binaries/Linux/PropHuntServer' -ArchiveLabel 'Archive serveur'
Assert-ArchiveEntry -Entries $serverEntries -ExpectedEntry 'Engine/Binaries/ThirdParty/Steamworks/Steamv164/x86_64-unknown-linux-gnu/libsteam_api.so' -ArchiveLabel 'Archive serveur'
Assert-ArchiveEntry -Entries $opsEntries -ExpectedEntry 'run-prophunt-server.sh' -ArchiveLabel 'Archive ops'
Assert-ArchiveEntry -Entries $opsEntries -ExpectedEntry 'preflight-prophunt-server.sh' -ArchiveLabel 'Archive ops'
Assert-ArchiveEntry -Entries $opsEntries -ExpectedEntry 'test-vps-bundle-local.sh' -ArchiveLabel 'Archive ops'
Assert-ArchiveEntry -Entries $opsEntries -ExpectedEntry 'prophunt-server.service' -ArchiveLabel 'Archive ops'

$serverHash = Get-FileHash -LiteralPath $serverArchive -Algorithm SHA256
$opsHash = Get-FileHash -LiteralPath $opsArchive -Algorithm SHA256
$serverInfo = Get-Item -LiteralPath $serverArchive
$opsInfo = Get-Item -LiteralPath $opsArchive

$manifest = [ordered]@{
    project = 'PropHunt'
    canonicalProject = $canonicalProject
    protocolVersion = 6
    includesDebugFiles = [bool]$IncludeDebugFiles
    requiresLinuxPermissionNormalization = $true
    generatedAtUtc = [DateTime]::UtcNow.ToString('o')
    server = [ordered]@{
        archive = $serverInfo.Name
        bytes = $serverInfo.Length
        sha256 = $serverHash.Hash.ToLowerInvariant()
        extractTo = '/opt/prophunt/server'
    }
    operations = [ordered]@{
        archive = $opsInfo.Name
        bytes = $opsInfo.Length
        sha256 = $opsHash.Hash.ToLowerInvariant()
        extractTo = '/opt/prophunt/ops'
    }
    requiredRuntime = '/opt/steamcmd/linux64/steamclient.so'
    preflight = '/opt/prophunt/ops/preflight-prophunt-server.sh --env /etc/prophunt/prophunt-server.env'
}

$utf8NoBom = [System.Text.UTF8Encoding]::new($false)
$manifestJson = $manifest | ConvertTo-Json -Depth 6
[System.IO.File]::WriteAllText($manifestPath, $manifestJson + [Environment]::NewLine, $utf8NoBom)

$checksumLines = @(
    "$($serverHash.Hash.ToLowerInvariant())  $($serverInfo.Name)",
    "$($opsHash.Hash.ToLowerInvariant())  $($opsInfo.Name)"
)
[System.IO.File]::WriteAllLines($checksumPath, $checksumLines, $utf8NoBom)

Write-Host "Bundle VPS protocole 6 créé sans écrasement :"
Write-Host "  Serveur : $serverArchive"
Write-Host "  Ops     : $opsArchive"
Write-Host "  Manifeste : $manifestPath"
Write-Host "  SHA-256 : $checksumPath"
