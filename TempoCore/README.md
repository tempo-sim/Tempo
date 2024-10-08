# TempoCore
TempoCore includes the `TempoTime` and `TempoScripting` modules, as well as utilities and other core features most any simulation application will need.

## Time
Tempo supports two time modes: `Real Time` and `Fixed Step`.

### Real Time Mode
In `Real Time` mode, time advances _strictly_ alongside the system clock. We actually override Unreal's engine time to do this, as it does not provide this guarrantee.

### Fixed Step Mode
In `Fixed Step` mode, time advances by a fixed amount, which you can choose, every frame. We express this increment in terms of a whole number of simulated steps per second (like, 10 steps per second), as opposed to a floating point fraction of a second (like, 0.1 seconds per step), because we use a fixed-point representation for time (again, overriding the engine's time) because we want it to be exactly correct (no rounding or floating point errors here).

## Scripting
Tempo supports scripting via [Protobuf](https://protobuf.dev/) and [gRPC](https://grpc.io/).

### Adding Scripting Support to a Module
Any module can define messages and services to allow external clients to control the editor or game, or stream data out.
To do so, a module must:
- Derive its module rules from `TempoModuleRules` (instead of `ModuleRules`)
- Add `TempoScripting` as a dependency

> [!Warning]
> You must **not** add `gRPC` as a direct dependency of your module. `gRPC` is a public dependency of `TempoScripting`, and you must receive the dependency through it.

### Defining Messages and Services
You can add proto files anywhere in your module's Public or Private folder and the corresponding C++ and Python code
will be automatically generated in a pre-build step.

> [!Note]
> When you aren't modifying proto definitions (or ROS IDL files, see the TempoROS README for more on that) and you've built at least once, you can skip the code generation pre-build step by setting the `TEMPO_SKIP_PREBUILD` environment variable. Note that you may have to restart your IDE after changing this.

Tempo enforces some conventions on the proto files you write, which enable composability of proto files. Proto files may import others, with appropriate visibility as defined by the module's dependencies graph. That is to say, importing protos in other protos is allowed if:
  - The imported proto file is in the same module (Public or Private folder) as the importing proto file **or**
  - The module with the importing proto file depends on the module with the imported proto file **and**
  - The module with the imported proto file defines it in it's Public folder

Composability necessitates file name and message/service name de-conflicting. Message/service name conflicts _within_ a module should be resolved by the user by adding a unique `package` to each proto file with a conflicting name. `TempoScripting` automatically resolves conflicts between modules (where two modules define a proto with the same name and module-relative path) by adding a `package` with the module name to every proto (or prepending it to a user-defined `package`).

To import/include proto files or protobuf-generated code in other proto files, C++, or Python code 
you will need to follow the convention
```
ModuleName/RelativePath/FileName.<proto/pb.h/_pb2.py>
```
When you refer to proto messages or services from anywhere outside of the proto file where they are defined you must refer to them
by their fully-qualified name, according to the autogenerated `package`. So that means:

In proto files:
```
ModuleName.OptionalCustomPackage.MessageName
```
In C++:
```
ModuleName::OptionalCustomPackage::MessageName
```
Note that Python does not have a concept of namespaces, and therefore proto package names have no effect on the generated Python code. In Python messages with the same name are distinguished by their containing file/module when importing.

Here is an example service with a single "simple" RPC:
```
// MyService.proto

syntax = "proto3";

// Protos can import protos from other modules to use their message types.
import "OtherModule/File.proto";

// Protos can use optional package names to deconflict duplicated message or service names within a module.
package OptionalCustomPackage;

message MyRequest {
  int32 some_request = 1;
}

message MyResponse {
  // All messages and services will get their module name prepended to their package.
  OtherModule.OtherCustomPackage.OtherMessage other_message = 1;
}

service MyService {
  // This is a "simple" RPC. A streaming RPC would return "stream MyResponse".
  rpc MyRPC(MyRequest) returns (MyResponse);
}
```

### Registering Services
The `TempoScripting` module hosts a single "scripting server". To connect your RPCs in C++ you implement `ITempoScriptable`.
For example, we could register handlers for the above service like this:
```
// MyScriptableActor.h

#include "TempoScriptable.h";

#include <grpcpp/grpcpp.h>

#include "CoreMinimal.h"

#include "TempoTimeServiceSubsystem.generated.h"

namespace MyModule
{
    namespace OptionalCustomPackage
    {
        class MyRequest;
        class MyResponse;
    }
}

UCLASS()
class MYMODULE_API AMyScriptableActor : public AActor, public ITempoScriptable
{
	GENERATED_BODY()
	
public:
	virtual void RegisterScriptingServices(UTempoScriptingServer* ScriptingServer) override;

private:
	grpc::Status Play(const MyModule::OptionalCustomPackage::MyRequest& Request, ResponseContinuationType<MyModule::OptionalCustomPackage::MyResponse>& ResponseContinuation) const;
};
```
```
// MyScriptableActor.cpp

#include "MyScriptableActor.h"

#include "TempoScriptingServer.h

#include "MyModule/RelativePath/MyProtoFile.grpc.pb.h";

using MyService = MyModule::OptionalCustomPackage::MyService::AsyncService;
using MyRequest = MyModule::OptionalCustomPackage::MyRequest;
using MyResponse = MyModule::OptionalCustomPackage::MyResponse;

void AMyScriptableActor::RegisterScriptingServices(UScriptingServer* ScriptingServer)
{
   ScriptingServer->RegisterService<MyService>(
        // TStreamingRequestHandler can handle streaming RPCs with otherwise-idential syntax.
        TSimpleRequestHandler<MyService, MyRequest, MyResponse>(&AMyScriptableActor::RequestMyRPC).BindUObject(this, &AMyScriptableActor::HandleMyRequest)
    );
}

grpc::Status AMyScriptableActor::HandleMyRequest(const MyRequest& Request, ResponseContinuationType<MyResponse>& ResponseContinuation)
{
    // Handle the request, produce the response.
    
    MyResponse Response;
    ResponseContinuation(Response, grpc::Status_OK);
}
```
You should include a TSimpleRequestHandler or TStreamingRequestHandler for every RPC in your service. You may not bind multiple handlers to one RPC.

### Using the Python API
Tempo automatically generates both the Python classes for the messages and services in your Protobuf files *and* a Python wrapper library to make 
writing client code extremely easy. All of this ends up in the `tempo` Python package. This package will be automatically installed for you to a 
Python virtual environment located at `<project_root>/TempoEnv`. You can activate that virtual environment with:
```
# On Windows:
source <project_root>/TempoEnv/Scripts/activate
# On Linux or Mac:
source <project_root>/TempoEnv/bin/activate
```
Alternatively, you can install the `tempo` package to your system frameworks or another virtual environment with:
```
pip install "<project_root>/Plugins/Tempo/TempoCore/Content/Python/API"
```
The `tempo` wrapper library adds a Python module for every C++ module in the Tempo project with synchronous and asynchronous wrappers for every RPC 
defined in that module. The request parameters will be laid out flat as arguments to the wrapper. For example, the above service `MyService` would 
result in the following two Python method signatures:
```
import tempo.my_module as t_mm
# Synchronous version
t_mm.my_rpc(some_request=3)
# Asynchronous version
await t_mm.my_rpc(some_request=3)
```
The synchronous and asynchronous wrappers have exactly the same signature. The correct one is deduced automatically based on whether it is called from a synchronous or
asynchronous context. Note that the service name does not appear anywhere in the signatures (nor does the file name or any optional package name).
Here we are prioritizing brevity of the Python API with a minor restriction in RPC naming: **RPC names must be unique within a project module.**

Phew, simple right? Don't worry - there are plenty of examples of using `TempoScripting` throughout the rest of the Tempo plugins to help get you started. In fact, `TempoTime`'s `TimeService`, which is also in the `TempoCore` plugin, would be a great one to check out.
