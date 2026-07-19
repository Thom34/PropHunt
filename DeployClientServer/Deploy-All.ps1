[CmdletBinding()]
param([switch]$SkipBuild, [switch]$SkipSteam, [switch]$SkipVps, [switch]$FullCook)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$scriptRoot = Split-Path -Parent $PSCommandPath

if (-not $SkipBuild) { & (Join-Path $scriptRoot 'Build-ClientServer.ps1') -Target Both -FullCook:$FullCook }
if (-not $SkipSteam) { & (Join-Path $scriptRoot 'Push-Client-Steam.ps1') }
if (-not $SkipVps) { & (Join-Path $scriptRoot 'Push-Server-Vps.ps1') }
Write-Host '[OK] Pipeline PropHunt client + serveur termine.'
