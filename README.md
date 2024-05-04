# Tempo
The Tempo Unreal Engine project and plugins
## Prerequisites
You will need the following:
- `jq`
- `curl`
- A `~/.netrc` file with a valid GitHub Personal Access Token for the TempoThirdParty repo in this format:
```
machine api.github.com
login user # Can be anything. Not used, but must be present.
password <your_token_here>
```
## Getting Started
Run `Setup.sh` (from the Tempo root) once to:
- Install the Tempo UnrealBuildTool toolchain
- Install third party dependencies
- Add git hooks to keep both of the above in sync automatically
## Scripting
Tempo supports scripting via [Protobuf](https://protobuf.dev/) and [gRPC](https://grpc.io/).
Any module can define messages and services to allow external clients to control the editor or game.
To do so, a module must:
- Derive its module rules from `TempoModuleRules` (instead of `ModuleRules`)
- Add `TempoScripting` as a dependency

[!IMPORTANT]  
You must **not** add `gRPC` as a direct dependency of your module. `gRPC` is a public dependency of `TempoScripting`, and you must receive the dependency through it.

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
Note that there are no namespaces in Python (proto package names have no effect on the generated Python code),
but de-conflicting of all the above types can still be achieved because Python allows importing down to the class level,
whereas Protobuf and C++ only allow importing/#including down to the file level.

### Registering Services
The `TempoScripting` module hosts two scripting servers. One (the "Engine Scripting Server") is always active, including before play in the Editor, and the other
(the "World Scripting Server") is only active during play in a game world. Based on the functionality you're trying to script, you should register your service
with either one, by implementing either of `ITempoEngineScritable` or `ITempoWorldScriptable`.

For example:
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
You should include a TSimpleRequestHandler or TStreamRequestHandler for every RPC in your service. You may not bind multiple handlers to one RPC.
