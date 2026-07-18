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

# Les archives sont produites depuis Windows : normaliser explicitement les modes Linux.
chmod -R u=rwX,g=rX,o= "${test_root}/server" "${test_root}/ops"
chmod 0750 \
  "${test_root}/server/PropHuntServer.sh" \
  "${test_root}/server/PropHunt/Binaries/Linux/PropHuntServer" \
  "${test_root}/ops/run-prophunt-server.sh" \
  "${test_root}/ops/preflight-prophunt-server.sh" \
  "${test_root}/ops/test-vps-bundle-local.sh"

bash "${test_root}/ops/preflight-prophunt-server.sh" \
  --package "${test_root}/server" \
  --package-only

if find "${test_root}/server" -type f \( -name 'PropHuntServer.debug' -o -name 'PropHuntServer.sym' \) | grep -q .; then
  printf 'Le bundle runtime contient encore des symboles séparés.\n' >&2
  exit 1
fi

set +e
timeout --signal=INT 20s bash "${test_root}/server/PropHuntServer.sh" \
  /Game/PropHunt/Tests/L_PH_CaptureTest \
  -NoSteam \
  -unattended \
  -NoSound \
  -stdout \
  -FullStdOutLogOutput \
  -port=7802 \
  -QueryPort=27032 \
  "-userdir=${test_root}/state" \
  "-abslog=${test_root}/PropHuntServer.log" \
  >"${test_root}/stdout.log" 2>&1
server_status=$?
set -e

if [[ ${server_status} -ne 124 && ${server_status} -ne 130 ]]; then
  printf 'Le smoke serveur Linux a quitté avec un code inattendu: %d\n' "${server_status}" >&2
  tail -n 80 "${test_root}/stdout.log" >&2
  exit 1
fi

required_log_patterns=(
  'Mounted IoStore container'
  'IpNetDriver listening on port 7802'
  'Bringing World /Game/PropHunt/Tests/L_PH_CaptureTest'
  'Dedicated world loaded; publishing the server session without a local player.'
  'World NetDriver shutdown'
)

for required_pattern in "${required_log_patterns[@]}"; do
  if ! grep -aFq "${required_pattern}" "${test_root}/stdout.log"; then
    printf 'Preuve runtime absente: %s\n' "${required_pattern}" >&2
    tail -n 80 "${test_root}/stdout.log" >&2
    exit 1
  fi
done

grep -aE \
  'Mounted IoStore container|IpNetDriver listening on port 7802|Bringing World /Game/PropHunt/Tests/L_PH_CaptureTest|Dedicated world loaded|World NetDriver shutdown' \
  "${test_root}/stdout.log"

printf '\nBundle runtime protocole 6 extrait, pré-validé, démarré et arrêté avec succès.\n'
