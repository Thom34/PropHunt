#!/usr/bin/env bash
set -uo pipefail

readonly AUDIT_VERSION="1"

usage() {
  cat <<'EOF'
Usage: audit-prophunt-vps-readonly.sh

Collecte sur stdout l'état système nécessaire au premier audit PropHunt Phase 2A.
Le script ne demande pas sudo, ne crée aucun fichier et ne modifie ni service,
ni pare-feu, ni package. Une section [LIMITATION] signale une lecture refusée.
EOF
}

if (($# > 0)); then
  case "$1" in
    -h|--help)
      usage
      exit 0
      ;;
    --version)
      printf '%s\n' "${AUDIT_VERSION}"
      exit 0
      ;;
    *)
      printf 'Argument inconnu: %s\n' "$1" >&2
      usage >&2
      exit 64
      ;;
  esac
fi

export LC_ALL=C

section() {
  printf '\n=== %s ===\n' "$1"
}

limitation() {
  printf '[LIMITATION] %s\n' "$1"
}

run_readonly() {
  local label="$1"
  shift

  printf -- '--- %s ---\n' "${label}"
  "$@" 2>&1
  local status=$?
  if ((status != 0)); then
    limitation "${label}: commande terminée avec le code ${status}"
  fi
  return 0
}

print_path_state() {
  local path="$1"
  if [[ -e "${path}" ]]; then
    stat --printf='%n | type=%F | owner=%U:%G | mode=%a | bytes=%s | modified=%y\n' -- "${path}" 2>&1 ||
      limitation "stat illisible pour ${path}"
  else
    printf '[ABSENT] %s\n' "${path}"
  fi
}

print_backup_hint() {
  local path="$1"
  local newest_epoch=""

  if [[ ! -d "${path}" ]]; then
    printf '[ABSENT] %s\n' "${path}"
    return
  fi

  print_path_state "${path}"
  newest_epoch=$(find "${path}" -xdev -maxdepth 3 -type f -printf '%T@\n' 2>/dev/null |
    sort -nr | head -n 1)
  if [[ -n "${newest_epoch}" ]]; then
    date --utc --date="@${newest_epoch%.*}" '+newest_file_utc=%Y-%m-%dT%H:%M:%SZ' 2>/dev/null ||
      limitation "date du dernier fichier illisible sous ${path}"
  else
    printf '[VIDE_OU_ILLISIBLE] %s\n' "${path}"
  fi
}

printf 'PROPHUNT_PHASE2A_READONLY_AUDIT version=%s\n' "${AUDIT_VERSION}"
printf 'contract=stdout_only,no_sudo,no_write,no_service_change,no_firewall_change\n'
printf 'started_utc=%s\n' "$(date -u '+%Y-%m-%dT%H:%M:%SZ')"

section 'Identité hôte et système'
printf 'user=%s uid=%s gid=%s\n' "$(id -un)" "$(id -u)" "$(id -g)"
printf 'hostname=%s\n' "$(hostname 2>/dev/null || printf 'inconnu')"
printf 'kernel=%s\n' "$(uname -srmo 2>/dev/null || printf 'inconnu')"
if [[ -r /etc/os-release ]]; then
  (
    # shellcheck disable=SC1091
    source /etc/os-release
    printf 'distribution=%s\n' "${PRETTY_NAME:-${NAME:-inconnue}}"
  )
else
  limitation '/etc/os-release illisible'
fi
if command -v systemd-detect-virt >/dev/null 2>&1; then
  printf 'virtualization=%s\n' "$(systemd-detect-virt 2>/dev/null || printf 'none-or-unknown')"
fi

section 'Capacité et charge'
if command -v lscpu >/dev/null 2>&1; then
  lscpu | awk -F: '/^(Architecture|CPU\(s\)|Model name|Thread\(s\) per core|Core\(s\) per socket|Socket\(s\)):/ {
    key=$1; value=$2; gsub(/^[ \t]+|[ \t]+$/, "", value); printf "%s=%s\n", key, value
  }'
else
  limitation 'lscpu absent'
fi
run_readonly 'Mémoire' free -h
run_readonly 'Charge' uptime
run_readonly 'Volumes montés' df -hPT -x tmpfs -x devtmpfs -x squashfs

