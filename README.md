# Tempo
The Tempo Unreal Engine plugins

## Project Setup
Tempo is a collection of Unreal Engine plugins and is intended to be a submodule of an Unreal project, within the `Plugins` directory. The [TempoSample](https://github.com/tempo-sim/TempoSample) project is provided as a reference for recommended settings, convenient scripts, code examples, and project organization.

The Tempo plugins require some changes to your Unreal Engine project to work properly:
- Your project's `*.Target.cs` files to use the Tempo UnrealBuildTool toolchain for your platform. See [TempoSample.Target.cs](https://github.com/tempo-sim/TempoSample/blob/main/Source/TempoSample.Target.cs) and [TempoSampleEditor.Target.cs](https://github.com/tempo-sim/TempoSample/blob/main/Source/TempoSampleEditor.Target.cs) for examples.
- To package Tempo's Python API along with your project, you must add this line to your `DefaultGame.ini`:
  `+DirectoriesToAlwaysStageAsNonUFS=(Path="../Plugins/Tempo/TempoCore/Content/Python/API")`

## Supported Platforms
- Ubuntu 22.04
- MacOS 13.0 (Ventura) or newer
- Windows 11 Pro

## Prerequisites
You will need the following:
- `jq`
- `curl`
- `pip`
- `python` Version `3.9.0` or greater.
> [!Note]
> On Windows installing Python through the Microsoft Store is recommended since it will also install `pip`, set up the `python` and `python3` aliases correctly, and add everything to your `PATH`. Note that you can install a specific Python version through the Microsoft Store by searching for it.
- A `~/.netrc` file with a valid GitHub Personal Access Token for the TempoThirdParty repo in this format:
```
machine api.github.com
login user # Can be anything. Not used, but must be present.
password <your_token_here>
```
- Unreal Engine 5.4.2
  - Linux users can download a pre-built Unreal [here](https://www.unrealengine.com/en-US/linux)
  - Windows and Mac users should use the Epic Games Launcher
- (optional, only if cross compiling for Linux from Windows) [Linux Cross-Compile Toolchain](https://dev.epicgames.com/documentation/en-us/unreal-engine/linux-development-requirements-for-unreal-engine?application_version=5.4)

## Environment Variables
- `UNREAL_ENGINE_PATH`: Your Unreal Engine installation directory (the folder containing `Engine`)
  - On Mac Epic Games Launcher will install to `/Users/Shared/Epic Games/UE_5.4`
  - On Windows Epic Games Launcher will install to `C:\Program Files\Epic Games\UE_5.4`
  - On Linux you choose where to install. `~/UE_5.4` is recommended.
- (Optional, Windows only) `LINUX_MULTIARCH_ROOT`: The extracted toolchain directory (for example `C:\UnrealToolchains\v22_clang-16.0.6-centos7`)

## Getting Started
### Clone Tempo
From your project's Plugins directory
`git submodule add https://github.com/tempo-sim/Tempo.git`

### One-Time Setup
Run `Setup.sh` (from the `Tempo` root) once to:
- Install the Tempo UnrealBuildTool toolchain
- Install third party dependencies
- Add git hooks to keep both of the above in sync automatically
> [!Note]
> If you run `Setup.sh` again it shouldn't do anything, because it can tell it's already run. You can force it to run if you think you need to with the `-force` flag.

## Using Tempo
### Configuring
Tempo has a number of user-configurable settings. These are stored in config files with an "ini" extension.
On Linux, they can be found under `<packaged_game_root>/Tempo/Saved/Config/<platform>`
The settings are organized in various categories:
- `Game.ini`: Project-specific settings, for example:
```
[/Script/TempoCoreShared.TempoCoreSettings]
SimulatedStepsPerSecond=10
```
- `Engine.ini`: Engine settings, for example:
```
[/Script/Engine.RendererSettings]
r.RayTracing=True
```
- `GameUserSettings.ini`: Settings that are intended to be changed through the UI and then persisted between runs, for example:
```
[/Script/Engine.GameUserSettings]
FullscreenMode=2
```
> [!Warning]
> Config ini files can not be edited while the packaged binary is running or the project is open in Unreal Editor.

### Debugging
Unreal projects write logs while running. These are a great starting point for debugging.
When running in Unreal Editor you can see the logs in the [Output Log](https://dev.epicgames.com/documentation/en-us/unreal-engine/logging-in-unreal-engine) window.
The packaged binary will write logs to `<packaged_game_root>/ProjectName/Saved/Logs`

## Scripting
Tempo supports scripting via [Protobuf](https://protobuf.dev/) and [gRPC](https://grpc.io/).
Any module can define messages and services to allow external clients to control the editor or game.
To do so, a module must:
- Derive its module rules from `TempoModuleRules` (instead of `ModuleRules`)
- Add `TempoScripting` as a dependency

> [!Warning]
> You must **not** add `gRPC` as a direct dependency of your module. `gRPC` is a public dependency of `TempoScripting`, and you must receive the dependency through it.

### Defining Messages and Services
You can add proto files anywhere in your module's Public or Private folder and the corresponding C++ and Python code
will be automatically generated in a pre-build step. Tempo enforces some conventions on the proto files you write, which enable
composable proto files. Proto files may import others, with appropriate visibility as defined by the module
dependency graph. Importing is allowed if:
  - The imported proto file is in the same module (Public or Private folder) as the importing proto file **or**
  - The module with the importing proto file depends on the module with the imported proto file **and**
  - The module with the imported proto file defines it in it's Public folder

Composability necessitates file name and message/service name de-conflicting. Message/service name conflicts within a module
should be resolved by the user by adding a unique `package` to each proto file with a conflicting name. Conflicts between
modules (where two modules define a proto with the same name and relative path) are resolved by adding a `package` with 
the module name to every proto (or prepending it to a user-supplied `package`).

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
Note that there are no namespaces in Python (proto package names have no effect on the generated Python code).
In Python messages with the same name are distinguished by their containing file/module when importing.

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
The `TempoScripting` module hosts two scripting servers. One (the "Engine Scripting Server") is always active, including before play in the Editor, and the other
(the "World Scripting Server") is only active during play in a game world. Based on the functionality you're trying to script, you should register your service
with either one, by implementing either of `ITempoEngineScritable` or `ITempoWorldScriptable`.

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
class MYMODULE_API AMyScriptableActor : public AActor, public ITempoWorldScriptable
{
	GENERATED_BODY()
	
public:
	virtual void RegisterWorldServices(UTempoScriptingServer* ScriptingServer) override;

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

void AMyScriptableActor::RegisterWorldServices(UScriptingServer* ScriptingServer)
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
Here we are prioritizing brevity of the Python API with a minor restriction in RPC naming: **RPC names must be unique within a C++ module.**
