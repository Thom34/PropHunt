[CmdletBinding()]
param(
    [switch]$PostBuild,
    [ValidateSet('Both', 'Client', 'Server')]
    [string]$ArtifactScope = 'Both',
    [switch]$SkipGatewayTests,
    [switch]$SkipUnrealTests,
    [switch]$PlanOnly
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$scriptRoot = Split-Path -Parent $PSCommandPath
$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $scriptRoot '..'))
$canonicalProject = 'C:\Users\Thomas\Documents\Unreal Projects\PropHunt\PropHunt.uproject'
$canonicalEngine = 'K:\UE58'
$projectFile = Join-Path $projectRoot 'PropHunt.uproject'
$configPath = Join-Path $scriptRoot 'deploy.config.psd1'
$config = & ([ScriptBlock]::Create([System.IO.File]::ReadAllText($configPath)))
if ($config -isnot [hashtable]) { throw "Configuration de déploiement invalide: $configPath" }

function Assert-Leaf([string]$Path, [string]$Label) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { throw "$Label absent: $Path" }
}

if ([System.IO.Path]::GetFullPath($projectFile) -ne $canonicalProject) {
    throw "Projet non canonique: $projectFile"
}
if ([string]$config.EngineRoot -ne $canonicalEngine) {
    throw "Moteur non canonique dans deploy.config.psd1: $($config.EngineRoot)"
}
Assert-Leaf $projectFile 'Projet Unreal canonique'
Assert-Leaf (Join-Path $canonicalEngine 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe') 'UnrealEditor-Cmd canonique'

& (Join-Path $projectRoot 'BuildTools\Version\Test-PropHuntVersion.ps1')

$gatewayRoot = Join-Path $projectRoot 'SourceArt\Deploy\PropHuntGateway'
$serviceUnit = Join-Path $gatewayRoot 'prophunt-gateway.service'
Assert-Leaf $serviceUnit 'Unité systemd gateway'
$unitText = Get-Content -LiteralPath $serviceUnit -Raw
foreach ($required in @(
    'Requires=nova-orchestrator.service',
    '[ -S /run/nova-orchestrator/api.sock ]',
    'Restart=on-failure'
)) {
    if (-not $unitText.Contains($required)) { throw "Garde systemd absente: $required" }
}
if ($unitText.Contains('ReadOnlyPaths=/run/nova-orchestrator/api.sock')) {
    throw 'L unité systemd réintroduit la course de démarrage sur le socket NOVA.'
}

$requiredPatch = Join-Path $projectRoot 'SourceArt\Deploy\NOVA\Patches\09-allocation-idempotent-request-id.patch'
Assert-Leaf $requiredPatch 'Patch NOVA idempotent'
if (-not (Select-String -LiteralPath $requiredPatch -SimpleMatch 'request_id' -Quiet)) {
    throw 'Le patch NOVA idempotent ne contient plus request_id.'
}

foreach ($script in Get-ChildItem -LiteralPath $scriptRoot -Filter '*.ps1' -File) {
    $tokens = $null
    $errors = $null
    [System.Management.Automation.Language.Parser]::ParseFile(
        $script.FullName, [ref]$tokens, [ref]$errors) | Out-Null
    if ($errors.Count -gt 0) {
        throw "PowerShell invalide dans $($script.Name): $($errors[0].Message)"
    }
}

& git -C $projectRoot diff --check
if ($LASTEXITCODE -ne 0) { throw 'git diff --check a échoué dans le dépôt PropHunt.' }
& git -C (Join-Path $projectRoot 'SourceArt') diff --check
if ($LASTEXITCODE -ne 0) { throw 'git diff --check a échoué dans SourceArt.' }

if ($PlanOnly) {
    Write-Host "[PLAN UNIQUEMENT] Gate release: version, scripts, gateway, Unreal; PostBuild=$PostBuild"
    return
}

if (-not $SkipGatewayTests) {
    & python -m compileall -q $gatewayRoot
    if ($LASTEXITCODE -ne 0) { throw 'Compilation Python gateway en échec.' }
    $oldPythonPath = $env:PYTHONPATH
    try {
        $env:PYTHONPATH = $gatewayRoot
        & python -m unittest discover -s (Join-Path $gatewayRoot 'tests') -v
        if ($LASTEXITCODE -ne 0) { throw 'Tests gateway en échec.' }
    } finally {
        if ($null -eq $oldPythonPath) { Remove-Item Env:PYTHONPATH -ErrorAction SilentlyContinue }
        else { $env:PYTHONPATH = $oldPythonPath }
    }
}

if (-not $SkipUnrealTests) {
    $logRoot = Join-Path $projectRoot 'Saved\ReleaseReadiness'
    [System.IO.Directory]::CreateDirectory($logRoot) | Out-Null
    $automationLog = Join-Path $logRoot 'UnrealAutomation.log'
    $editorCmd = Join-Path $canonicalEngine 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
    & $editorCmd $canonicalProject -unattended -nop4 -nosplash -NullRHI `
        '-ExecCmds=Automation RunTests PropHunt.;Quit' `
        '-TestExit=Automation Test Queue Empty' `
        "-abslog=$automationLog"
    if ($LASTEXITCODE -ne 0) {
        throw "Automation Unreal PropHunt en échec (code $LASTEXITCODE): $automationLog"
    }
    if (-not (Select-String -LiteralPath $automationLog -SimpleMatch '**** TEST COMPLETE. EXIT CODE: 0 ****' -Quiet)) {
        throw "Fin réussie de la suite Unreal non prouvée dans $automationLog"
    }
    if (Select-String -LiteralPath $automationLog -Pattern 'Test Completed\. Result=\{(Fail|NotRun|Cancelled)' -Quiet) {
        throw "Au moins un test Unreal n'est pas passé: $automationLog"
    }
}

if ($PostBuild) {
    $versionManifest = Get-Content -LiteralPath (Join-Path $projectRoot 'BuildTools\Version\PropHuntVersion.json') -Raw | ConvertFrom-Json
    $currentFingerprint = & (Join-Path $scriptRoot 'Get-ReleaseInputFingerprint.ps1')
    $provenanceTargets = if ($ArtifactScope -eq 'Both') { @('Client', 'Server') } else { @($ArtifactScope) }
    foreach ($provenanceTarget in $provenanceTargets) {
        if ($provenanceTarget -eq 'Client') {
            $archiveRoot = Join-Path $projectRoot ([string]$config.ClientArchive)
        } else {
            $archiveRoot = Join-Path $projectRoot ([string]$config.ServerArchive)
        }
        $provenancePath = Join-Path $archiveRoot 'build-provenance.json'
        Assert-Leaf $provenancePath "Provenance $provenanceTarget"
        $provenance = Get-Content -LiteralPath $provenancePath -Raw | ConvertFrom-Json
        $provenanceSchema = [int]$provenance.schema
        $provenanceTargetName = [string]$provenance.target
        $provenanceRelease = [string]$provenance.release_version
        $provenanceProtocol = [int]$provenance.protocol_version
        $provenanceFingerprint = [string]$provenance.input_fingerprint
        $expectedRelease = [string]$versionManifest.release_version
        $expectedProtocol = [int]$versionManifest.protocol_version
        $provenanceMatches = $provenanceSchema -eq 1 -and $provenanceTargetName -eq $provenanceTarget -and $provenanceRelease -eq $expectedRelease -and $provenanceProtocol -eq $expectedProtocol -and $provenanceFingerprint -eq $currentFingerprint
        if (-not $provenanceMatches) {
            throw "Package $provenanceTarget obsolète ou construit depuis d'autres entrées: $provenancePath"
        }
    }
    if ($ArtifactScope -in @('Both', 'Client')) {
        & (Join-Path $projectRoot 'BuildTools\Steam\Test-PropHuntSteamPackage.ps1')
        $clientBinary = Join-Path (Join-Path $projectRoot ([string]$config.ClientContent)) 'PropHunt\Binaries\Win64\PropHunt-Win64-Shipping.exe'
        Assert-Leaf $clientBinary 'Binaire client Win64 Shipping'
    }
    if ($ArtifactScope -in @('Both', 'Server')) {
        $serverBinary = Join-Path (Join-Path $projectRoot ([string]$config.ServerContent)) 'PropHunt\Binaries\Linux\PropHuntServer-Linux-Shipping'
        Assert-Leaf $serverBinary 'Binaire serveur Linux Shipping'
    }
}

Write-Host "[OK] Gate release PropHunt validée. PostBuild=$PostBuild ArtifactScope=$ArtifactScope"
