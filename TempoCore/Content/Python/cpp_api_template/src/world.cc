// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "tempo/world.h"

#include <map>

#include <grpcpp/client_context.h>

#include "tempo/context.h"
#include "tempo/conversion.h"

#include "TempoWorld/WorldControl.grpc.pb.h"

namespace tempo::world {

namespace {

/// The class every asset derives from, which asks the registry for all of them.
constexpr const char* kAnyAsset = "/Script/CoreUObject.Object";

Result<std::unique_ptr<TempoWorld::WorldControlService::Stub>> stub() {
    auto channel = TempoContext::instance().channel();
    if (!channel) return std::move(channel).error();
    return TempoWorld::WorldControlService::NewStub(channel.value());
}

Result<TempoWorld::GetAllActorsResponse> all_actors() {
    auto client = stub();
    if (!client) return std::move(client).error();
    TempoWorld::GetAllActorsResponse response;
    grpc::ClientContext context;
    grpc::Status status = client.value()->GetAllActors(&context, TempoCore::Empty(), &response);
    if (!status.ok()) return TempoError::from_status(status);
    return response;
}

Result<TempoWorld::GetAllActorsResponse> of_class(std::string_view class_path) {
    auto client = stub();
    if (!client) return std::move(client).error();
    TempoWorld::GetActorsOfClassRequest request;
    request.set_actor_type(std::string(class_path));
    request.set_include_subclasses(true);
    TempoWorld::GetAllActorsResponse response;
    grpc::ClientContext context;
    grpc::Status status = client.value()->GetActorsOfClass(&context, request, &response);
    if (!status.ok()) return TempoError::from_status(status);
    return response;
}

/// One entry the sim no longer agrees about.
struct Finding {
    unreal::EntryKind kind;
    std::string name;
    // False when the sim does not have this entry at all, rather than having a different one.
    bool present;
};

/// A few findings by name. Enough to recognise the change; the count carries the scale.
std::string examples(const std::vector<const Finding*>& findings) {
    constexpr size_t kLimit = 5;
    std::string shown;
    for (size_t index = 0; index < findings.size() && index < kLimit; ++index) {
        if (index > 0) shown += ", ";
        shown += std::string(unreal::label(findings[index]->kind)) + " " + findings[index]->name;
    }
    if (findings.size() > kLimit) shown += ", ...";
    return shown;
}

}  // namespace

namespace detail {

Result<std::vector<unreal::ObjectRef>> actors_of_class(std::string_view class_path) {
    auto response = of_class(class_path);
    if (!response) return std::move(response).error();
    // Carrying each actor's real class, not the one that was asked for: the handle is typed as the
    // queried class so the call site checks, and is_a still has to be able to tell the pawns that
    // came back apart.
    std::vector<unreal::ObjectRef> actors;
    for (const auto& descriptor : response.value().actors()) {
        actors.push_back(unreal::ObjectRef::actor(descriptor.name()).with_class(descriptor.class_path()));
    }
    return actors;
}

Result<unreal::ObjectRef> actor_of_class(std::string_view class_path, std::string_view name) {
    // Ask for the class, then look for the name in the answer: one filtered call rather than a
    // description of every actor in the world.
    auto response = of_class(class_path);
    if (!response) return std::move(response).error();
    for (const auto& descriptor : response.value().actors()) {
        if (descriptor.name() == name) {
            return unreal::ObjectRef::actor(descriptor.name()).with_class(descriptor.class_path());
        }
    }
    // Not of that class, or not there at all -- worth distinguishing, because the two mean very
    // different things to whoever wrote the name.
    auto all = all_actors();
    if (!all) return std::move(all).error();
    for (const auto& descriptor : all.value().actors()) {
        if (descriptor.name() == name) {
            return TempoError::protocol("actor \"" + std::string(name) + "\" is " + descriptor.class_path() +
                                        ", which is not " + std::string(class_path));
        }
    }
    return TempoError::protocol("no actor named \"" + std::string(name) + "\"");
}

Result<void> resolve_subsystem(std::string_view class_path) {
    auto client = stub();
    if (!client) return std::move(client).error();
    TempoWorld::GetPropertiesRequest request;
    request.set_native_units(true);
    request.set_subsystem(std::string(class_path));
    TempoWorld::GetPropertiesByNameResponse response;
    grpc::ClientContext context;
    grpc::Status status = client.value()->GetProperties(&context, request, &response);
    if (!status.ok()) return TempoError::from_status(status);
    return {};
}

Result<std::string> spawn(std::string_view class_path, bool deferred,
                          const std::optional<unreal::UnrealTransform>& transform, std::string_view relative_to) {
    auto client = stub();
    if (!client) return std::move(client).error();
    TempoWorld::SpawnActorRequest request;
    request.set_actor_type(std::string(class_path));
    request.set_deferred(deferred);
    // SpawnActor converts: it reads meters, radians and right-handed, unlike the typed property API,
    // which is Unreal-native throughout. So a native-unit transform is converted on the way in.
    *request.mutable_transform() = conversion::unreal_to_si(transform.value_or(unreal::UnrealTransform{}));
    request.set_relative_to_actor(std::string(relative_to));
    TempoWorld::SpawnActorResponse response;
    grpc::ClientContext context;
    grpc::Status status = client.value()->SpawnActor(&context, request, &response);
    if (!status.ok()) return TempoError::from_status(status);
    return response.name();
}

Result<void> finish_spawning(const std::string& name) {
    auto client = stub();
    if (!client) return std::move(client).error();
    TempoWorld::FinishSpawningActorRequest request;
    request.set_actor(name);
    TempoWorld::FinishSpawningActorResponse response;
    grpc::ClientContext context;
    grpc::Status status = client.value()->FinishSpawningActor(&context, request, &response);
    if (!status.ok()) return TempoError::from_status(status);
    return {};
}

Result<void> compare(const TempoWorld::DescribeClassesResponse& reported,
                     const std::vector<std::pair<std::string, std::string>>& reported_assets) {
    using unreal::EntryKind;

    const auto linked = unreal::hierarchies();
    if (linked.empty()) {
        return TempoError::schema_mismatch(
            "no generated classes are linked, so there is nothing to check. Link libtempo_reflected "
            "(the CMake target tempo::reflected), which ships in Packaged/API/Cpp beside libtempo.");
    }

    std::map<std::string_view, std::string_view> classes, structs, enums, assets;
    for (const auto& entry : reported.classes()) classes[entry.class_path()] = entry.content_hash();
    for (const auto& entry : reported.structs()) structs[entry.name()] = entry.content_hash();
    for (const auto& entry : reported.enums()) enums[entry.name()] = entry.content_hash();
    for (const auto& [path, class_path] : reported_assets) assets[path] = class_path;

    std::vector<Finding> findings;
    for (const unreal::Hierarchy* hierarchy : linked) {
        for (auto [kind, current] : {std::pair{EntryKind::Class, &classes}, std::pair{EntryKind::Struct, &structs},
                                     std::pair{EntryKind::Enum, &enums}, std::pair{EntryKind::Asset, &assets}}) {
            for (const auto& [name, linked_hash] : hierarchy->hashes(kind)) {
                auto found = current->find(name);
                // An entry the sim has and the library does not is deliberately not a finding: a
                // library describing part of a build is in its normal condition.
                if (found == current->end()) {
                    findings.push_back(Finding{kind, std::string(name), false});
                } else if (!linked_hash.empty() && found->second != linked_hash) {
                    findings.push_back(Finding{kind, std::string(name), true});
                }
            }
        }
    }
    if (findings.empty()) return {};

    std::vector<const Finding*> changed, absent;
    for (const auto& finding : findings) (finding.present ? changed : absent).push_back(&finding);
    std::string detail;
    if (!changed.empty()) detail += "\n  " + std::to_string(changed.size()) + " changed (" + examples(changed) + ")";
    if (!absent.empty()) {
        detail += "\n  " + std::to_string(absent.size()) + " not in this sim (" + examples(absent) + ")";
    }
    return TempoError::schema_mismatch(
        "the linked generated classes do not match this sim." + detail + "\n  the sim reports: engine " +
        reported.header().engine_version() + ", schema " + reported.header().schema_version() +
        "\nRegenerate the client against this build, or connect to the build the client came from.");
}

}  // namespace detail

Result<unreal::ObjectRef> find_actor(std::string_view name) {
    // One call to learn what it is: a handle that does not know what it points at cannot answer
    // is_a, and this is the place where finding out costs a call that was coming anyway.
    auto all = all_actors();
    if (!all) return std::move(all).error();
    for (const auto& descriptor : all.value().actors()) {
        if (descriptor.name() == name) {
            return unreal::ObjectRef::actor(std::string(name)).with_class(descriptor.class_path());
        }
    }
    return TempoError::protocol("no actor named \"" + std::string(name) + "\"");
}

Result<void> check_schema() {
    auto client = stub();
    if (!client) return std::move(client).error();

    // These three options must match what query_packaged_classes.py asked when the linked classes
    // were stamped. The hashes cover function signatures, so a different request yields different
    // numbers for the same build.
    TempoWorld::DescribeClassesRequest request;
    request.set_include_blueprint_classes(true);
    request.set_blueprint_accessible_only(true);
    request.set_include_functions(true);
    TempoWorld::DescribeClassesResponse reported;
    {
        grpc::ClientContext context;
        grpc::Status status = client.value()->DescribeClasses(&context, request, &reported);
        if (!status.ok()) return TempoError::from_status(status);
    }

    // The sim's assets, only when a linked library has a catalog to compare them against.
    std::vector<std::pair<std::string, std::string>> assets;
    bool has_catalog = false;
    for (const unreal::Hierarchy* hierarchy : unreal::hierarchies()) {
        has_catalog = has_catalog || !hierarchy->hashes(unreal::EntryKind::Asset).empty();
    }
    if (has_catalog) {
        TempoWorld::GetAssetsRequest assets_request;
        assets_request.set_asset_type(kAnyAsset);
        assets_request.set_include_subclasses(true);
        TempoWorld::GetAssetsResponse response;
        grpc::ClientContext context;
        grpc::Status status = client.value()->GetAssets(&context, assets_request, &response);
        if (!status.ok()) return TempoError::from_status(status);
        for (const auto& asset : response.assets()) assets.emplace_back(asset.path(), asset.class_path());
    }
    return detail::compare(reported, assets);
}

}  // namespace tempo::world
