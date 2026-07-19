[CmdletBinding()]
param(
    [string]$VpsHost,
    [string]$ReleaseName,
    [ValidateSet('Auto', 'Reuse', 'Regenerate')]
    [string]$BundleMode = 'Auto',
    [switch]$SkipGatewayTests,
    [switch]$SkipReadiness,
    [switch]$PlanOnly
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$scriptRoot = Split-Path -Parent $PSCommandPath
$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $scriptRoot '..'))
$configPath = Join-Path $scriptRoot 'deploy.config.psd1'
$config = & ([ScriptBlock]::Create([System.IO.File]::ReadAllText($configPath)))
if ($config -isnot [hashtable]) { throw "Configuration de déploiement invalide: $configPath" }
if (-not $VpsHost) { $VpsHost = [string]$config.VpsHost }
if (-not $ReleaseName) { $ReleaseName = [string]$config.VpsReleaseName }

if (-not $SkipReadiness) {
    & (Join-Path $scriptRoot 'Test-ReleaseReadiness.ps1') -PostBuild -ArtifactScope Both -PlanOnly:$PlanOnly
}

& (Join-Path $projectRoot 'BuildTools\Version\Test-PropHuntVersion.ps1')
$manifest = Get-Content -LiteralPath (Join-Path $projectRoot 'BuildTools\Version\PropHuntVersion.json') -Raw | ConvertFrom-Json
$buildVersion = [string]$manifest.release_version
$protocolVersion = [int]$manifest.protocol_version
if ($ReleaseName -ne [string]$manifest.nova_release_name) { throw "Release VPS incohérente: $ReleaseName" }
if ($ReleaseName -notmatch '^build-prophunt-v[0-9]+(?:\.[0-9]+){2}$') { throw "Nom de release VPS non sûr: $ReleaseName" }
if ($buildVersion -notmatch '^[0-9]+(?:\.[0-9]+){2}$') { throw "Version de build non sûre: $buildVersion" }

$gatewayRoot = Join-Path $projectRoot 'SourceArt\Deploy\PropHuntGateway'
$gatewayPackage = Join-Path $gatewayRoot 'prophunt_gateway'
$gatewayFiles = @(Get-ChildItem -LiteralPath $gatewayPackage -Filter '*.py' -File | Sort-Object Name)
if ($gatewayFiles.Count -lt 5) { throw "Paquet gateway incomplet: $gatewayPackage" }
$gatewayServiceUnit = Join-Path $gatewayRoot 'prophunt-gateway.service'
if (-not (Test-Path -LiteralPath $gatewayServiceUnit -PathType Leaf)) {
    throw "Unité systemd gateway absente: $gatewayServiceUnit"
}
$gatewayPaths = [string[]]@(
    @($gatewayFiles | ForEach-Object { $_.FullName })
    $gatewayServiceUnit
)

Write-Host '[plan] 1. Validation version et tests gateway locaux'
Write-Host '[plan] 2. Sauvegarde gateway/NOVA/SQLite et arrêt de la gateway'
Write-Host '[plan] 3. Installation atomique de la release serveur Linux'
Write-Host '[plan] 4. Bascule de la route NOVA, restart NOVA, attente du socket'
Write-Host '[plan] 5. Redémarrage gateway et preuve /healthz'
if ($PlanOnly) {
    Write-Host "[PLAN UNIQUEMENT] $ReleaseName / build $buildVersion / protocole $protocolVersion / hôte $VpsHost"
    return
}

if (-not $SkipGatewayTests) {
    $oldPythonPath = $env:PYTHONPATH
    try {
        $env:PYTHONPATH = $gatewayRoot
        & python -m unittest discover -s (Join-Path $gatewayRoot 'tests') -v
        if ($LASTEXITCODE -ne 0) { throw "Tests gateway en échec (code $LASTEXITCODE)." }
    } finally {
        if ($null -eq $oldPythonPath) { Remove-Item Env:PYTHONPATH -ErrorAction SilentlyContinue }
        else { $env:PYTHONPATH = $oldPythonPath }
    }
}

$deploymentId = 'ph-' + (Get-Date).ToUniversalTime().ToString('yyyyMMdd-HHmmss') + '-' + $PID
$remoteUpload = "$($config.VpsIncomingRoot)/$ReleaseName-control-$deploymentId"
$remoteBackup = "/home/ue-game/backups/prophunt-$deploymentId"

& ssh $VpsHost "install -d -m 0750 '$remoteUpload'"
if ($LASTEXITCODE -ne 0) { throw 'Préparation du sas gateway en échec.' }
& scp @gatewayPaths "${VpsHost}:${remoteUpload}/"
if ($LASTEXITCODE -ne 0) { throw 'Transfert du paquet gateway en échec.' }

$prepareScript = @'
set -Eeuo pipefail
upload="$1"
backup="$2"
protocol="$3"
build_version="$4"

if pgrep -af '[P]ropHuntServer' >/dev/null; then
  printf 'Refus: un worker PropHunt est actif.\n' >&2
  exit 70
