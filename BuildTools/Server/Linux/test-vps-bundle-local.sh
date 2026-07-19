#!/usr/bin/env bash
set -Eeuo pipefail

if (($# != 2)); then
  printf 'Usage: %s ARCHIVE_SERVEUR ARCHIVE_OPS\n' "$0" >&2
  exit 64
fi

readonly SERVER_ARCHIVE="$1"
readonly OPS_ARCHIVE="$2"

if [[ ! -r "${SERVER_ARCHIVE}" || ! -r "${OPS_ARCHIVE}" ]]; then
  printf 'Archive serveur ou ops illisible.\n' >&2
  exit 66
fi

test_root="$(mktemp -d /tmp/prophunt-bundle-test.XXXXXX)"

cleanup() {
  case "${test_root}" in
    /tmp/prophunt-bundle-test.*)
      rm -rf -- "${test_root}"
      ;;
    *)
      printf 'Refus de nettoyer un chemin temporaire inattendu: %s\n' "${test_root}" >&2
      ;;
  esac
}
trap cleanup EXIT

mkdir -p "${test_root}/server" "${test_root}/ops"
tar -xzf "${SERVER_ARCHIVE}" -C "${test_root}/server"
tar -xzf "${OPS_ARCHIVE}" -C "${test_root}/ops"

server_binary="${test_root}/server/PropHunt/Binaries/Linux/PropHuntServer-Linux-Shipping"
if [[ ! -f "${server_binary}" && -f "${test_root}/server/PropHunt/Binaries/Linux/PropHuntServer" ]]; then
  server_binary="${test_root}/server/PropHunt/Binaries/Linux/PropHuntServer"
fi

# Les archives sont produites depuis Windows : normaliser explicitement les modes Linux.
chmod -R u=rwX,g=rX,o= "${test_root}/server" "${test_root}/ops"
chmod 0750 \
  "${test_root}/server/PropHuntServer.sh" \
  "${server_binary}" \
  "${test_root}/ops/preflight-prophunt-server.sh" \
  "${test_root}/ops/test-vps-bundle-local.sh"

if [[ -f "${test_root}/ops/run-prophunt-server.sh" ]]; then
  chmod 0750 "${test_root}/ops/run-prophunt-server.sh"
fi

# Les bundles figés avant la Phase 2A ne contiennent pas encore le collecteur.
# Le générateur courant l'exige pour toute nouvelle archive ops.
if [[ -f "${test_root}/ops/audit-prophunt-vps-readonly.sh" ]]; then
  chmod 0750 "${test_root}/ops/audit-prophunt-vps-readonly.sh"
fi

bash "${test_root}/ops/preflight-prophunt-server.sh" \
  --package "${test_root}/server" \
  --package-only

if find "${test_root}/server" -type f \( -name 'PropHuntServer*.debug' -o -name 'PropHuntServer*.sym' \) | grep -q .; then
  printf 'Le bundle runtime contient encore des symboles séparés.\n' >&2
  exit 1
fi

printf '\nBundle runtime extrait et pré-validé. Le serveur NOVA Shipping protocole 8+ est fail-closed : son smoke exige une réservation NOVA, Steam et un roster sidecar réels.\n'
