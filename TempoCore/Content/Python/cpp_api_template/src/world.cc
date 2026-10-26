// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "tempo/world.h"

#include <map>

#include <grpcpp/client_context.h>

#include "tempo/context.h"
#include "tempo/tempo_world.h"

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

// The names tempo_world wants, from handles, refusing the wrong kind of handle: a component where an
// actor is wanted is a mistake the server would report less clearly, or not at all.
Result<std::string> actor_name(const unreal::Handle& handle, std::string_view role) {
    const unreal::ObjectRef& object = handle.object();
    if (!object.component_name().empty() || !object.subsystem_class().empty()) {
        return TempoError::protocol(std::string(role) + " must be an actor, not " + object.reference_name());
    }
    return object.actor_name();
}

Result<std::string> optional_actor(const unreal::Handle* handle, std::string_view role) {
    if (handle == nullptr) return std::string();
    return actor_name(*handle, role);
}

Result<std::pair<std::string, std::string>> component_names(const unreal::Handle& handle, std::string_view role) {
    const unreal::ObjectRef& object = handle.object();
    if (object.component_name().empty()) {
        return TempoError::protocol(std::string(role) + " must be a component, not " + object.reference_name());
    }
    return std::pair{object.actor_name(), object.component_name()};
}

Result<std::string> optional_component(const unreal::Handle* handle, std::string_view role) {
    if (handle == nullptr) return std::string();
    auto names = component_names(*handle, role);
    if (!names) return std::move(names).error();
    return names.value().second;
}

template <typename T>
Result<void> discard(Result<T> result) {
    if (!result) return std::move(result).error();
    return {};
}

}  // namespace

namespace detail {

Result<std::vector<unreal::ObjectRef>> components_of_class(const unreal::Handle& actor, std::string_view class_path) {
    auto owner = actor_name(actor, "actor");
    if (!owner) return std::move(owner).error();
    auto response = tempo_world::get_all_components({.actor = owner.value()});
    if (!response) return std::move(response).error();
    std::vector<unreal::ObjectRef> components;
    for (const auto& descriptor : response.value().components()) {
        unreal::ObjectRef object =
            unreal::ObjectRef::component(descriptor.actor(), descriptor.name()).with_class(descriptor.class_path());
        if (object.is_a(class_path)) components.push_back(std::move(object));
    }
    return components;
}

Result<unreal::ObjectRef> add(std::string_view class_path, const unreal::Handle& actor, std::string_view name,
                              const unreal::Handle* parent, const std::optional<Transform>& transform,
                              std::string_view socket) {
    auto owner = actor_name(actor, "actor");
    if (!owner) return std::move(owner).error();
    auto parent_name = optional_component(parent, "parent");
    if (!parent_name) return std::move(parent_name).error();
    auto response = tempo_world::add_component({.component_type = std::string(class_path),
                                                .actor = owner.value(),
                                                .name = std::string(name),
                                                .parent = parent_name.value(),
                                                .transform = transform,
                                                .socket = std::string(socket)});
    if (!response) return std::move(response).error();
    return unreal::ObjectRef::component(owner.value(), response.value().name()).with_class(std::string(class_path));
}

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
    request.set_subsystem(std::string(class_path));
    TempoWorld::GetPropertiesByNameResponse response;
    grpc::ClientContext context;
    grpc::Status status = client.value()->GetProperties(&context, request, &response);
    if (!status.ok()) return TempoError::from_status(status);
    return {};
}

