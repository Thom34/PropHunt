[CmdletBinding()]
param(
    [string]$VpsHost,
    [string]$ReleaseName,
    [ValidateSet('Auto', 'Reuse', 'Regenerate')]
    [string]$BundleMode = 'Auto',
    [switch]$SkipReadiness,
    [switch]$PlanOnly
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$scriptRoot = Split-Path -Parent $PSCommandPath
$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $scriptRoot '..'))
$configPath = Join-Path $scriptRoot 'deploy.config.psd1'
$config = & ([ScriptBlock]::Create([System.IO.File]::ReadAllText($configPath)))
if ($config -isnot [hashtable]) { throw "Configuration de deploiement invalide: $configPath" }
if (-not $VpsHost) { $VpsHost = $config.VpsHost }
if (-not $ReleaseName) { $ReleaseName = $config.VpsReleaseName }
if (-not $SkipReadiness) {
    & (Join-Path $scriptRoot 'Test-ReleaseReadiness.ps1') -PostBuild -ArtifactScope Server -PlanOnly:$PlanOnly
}
& (Join-Path $projectRoot 'BuildTools\Version\Test-PropHuntVersion.ps1')
$versionManifest = Get-Content -LiteralPath (Join-Path $projectRoot 'BuildTools\Version\PropHuntVersion.json') -Raw | ConvertFrom-Json
if ($ReleaseName -ne [string]$versionManifest.nova_release_name) { throw "Nom de release VPS incohérent: $ReleaseName" }
$protocol = [int]$versionManifest.protocol_version
$buildVersion = [string]$versionManifest.release_version

$serverRoot = Join-Path $projectRoot $config.ServerContent
$bundleRoot = Join-Path $projectRoot $config.VpsBundle
$bundleFiles = @(
    (Join-Path $bundleRoot "PropHuntServer-Linux-$buildVersion-Runtime.tar.gz"),
    (Join-Path $bundleRoot "PropHuntServer-Ops-$buildVersion.tar.gz"),
    (Join-Path $bundleRoot "PropHuntServer-$buildVersion-manifest.json"),
    (Join-Path $bundleRoot 'SHA256SUMS')
)

$bundleComplete = @($bundleFiles | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf }).Count -eq $bundleFiles.Count
$mustGenerate = $BundleMode -eq 'Regenerate' -or -not $bundleComplete
if ($BundleMode -eq 'Auto' -and $bundleComplete) {
    $bundleManifestPath = Join-Path $bundleRoot "PropHuntServer-$buildVersion-manifest.json"
    $bundleManifest = Get-Content -LiteralPath $bundleManifestPath -Raw | ConvertFrom-Json
    if (
        [string]$bundleManifest.buildVersion -ne $buildVersion -or
        [int]$bundleManifest.protocolVersion -ne $protocol -or
        [string]$bundleManifest.releaseName -ne $ReleaseName
    ) {
        $mustGenerate = $true
    } else {
        $generatedAtText = [string]$bundleManifest.generatedAtUtc
        $generatedAtValue = [DateTimeOffset]::MinValue
        $dateStyles = [Globalization.DateTimeStyles]::AssumeUniversal -bor [Globalization.DateTimeStyles]::AdjustToUniversal
        if (-not [DateTimeOffset]::TryParse(
            $generatedAtText,
            [Globalization.CultureInfo]::InvariantCulture,
            $dateStyles,
            [ref]$generatedAtValue
        )) {
            throw "Date generatedAtUtc invalide dans le manifeste VPS: $generatedAtText"
        }
        $generatedAt = $generatedAtValue.UtcDateTime
        $newerInput = Get-ChildItem -LiteralPath $serverRoot, (Join-Path $projectRoot 'BuildTools\Server\Linux') -Recurse -File |
            Where-Object { $_.LastWriteTimeUtc -gt $generatedAt } |
            Select-Object -First 1
        if ($null -ne $newerInput) {
            Write-Host "[bundle] Entrée plus récente détectée: $($newerInput.FullName)"
            $mustGenerate = $true
        }
    }
}
if ($BundleMode -eq 'Reuse' -and -not $bundleComplete) {
    throw "Bundle VPS réutilisable incomplet: $bundleRoot"
}
if ($PlanOnly -and $mustGenerate) {
    Write-Host "[PLAN UNIQUEMENT] Le bundle serait archivé/régénéré avec BundleMode=$BundleMode; aucun fichier ni hôte modifié."
    return
}

