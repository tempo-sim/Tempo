# Tempo + CitySample Quick Start

Epic's **CitySample** — the project behind the *Matrix Awakens* city — is a large, fully authored
urban environment with traffic, crowds and drivable vehicles. It's a great set of content to show
off Tempo's features.

## 1. Prerequisites

- The [prerequisites](../getting-started/prerequisites.md) for your platform — `jq`, and
  `UNREAL_ENGINE_PATH` on Linux.
- The [CitySample](https://www.fab.com/listings/4898e707-7855-404b-af0e-a505ee690e68) project on **UE 5.8**

## 2. Add Tempo

```bash
cd <your_citysample>/Plugins
git clone https://github.com/tempo-sim/Tempo.git
```

## 3. Run Setup.sh

```bash
Plugins/Tempo/Setup.sh    # Setup.bat on Windows
```
Setup.sh will prompt you to download third party dependencies (our custom build of gRPC). Say yes.

In the CitySample project Setup.sh does a few additional steps (none of which you need to know about, just FYI):
- Patches bugs in the project's source that would prevent you from building
- Disables the project's RuleProcessor and Traffic plugins (which Tempo replaces)

## 4. Build and Package

```bash
Plugins/Tempo/Scripts/Build.sh
Plugins/Tempo/Scripts/Run.sh   # Optional - open the project in Unreal Editor to test
Plugins/Tempo/Scripts/Package.sh --release # --release packages a Shipping build, remove for a Development build
```

Packaging will take a few hours. Be patient, good things come to those who wait.

## 5. Profit

```bash
source ./TempoEnv/bin/activate # TempoEnv/Scripts/activate on Windows
python Plugins/Tempo/ExampleClients/Python/CitySampleAV.py --launch # remove --launch if running it yourself
```

## 6. Explore
Check out the [README](https://github.com/tempo-sim/Tempo/tree/main/ExampleClients/Python/CitySampleAV) or
```
python Plugins/Tempo/ExampleClients/Python/CitySampleAV.py --help
```
for a full set of features.

## 7. Bonus: Labeled Road Markings

<img width="2880" height="600" alt="lane_rm_semantic_FisheyeCamera_000042" src="https://github.com/user-attachments/assets/c5f3d5e8-c3b5-4020-bd52-2890e771107d" />

Getting the road markings to show up as a different semantic label than the road requires a couple of small content changes:
  | Asset | Change | Do this |
  | --- | --- | --- |
  | `M_Freeway_Asphalt` | Change the Shading Model to Subsurface (from DefaultLit) | <img width="356" height="176" alt="M_Freeway_Asphalt_subsurface" src="https://github.com/user-attachments/assets/334e1843-4ade-49ca-a35f-d32181fdb415" /> |
  | `M_Freeway_Asphalt` | Create new named reroute nodes containing the Base Color before and after applying decals | <img width="480" height="333" alt="M_Freeway_Asphalt_after_2" src="https://github.com/user-attachments/assets/86b4827c-e63f-4c25-a134-a05b2865180b" /> |
  | `M_Freeway_Asphalt` | Use the new reroute nodes to set the subsurface color to 1 where there are road markings | <img width="1288" height="530" alt="M_Freeway_Asphalt_after_1" src="https://github.com/user-attachments/assets/e6826e50-bd43-4bd8-ab60-1724200e86fd" /> |
  | `MI_FreewayAsphalt_RoadDark` | Stop overriding the Shading Mode | <img width="520" height="248" alt="MI_FreewayAsphalt_RoadDark_after" src="https://github.com/user-attachments/assets/4c7e6531-be79-4b50-919f-2a733ac7528a" /> |

TempoCamera will replace the Road label with RoadMarking anywhere it picks up this subtle change in subsurface color (this is already configured in `CitySampleAV.py`)

## What changed in your project

| | |
|---|---|
| `Source/CitySample/Util/CitySampleBlueprintLibrary.cpp` | One line patched by `Setup.sh` (step 3) |
| `Source/CitySample/Vehicles/CitySampleVehicleBase.cpp` | `PossessedBy` and one `#include` patched by `Setup.sh` (step 3) |
| `Source/CitySample.Target.cs` | Its two LLM test settings guarded to a unique build environment by `Setup.sh` (step 3) |
| `Plugins/CitySampleMassCrowd/.../MassCrowdAnimationProcessor.h` | One `#include` added by `Setup.sh` (step 3) |
| `Plugins/Traffic/`, `Plugins/RuleProcessor/` | Descriptors renamed `*.disabled-by-tempo` |
| `Config/DefaultEngine.ini` | Tempo's world settings and Mac entitlements (a `TEMPO_PROJECT_CONFIG` block at the end) |
| `TempoEnv/` | Generated Python virtual environment with the client packages |

## See also

- [Traffic](../plugins/traffic.md) — what Tempo's fork adds over CitySample's plugin, and how it
  stands in for it
- [TempoAgents](../plugins/tempo-agents.md) — the gRPC surface over the road network and traffic
  controls
- [Troubleshooting](troubleshooting.md) — known failure modes, including the plugin collision
  above
