ARG UNREAL_VERSION=5.4.4

FROM ghcr.io/epicgames/unreal-engine:dev-slim-${UNREAL_VERSION}

ENV UNREAL_ENGINE_PATH='/home/ue4/UnrealEngine'

# Unreal can't find these plugins' libraries at runtime or cook time. TODO: Why? Fix this?
ENV LD_LIBRARY_PATH="/home/ue4/UnrealEngine/Engine/Plugins/AI/MassAI/Binaries/Linux:/home/ue4/UnrealEngine/Engine/Plugins/Runtime/MassGameplay/Binaries/Linux:/home/ue4/UnrealEngine/Engine/Plugins/Runtime/ZoneGraph/Binaries/Linux:/home/ue4/UnrealEngine/Engine/Plugins/Runtime/ZoneGraphAnnotations/Binaries/Linux"

# Install dependencies
RUN sudo apt-get update && sudo apt-get install -y jq rsync
