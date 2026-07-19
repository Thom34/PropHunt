[CmdletBinding()]
param([string]$VpsHost, [string]$ReleaseName)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$scriptRoot = Split-Path -Parent $PSCommandPath
$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $scriptRoot '..'))
$configPath = Join-Path $scriptRoot 'deploy.config.psd1'
$config = & ([ScriptBlock]::Create([System.IO.File]::ReadAllText($configPath)))
if ($config -isnot [hashtable]) { throw "Configuration de deploiement invalide: $configPath" }
if (-not $VpsHost) { $VpsHost = $config.VpsHost }
if (-not $ReleaseName) { $ReleaseName = $config.VpsReleaseName }
& (Join-Path $projectRoot 'BuildTools\Version\Test-PropHuntVersion.ps1')
$versionManifest = Get-Content -LiteralPath (Join-Path $projectRoot 'BuildTools\Version\PropHuntVersion.json') -Raw | ConvertFrom-Json
if ($ReleaseName -ne [string]$versionManifest.nova_release_name) { throw "Nom de release VPS incohérent: $ReleaseName" }
$protocol = [int]$versionManifest.protocol_version
$buildVersion = [string]$versionManifest.release_version

$serverRoot = Join-Path $projectRoot $config.ServerContent
$bundleRoot = Join-Path $projectRoot $config.VpsBundle
& (Join-Path $projectRoot 'BuildTools\Server\New-PropHuntVpsBundle.ps1') `
    -PackageRoot $serverRoot `
    -OutputDirectory $bundleRoot `
    -ProtocolVersion $protocol `
    -BuildVersion $buildVersion `
    -NovaReleaseName $ReleaseName

$bundleFiles = @(
    (Join-Path $bundleRoot "PropHuntServer-Linux-$buildVersion-Runtime.tar.gz"),
    (Join-Path $bundleRoot "PropHuntServer-Ops-$buildVersion.tar.gz"),
    (Join-Path $bundleRoot "PropHuntServer-$buildVersion-manifest.json"),
    (Join-Path $bundleRoot 'SHA256SUMS')
)
foreach ($file in $bundleFiles) {
    if (-not (Test-Path -LiteralPath $file -PathType Leaf)) { throw "Bundle VPS incomplet: $file" }
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
bash "${stage}/ops/preflight-prophunt-server.sh" --package "${stage}" --package-only

rm -rf -- "${rollback}"
if [[ -d "${release}" ]]; then
  mv -- "${release}" "${rollback}"
fi
mv -- "${stage}" "${release}"
rm -rf -- "${upload}"
printf '[OK] Release active remplacee: %s\n' "${release}"
'@

$remoteScript | & ssh $VpsHost "bash -s -- '$ReleaseName' '$buildVersion' '$remoteUpload' '$($config.VpsReleaseRoot)'"
if ($LASTEXITCODE -ne 0) { throw 'Activation atomique du serveur VPS en echec.' }
Write-Host "[OK] Serveur PropHunt envoye sur $VpsHost dans $ReleaseName."