if ($mustGenerate -and (Test-Path -LiteralPath $bundleRoot)) {
    $backupRoot = Join-Path $projectRoot 'Saved\Deployments\bundle-backups'
    [System.IO.Directory]::CreateDirectory($backupRoot) | Out-Null
    $backupPath = Join-Path $backupRoot ((Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + (Split-Path -Leaf $bundleRoot))
    if (Test-Path -LiteralPath $backupPath) { throw "Sauvegarde bundle déjà présente: $backupPath" }
    Move-Item -LiteralPath $bundleRoot -Destination $backupPath
    Write-Host "[bundle] Ancien bundle archivé -> $backupPath"
}

if ($mustGenerate) {
    & (Join-Path $projectRoot 'BuildTools\Server\New-PropHuntVpsBundle.ps1') `
        -PackageRoot $serverRoot `
        -OutputDirectory $bundleRoot `
        -ProtocolVersion $protocol `
        -BuildVersion $buildVersion `
        -NovaReleaseName $ReleaseName
} else {
    Write-Host "[bundle] Bundle $buildVersion existant réutilisé -> $bundleRoot"
}

foreach ($file in $bundleFiles) {
    if (-not (Test-Path -LiteralPath $file -PathType Leaf)) { throw "Bundle VPS incomplet: $file" }
}

$checksumPath = Join-Path $bundleRoot 'SHA256SUMS'
foreach ($line in Get-Content -LiteralPath $checksumPath) {
    if ($line -notmatch '^([0-9a-fA-F]{64})\s+(.+)$') { throw "Ligne SHA256SUMS invalide: $line" }
    $expectedHash = $Matches[1].ToLowerInvariant()
    $hashedFile = Join-Path $bundleRoot $Matches[2]
    if (-not (Test-Path -LiteralPath $hashedFile -PathType Leaf)) { throw "Fichier SHA256SUMS absent: $hashedFile" }
    $stream = [System.IO.File]::OpenRead($hashedFile)
    try {
        $sha = [System.Security.Cryptography.SHA256]::Create()
        try { $actualHash = ([BitConverter]::ToString($sha.ComputeHash($stream)) -replace '-', '').ToLowerInvariant() }
        finally { $sha.Dispose() }
    } finally {
        $stream.Dispose()
    }
    if ($actualHash -ne $expectedHash) { throw "SHA-256 local invalide: $hashedFile" }
}
Write-Host '[bundle] SHA-256 locaux validés.'
if ($PlanOnly) {
    Write-Host "[PLAN UNIQUEMENT] Bundle prêt pour $ReleaseName; aucun SSH/SCP exécuté."
    return
}

$remoteUpload = "$($config.VpsIncomingRoot)/$ReleaseName.upload"
& ssh $VpsHost "install -d -m 0750 '$remoteUpload'"
if ($LASTEXITCODE -ne 0) { throw 'Preparation du dossier VPS en echec.' }
& scp @bundleFiles "${VpsHost}:${remoteUpload}/"
if ($LASTEXITCODE -ne 0) { throw 'Transfert du bundle VPS en echec.' }

$remoteScript = @'
set -Eeuo pipefail
release_name="$1"
build_version="$2"
upload="$3"
release_root="$4"
release="${release_root}/${release_name}"
stage="${release}.incoming"
rollback="${release}.rollback"

if pgrep -af '[P]ropHuntServer' >/dev/null; then
  printf 'Refus: un worker PropHunt est actif.\n' >&2
  exit 70
fi

cd "${upload}"
sha256sum -c SHA256SUMS
rm -rf -- "${stage}"
install -d -m 0750 "${stage}" "${stage}/ops"
tar -xzf "PropHuntServer-Linux-${build_version}-Runtime.tar.gz" -C "${stage}"
tar -xzf "PropHuntServer-Ops-${build_version}.tar.gz" -C "${stage}/ops"
cp "PropHuntServer-${build_version}-manifest.json" SHA256SUMS "${stage}/"

# Les archives sont produites sous Windows : restaurer explicitement le propriétaire
# et les modes attendus par NOVA avant de qualifier puis activer la release.
# Le helper refuse volontairement toute release inscriptible par le groupe/le monde,
# et le worker `uegame` doit pouvoir traverser puis exécuter le package.
chown -R root:uegame "${stage}"
find "${stage}" -type d -exec chmod 0750 {} +
find "${stage}" -type f -exec chmod 0640 {} +
chmod 0750 \
  "${stage}/PropHuntServer.sh" \
  "${stage}/PropHunt/Binaries/Linux/PropHuntServer-Linux-Shipping" \
  "${stage}/ops/preflight-prophunt-server.sh" \
  "${stage}/ops/test-vps-bundle-local.sh"
for optional_script in run-prophunt-server.sh audit-prophunt-vps-readonly.sh; do
  if [[ -f "${stage}/ops/${optional_script}" ]]; then
    chmod 0750 "${stage}/ops/${optional_script}"
  fi
done
bash "${stage}/ops/preflight-prophunt-server.sh" --package "${stage}" --package-only
[[ "$(stat -c '%U:%G:%a' "${stage}")" == 'root:uegame:750' ]]
sudo -u uegame test -x "${stage}/PropHuntServer.sh"
sudo -u uegame test -x "${stage}/PropHunt/Binaries/Linux/PropHuntServer-Linux-Shipping"

rm -rf -- "${rollback}"
if [[ -d "${release}" ]]; then
  mv -- "${release}" "${rollback}"
fi
mv -- "${stage}" "${release}"
[[ "$(stat -c '%U:%G:%a' "${release}")" == 'root:uegame:750' ]]
sudo -u uegame test -x "${release}/PropHuntServer.sh"
rm -rf -- "${upload}"
printf '[OK] Release active remplacee: %s\n' "${release}"
'@

$remoteScript | & ssh $VpsHost "sudo -n bash -s -- '$ReleaseName' '$buildVersion' '$remoteUpload' '$($config.VpsReleaseRoot)'"
if ($LASTEXITCODE -ne 0) { throw 'Activation atomique du serveur VPS en echec.' }
Write-Host "[OK] Serveur PropHunt envoye sur $VpsHost dans $ReleaseName."
