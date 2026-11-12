# Unreal Engine 5.7 for MARE on Linux: Epic's official development image plus
# what the project needs on top. Pulling it needs a GitHub account linked to an
# Epic Games account (https://www.unrealengine.com/en-US/ue-on-github) and
#   docker login ghcr.io -u <github user>      (a token with read:packages)
# Build and run it through mare.sh, next to this file.
# Pinned to one engine release, so every machine builds the same engine; to
# upgrade, change the version here and in compose.yaml, then REBUILD=1 mare.sh build.
ARG UE_IMAGE=ghcr.io/epicgames/unreal-engine:dev-slim-5.7.4
FROM ${UE_IMAGE}

# Vulkan loader (the NVIDIA ICD comes from the container toolkit), vulkaninfo for
# diagnosis, and xclip: Terrain Studio's rover-shadow helper pastes through the
# clipboard. CMake 3.24+ builds URLab's native libraries; Ubuntu 22.04's is
# older, so it comes from pip.
USER root
RUN apt-get update \
 && apt-get install -y --no-install-recommends libvulkan1 vulkan-tools xclip \
 && rm -rf /var/lib/apt/lists/* \
 && pip3 install --no-cache-dir cmake==3.31.6
USER ue4
ENV UE_ROOT=/home/ue4/UnrealEngine
WORKDIR /work/MARE-Lunar-Simulator
