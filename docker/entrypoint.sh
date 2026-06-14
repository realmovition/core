#!/bin/bash
set -e

# Default values if not provided
HOST_USER=${HOST_USER:-developer}
HOST_UID=${HOST_UID:-1000}
HOST_GID=${HOST_GID:-1000}

echo "Starting entrypoint script..."
echo "HOST_USER: ${HOST_USER}, HOST_UID: ${HOST_UID}, HOST_GID: ${HOST_GID}"

# 1. Create group if it doesn't exist
if ! getent group "${HOST_GID}" >/dev/null; then
    groupadd -g "${HOST_GID}" "${HOST_USER}" || true
fi

# 2. Create user if it doesn't exist
if ! id -u "${HOST_USER}" >/dev/null 2>&1; then
    useradd -u "${HOST_UID}" -g "${HOST_GID}" -m -s /bin/bash "${HOST_USER}" || true
    echo "${HOST_USER} ALL=(ALL) NOPASSWD:ALL" >> /etc/sudoers
fi

# 3. Fix permissions for wheel directory
mkdir -p /opt/wheel
chown -R "${HOST_UID}:${HOST_GID}" /opt/wheel

# 4. If the home directory cache exists, fix permissions
if [ -d "/home/${HOST_USER}" ]; then
    chown -R "${HOST_UID}:${HOST_GID}" "/home/${HOST_USER}" || true
fi

# Execute CMD
exec "$@"
