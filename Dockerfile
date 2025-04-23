ARG UNREAL_VERSION=5.4.4

FROM ghcr.io/epicgames/unreal-engine:dev-slim-${UNREAL_VERSION}

ENV UNREAL_ENGINE_PATH='/home/ue4/UnrealEngine'

# Install dependencies
RUN sudo apt-get update && sudo apt-get install -y jq rsync
