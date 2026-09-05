# Adding Your Own Services

Any module in your project can define Protobuf messages and services, letting external clients
control the Editor or the game, or stream data out. Tempo generates the C++ server stubs and the
Python, Rust and C++ client wrappers for you.

!!! tip "Read the finished version first"

    The [Greeter](https://github.com/tempo-sim/Greeter/) plugin — included in TempoSample and
    enabled by default — is the smallest complete example of everything below. It is about 60
    lines of code total. This guide walks through exactly that plugin.

## 1. Make your module a Tempo module

A module that defines services must:

- Derive its module rules from **`TempoModuleRules`** instead of `ModuleRules`
- Add **`TempoCore`** as a dependency

```csharp title="Greeter.Build.cs"
using UnrealBuildTool;

public class Greeter : TempoModuleRules
{
    public Greeter(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "TempoCore",
        });

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "CoreUObject",
            "Engine",
            "Slate",
            "SlateCore",
        });
    }
}
```

!!! danger "Do not depend on gRPC directly"

    You must **not** add `gRPC` as a direct dependency of your module. `gRPC` is a public
    dependency of `TempoCore`, and you must receive it through `TempoCore`.

## 2. Define messages and services

Put `.proto` files anywhere in your module's `Public` or `Private` folder. The corresponding C++
and Python code is generated automatically in a prebuild step.

```proto title="Source/Greeter/Public/GreeterService.proto"
syntax = "proto3";

// Protos can import protos from other modules to use their message types.
// import "TempoCore/Geometry.proto";

// Optional: declare a custom package to override the default (the owning module name).
// package MyCustomPackage;

message GreeterRequest {
  string message = 1;
}

message GreeterResponse {
  string message = 1;
}

service GreeterService {
  // A "simple" RPC. A streaming RPC would return "stream GreeterResponse".
  rpc Greet(GreeterRequest) returns (GreeterResponse);
}
```

### Composability rules

Proto files may import others, with visibility governed by the module dependency graph. Importing
is allowed if:

- The imported proto is in the **same module** (Public or Private) as the importing proto, **or**
- The importing proto's module **depends on** the imported proto's module, **and** the imported
  proto is in that module's **Public** folder.

### Packages

The proto's `package` declaration is the wire identity — gRPC method paths, `Any` type URLs — and
is what consumers reference by fully-qualified name.

The prebuild leaves source-declared packages verbatim. When a proto has **no** `package` line, the
prebuild appends one whose value is the owning **module name**. So by default a proto in module
`Foo` lands in package `Foo`.

Declare `package` explicitly if you want a different name — to deconflict against another proto
with the same basename in a different module, or to namespace messages within a module.

### Include and import paths

To import proto files, or include protobuf-generated code, follow the convention:

```text
ModuleName/RelativePath/FileName.<proto|pb.h>
```

In Python, generated `_pb2.py` modules nest under their owning package namespace — `tempo_sim`
for Tempo plugin modules, your project's own package (e.g. `tempo_sample`) for project modules:

```text
<package>/ModuleName/RelativePath/FileName_pb2.py
```

for example `import tempo_sim.TempoCore.Geometry_pb2`.

When you refer to messages or services from outside the proto file where they are defined, use
their fully-qualified name:

| Context | Form |
|---|---|
| Proto files | `PackageName.MessageName` |
| C++ | `PackageName::MessageName` |
| Python | not applicable — messages are distinguished by the module they're imported from |

## 3. Register and activate the service

TempoCore hosts a single Tempo gRPC server (`FTempoServer`). To connect your RPCs in C++:

- Implement **`ITempoServiceProvider`** — specifically `RegisterServices`, which constructs and
  registers handlers for your service
- Call **`FTempoServer::ActivateService`** when you want the service available
- Call **`FTempoServer::DeactivateService`** when you want it unavailable

```cpp title="GreeterActor.h"
#pragma once

#include "TempoServer.h"
#include "TempoServiceProvider.h"

#include <grpcpp/grpcpp.h>

#include "CoreMinimal.h"

#include "GreeterActor.generated.h"

namespace Greeter
{
    class GreeterRequest;
    class GreeterResponse;
}

UCLASS(Blueprintable)
class GREETER_API AGreeterActor : public AActor, public ITempoServiceProvider
{
    GENERATED_BODY()

public:
    virtual void RegisterServices(FTempoServer& Server) override;

    virtual void BeginPlay() override;

    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

protected:
    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    FString ResponseMessage = TEXT("Hello from server!");

private:
    void HandleGreeterRequest(const Greeter::GreeterRequest& Request,
                              const TResponseDelegate<Greeter::GreeterResponse>& ResponseContinuation) const;
};
```

```cpp title="GreeterActor.cpp"
#include "GreeterActor.h"

#include "Greeter/GreeterService.grpc.pb.h"
#include "Greeter/GreeterService.pb.h"

using GreeterService = Greeter::GreeterService;
using GreeterServiceAsync = Greeter::GreeterService::AsyncService;
using GreeterRequest = Greeter::GreeterRequest;
using GreeterResponse = Greeter::GreeterResponse;

void AGreeterActor::RegisterServices(FTempoServer& Server)
{
    Server.RegisterService<GreeterService>(
        // StreamingRequestHandler constructs a handler for streaming RPCs with
        // otherwise-identical syntax.
        SimpleRequestHandler(&GreeterServiceAsync::RequestGreet, &AGreeterActor::HandleGreeterRequest)
    );
}

void AGreeterActor::BeginPlay()
{
    Super::BeginPlay();

    FTempoServer::Get().ActivateService<GreeterService>(this);
}

void AGreeterActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    Super::EndPlay(EndPlayReason);

    FTempoServer::Get().DeactivateService<GreeterService>();
}

void AGreeterActor::HandleGreeterRequest(const GreeterRequest& Request,
                                         const TResponseDelegate<GreeterResponse>& ResponseContinuation) const
{
    const FString RequestMessage(UTF8_TO_TCHAR(Request.message().c_str()));
    UE_LOG(LogTemp, Warning, TEXT("GreeterActor received message: %s"), *RequestMessage);

    GreeterResponse Response;
    Response.set_message(std::string(TCHAR_TO_UTF8(*ResponseMessage)));
    ResponseContinuation.ExecuteIfBound(Response, grpc::Status_OK);
}
```

Rules to keep in mind:

- Include a `SimpleRequestHandler` or `StreamingRequestHandler` for **every** RPC in your service.
  You may not bind multiple handlers to one RPC.
- Services can currently only be registered or activated from `UObject`s — actors, components, or
  subsystems.
- Activating the same service on multiple objects simultaneously is an error.
- Handlers run on the **game thread**. A handler that must answer later can hold its
  `ResponseContinuation` and invoke it when ready — that is how deferred level loads and sensor
  streams work.

## 4. Build, and call it

```bash
Scripts/Build.sh
source ./TempoEnv/bin/activate
```

Your project package now exists, with a module named after the owning C++ module:

```python
import tempo_sample.greeter as greeter

greeter.greet(message="Hello from the client!")
# -> GreeterResponse(message='Hello from server!')
```

You did nothing to "create" the `tempo_sample` package — it falls out of the build as soon as your
project defines services. In Rust it is `tempo_sample::greeter::greet(...)`, and in C++
`tempo::greeter::greet(...)`.

!!! note "RPC names must be unique within a module"

    The service name does not appear in the generated client function name — that brevity is
    deliberate. See [RPC names](../concepts/naming.md#rpc-names).

## 5. Share it

Your project package can be published to PyPI and crates.io so your users can drive your simulator
without building it:

[:octicons-arrow-right-24: Publishing the Python package](../clients/python.md#publishing-your-own-package) &nbsp;·&nbsp;
[:octicons-arrow-right-24: Publishing the Rust crate](../clients/rust.md#publishing-your-own-crate)

## Bridging to ROS

If your project uses ROS, the same handler can serve a ROS service too — see how the
[TempoROSBridge](../plugins/tempo-ros-bridge.md#extending-the-bridge) modules do it.

## Speeding up iteration

When you aren't modifying proto definitions (or ROS IDL files) and you have built at least once,
set `TEMPO_SKIP_PREBUILD=1` to skip the code generation prebuild step. You may have to restart
your IDE after changing it.
