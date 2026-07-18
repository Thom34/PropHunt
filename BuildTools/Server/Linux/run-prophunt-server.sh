#!/usr/bin/env bash
set -Eeuo pipefail

readonly SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
readonly ENV_FILE="${PH_ENV_FILE:-${SCRIPT_DIR}/prophunt-server.env}"

if [[ ! -r "${ENV_FILE}" ]]; then
  printf 'Configuration illisible: %s\n' "${ENV_FILE}" >&2
  exit 64
fi

set -a
# shellcheck disable=SC1090
source "${ENV_FILE}"
set +a

required_variables=(
  PH_SERVER_ROOT
  PH_MAP
  PH_GAME_PORT
  PH_QUERY_PORT
  PH_LOG_DIR
  PH_STATE_DIR
  PH_STEAM_SDK64_DIR
)

for variable_name in "${required_variables[@]}"; do
  if [[ -z "${!variable_name:-}" ]]; then
    printf 'Variable obligatoire absente: %s\n' "${variable_name}" >&2
    exit 64
  fi
done

validate_port() {
  local port_name="$1"
  local port_value="$2"

  if [[ ! "${port_value}" =~ ^[0-9]+$ ]] || ((port_value < 1 || port_value > 65535)); then
    printf 'Port %s invalide: %s\n' "${port_name}" "${port_value}" >&2
    exit 64
  fi
}

validate_port PH_GAME_PORT "${PH_GAME_PORT}"
validate_port PH_QUERY_PORT "${PH_QUERY_PORT}"

if [[ "${PH_GAME_PORT}" == "${PH_QUERY_PORT}" ]]; then
  printf 'PH_GAME_PORT et PH_QUERY_PORT doivent être différents.\n' >&2
  exit 64
fi

readonly PACKAGED_LAUNCHER="${PH_SERVER_ROOT}/PropHuntServer.sh"
readonly SERVER_BINARY="${PH_SERVER_ROOT}/PropHunt/Binaries/Linux/PropHuntServer"

if [[ ! -x "${PACKAGED_LAUNCHER}" || ! -x "${SERVER_BINARY}" ]]; then
  printf 'Package serveur incomplet ou non exécutable sous %s\n' "${PH_SERVER_ROOT}" >&2
  exit 66
fi

if [[ ! -r "${PH_STEAM_SDK64_DIR}/steamclient.so" ]]; then
  printf 'Runtime SteamCMD absent: %s/steamclient.so\n' "${PH_STEAM_SDK64_DIR}" >&2
  exit 69
fi

install -d -m 0750 "${PH_LOG_DIR}" "${PH_STATE_DIR}"

export HOME="${PH_STATE_DIR}"
export LD_LIBRARY_PATH="${PH_STEAM_SDK64_DIR}${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"

exec "${PACKAGED_LAUNCHER}" \
  "${PH_MAP}" \
  "-port=${PH_GAME_PORT}" \
  "-QueryPort=${PH_QUERY_PORT}" \
  "-userdir=${PH_STATE_DIR}" \
  "-abslog=${PH_LOG_DIR}/PropHuntServer.log" \
  -unattended \
  -NoSound \
  -stdout \
  -FullStdOutLogOutput \
  "$@"
