[CmdletBinding()]
param(
    [ValidateSet('Both', 'Client', 'Server')]
    [string]$Target = 'Both',
    [switch]$FullCook,
    [switch]$SkipCompile
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$scriptRoot = Split-Path -Parent $PSCommandPath
$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $scriptRoot '..'))
$configPath = Join-Path $scriptRoot 'deploy.config.psd1'
$config = & ([ScriptBlock]::Create([System.IO.File]::ReadAllText($configPath)))
if ($config -isnot [hashtable]) { throw "Configuration de deploiement invalide: $configPath" }
$project = Join-Path $projectRoot $config.ProjectFile
$uat = Join-Path $config.EngineRoot 'Engine\Build\BatchFiles\RunUAT.bat'

& (Join-Path $projectRoot 'BuildTools\Version\Test-PropHuntVersion.ps1')

if ($project -ne 'C:\Users\Thomas\Documents\Unreal Projects\PropHunt\PropHunt.uproject') { throw "Projet non canonique refuse: $project" }
foreach ($required in @($project, $uat)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) { throw "Fichier obligatoire absent: $required" }
}

function Invoke-PropHuntUat {
    param([Parameter(Mandatory = $true)][string[]]$Arguments)
    & $uat @Arguments
    if ($LASTEXITCODE -ne 0) { throw "BuildCookRun en echec (code $LASTEXITCODE)." }
}

$common = @('BuildCookRun', "-project=$project", '-noP4', '-utf8output', '-cook', '-stage', '-pak', '-archive')
if (-not $SkipCompile) { $common += '-build' }
if (-not $FullCook) { $common += '-iterate' }

if ($Target -in @('Both', 'Client')) {
    $clientArchive = Join-Path $projectRoot $config.ClientArchive
    $clientArgs = $common + @('-platform=Win64', '-clientconfig=Shipping', '-iostore', '-compressed', '-prereqs', "-archivedirectory=$clientArchive")
    Write-Host "[build] Client Shipping -> $clientArchive"
    Invoke-PropHuntUat -Arguments $clientArgs
    $clientExe = Join-Path $projectRoot ($config.ClientContent + '\PropHunt.exe')
    if (-not (Test-Path -LiteralPath $clientExe -PathType Leaf)) { throw "Client archive incomplet: $clientExe" }
}

if ($Target -in @('Both', 'Server')) {
    $serverArchive = Join-Path $projectRoot $config.ServerArchive
    $serverArgs = $common + @('-server', '-noclient', '-serverconfig=Shipping', '-serverplatform=Linux', "-archivedirectory=$serverArchive")
    Write-Host "[build] Serveur Linux Shipping -> $serverArchive"
    Invoke-PropHuntUat -Arguments $serverArgs
    $serverBinary = Join-Path $projectRoot ($config.ServerContent + '\PropHunt\Binaries\Linux\PropHuntServer-Linux-Shipping')
    if (-not (Test-Path -LiteralPath $serverBinary -PathType Leaf)) { throw "Serveur archive incomplet: $serverBinary" }
}

Write-Host '[OK] Build client/serveur termine dans package/client et package/server.'