fi
python3 - <<'PY'
import sqlite3
from pathlib import Path

path = Path('/var/lib/prophunt-gateway/tickets.sqlite3')
if not path.exists():
    print('[gate] gateway database absent; no active ticket to preserve')
    raise SystemExit(0)
connection = sqlite3.connect(f'file:{path}?mode=ro', uri=True)
active = connection.execute(
    "SELECT COUNT(*) FROM tickets WHERE state IN ('queued','forming','allocating','ready')"
).fetchone()[0]
pending_allocations = connection.execute("SELECT COUNT(*) FROM allocation_attempts").fetchone()[0]
pending_cancellations = connection.execute("SELECT COUNT(*) FROM allocation_cancellations").fetchone()[0]
print(f'[gate] active_tickets={active} allocation_attempts={pending_allocations} cancellation_outbox={pending_cancellations}')
if active or pending_allocations or pending_cancellations:
    raise SystemExit('Refus: matchmaking ou outbox encore actif; passer en maintenance et drainer/reset explicitement.')
PY
if [[ -e "${backup}" ]]; then
  printf 'Refus: sauvegarde déjà présente: %s\n' "${backup}" >&2
  exit 71
fi
grep -Fq 'prophunt.get("build_id") != str(run["build_id"])' /opt/nova-orchestrator/src/nova_orchestrator/repository.py
grep -Fq '"build_id": value["build_id"]' /opt/nova-orchestrator/src/nova_orchestrator/sidecar.py

systemctl stop prophunt-gateway
install -d -m 0750 "${backup}"
cp -a /opt/prophunt-gateway "${backup}/gateway-opt"
cp -a /etc/prophunt-gateway/config.json "${backup}/gateway-config.json"
cp -a /etc/nova-orchestrator/config.toml "${backup}/nova-config.toml"
cp -a /etc/systemd/system/prophunt-gateway.service "${backup}/prophunt-gateway.service"
if [[ -f /var/lib/prophunt-gateway/tickets.sqlite3 ]]; then
  cp -a /var/lib/prophunt-gateway/tickets.sqlite3 "${backup}/tickets.sqlite3"
fi

cp -a "${upload}/"*.py /opt/prophunt-gateway/prophunt_gateway/
find /opt/prophunt-gateway/prophunt_gateway -maxdepth 1 -type f -name '*.py' -exec chmod 0644 {} +
chown -R root:root /opt/prophunt-gateway/prophunt_gateway
install -o root -g root -m 0644 "${upload}/prophunt-gateway.service" /etc/systemd/system/prophunt-gateway.service
systemd-analyze verify /etc/systemd/system/prophunt-gateway.service
systemctl daemon-reload

python3 - "${protocol}" "${build_version}" <<'PY'
import json
import os
import sys
from pathlib import Path

path = Path('/etc/prophunt-gateway/config.json')
value = json.loads(path.read_text(encoding='utf-8'))
value['protocol_version'] = int(sys.argv[1])
value['project_build_id'] = sys.argv[2]
temporary = path.with_name(path.name + '.deploy-tmp')
temporary.write_text(json.dumps(value, indent=2) + '\n', encoding='utf-8')
os.chown(temporary, path.stat().st_uid, path.stat().st_gid)
os.chmod(temporary, path.stat().st_mode & 0o777)
os.replace(temporary, path)
PY

python3 -m json.tool /etc/prophunt-gateway/config.json >/dev/null
printf '[OK] Plan de contrôle préparé; gateway maintenue arrêtée.\n'
'@

$activateScript = @'
set -Eeuo pipefail
release="$1"
build_version="$2"
protocol="$3"
upload="$4"

release_path="/home/ue-game/releases/${release}"
[[ "$(stat -c '%U:%G:%a' "${release_path}")" == 'root:uegame:750' ]]
sudo -u uegame test -x "${release_path}/PropHuntServer.sh"
sudo -u uegame test -x "${release_path}/PropHunt/Binaries/Linux/PropHuntServer-Linux-Shipping"
python3 - "${release}" "${build_version}" <<'PY'
import os
import re
import sys
from pathlib import Path

path = Path('/etc/nova-orchestrator/config.toml')
lines = path.read_text(encoding='utf-8').splitlines()
pattern = re.compile(r'^"build-prophunt[^"]*"\s*=\s*"[^"]+"\s*$')
matches = [index for index, line in enumerate(lines) if pattern.fullmatch(line)]
if len(matches) != 1:
    raise SystemExit(f'route PropHunt ambiguë: {len(matches)} ligne(s)')
lines[matches[0]] = f'"{sys.argv[1]}" = "{sys.argv[2]}"'
temporary = path.with_name(path.name + '.deploy-tmp')
temporary.write_text('\n'.join(lines) + '\n', encoding='utf-8')
os.chown(temporary, path.stat().st_uid, path.stat().st_gid)
os.chmod(temporary, path.stat().st_mode & 0o777)
os.replace(temporary, path)
PY