section 'Services et processus'
if command -v systemctl >/dev/null 2>&1; then
  run_readonly 'Services actifs' systemctl list-units --type=service --state=running --no-legend --no-pager --plain
  printf -- '--- Services PropHunt, JeuMulti58, Nova, Steam ou conteneurs ---\n'
  systemctl list-unit-files --type=service --no-legend --no-pager 2>&1 |
    awk 'tolower($1) ~ /(prophunt|jeumulti|nova|steam|docker|podman|containerd)/ {print}' ||
    limitation 'inventaire ciblé des unités systemd impossible'
  printf -- '--- Timers de sauvegarde détectables ---\n'
  systemctl list-timers --all --no-legend --no-pager 2>&1 |
    awk 'tolower($0) ~ /(backup|snapshot|dump)/ {print; found=1} END {if (!found) print "[AUCUN_TIMER_CIBLE_VISIBLE]"}' ||
    limitation 'inventaire des timers impossible'
else
  limitation 'systemctl absent'
fi

printf -- '--- Processus de jeu et orchestration, sans arguments ---\n'
ps -eo pid=,user=,comm=,etimes= 2>&1 |
  awk 'tolower($3) ~ /(prophunt|jeumulti|nova|steamcmd)/ {print; found=1} END {if (!found) print "[AUCUN_PROCESSUS_CIBLE_VISIBLE]"}' ||
  limitation 'inventaire ciblé des processus impossible'

section 'Conteneurs'
if command -v docker >/dev/null 2>&1; then
  run_readonly 'Docker ps' docker ps --no-trunc --format '{{.ID}} | {{.Names}} | {{.Image}} | {{.Status}} | {{.Ports}}'
else
  printf '[ABSENT] docker\n'
fi
if command -v podman >/dev/null 2>&1; then
  run_readonly 'Podman ps' podman ps --no-trunc --format '{{.ID}} | {{.Names}} | {{.Image}} | {{.Status}} | {{.Ports}}'
else
  printf '[ABSENT] podman\n'
fi

section 'Écoutes réseau'
if command -v ss >/dev/null 2>&1; then
  run_readonly 'Sockets TCP et UDP en écoute, sans payload' ss -H -lntu
else
  limitation 'ss absent'
fi

section 'Pare-feu visible sans élévation'
if command -v ufw >/dev/null 2>&1; then
  run_readonly 'UFW status verbose' ufw status verbose
else
  printf '[ABSENT] ufw\n'
fi
if command -v nft >/dev/null 2>&1; then
  run_readonly 'nft list ruleset' nft list ruleset
elif command -v iptables-save >/dev/null 2>&1; then
  run_readonly 'iptables-save' iptables-save
else
  printf '[ABSENT] nft et iptables-save\n'
fi

section 'SteamCMD et runtime Steamworks'
if command -v steamcmd >/dev/null 2>&1; then
  printf 'steamcmd=%s\n' "$(command -v steamcmd)"
else
  printf '[ABSENT_DU_PATH] steamcmd\n'
fi
for steam_path in \
  /opt/steamcmd \
  /opt/steamcmd/linux64/steamclient.so \
  /home/steam/steamcmd \
  /home/steam/steamcmd/linux64/steamclient.so; do
  print_path_state "${steam_path}"
done

section 'Chemins PropHunt préparés'
for prophunt_path in \
  /opt/prophunt \
  /opt/prophunt/server \
  /opt/prophunt/ops \
  /etc/prophunt \
  /var/lib/prophunt \
  /var/log/prophunt; do
  print_path_state "${prophunt_path}"
done

section 'Chemins d infrastructure ciblés'
for search_root in /opt /srv; do
  if [[ -d "${search_root}" ]]; then
    find "${search_root}" -maxdepth 3 -type d 2>/dev/null |
      awk 'tolower($0) ~ /(prophunt|jeumulti|nova|steamcmd)/ {print; found=1} END {if (!found) print "[AUCUN_CHEMIN_CIBLE_VISIBLE] " root}' root="${search_root}"
  else
    printf '[ABSENT] %s\n' "${search_root}"
  fi
done

section 'Indicateurs de sauvegarde'
printf 'note=les dates ci-dessous ne prouvent ni intégrité ni restaurabilité\n'
for backup_path in /var/backups /opt/backups /srv/backups; do
  print_backup_hint "${backup_path}"
done

section 'Limites de la collecte'
printf '%s\n' \
  'Les lectures refusées doivent être reprises séparément après analyse et accord.' \
  'Aucune présence de fichier ne constitue une preuve de sauvegarde restaurable.' \
  'Aucun service, port, règle, package ou fichier n a été modifié par ce script.'

printf '\ncompleted_utc=%s\n' "$(date -u '+%Y-%m-%dT%H:%M:%SZ')"
printf 'PROPHUNT_PHASE2A_READONLY_AUDIT_COMPLETE\n'
exit 0
