FROM docker.m.daocloud.io/library/ubuntu:22.04

# Avoid interactive prompts during apt-get
ENV DEBIAN_FRONTEND=noninteractive

# Update apt source to ali mirrors for faster downloads inside China
RUN sed -i 's/archive.ubuntu.com/mirrors.aliyun.com/g' /etc/apt/sources.list && \
    sed -i 's/security.ubuntu.com/mirrors.aliyun.com/g' /etc/apt/sources.list

# Install basic dependencies (wget, curl, sudo, build-essential, g++, unzip, zip, python3, zlib1g-dev, git)
RUN apt-get update && apt-get install -y \
    wget \
    curl \
    sudo \
    build-essential \
    g++ \
    unzip \
    zip \
    python3 \
    zlib1g-dev \
    git \
    && rm -rf /var/lib/apt/lists/*

# Add runtime.bash sourcing to the global bashrc so that any interactive bash shell
# (root or host user) automatically gets the environment.
RUN echo "[ -f /workspace/scripts/env/runtime.bash ] && source /workspace/scripts/env/runtime.bash" >> /etc/bash.bashrc

# Copy scripts and .bazelversion to install bazel
WORKDIR /tmp/build
COPY scripts scripts/
COPY .bazelversion ./

# Run the build script to install bazel
RUN sudo bash scripts/deploy/build.sh

# Clean up build files
WORKDIR /workspace
RUN rm -rf /tmp/build

# Set up entrypoint script
COPY docker/entrypoint.sh /usr/local/bin/entrypoint.sh
RUN chmod +x /usr/local/bin/entrypoint.sh

ENTRYPOINT ["/usr/local/bin/entrypoint.sh"]
CMD ["sleep", "infinity"]