python3 -c "import tomllib; tomllib.load(open('/etc/nova-orchestrator/config.toml','rb'))"
systemctl restart nova-orchestrator
for ((attempt=0; attempt<30; attempt++)); do
  [[ -S /run/nova-orchestrator/api.sock ]] && break
  sleep 1
done
[[ -S /run/nova-orchestrator/api.sock ]]
systemctl start prophunt-gateway
health=''
for ((attempt=0; attempt<30; attempt++)); do
  if health="$(curl -fsS http://127.0.0.1:8790/healthz 2>/dev/null)"; then
    break
  fi
  sleep 1
done
[[ -n "${health}" ]]
printf '%s\n' "${health}"
jq -e --arg build "${build_version}" --argjson protocol "${protocol}" \
  '.ok == true and .build_id == $build and .protocol_version == $protocol' <<<"${health}" >/dev/null
rm -rf -- "${upload}"
systemctl is-active nova-orchestrator prophunt-gateway >/dev/null
[[ "$(stat -c '%U:%G:%a' "${release_path}")" == 'root:uegame:750' ]]
sudo -u uegame test -x "${release_path}/PropHuntServer.sh"
python3 - <<'PY'
import sqlite3
from pathlib import Path
path = Path('/var/lib/prophunt-gateway/tickets.sqlite3')
if not path.exists():
    raise SystemExit(0)
connection = sqlite3.connect(f'file:{path}?mode=ro', uri=True)
checks = {
    'active_tickets': "SELECT COUNT(*) FROM tickets WHERE state IN ('queued','forming','allocating','ready')",
    'allocation_attempts': 'SELECT COUNT(*) FROM allocation_attempts',
    'cancellation_outbox': 'SELECT COUNT(*) FROM allocation_cancellations',
}
counts = {name: connection.execute(query).fetchone()[0] for name, query in checks.items()}
print('[post-gate] ' + ' '.join(f'{name}={count}' for name, count in counts.items()))
if any(counts.values()):
    raise SystemExit('Le plan de contrôle a redémarré avec un état matchmaking non drainé.')
PY
printf '[OK] Plan de contrôle activé pour %s.\n' "${release}"
'@

$rollbackScript = @'
set -Eeuo pipefail
backup="$1"
if [[ ! -d "${backup}" ]]; then
  printf 'Sauvegarde de rollback absente: %s\n' "${backup}" >&2
  exit 72
fi
systemctl stop prophunt-gateway || true
rm -rf -- /opt/prophunt-gateway
cp -a "${backup}/gateway-opt" /opt/prophunt-gateway
cp -a "${backup}/gateway-config.json" /etc/prophunt-gateway/config.json
cp -a "${backup}/nova-config.toml" /etc/nova-orchestrator/config.toml
cp -a "${backup}/prophunt-gateway.service" /etc/systemd/system/prophunt-gateway.service
if [[ -f "${backup}/tickets.sqlite3" ]]; then
  cp -a "${backup}/tickets.sqlite3" /var/lib/prophunt-gateway/tickets.sqlite3
fi
systemctl daemon-reload
systemctl restart nova-orchestrator
for ((attempt=0; attempt<30; attempt++)); do
  [[ -S /run/nova-orchestrator/api.sock ]] && break
  sleep 1
done
[[ -S /run/nova-orchestrator/api.sock ]]
systemctl start prophunt-gateway
health=''
for ((attempt=0; attempt<30; attempt++)); do
  if health="$(curl -fsS http://127.0.0.1:8790/healthz 2>/dev/null)"; then
    break
  fi
  sleep 1
done
[[ -n "${health}" ]]
printf '%s\n' "${health}"
printf '\n[ROLLBACK] Plan de contrôle précédent restauré depuis %s.\n' "${backup}"
'@

$prepared = $true
try {
    $prepareScript | & ssh $VpsHost "sudo -n bash -s -- '$remoteUpload' '$remoteBackup' '$protocolVersion' '$buildVersion'"
    if ($LASTEXITCODE -ne 0) { throw 'Préparation coordonnée gateway/NOVA en échec.' }

    & (Join-Path $scriptRoot 'Push-Server-Vps.ps1') -VpsHost $VpsHost -ReleaseName $ReleaseName -BundleMode $BundleMode -SkipReadiness

    $activateScript | & ssh $VpsHost "sudo -n bash -s -- '$ReleaseName' '$buildVersion' '$protocolVersion' '$remoteUpload'"
    if ($LASTEXITCODE -ne 0) { throw 'Activation coordonnée gateway/NOVA en échec.' }
} catch {
    Write-Warning "Déploiement VPS interrompu: $($_.Exception.Message)"
    if ($prepared) {
        $rollbackScript | & ssh $VpsHost "sudo -n bash -s -- '$remoteBackup'"
        if ($LASTEXITCODE -ne 0) { Write-Warning "Rollback automatique en échec; sauvegarde: $remoteBackup" }
    }
    throw
}

Write-Host "[OK] VPS coordonné: $ReleaseName / $buildVersion / protocole $protocolVersion"
Write-Host "[rollback] Sauvegarde conservée sur le VPS: $remoteBackup"
