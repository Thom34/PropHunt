[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$scriptRoot = Split-Path -Parent $PSCommandPath
$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $scriptRoot '..\..'))
$manifestPath = Join-Path $scriptRoot 'PropHuntVersion.json'
$version = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json

if ($version.release_series -ne 'P9.1' -or $version.release_version -ne '0.1.1907001') {
    throw 'Le manifeste de version courant doit identifier P9.1 / 0.1.1907001.'
}
if ($version.nova_build_id -ne $version.release_version) {
    throw 'Le build_id NOVA doit être identique à la version client/serveur.'
}
if ([int]$version.compatibility_build_id -ne 1907001 -or [int]$version.protocol_version -ne 91) {
    throw 'La projection numérique de P9.1 / 0.1.1907001 est incohérente.'
}
if ($version.nova_release_name -ne "build-prophunt-v$($version.release_version)") {
    throw 'Le nom de release NOVA ne correspond pas au build_id versionné.'
}

$checks = @(
    @{ Path = 'Config\DefaultGame.ini'; Pattern = "(?m)^ProjectVersion=$([regex]::Escape($version.release_version))$" },
    @{ Path = 'Config\DefaultGame.ini'; Pattern = "(?m)^ProtocolVersion=$($version.protocol_version)$" },
    @{ Path = 'Config\DefaultGame.ini'; Pattern = "(?m)^ReleaseVersion=$([regex]::Escape($version.release_version))$" },
    @{ Path = 'Config\DefaultGame.ini'; Pattern = "(?m)^NOVAProjectBuildId=$([regex]::Escape($version.nova_build_id))$" },
    @{ Path = 'Config\DefaultEngine.ini'; Pattern = "(?m)^BuildIdOverride=$($version.compatibility_build_id)$" },
    @{ Path = 'Source\PropHunt\Public\Online\PHSessionSubsystem.h'; Pattern = "ProtocolVersion = $($version.protocol_version);" },
    @{ Path = 'Source\PropHunt\Public\Online\PHSessionSubsystem.h'; Pattern = ('ReleaseVersion = TEXT\("{0}"\)' -f [regex]::Escape($version.release_version)) },
    @{ Path = 'Source\PropHunt\Private\Online\PHServerInstanceContract.h'; Pattern = "ProtocolVersion = $($version.protocol_version);" },
    @{ Path = 'Source\PropHunt\Private\Online\PHServerInstanceContract.h'; Pattern = ('BuildId = TEXT\("{0}"\)' -f [regex]::Escape($version.nova_build_id)) },
    @{ Path = 'SourceArt\Deploy\PropHuntGateway\config.example.json'; Pattern = ('"protocol_version"\s*:\s*{0}' -f $version.protocol_version) },
    @{ Path = 'SourceArt\Deploy\PropHuntGateway\config.example.json'; Pattern = ('"project_build_id"\s*:\s*"{0}"' -f [regex]::Escape($version.nova_build_id)) },
    @{ Path = 'DeployClientServer\deploy.config.psd1'; Pattern = "VpsReleaseName\s*=\s*'$([regex]::Escape($version.nova_release_name))'" },
    @{ Path = 'BuildTools\Steam\app_build_1551300.vdf'; Pattern = "P9\.1.*$([regex]::Escape($version.release_version))" }
)

foreach ($check in $checks) {
    $path = Join-Path $projectRoot $check.Path
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Fichier de version obligatoire absent : $path"
    }
    $content = [System.IO.File]::ReadAllText($path)
    if ($content -notmatch $check.Pattern) {
        throw "Version incohérente dans $($check.Path)"
    }
}

Write-Host "[OK] Version PropHunt cohérente : $($version.release_series) / $($version.release_version) / protocole $($version.protocol_version)."
