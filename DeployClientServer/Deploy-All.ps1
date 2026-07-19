[CmdletBinding()]
param(
    [ValidateSet('All', 'Validate', 'Build', 'Vps', 'ServerOnly', 'Steam')]
    [string]$Module = 'All',
    [switch]$SkipBuild,
    [switch]$SkipSteam,
    [switch]$SkipVps,
    [switch]$FullCook,
    [switch]$PlanOnly,
    [switch]$SkipGatewayTests,
    [switch]$SkipUnrealTests,
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

function Invoke-Readiness {
    param(
        [switch]$PostBuild,
        [ValidateSet('Both', 'Client', 'Server')]
        [string]$ArtifactScope = 'Both',
        [switch]$RuntimeChecksAlreadyPassed
    )
    & (Join-Path $scriptRoot 'Test-ReleaseReadiness.ps1') `
        -PostBuild:$PostBuild `
        -ArtifactScope $ArtifactScope `
        -SkipGatewayTests:($SkipGatewayTests -or $RuntimeChecksAlreadyPassed) `
        -SkipUnrealTests:($SkipUnrealTests -or $RuntimeChecksAlreadyPassed) `
        -PlanOnly:$PlanOnly
}

function Invoke-Build {
    & (Join-Path $scriptRoot 'Build-ClientServer.ps1') -Target Both -FullCook:$FullCook -SkipReadiness
}

function Invoke-Vps {
    & (Join-Path $scriptRoot 'Push-Coordinated-Vps.ps1') `
        -BundleMode $BundleMode `
        -PlanOnly:$PlanOnly `
        -SkipReadiness `
        -SkipGatewayTests
}

function Invoke-ServerOnly {
    & (Join-Path $scriptRoot 'Push-Server-Vps.ps1') -BundleMode $BundleMode -PlanOnly:$PlanOnly -SkipReadiness
}

function Invoke-Steam {
    if ($PlanOnly) {
        Write-Host '[PLAN UNIQUEMENT] Publication client sur la branche Steam beta en dernière étape.'
        return
    }
    $arguments = @{}
    if ($SteamCmdPath) { $arguments.SteamCmdPath = $SteamCmdPath }
    if ($SteamCredentialsPath) { $arguments.SteamCredentialsPath = $SteamCredentialsPath }
    $arguments.SkipReadiness = $true
    & (Join-Path $scriptRoot 'Push-Client-Steam.ps1') @arguments
}

Invoke-VersionGuard

switch ($Module) {
    'Validate' {
        Invoke-Readiness
        Write-Host '[OK] Gate complète validée; aucun build ni déploiement exécuté.'
    }
    'Build' {
        Invoke-Readiness
        if ($PlanOnly) { Write-Host "[PLAN UNIQUEMENT] Build client + serveur; FullCook=$FullCook" }
        else {
            Invoke-Build
            Invoke-Readiness -PostBuild -RuntimeChecksAlreadyPassed
        }
    }
    'Vps' {
        Invoke-Readiness -PostBuild
        Invoke-Vps
    }
    'ServerOnly' {
        Invoke-Readiness -PostBuild -ArtifactScope Server
        Invoke-ServerOnly
    }
    'Steam' {
        Invoke-Readiness -PostBuild -ArtifactScope Client
        Invoke-Steam
    }
    'All' {
        Write-Host '[pipeline] Ordre sécurisé: build -> VPS coordonné -> Steam beta.'
        Invoke-Readiness
        if (-not $SkipBuild) {
            if ($PlanOnly) { Write-Host "[PLAN UNIQUEMENT] Build client + serveur; FullCook=$FullCook" }
            else {
                Invoke-Build
                Invoke-Readiness -PostBuild -RuntimeChecksAlreadyPassed
            }
        } else {
            Invoke-Readiness -PostBuild -RuntimeChecksAlreadyPassed
        }
        if (-not $SkipVps) { Invoke-Vps }
        if (-not $SkipSteam) { Invoke-Steam }
        Write-Host '[OK] Pipeline PropHunt terminé dans l ordre sécurisé.'
    }
}
