# TempoROS

TempoROS integrates ROS 2 and Unreal via [`rclcpp`](https://github.com/ros2/rclcpp), ROS's C++
client library, running **in-process** — there is no separate bridge process.

!!! info "TempoROS has its own documentation site"

    Unlike every other plugin here, **TempoROS is a standalone unit**: you can use it in any
    Unreal project without any other Tempo plugin. It lives in its own
    [repository](https://github.com/tempo-sim/TempoROS) and has its own documentation site.

    It is **not shipped with Tempo** — you add TempoROS to your own project's `Plugins` folder,
    beside Tempo rather than inside it. Tempo guarantees compatibility between its `main` and
    TempoROS's `main`, and verifies it in CI.

    :material-book-open-page-variant: **TempoROS documentation** — *coming soon.* Until it is
    published, the [TempoROS README](https://github.com/tempo-sim/TempoROS#readme) is the
    reference.

## What it is

`TempoROS` wraps ROS's Node type in a `UObject`, `UTempoROSNode`, with full Blueprint support:

```cpp
// UTempoROSNodes are UObjects — store them in a UPROPERTY().
ROSNode = UTempoROSNode::Create("MyNode", this);

ROSNode->AddPublisher<FString>("my_topic", false /*bPrependNodeName*/);

ROSNode->AddSubscription<FString>("my_topic", TROSSubscriptionDelegate<FString>::CreateLambda(
    [](const FString& Message)
    {
        UE_LOG(LogTemp, Display, TEXT("%s"), *Message);
    }));

ROSNode->Publish<FString>("my_topic", TEXT("Hello World!"));
```

Publishing and subscribing work with **native Unreal types** — define a
`TImplicitToROSConverter` / `TImplicitFromROSConverter` for your type and TempoROS handles the
translation, including the coordinate and unit conversion. Common types
(`FVector`, `FTransform`, …) are provided in `TempoROSCommonConverters.h`.

It also covers services, custom message and service types via ROS IDL, tf2, image transport, a
`/clock` server, and shared-memory transport via CycloneDDS and iceoryx on Linux.

## Why in-process

Other popular Unreal plugins for ROS use a custom API to interface with Unreal and then an
external "bridge" library or process to translate messages. TempoROS's design avoids that:

- **No redundant serialization.** ROS messages are used directly, rather than serialized into a
  custom format, transported, and deserialized again.
- **Real ROS client libraries.** `tf2`, `image_transport` and friends are available inside your
  Unreal project with their normal APIs.
- **Zero-copy transport** to an external ROS node on the same machine, using shared memory.

## Using it with the rest of Tempo

TempoROS is optional, and Tempo does not ship it. Tempo's primary interface is gRPC, which
requires no ROS installation at all — see [Client APIs](../clients/index.md).

If you add TempoROS to a project that *does* use the other Tempo plugins, also enable
[TempoROSBridge](tempo-ros-bridge.md), which adapts Tempo's existing services and sensor data onto
ROS topics and services. It ships with Tempo but is **opt-in**.

!!! info "Not using ROS?"

    Then there is nothing to do. TempoROS is a separate repository you never have to clone, and
    `TempoROSBridge.uplugin` sets `"EnabledByDefault": false`, so Unreal leaves the bridge off
    unless your `.uproject` names it — no ROS-dependent modules get built, nothing ROS-related gets
    packaged, and `rclcpp` is never downloaded. TempoSample adds both because it demonstrates them.

!!! warning "Enable exceptions"

    Any module that depends on `TempoROS` or `rclcpp` must set `bEnableExceptions = true;` in its
    `Build.cs`. `rclcpp` uses C++ features Unreal disables by default.

## Compatibility

| | Supported |
|---|---|
| Operating system | Linux (Ubuntu 22.04 and 24.04), macOS 13.0 "Ventura" or newer (Apple silicon only), Windows 10 and 11 |
| Unreal Engine | 5.6, 5.7 and 5.8 |
| ROS 2 | [Humble](https://docs.ros.org/en/humble/index.html) |

Note that TempoROS still supports UE 5.6, which the rest of Tempo no longer does.

## Setup

TempoROS is a separate repository, so add it to your project's `Plugins` folder yourself — beside
Tempo, not inside it:

```bash
cd Plugins
git submodule add https://github.com/tempo-sim/TempoROS.git
```

Then run Tempo's `Setup.sh`. It finds TempoROS wherever it sits under `Plugins`, runs TempoROS's own
`Setup.sh` to install the `rclcpp` dependencies, and keeps them in sync from then on. Running
`Plugins/TempoROS/Setup.sh` directly does the same thing.

Track TempoROS's `main`: that is the only branch Tempo's `main` is tested against.

Using TempoROS **standalone**, without the rest of Tempo, is documented on the TempoROS site (and,
until it is live, in the [TempoROS README](https://github.com/tempo-sim/TempoROS#readme)).

For quick CLI debugging, TempoROS bundles a minimal ROS environment:

```bash
source Plugins/TempoROS/Scripts/ROSEnv.sh
ros2 topic list
ros2 topic echo /TempoSensors/image/color/bp_sensorrig/tempocamera
```
