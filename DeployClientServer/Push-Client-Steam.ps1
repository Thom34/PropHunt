[CmdletBinding()]
param(
    [string]$SteamCmdPath,
    [string]$SteamCredentialsPath,
    [string]$VpsHost,
    [switch]$SkipReadiness
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$scriptRoot = Split-Path -Parent $PSCommandPath
$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $scriptRoot '..'))
$configPath = Join-Path $scriptRoot 'deploy.config.psd1'
$config = & ([ScriptBlock]::Create([System.IO.File]::ReadAllText($configPath)))
if ($config -isnot [hashtable]) { throw "Configuration de deploiement invalide: $configPath" }
if (-not $VpsHost) { $VpsHost = [string]$config.VpsHost }
if (-not $SteamCmdPath) { $SteamCmdPath = $config.SteamCmd }
if (-not $SteamCredentialsPath) { $SteamCredentialsPath = $config.SteamCredentials }
if (-not [System.IO.Path]::IsPathRooted($SteamCmdPath)) {
    $SteamCmdPath = Join-Path $projectRoot $SteamCmdPath
}
if (-not [System.IO.Path]::IsPathRooted($SteamCredentialsPath)) {
    $SteamCredentialsPath = Join-Path $projectRoot $SteamCredentialsPath
}
$appBuild = Join-Path $projectRoot $config.SteamAppBuild
foreach ($required in @($SteamCmdPath, $SteamCredentialsPath, $appBuild)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) { throw "Fichier obligatoire absent: $required" }
}

if (-not $SkipReadiness) {
    & (Join-Path $scriptRoot 'Test-ReleaseReadiness.ps1') -PostBuild -ArtifactScope Client
}

& (Join-Path $projectRoot 'BuildTools\Version\Test-PropHuntVersion.ps1')

$versionManifest = Get-Content -LiteralPath (Join-Path $projectRoot 'BuildTools\Version\PropHuntVersion.json') -Raw | ConvertFrom-Json
$healthText = & ssh $VpsHost 'curl -fsS http://127.0.0.1:8790/healthz'
if ($LASTEXITCODE -ne 0) { throw 'Publication Steam refusée: health gateway VPS inaccessible.' }
$health = $healthText | ConvertFrom-Json
$healthBuild = [string]$health.build_id
$healthProtocol = [int]$health.protocol_version
$expectedBuild = [string]$versionManifest.release_version
$expectedProtocol = [int]$versionManifest.protocol_version
if (-not $health.ok -or $healthBuild -ne $expectedBuild -or $healthProtocol -ne $expectedProtocol) {
    throw "Publication Steam refusée: VPS=$($health.build_id)/$($health.protocol_version), candidat=$($versionManifest.release_version)/$($versionManifest.protocol_version)."
}
Write-Host "[gate] VPS prêt pour le client $($versionManifest.release_version) / protocole $($versionManifest.protocol_version)."

& (Join-Path $projectRoot 'BuildTools\Steam\Test-PropHuntSteamPackage.ps1')

$settings = @{}
foreach ($line in Get-Content -LiteralPath $SteamCredentialsPath) {
    $trimmed = $line.Trim()
    if (-not $trimmed -or $trimmed.StartsWith('#') -or $trimmed.StartsWith(';') -or $trimmed.StartsWith('[')) { continue }
    $parts = $trimmed.Split('=', 2)
    if ($parts.Count -eq 2) { $settings[$parts[0].Trim()] = $parts[1].Trim().Trim('"') }
}
$username = [string]$settings['Username']
$password = [string]$settings['Password']
if (-not $username -or $username -eq 'A_REMPLIR') { throw 'Compte Steam de build absent.' }

$machinePath = [Environment]::GetEnvironmentVariable('Path', 'Machine')
$userPath = [Environment]::GetEnvironmentVariable('Path', 'User')
[Environment]::SetEnvironmentVariable('PATH', $null, 'Process')
[Environment]::SetEnvironmentVariable('Path', (($machinePath, $userPath | Where-Object { $_ }) -join ';'), 'Process')

$loginArgs = @('+login', $username)
if ($password) { $loginArgs += $password }
& $SteamCmdPath @loginArgs +run_app_build $appBuild +quit
if ($LASTEXITCODE -ne 0) { throw "Upload Steam en echec (code $LASTEXITCODE)." }

$appLog = Join-Path $projectRoot 'Saved\SteamBuildOutput\app_build_1551300.log'
$success = Select-String -LiteralPath $appLog -Pattern 'Successfully finished AppID 1551300 build \(BuildID (\d+)\)' | Select-Object -Last 1
if ($null -eq $success) { throw "BuildID absent du journal Steam: $appLog" }
Write-Host "[OK] Client PropHunt publie sur beta. BuildID=$($success.Matches[0].Groups[1].Value)"