Result<std::string> spawn(std::string_view class_path, bool deferred,
                          const std::optional<Transform>& transform, const unreal::Handle* relative_to_actor) {
    auto relative = optional_actor(relative_to_actor, "relative_to_actor");
    if (!relative) return std::move(relative).error();
    auto client = stub();
    if (!client) return std::move(client).error();
    TempoWorld::SpawnActorRequest request;
    request.set_actor_type(std::string(class_path));
    request.set_deferred(deferred);
    *request.mutable_transform() = transform.value_or(Transform{});
    request.set_relative_to_actor(relative.value());
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

Result<void> destroy_actor(const unreal::Handle& actor) {
    auto name = actor_name(actor, "actor");
    if (!name) return std::move(name).error();
    return discard(tempo_world::destroy_actor({.actor = name.value()}));
}

namespace {

template <typename Call>
Result<void> on_component(const unreal::Handle& component, Call call) {
    auto names = component_names(component, "component");
    if (!names) return std::move(names).error();
    return discard(call(names.value().first, names.value().second));
}

}  // namespace

Result<void> destroy_component(const unreal::Handle& component) {
    return on_component(component, [](const std::string& a, const std::string& c) {
        return tempo_world::destroy_component({.actor = a, .component = c});
    });
}

Result<void> activate_component(const unreal::Handle& component) {
    return on_component(component, [](const std::string& a, const std::string& c) {
        return tempo_world::activate_component({.actor = a, .component = c});
    });
}

Result<void> deactivate_component(const unreal::Handle& component) {
    return on_component(component, [](const std::string& a, const std::string& c) {
        return tempo_world::deactivate_component({.actor = a, .component = c});
    });
}

Result<void> set_actor_transform(const unreal::Handle& actor, const Transform& transform,
                                 const unreal::Handle* relative_to_actor) {
    auto name = actor_name(actor, "actor");
    if (!name) return std::move(name).error();
    auto relative = optional_actor(relative_to_actor, "relative_to_actor");
    if (!relative) return std::move(relative).error();
    return discard(tempo_world::set_actor_transform(
        {.actor = name.value(), .transform = transform, .relative_to_actor = relative.value()}));
}

Result<void> set_actor_location(const unreal::Handle& actor, const Vector& location,
                                const unreal::Handle* relative_to_actor) {
    auto name = actor_name(actor, "actor");
    if (!name) return std::move(name).error();
    auto relative = optional_actor(relative_to_actor, "relative_to_actor");
    if (!relative) return std::move(relative).error();
    return discard(tempo_world::set_actor_location(
        {.actor = name.value(), .location = location, .relative_to_actor = relative.value()}));
}

Result<void> set_actor_rotation(const unreal::Handle& actor, const Rotation& rotation,
                                const unreal::Handle* relative_to_actor) {
    auto name = actor_name(actor, "actor");
    if (!name) return std::move(name).error();
    auto relative = optional_actor(relative_to_actor, "relative_to_actor");
    if (!relative) return std::move(relative).error();
    return discard(tempo_world::set_actor_rotation(
        {.actor = name.value(), .rotation = rotation, .relative_to_actor = relative.value()}));
}

Result<void> set_actor_scale3d(const unreal::Handle& actor, const Vector& scale) {
    auto name = actor_name(actor, "actor");
    if (!name) return std::move(name).error();
    return discard(tempo_world::set_actor_scale3d({.actor = name.value(), .scale = scale}));
}

Result<void> set_component_transform(const unreal::Handle& component, const Transform& transform,
                                     bool relative_to_world) {
    return on_component(component, [&](const std::string& a, const std::string& c) {
        return tempo_world::set_component_transform(
            {.actor = a, .component = c, .transform = transform, .relative_to_world = relative_to_world});
    });
}

Result<void> set_component_location(const unreal::Handle& component, const Vector& location, bool relative_to_world) {
    return on_component(component, [&](const std::string& a, const std::string& c) {
        return tempo_world::set_component_location(
            {.actor = a, .component = c, .location = location, .relative_to_world = relative_to_world});
    });
}

Result<void> set_component_rotation(const unreal::Handle& component, const Rotation& rotation, bool relative_to_world) {
    return on_component(component, [&](const std::string& a, const std::string& c) {
        return tempo_world::set_component_rotation(
            {.actor = a, .component = c, .rotation = rotation, .relative_to_world = relative_to_world});
    });
}

Result<void> set_component_scale3d(const unreal::Handle& component, const Vector& scale, bool relative_to_world) {
    return on_component(component, [&](const std::string& a, const std::string& c) {
        return tempo_world::set_component_scale3d(
            {.actor = a, .component = c, .scale = scale, .relative_to_world = relative_to_world});
    });
}

namespace {

template <typename Response, typename Call>
Result<Response> actor_query(const unreal::Handle& actor, const unreal::Handle* relative_to_actor, Call call) {
    auto name = actor_name(actor, "actor");
    if (!name) return std::move(name).error();
    auto relative = optional_actor(relative_to_actor, "relative_to_actor");
    if (!relative) return std::move(relative).error();
    return call(name.value(), relative.value());
}

template <typename Response, typename Call>
Result<Response> component_query(const unreal::Handle& component, const unreal::Handle* relative_to_component,
                                 Call call) {
    auto names = component_names(component, "component");
    if (!names) return std::move(names).error();
    auto relative = optional_component(relative_to_component, "relative_to_component");
    if (!relative) return std::move(relative).error();
    return call(names.value().first, names.value().second, relative.value());
}

}  // namespace

Result<TempoWorld::GetTransformResponse> get_actor_transform(const unreal::Handle& actor,
                                                             const unreal::Handle* relative_to_actor) {
    return actor_query<TempoWorld::GetTransformResponse>(actor, relative_to_actor,
        [](const std::string& a, const std::string& r) { return tempo_world::get_actor_transform({.actor = a, .relative_to_actor = r}); });
}

Result<TempoWorld::GetLocationResponse> get_actor_location(const unreal::Handle& actor,
                                                           const unreal::Handle* relative_to_actor) {
    return actor_query<TempoWorld::GetLocationResponse>(actor, relative_to_actor,
        [](const std::string& a, const std::string& r) { return tempo_world::get_actor_location({.actor = a, .relative_to_actor = r}); });
}

Result<TempoWorld::GetRotationResponse> get_actor_rotation(const unreal::Handle& actor,
                                                           const unreal::Handle* relative_to_actor) {
    return actor_query<TempoWorld::GetRotationResponse>(actor, relative_to_actor,
        [](const std::string& a, const std::string& r) { return tempo_world::get_actor_rotation({.actor = a, .relative_to_actor = r}); });
}

Result<TempoWorld::GetScale3DResponse> get_actor_scale3d(const unreal::Handle& actor) {
    auto name = actor_name(actor, "actor");
    if (!name) return std::move(name).error();
    return tempo_world::get_actor_scale3d({.actor = name.value()});
}

Result<TempoWorld::GetTransformResponse> get_component_transform(const unreal::Handle& component,
                                                                 TempoWorld::ComponentFrame frame,
                                                                 const unreal::Handle* relative_to_component,
                                                                 std::string_view socket) {
    return component_query<TempoWorld::GetTransformResponse>(component, relative_to_component,
        [&](const std::string& a, const std::string& c, const std::string& r) {
            return tempo_world::get_component_transform({.actor = a,
                                                    .component = c,
                                                    .frame = frame,
                                                    .relative_to_component = r,
                                                    .socket = std::string(socket)});
        });
}

Result<TempoWorld::GetLocationResponse> get_component_location(const unreal::Handle& component,
                                                               TempoWorld::ComponentFrame frame,
                                                               const unreal::Handle* relative_to_component,
                                                               std::string_view socket) {
    return component_query<TempoWorld::GetLocationResponse>(component, relative_to_component,
        [&](const std::string& a, const std::string& c, const std::string& r) {
            return tempo_world::get_component_location({.actor = a,
                                                    .component = c,
                                                    .frame = frame,
                                                    .relative_to_component = r,
                                                    .socket = std::string(socket)});
        });
}

Result<TempoWorld::GetRotationResponse> get_component_rotation(const unreal::Handle& component,
                                                               TempoWorld::ComponentFrame frame,
                                                               const unreal::Handle* relative_to_component,
                                                               std::string_view socket) {
    return component_query<TempoWorld::GetRotationResponse>(component, relative_to_component,
        [&](const std::string& a, const std::string& c, const std::string& r) {
            return tempo_world::get_component_rotation({.actor = a,
                                                    .component = c,
                                                    .frame = frame,
                                                    .relative_to_component = r,
                                                    .socket = std::string(socket)});
        });
}

Result<TempoWorld::GetScale3DResponse> get_component_scale3d(const unreal::Handle& component,
                                                             TempoWorld::ComponentFrame frame,
                                                             const unreal::Handle* relative_to_component,
                                                             std::string_view socket) {
    return component_query<TempoWorld::GetScale3DResponse>(component, relative_to_component,
        [&](const std::string& a, const std::string& c, const std::string& r) {
            return tempo_world::get_component_scale3d({.actor = a,
                                                    .component = c,
                                                    .frame = frame,
                                                    .relative_to_component = r,
                                                    .socket = std::string(socket)});
        });
}

Result<TempoWorld::ActorState> get_current_actor_state(const unreal::Handle& actor, bool include_hidden_components) {
    auto name = actor_name(actor, "actor");
    if (!name) return std::move(name).error();
    return tempo_world::get_current_actor_state(
        {.actor = name.value(), .include_hidden_components = include_hidden_components});
}

Result<ServerStream<TempoWorld::ActorState>> stream_actor_state(const unreal::Handle& actor,
                                                                bool include_hidden_components) {
    auto name = actor_name(actor, "actor");
    if (!name) return std::move(name).error();
    return tempo_world::stream_actor_state(
        {.actor = name.value(), .include_hidden_components = include_hidden_components});
}

Result<TempoWorld::ActorStates> get_current_actor_states_near(const unreal::Handle& near_actor, float search_radius_m,
                                                              bool include_static, bool include_hidden_actors,
                                                              bool include_hidden_components) {
    auto name = actor_name(near_actor, "near_actor");
    if (!name) return std::move(name).error();
    return tempo_world::get_current_actor_states_near({.near_actor = name.value(),
                                                .search_radius_m = search_radius_m,
                                                .include_static = include_static,
                                                .include_hidden_actors = include_hidden_actors,
                                                .include_hidden_components = include_hidden_components});
}

Result<ServerStream<TempoWorld::ActorStates>> stream_actor_states_near(const unreal::Handle& near_actor,
                                                                       float search_radius_m, bool include_static,
                                                                       bool include_hidden_actors,
                                                                       bool include_hidden_components) {
    auto name = actor_name(near_actor, "near_actor");
    if (!name) return std::move(name).error();
    return tempo_world::stream_actor_states_near({.near_actor = name.value(),
                                           .search_radius_m = search_radius_m,
                                           .include_static = include_static,
                                           .include_hidden_actors = include_hidden_actors,
                                           .include_hidden_components = include_hidden_components});
}

Result<ServerStream<TempoWorld::OverlapEventResponse>> stream_overlap_events(const unreal::Handle& actor) {
    auto name = actor_name(actor, "actor");
    if (!name) return std::move(name).error();
    return tempo_world::stream_overlap_events({.actor = name.value()});
}

Result<TempoWorld::RaycastResponse> raycast(const Vector& start, const Vector& end,
                                            TempoWorld::CollisionChannel collision_channel,
                                            const std::vector<const unreal::Handle*>& ignored_actors) {
    std::vector<std::string> ignored;
    for (const unreal::Handle* actor : ignored_actors) {
        if (actor == nullptr) continue;
        auto name = actor_name(*actor, "ignored_actors");
        if (!name) return std::move(name).error();
        ignored.push_back(name.value());
    }
    return tempo_world::raycast(
        {.start = start, .end = end, .collision_channel = collision_channel, .ignored_actors = ignored});
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
