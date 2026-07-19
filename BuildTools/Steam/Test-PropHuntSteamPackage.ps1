[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Get-Sha256Hex {
    param([Parameter(Mandatory = $true)][string]$LiteralPath)
    $stream = [System.IO.File]::OpenRead($LiteralPath)
    try {
        $sha256 = [System.Security.Cryptography.SHA256]::Create()
        try {
            return ([System.BitConverter]::ToString($sha256.ComputeHash($stream))).Replace('-', '').ToLowerInvariant()
        }
        finally {
            $sha256.Dispose()
        }
    }
    finally {
        $stream.Dispose()
    }
}

$scriptDirectory = Split-Path -Parent $PSCommandPath
$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $scriptDirectory '..\..'))
$appBuildPath = Join-Path $scriptDirectory 'app_build_1551300.vdf'
$depotBuildPath = Join-Path $scriptDirectory 'depot_build_1551301.vdf'

foreach ($requiredFile in @($appBuildPath, $depotBuildPath, (Join-Path $projectRoot 'PropHunt.uproject'))) {
    if (-not (Test-Path -LiteralPath $requiredFile -PathType Leaf)) {
        throw "Fichier obligatoire absent : $requiredFile"
    }
}

$appBuild = [System.IO.File]::ReadAllText($appBuildPath)
$depotBuild = [System.IO.File]::ReadAllText($depotBuildPath)
$appContentRootMatch = [regex]::Match($appBuild, '"contentroot"\s+"([^"]+)"', 'IgnoreCase')
$depotContentRootMatch = [regex]::Match($depotBuild, '"ContentRoot"\s+"([^"]+)"', 'IgnoreCase')
if (-not $appContentRootMatch.Success -or -not $depotContentRootMatch.Success) {
    throw 'ContentRoot introuvable dans les manifests Steam.'
}

$appContentRoot = [System.IO.Path]::GetFullPath($appContentRootMatch.Groups[1].Value.Replace('/', '\'))
$depotContentRoot = [System.IO.Path]::GetFullPath($depotContentRootMatch.Groups[1].Value.Replace('/', '\'))
if ($appContentRoot -ne $depotContentRoot) {
    throw "Les ContentRoot app/depot diffèrent : $appContentRoot / $depotContentRoot"
}

$projectPrefix = $projectRoot.TrimEnd('\', '/') + [System.IO.Path]::DirectorySeparatorChar
if (-not $appContentRoot.StartsWith($projectPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Le ContentRoot Steam doit rester sous le dépôt PropHunt : $appContentRoot"
}
if (-not (Test-Path -LiteralPath $appContentRoot -PathType Container)) {
    throw "Package Steam absent : $appContentRoot"
}

$requiredPackageFiles = @(
    (Join-Path $appContentRoot 'PropHunt.exe'),
    (Join-Path $appContentRoot 'Manifest_UFSFiles_Win64.txt'),
    (Join-Path $appContentRoot 'PropHunt\Content\Paks\PropHunt-Windows.utoc')
)
foreach ($requiredPackageFile in $requiredPackageFiles) {
    if (-not (Test-Path -LiteralPath $requiredPackageFile -PathType Leaf)) {
        throw "Package Steam incomplet : $requiredPackageFile"
    }
}

$ufsManifestPath = Join-Path $appContentRoot 'Manifest_UFSFiles_Win64.txt'
$waitingRoomEntry = Select-String -LiteralPath $ufsManifestPath -SimpleMatch 'PropHunt/Content/PropHunt/Maps/L_PH_WaitingRoom.umap'
if ($null -eq $waitingRoomEntry) {
    throw 'Le package Steam ne contient pas L_PH_WaitingRoom.'
}

if ($appBuild -notmatch '"setlive"\s+"beta"') {
    throw 'Le build Steam ne cible pas exclusivement la branche beta.'
}
if ($appBuild -notmatch 'P9\.1' -or $appBuild -notmatch '0\.1\.1907002' -or $appBuild -notmatch 'NOVA' -or $appBuild -notmatch 'WaitingRoom') {
	throw 'La description Steam doit identifier P9.1, le build 0.1.1907002, NOVA et WaitingRoom.'
}
$hasPdbExclusion = $depotBuild -match '"FileExclusion"\s+"\*\.pdb"'
$hasSteamAppIdExclusion = $depotBuild -match '"FileExclusion"\s+"steam_appid\.txt"'
if (-not $hasPdbExclusion -or -not $hasSteamAppIdExclusion) {
    throw 'Les exclusions PDB/steam_appid.txt du depot ne sont pas toutes présentes.'
}

$embeddedSteamAppIds = @(Get-ChildItem -LiteralPath $appContentRoot -Recurse -Force -File -Filter 'steam_appid.txt')
if ($embeddedSteamAppIds.Count -ne 0) {
    throw "Le package contient encore steam_appid.txt : $($embeddedSteamAppIds[0].FullName)"
}

$exeHash = Get-Sha256Hex -LiteralPath (Join-Path $appContentRoot 'PropHunt.exe')
$utocHash = Get-Sha256Hex -LiteralPath (Join-Path $appContentRoot 'PropHunt\Content\Paks\PropHunt-Windows.utoc')

Write-Host '[OK] Package Steam beta P9.1 / 0.1.1907002 NOVA WaitingRoom cohérent.'
Write-Host "content_root=$appContentRoot"
Write-Host "exe_sha256=$exeHash"
Write-Host "utoc_sha256=$utocHash"
