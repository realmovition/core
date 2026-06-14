#!/bin/bash

# Determine the directory of this script
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
cd "${DIR}/.."

export HOST_USER=$(id -un)
export HOST_UID=$(id -u)
export HOST_GID=$(id -g)
BAZEL_CACHE="${HOME}/.cache/bazel-apollo-docker"

mkdir -p "${BAZEL_CACHE}"

echo "🔄 正在清理并重新部署容器 (Docker Compose)..."

# 1. 停止并删除旧容器 (包括可能重名的非compose容器)
docker rm -f ubuntu2204 >/dev/null 2>&1
docker compose -f docker/docker-compose.yml down -v

# 2. 构建并启动新容器
docker compose -f docker/docker-compose.yml up -d --build

echo "------------------------------------------------"
echo "容器更新并启动成功！"
echo "运行 ./docker/enter_container.sh 进入！"
echo "------------------------------------------------"

