ARG UNREAL_VERSION=5.4.4

FROM ghcr.io/epicgames/unreal-engine:dev-slim-${UNREAL_VERSION}

ENV UNREAL_ENGINE_PATH='/home/ue4/UnrealEngine'

# Install dependencies. cmake is required for TEMPO_GEN_CPP_API=1 (needs >= 3.20;
# Ubuntu 22.04 ships 3.22, 24.04 ships 3.28).
RUN sudo apt-get update && sudo apt-get install -y jq rsync cmake curl

# Install Rust toolchain (cargo) so TEMPO_GEN_RUST_API=1 works. rustup installs
# under the ue4 user's home; add ~/.cargo/bin to PATH for all subsequent shells.
RUN curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh -s -- -y --default-toolchain stable --profile minimal
ENV PATH="/home/ue4/.cargo/bin:${PATH}"
