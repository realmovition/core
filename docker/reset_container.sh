#!/bin/bash
set -euo pipefail

# Define colors
BLUE='\033[1;34m'
GREEN='\033[1;32m'
RED='\033[0;31m'
NC='\033[0m'

echo -e "${BLUE}🛠 Initializing development environment...${NC}"

# Change to project root directory
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
ROOT_DIR="${DIR}/.."
cd "${ROOT_DIR}"

# 1. Auto-generate .env file (for Docker Compose)
cat <<EOF > .env
HOST_USER=$(id -un)
HOST_UID=$(id -u)
HOST_GID=$(id -g)
BAZEL_CACHE=${HOME}/.cache/bazel-apollo-docker
EOF
echo -e "✅ .env configuration file generated"

# 2. Ensure host Bazel cache dir exists to avoid Docker creating it as root
mkdir -p "${HOME}/.cache/bazel-apollo-docker"

echo -e "${BLUE}🔄 Cleaning and redeploying containers...${NC}"
# Clean old containers and orphan networks
docker compose --env-file .env -f docker/docker-compose.yml down --remove-orphans >/dev/null 2>&1

# Build and start
docker compose --env-file .env -f docker/docker-compose.yml up -d --build

# 3. Verify status
if [ "$(docker compose --env-file .env -f docker/docker-compose.yml ps -q cyber-dev)" ]; then
    echo -e "------------------------------------------------"
    echo -e "${GREEN}🚀 Container updated and started successfully!${NC}"
    echo -e "👉 Run ${BLUE}bash docker/enter_container.sh${NC} to enter the environment"
    echo -e "------------------------------------------------"
else
    echo -e "${RED}❌ Container failed to start; check 'docker compose logs' for details.${NC}"
    exit 1
fi
