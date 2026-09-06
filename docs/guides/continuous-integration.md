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
| `publish_engine_mods.yml` | Publish a pre-modded Unreal image to your private GHCR. |
| `prune_engine_mods.yml` | Keep only recent engine-mod image tags. |

## Prerequisites

You'll need `EPIC_DOCKER_USERNAME` and `EPIC_DOCKER_TOKEN` secrets configured, to pull Epic's base
Unreal image.

## Speeding up builds with a pre-modded engine image

Tempo modifies the Unreal engine in place via patches in `EngineMods/`. By default
`build_and_package.yml` applies these inside each CI run, which adds **~10–15 minutes** and
consumes the GitHub Actions cache budget — 10 GB on the free tier, shared with the per-commit
build cache.

For larger projects you can opt into a pre-modded Unreal image hosted on your own private GHCR,
which the build workflow pulls instead of re-applying mods every run.

### 1. Add a publish workflow

```yaml title=".github/workflows/publish_engine_mods.yml"
name: Publish Pre-Modded Engine Image
on:
  push:
    branches: [main]
    paths:
      - 'Plugins/Tempo/EngineMods/**'
      - 'Plugins/Tempo/Dockerfile'
      - 'Plugins/Tempo/Scripts/InstallEngineMods.sh'
  workflow_dispatch:
jobs:
  publish:
    permissions:
      contents: read
      packages: write
    strategy:
      matrix:
        unreal_version: ["5.7", "5.8"]
      fail-fast: false
    uses: tempo-sim/Tempo/.github/workflows/publish_engine_mods.yml@main
    with:
      unreal_version: ${{ matrix.unreal_version }}
      image_name: ghcr.io/${{ github.repository_owner }}/tempo-unreal-modded
      tempo_root: Plugins/Tempo  # adjust to your Tempo submodule path
    secrets: inherit
  prune:
    needs: publish
    permissions:
      packages: write
    uses: tempo-sim/Tempo/.github/workflows/prune_engine_mods.yml@main
    with:
      package_name: tempo-unreal-modded
      package_owner: ${{ github.repository_owner }}
    secrets: inherit
```

### 2. Point your build workflow at it

Pass `engine_mods_image: ghcr.io/<your-org>/tempo-unreal-modded` to your existing
`build_and_package.yml` caller, and grant it `packages: read`.

The build workflow computes the EngineMods hash, attempts a `docker pull` of the matching tag, and
skips the in-workflow engine-mods install when the pull succeeds. **If the pull fails for any
reason** — image not yet published for the current hash, permissions misconfigured — the workflow
falls back to the in-workflow path. There is no breakage, just no speedup.

### Setup notes

- Run your new `publish_engine_mods` workflow manually once via `workflow_dispatch` **before**
  merging the `engine_mods_image` change to your build workflow, so the first image exists when
  the build runs.
- The published image contains **UE-derived content**, which the Unreal Engine EULA does not
  permit redistributing publicly. Your org's GHCR package-creation policy must be set to
  "Private" (under `Settings → Packages` in the org admin), and the package will inherit private
  visibility on first push. If your org's policy allows public, flip the package to private via
  its settings page after the first push.
- The `prune` job keeps only the most recent tag per Unreal version, which is enough since
  EngineMods rarely change. Pass `keep: 3` (or higher) for a longer history.

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
