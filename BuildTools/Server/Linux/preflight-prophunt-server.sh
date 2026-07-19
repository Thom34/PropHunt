#!/usr/bin/env bash
set -Eeuo pipefail

usage() {
  cat <<'EOF'
Usage:
  preflight-prophunt-server.sh --package /chemin/du/package [--package-only]
  preflight-prophunt-server.sh --env /etc/prophunt/prophunt-server.env

--package-only valide l'archive cuisinée sans exiger SteamCMD, les répertoires
d'exploitation ou les ports libres du futur VPS.
EOF
}

package_root=""
env_file=""
package_only=false

while (($# > 0)); do
  case "$1" in
    --package)
      [[ $# -ge 2 ]] || { usage >&2; exit 64; }
      package_root="$2"
      shift 2
      ;;
    --env)
      [[ $# -ge 2 ]] || { usage >&2; exit 64; }
      env_file="$2"
      shift 2
      ;;
    --package-only)
      package_only=true
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      printf 'Argument inconnu: %s\n' "$1" >&2
      usage >&2
      exit 64
      ;;
  esac
done

if [[ -n "${env_file}" ]]; then
  if [[ ! -r "${env_file}" ]]; then
    printf '[ECHEC] Configuration illisible: %s\n' "${env_file}" >&2
    exit 66
  fi
  set -a
  # shellcheck disable=SC1090
  source "${env_file}"
  set +a
  package_root="${PH_SERVER_ROOT:-}"
fi

if [[ -z "${package_root}" ]]; then
  usage >&2
  exit 64
fi

failures=0

pass() {
  printf '[OK] %s\n' "$1"
}

fail() {
  printf '[ECHEC] %s\n' "$1" >&2
  failures=$((failures + 1))
}

require_file() {
  local file_path="$1"
  local label="$2"
  if [[ -f "${file_path}" ]]; then
    pass "${label}: ${file_path}"
  else
    fail "${label} absent: ${file_path}"
  fi
}

readonly launcher="${package_root}/PropHuntServer.sh"
binary="${package_root}/PropHunt/Binaries/Linux/PropHuntServer-Linux-Shipping"
if [[ ! -f "${binary}" && -f "${package_root}/PropHunt/Binaries/Linux/PropHuntServer" ]]; then
  binary="${package_root}/PropHunt/Binaries/Linux/PropHuntServer"
fi
readonly binary
readonly steam_api="${package_root}/Engine/Binaries/ThirdParty/Steamworks/Steamv164/x86_64-unknown-linux-gnu/libsteam_api.so"

require_file "${launcher}" 'Lanceur packagé'
require_file "${binary}" 'Binaire ELF serveur'
require_file "${steam_api}" 'Steamworks libsteam_api.so'

for executable_path in "${launcher}" "${binary}"; do
  if [[ -x "${executable_path}" ]]; then
    pass "Exécutable autorisé: ${executable_path}"
  else
    fail "Bit exécutable absent: ${executable_path}"
  fi
done

pak_count=$(find "${package_root}/PropHunt/Content/Paks" -maxdepth 1 -type f \( -name '*.pak' -o -name '*.utoc' -o -name '*.ucas' \) 2>/dev/null | wc -l)
if ((pak_count >= 3)); then
  pass "Contenu cuisiné: ${pak_count} conteneurs pak/IoStore"
else
  fail "Contenu cuisiné incomplet: ${pak_count} conteneur(s) trouvé(s)"
fi

if [[ -f "${binary}" ]] && command -v file >/dev/null 2>&1; then
  if file "${binary}" | grep -q 'ELF 64-bit'; then
    pass 'Architecture binaire: ELF 64-bit'
  else
    fail 'Le binaire serveur n est pas un ELF 64-bit'
  fi
fi

if [[ -f "${binary}" ]] && command -v ldd >/dev/null 2>&1; then
  missing_dependencies=$(ldd "${binary}" 2>/dev/null | grep 'not found' || true)
  if [[ -z "${missing_dependencies}" ]]; then
    pass 'Dépendances dynamiques du binaire résolues sur cet hôte'
  else
    fail "Dépendances dynamiques absentes: ${missing_dependencies//$'\n'/; }"
  fi
fi

if [[ "${package_only}" == false ]]; then
  required_env=(PH_SERVER_ROOT PH_MAP PH_GAME_PORT PH_QUERY_PORT PH_LOG_DIR PH_STATE_DIR PH_STEAM_SDK64_DIR)
  for variable_name in "${required_env[@]}"; do
    if [[ -n "${!variable_name:-}" ]]; then
      pass "Variable ${variable_name} définie"
    else
      fail "Variable ${variable_name} absente"
    fi
  done

  if [[ -n "${PH_STEAM_SDK64_DIR:-}" ]]; then
    require_file "${PH_STEAM_SDK64_DIR}/steamclient.so" 'Runtime SteamCMD steamclient.so'
  fi

  if ((EUID == 0)); then
    fail 'Le pré-vol complet doit être exécuté avec le compte non privilégié prophunt'
  else
    pass "Compte non privilégié: $(id -un)"
  fi

  if [[ "${PH_MAP:-}" == /Game/* ]]; then
    pass "Carte Unreal valide: ${PH_MAP}"
  else
    fail "Carte Unreal invalide: ${PH_MAP:-absente}"
  fi

  for writable_directory in "${PH_LOG_DIR:-}" "${PH_STATE_DIR:-}"; do
    [[ -n "${writable_directory}" ]] || continue
    if [[ -d "${writable_directory}" && -w "${writable_directory}" ]]; then
      pass "Répertoire accessible en écriture: ${writable_directory}"
    else
      fail "Répertoire absent ou non accessible en écriture: ${writable_directory}"
    fi
  done

  for port_name in PH_GAME_PORT PH_QUERY_PORT; do
    port_value="${!port_name:-}"
    if [[ "${port_value}" =~ ^[0-9]+$ ]] && ((port_value >= 1 && port_value <= 65535)); then
      pass "${port_name} valide: ${port_value}/udp"
    else
      fail "${port_name} invalide: ${port_value:-absent}"
    fi
  done

  if [[ -n "${PH_GAME_PORT:-}" && "${PH_GAME_PORT:-}" == "${PH_QUERY_PORT:-}" ]]; then
    fail 'Les ports jeu et query doivent être différents'
  fi

  if command -v ss >/dev/null 2>&1; then
    for port_value in "${PH_GAME_PORT:-}" "${PH_QUERY_PORT:-}"; do
      [[ "${port_value}" =~ ^[0-9]+$ ]] || continue
      if ss -H -lun "sport = :${port_value}" | grep -q .; then
        fail "Port UDP déjà occupé: ${port_value}"
      else
        pass "Port UDP libre: ${port_value}"
      fi
    done
  fi
fi

if ((failures > 0)); then
  printf '\nPré-vol terminé avec %d échec(s).\n' "${failures}" >&2
  exit 1
fi

printf '\nPré-vol PropHunt réussi.\n'
