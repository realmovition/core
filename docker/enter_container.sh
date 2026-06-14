#!/bin/bash

# Determine the directory of this script
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
cd "${DIR}/.."

export HOST_USER=$(id -un)
export HOST_UID=$(id -u)
export HOST_GID=$(id -g)

# 1. Ensure container is running
if [ -z "$(docker compose -f docker/docker-compose.yml ps -q cyber-dev)" ]; then
    echo "🔄 启动容器 (Docker Compose)..."
    docker compose -f docker/docker-compose.yml up -d
fi

# 2. Enter container as non-root user
echo "🚀 以用户 [${HOST_USER}] 进入容器..."
docker compose -f docker/docker-compose.yml exec --user "${HOST_USER}" cyber-dev /bin/bash

