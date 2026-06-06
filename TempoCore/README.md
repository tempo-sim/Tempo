# TempoCore
TempoCore hosts time control, the gRPC server, and other core utilities most any simulation application will need.

## Time
Tempo supports two time modes: `Wall Clock` and `Fixed Step`.

### Wall Clock Mode
In `Wall Clock` mode, time advances _strictly_ alongside the system clock. We actually override Unreal's engine time to do this, as it does not provide this guarantee. This can lead to large time steps when slow tasks run on the game thread. If you'd like sim time to follow the system clock _except_ when doing so would cause a large time step, you can set the `MaxWallClockTimeStep` setting to non-zero.

### Fixed Step Mode
In `Fixed Step` mode, time advances by a fixed amount, which you can choose, every frame. We express this increment in terms of a whole number of simulated steps per second (like, 10 steps per second), as opposed to a floating point fraction of a second (like, 0.1 seconds per step), because we use a fixed-point representation for time (again, overriding the engine's time) because we want it to be exactly correct (no rounding or floating point errors here).

## Default HUD
<img width="1273" height="638" alt="image" src="https://github.com/user-attachments/assets/41ece0a4-b18a-47c9-bf00-07b5733987b4" />


TempoCore comes with a default HUD allowing the user to control time and other aspects of the simulation. You can use it by adding a `Tempo_HUDAdvanced` to your viewport, for example in your game mode.

<img width="650" height="146" alt="image" src="https://github.com/user-attachments/assets/f9436705-b159-43ab-8538-5bee56981374" />


### Exposed Bindings
<img width="218" height="80" alt="image" src="https://github.com/user-attachments/assets/3ee1ae4d-bc8c-4f03-b79a-3fa1cdaeb67f" />

The bindings widget allows the user to dynamically rebind existing functions to new keys by clicking on the binding you wish to change and pressing the new key. Any _new functions_ that are created and added to the **Project Settings** will automatically be found and populated into the scrollable list.

### Pawn Possession/Creation/Destruction
<img width="188" height="98" alt="image" src="https://github.com/user-attachments/assets/fafe6c78-8341-4087-878a-51898fb730b2" />

The possessable actors widget holds all the possessable actors in the scene. For example in the [TempoSample](https://github.com/tempo-sim/TempoSample) project, we have a spectator pawn, a street sweeper, and several "block bots". Any of those pawns can be possessed by left clicking on them or their entry in the widget. The widget also allows you to create or destroy the block bots via middle click and right click, respectively. When unpossessing a pawn, it will be automatically repossessed by the controller that was previously possessing it.

Additionally, when hovering over a possessable pawn in the GUI, a cursor icon appears that indicates the actor you are hovering over is possessable.

<img width="300" height="200" alt="image" src="https://github.com/user-attachments/assets/398d13ed-f1e4-440c-8a06-bf0453605953" />

>[!Note]
>There are groupings for the pawns themselves. You can use the default bindings of `Possess Next` and `Possess Previous`, but in order to possess pawns of a different group, you must use the `Switch Group` binding. You can modify the groupings by going into `TempoPlayerController_BP` and then **Class Defaults**. The pawns are grouped as so by default with the lowest index having highest priority:

<img width="483" height="192" alt="image" src="https://github.com/user-attachments/assets/f6bff8e0-450f-4c5b-8747-1fa3ada6b0b5" />

Turning on highlight possessed pawn will create a debug point above your current pawn for easy tracking.

### Hiding Elements
<img width="194" height="77" alt="image" src="https://github.com/user-attachments/assets/0296c4bc-eaf4-4d04-bd1d-662a31649898" />

Individual widgets can be hidden by toggling the categories shown in the hover widget above. If you wish to hide all the widgets for a clean display, `Immersive Mode` can be entered by the default binding of `0`.


## Server
Tempo exposes gRPC services defined via [Protobuf](https://protobuf.dev/) and [gRPC](https://grpc.io/).

> [!Tip]
> Check out the [Greeter](https://github.com/tempo-sim/Greeter/) plugin for a bare-bones demonstration of adding and hooking up simple RPCs to the Tempo server.

### Adding Service Support to a Module
Any module can define messages and services to allow external clients to control the editor or game, or stream data out.
To do so, a module must:
- Derive its module rules from `TempoModuleRules` (instead of `ModuleRules`)
- Add `TempoCore` as a dependency

> [!Warning]
> You must **not** add `gRPC` as a direct dependency of your module. `gRPC` is a public dependency of `TempoCore`, and you must receive the dependency through it.

### Defining Messages and Services
You can add proto files anywhere in your module's Public or Private folder and the corresponding C++ and Python code
will be automatically generated in a pre-build step.

> [!Note]
> When you aren't modifying proto definitions (or ROS IDL files, see the TempoROS README for more on that) and you've built at least once, you can skip the code generation pre-build step by setting the `TEMPO_SKIP_PREBUILD` environment variable. Note that you may have to restart your IDE after changing this.

Tempo enforces some conventions on the proto files you write, which enable composability of proto files. Proto files may import others, with appropriate visibility as defined by the module's dependencies graph. That is to say, importing protos in other protos is allowed if:
  - The imported proto file is in the same module (Public or Private folder) as the importing proto file **or**
  - The module with the importing proto file depends on the module with the imported proto file **and**
  - The module with the imported proto file defines it in it's Public folder

Composability necessitates file name and message/service name de-conflicting. The proto's `package` declaration is the wire identity (gRPC method paths, `Any` type URLs) and is what consumers reference by fully-qualified name. The prebuild leaves source-declared packages verbatim; when a proto has no `package` line, the prebuild appends one whose value is the owning module name. So by default, a proto in module `Foo` lands in package `Foo`; if you want a different name (to deconflict against another proto with the same basename in a different module, or to namespace messages within a module), declare the `package` explicitly in the proto file.

To import/include proto files or protobuf-generated code in other proto files or C++ code
you will need to follow the convention
```
ModuleName/RelativePath/FileName.<proto/pb.h>
```
In Python, the generated `_pb2.py` modules are nested under their owning package namespace — `tempo_sim` for Tempo plugin modules, and your project's own package (e.g. `tempo_sample`) for project modules — so the convention is
```
<package>/ModuleName/RelativePath/FileName_pb2.py
```
(for example, `import tempo_sim.TempoCore.Geometry_pb2`).
When you refer to proto messages or services from anywhere outside of the proto file where they are defined you must refer to them
by their fully-qualified name, according to the proto's `package`. So that means:

In proto files:
```
PackageName.MessageName
```
In C++:
```
PackageName::MessageName
```
Note that Python does not have a concept of namespaces, and therefore proto package names have no effect on the generated Python code. In Python messages with the same name are distinguished by their containing file/module when importing.

Here is an example service with a single "simple" RPC:
```
// MyService.proto

syntax = "proto3";

// Protos can import protos from other modules to use their message types.
// import "OtherModule/File.proto";

// Optional: declare a custom package to override the default (the owning module name).
// package MyCustomPackage;

message MyRequest {
  int32 some_request = 1;
}

message MyResponse {
  int32 some_response = 1;
  // OtherPackage.OtherMessage other_message = 2; // Use a type from another module like this
}

service MyService {
  // This is a "simple" RPC. A streaming RPC would return "stream MyResponse".
  rpc MyRPC(MyRequest) returns (MyResponse);
}
```

### Registering and Activating Services
TempoCore hosts a single Tempo gRPC server (`FTempoServer`). To connect your RPCs in C++ you must:
- Implement `ITempoServiceProvider`, specifically its `RegisterServices` method to construct and register handlers for your service
- Call `FTempoServer::ActivateService` when you want your service to be available
- Call `FTempoServer::DeactivateService` when you want your service to be unavailable
For example, we could register handlers and activate/deactivate the above service like this:
```
// MyServiceProviderActor.h

#pragma once

#include "TempoServer.h"
#include "TempoServiceProvider.h"

#include <grpcpp/grpcpp.h>

#include "CoreMinimal.h"

#include "MyServiceProviderActor.generated.h"

namespace MyPackage
{
    class MyRequest;
    class MyResponse;
}

UCLASS()
class MYMODULE_API AMyServiceProviderActor : public AActor, public ITempoServiceProvider
{
	GENERATED_BODY()
	
public:
	virtual void RegisterServices(FTempoServer& Server) override;

        virtual void BeginPlay() override;

        virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void HandleMyRequest(const MyPackage::MyRequest& Request, TResponseDelegate<MyPackage::MyResponse>& ResponseContinuation) const;
};
```
```
// MyServiceProviderActor.cpp

#include "MyServiceProviderActor.h"

#include "MyModule/RelativePath/MyProtoFile.grpc.pb.h"

using MyService = MyPackage::MyService;
using MyServiceAsync = MyPackage::MyService::AsyncService;
using MyRequest = MyPackage::MyRequest;
using MyResponse = MyPackage::MyResponse;

void AMyServiceProviderActor::RegisterServices(FTempoServer& Server)
{
   Server.RegisterService<MyService>(
        // StreamingRequestHandler can construct a handler for streaming RPCs with otherwise-idential syntax.
        SimpleRequestHandler(&MyServiceAsync::RequestMyRPC, &AMyServiceProviderActor::HandleMyRequest)
    );
}

void AMyServiceProviderActor::BeginPlay()
{
   Super::BeginPlay();

   FTempoServer::Get().ActivateService<MyService>(this);
}

void AMyServiceProviderActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
   Super::EndPlay(EndPlayReason);

   FTempoServer::Get().DeactivateService<MyService>();
}

void AMyServiceProviderActor::HandleMyRequest(const MyRequest& Request, TResponseDelegate<MyResponse>& ResponseContinuation) const
{
    // Handle the request, produce the response.
    
    MyResponse Response;
    ResponseContinuation.ExecuteIfBound(Response, grpc::Status_OK);
}
```
You should include a SimpleRequestHandler or StreamingRequestHandler for every RPC in your service. You may not bind multiple handlers to one RPC.

Currently, services can only be registered or activated from UObjects (for example Actors, Components, or Subsystems).

Activating the same service on multiple objects simultaneously will result in an error.

### Using the Python API
Tempo automatically generates both the Python classes for the messages and services in your Protobuf files *and* a Python wrapper library to make 
writing client code extremely easy. Tempo's own modules end up in the publishable `tempo_sim` Python package (PyPI dist name `tempo-sim`, import name `tempo_sim`); your project's own service modules end up in a separate project package (e.g. `tempo_sample`) that depends on `tempo_sim` and re-exports its runtime helpers. Both packages will be automatically installed for you to a 
Python virtual environment located at `<project_root>/TempoEnv`. You can activate that virtual environment with:
```
# On Windows:
source <project_root>/TempoEnv/Scripts/activate
# On Linux or Mac:
source <project_root>/TempoEnv/bin/activate
```
Alternatively, you can install the `tempo-sim` package (and, if your project defines its own services, the project package) to your system frameworks or another virtual environment with:
```
pip install "<project_root>/Plugins/Tempo/TempoCore/Content/Python/API"
# And, for the project package (if present):
pip install "<project_root>/Content/Python/API"
```
The wrapper library adds a Python module for every C++ module in the Tempo project with synchronous and asynchronous wrappers for every RPC 
defined in that module. The request parameters will be laid out flat as arguments to the wrapper. For example, the above service `MyService` would 
result in the following two Python method signatures:
```
import tempo_sim.my_module as t_mm
# Synchronous version
t_mm.my_rpc(some_request=3)
# Asynchronous version
await t_mm.my_rpc(some_request=3)
```
The synchronous and asynchronous wrappers have exactly the same signature. The correct one is deduced automatically based on whether it is called from a synchronous or asynchronous context.

> [!Note]
> The Tempo Python API works in iPython and Jupyter notebooks. However, all code in a Jupyter notebook runs in an `async` context, so you must always use the asynchronous versions of the Tempo methods (e.g. `await t_mm.my_rpc`) in this context.

Note that the service name does not appear anywhere in the signatures (nor does the file name or any optional package name).
Here we are prioritizing brevity of the Python API with a minor restriction in RPC naming: **RPC names must be unique within a project module.**

Phew, simple right? Don't worry - there are plenty of examples of using the Tempo server in `TempoCore` and the rest of the Tempo plugins to help get you started.

### Using the Rust API
Tempo can also generate a Rust client crate alongside the Python package. Generation is **opt-in** via the `TEMPO_GEN_RUST_API` environment variable, since the Rust toolchain is not bundled with Unreal:
```
# On Linux or Mac:
export TEMPO_GEN_RUST_API=1
# On Windows (cmd):
set TEMPO_GEN_RUST_API=1
```
With the variable set, the prebuild step will generate Rust wrappers, build the crate, and produce a packaged `.crate` at `<plugin_root>/TempoCore/Content/Rust/API/target/package/tempo-sim-<version>.crate`.

**System dependencies**: only the Rust toolchain (`cargo` + `rustc` — install via [rustup](https://rustup.rs/)). You do **not** need a system `protoc` — the proto code is pre-compiled into the published crate, so consumers only need `cargo build`. (At prebuild time, generation uses a vendored `protoc` via the `tempo-sim-codegen` helper crate; consumers never see it.)

To consume the crate from another Rust project, depend on it by path or git ref, e.g.:
```toml
[dependencies]
tempo-sim = { path = "<plugin_root>/TempoCore/Content/Rust/API" }
```
The wrapper API mirrors Python: each Tempo module becomes a Rust module with sync and async functions for every RPC. The package name is `tempo-sim` but the Rust import path is `tempo_sim` (cargo translates hyphens to underscores). For example:
```rust
use tempo_sim::{set_server_async, my_module};

#[tokio::main]
async fn main() -> Result<(), tempo_sim::TempoError> {
    set_server_async("localhost", 10001).await;
    let response = my_module::my_rpc_async(3).await?;
    Ok(())
}
```

## Tempo Core Services
Tempo core includes services for managing the lifecycle of a simulation and controlling time.

The Tempo core service includes RPCs for loading levels. There are cases where you may want to load a level and set some properties (learn more about setting properties at runtime in the [TempoWorld](https://github.com/tempo-sim/Tempo/tree/release/TempoWorld) README) on the Actors in that level _before_ the `BeginPlay` event has happened. For example, you may want to set a property that will be used _during_ `BeginPlay`. For this reason, the Tempo core service supports "deferred" level loads, where all of a level's Actors are loaded but BeginPlay` is not called yet. In a Python client, managing the lifecycle of a level could look like this:
```
import tempo_sim.tempo_core as tc

tc.load_level("MyLevel", deferred=True)
# Set some properties on the Actors in MyLevel
tc.finish_loading_level()
# Do some great simulation
# ...
# All done
tc.quit()
```

The Tempo time service includes RPCs for setting the time mode and simulation time step, as well as pausing, stepping, and playing time. In a python client, you could pause time, set the time mode to fixed step, set the simulation time step to 0.1s, and step the simulation in a loop like this:
```
import tempo_sim.tempo_core as tc
import tempo_sim.TempoCore.Time_pb2 as Time

tc.pause()
tc.set_time_mode(Time.TM_FIXED_STEP)
tc.set_sim_steps_per_second(10)
while True:
  tc.step()
  # Do some great simulation
```

## Setting the Server Address
By default a Tempo client (Python program) will try to connect to a Tempo server (Unreal Editor or packaged binary using Tempo) on the same machine (`localhost`), at port 10001.

However, the client and server need not be on the same machine. In python, you can set the server address like this:
```
import tempo_sim
tempo_sim.set_server(address="my_server_ip")

# Or in an async context (including Jupyter notebooks):
await tempo_sim.set_server(address="my_server_ip")
```

You can also run multiple Tempo servers on the same machine by setting a non-default port through the project settings or by specifying the `ServerPort` command line argument when running UnrealEditor or the packaged binary. For example:
```
$UNREAL_ENGINE_PATH/Engine/Binaries/<PLATFORM>/UnrealEditor -ServerPort=10002
```
or 
```
MyGame.sh/.exe/.app -ServerPort=10002
```
The command line value takes precedence over project settings, until project settings are modified during an Editor session.

You can tell a Python client which server to connect to like this:
```
import tempo_sim
tempo_sim.set_server(port=10002)

# Or in an async context (including Jupyter notebooks):
await tempo_sim.set_server(port=10002)
```
