[CmdletBinding()]
param(
    [ValidateSet('All', 'Validate', 'Build', 'Vps', 'ServerOnly', 'Steam')]
    [string]$Module = 'All',
    [switch]$SkipBuild,
    [switch]$SkipSteam,
    [switch]$SkipVps,
    [switch]$FullCook,
    [switch]$PlanOnly,
    [ValidateSet('Auto', 'Reuse', 'Regenerate')]
    [string]$BundleMode = 'Auto',
    [string]$SteamCmdPath,
    [string]$SteamCredentialsPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$scriptRoot = Split-Path -Parent $PSCommandPath
$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $scriptRoot '..'))

function Invoke-VersionGuard {
    & (Join-Path $projectRoot 'BuildTools\Version\Test-PropHuntVersion.ps1')
}

function Invoke-Build {
    & (Join-Path $scriptRoot 'Build-ClientServer.ps1') -Target Both -FullCook:$FullCook
}

function Invoke-Vps {
    & (Join-Path $scriptRoot 'Push-Coordinated-Vps.ps1') -BundleMode $BundleMode -PlanOnly:$PlanOnly
}

function Invoke-ServerOnly {
    & (Join-Path $scriptRoot 'Push-Server-Vps.ps1') -BundleMode $BundleMode -PlanOnly:$PlanOnly
}

function Invoke-Steam {
    if ($PlanOnly) {
        Write-Host '[PLAN UNIQUEMENT] Publication client sur la branche Steam beta en dernière étape.'
        return
    }
    $arguments = @{}
    if ($SteamCmdPath) { $arguments.SteamCmdPath = $SteamCmdPath }
    if ($SteamCredentialsPath) { $arguments.SteamCredentialsPath = $SteamCredentialsPath }
    & (Join-Path $scriptRoot 'Push-Client-Steam.ps1') @arguments
}

Invoke-VersionGuard

switch ($Module) {
    'Validate' {
        Write-Host '[OK] Garde de version validée; aucun build ni déploiement exécuté.'
    }
    'Build' {
        if ($PlanOnly) { Write-Host "[PLAN UNIQUEMENT] Build client + serveur; FullCook=$FullCook" }
        else { Invoke-Build }
    }
    'Vps' {
        Invoke-Vps
    }
    'ServerOnly' {
        Invoke-ServerOnly
    }
    'Steam' {
        Invoke-Steam
    }
    'All' {
        Write-Host '[pipeline] Ordre sécurisé: build -> VPS coordonné -> Steam beta.'
        if (-not $SkipBuild) {
            if ($PlanOnly) { Write-Host "[PLAN UNIQUEMENT] Build client + serveur; FullCook=$FullCook" }
            else { Invoke-Build }
        }
        if (-not $SkipVps) { Invoke-Vps }
        if (-not $SkipSteam) { Invoke-Steam }
        Write-Host '[OK] Pipeline PropHunt terminé dans l ordre sécurisé.'
    }
}
