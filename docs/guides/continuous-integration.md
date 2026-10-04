# Continuous Integration

Tempo ships **reusable GitHub Actions workflows** so your project can build, package, run, test
and release without writing pipeline code from scratch.

They live in [`.github/workflows`](https://github.com/tempo-sim/Tempo/tree/main/.github/workflows).
TempoSample's
[`tempo_sample_build_and_package.yml`](https://github.com/tempo-sim/TempoSample/blob/main/.github/workflows/tempo_sample_build_and_package.yml)
is a good reference for calling them.

| Workflow | Purpose |
|---|---|
| `build_and_package.yml` | Build, package, and optionally release your Tempo project. |
| `test_packaged.yml` | Run client API tests against a packaged artifact. Language-agnostic and reusable. |

## Prerequisites

You'll need `EPIC_DOCKER_USERNAME` and `EPIC_DOCKER_TOKEN` secrets configured, to pull Epic's
Unreal image.

## The engine image

`build_and_package.yml` builds in Epic's `unreal-engine:dev-slim-<version>` image, with the engine
as Epic ships it. Each run resolves that tag to a digest first, so the image and the build cache
always match one engine. Tempo's
[`Dockerfile`](https://github.com/tempo-sim/Tempo/blob/main/Dockerfile) adds the few build tools
Tempo's scripts use (`jq`, `cmake` and a Rust toolchain). That layer is built in the run, takes a
minute or two, and is never published.

!!! note "Upgrading from a workflow that used a pre-modded image"

    `build_and_package.yml` no longer has an `engine_mods_image` input, nor the `registry`,
    `registry_username`, `aws_region` and `aws_role_to_assume` inputs and the secrets that went with
    it. Remove them from your caller, and delete any workflow that called
    `publish_engine_mods.yml` or `prune_engine_mods.yml`. You can delete the image package they
    published, too.

## Running the test suites

`tempo_build_and_package.yml` builds and packages **once**, uploads the artifact, then fans out to
the reusable `test_packaged.yml` — parallel jobs per (Unreal version × test group), across both the
Python and Rust clients. Each job downloads the same artifact, so the expensive build runs once
while tests run in parallel.

[:octicons-arrow-right-24: Testing](testing.md)

## Things that only bite in CI

!!! warning "Clean builds are slow"

    A fully clean CI run takes about two hours. Cache entries expire after a configurable
    retention period (GitHub's default is 7 days; set it per repository under
    `Settings → Actions → General`), and are also evicted once the repository exceeds its cache
    size limit. Either way, any fix has to work from a cold start — a change that only works
    incrementally will eventually meet a run with nothing cached.

!!! warning "Cold packages need `-skipiostore`"

    A clean CI package can crash because iostore cannot find engine content under `Saved/Temp`.
    Pass `-skipiostore` in CI.
