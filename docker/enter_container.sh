#!/bin/bash
set -euo pipefail

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
cd "${DIR}/.."

# Ensure environment is initialized
if [ ! -f .env ]; then
    echo "❌ .env not found; run: bash docker/reset_container.sh"
    exit 1
fi

# Load environment variables
source .env

# Ensure the container is running
if [ -z "$(docker compose --env-file .env -f docker/docker-compose.yml ps -q cyber-dev)" ]; then
    echo "🔄 Container not running — starting in background..."
    docker compose --env-file .env -f docker/docker-compose.yml up -d
fi

echo "🚀 Entering container as [${HOST_USER}]..."

# Enter container with numeric UID:GID to keep strict non-root mode and avoid
# username lookup dependency in /etc/passwd.
CUSTOM_PS1="${HOST_USER}@cyber-dev:\\w\\$ "
docker compose --env-file .env -f docker/docker-compose.yml exec \
  --user "${HOST_UID}:${HOST_GID}" \
  -e HOME="/home/${HOST_USER}" \
  -e USER="${HOST_USER}" \
  cyber-dev env PS1="${CUSTOM_PS1}" /bin/bash --noprofile --norc -i
