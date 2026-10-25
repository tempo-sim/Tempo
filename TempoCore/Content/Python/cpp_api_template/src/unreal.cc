// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "tempo/unreal.h"

#include <algorithm>
#include <mutex>

#include <grpcpp/client_context.h>

#include "tempo/context.h"

#include "TempoWorld/WorldControl.grpc.pb.h"

namespace tempo::unreal {

namespace {

/// grpc's ABORTED: what RunBatch reports for an op it did not run because an earlier one failed.
constexpr int kAborted = 10;

Result<std::unique_ptr<TempoWorld::WorldControlService::Stub>> stub() {
    auto channel = TempoContext::instance().channel();
    if (!channel) return std::move(channel).error();
    return TempoWorld::WorldControlService::NewStub(channel.value());
}

std::string where_of(const ObjectRef& object, std::string_view member) {
    if (!object.subsystem_class().empty()) return object.subsystem_class() + "." + std::string(member);
    if (object.component_name().empty()) return object.actor_name() + "." + std::string(member);
    return object.actor_name() + ":" + object.component_name() + "." + std::string(member);
}

TempoWorld::GetPropertyRequest get_request(const ObjectRef& object, std::string_view property) {
    TempoWorld::GetPropertyRequest request;
    request.set_actor(object.actor_name());
    request.set_component(object.component_name());
    request.set_property(std::string(property));
    request.set_native_units(true);
    request.set_subsystem(object.subsystem_class());
    return request;
}

TempoWorld::CallFunctionRequest call_request(const ObjectRef& object, std::string_view function,
                                             std::vector<TempoWorld::FunctionArg> args) {
    TempoWorld::CallFunctionRequest request;
    request.set_actor(object.actor_name());
    request.set_component(object.component_name());
    request.set_function(std::string(function));
    for (auto& arg : args) *request.add_args() = std::move(arg);
    request.set_native_units(true);
    request.set_subsystem(object.subsystem_class());
    return request;
}

TempoWorld::Value string_array(TempoWorld::StringArray* (TempoWorld::Value::*field)(),
                               const std::vector<std::string>& values) {
    TempoWorld::Value value;
    TempoWorld::StringArray* array = (value.*field)();
    for (const auto& element : values) array->add_values(element);
    return value;
}

// -- the batch --------------------------------------------------------------------------------------

struct Queued {
    TempoWorld::BatchOp op;
    // What the op was, for the error that names it if it fails.
    std::string description;
    detail::Resolver resolve;
};

// Thread-local because the client is not thread-safe anyway, so a batch opened on one thread has no
// business capturing writes made on another.
thread_local std::optional<std::vector<Queued>> open_batch;
// Set while defer() runs its lambda: the read the lambda makes is queued, and its slot left here
// for defer() to hand back.
thread_local std::optional<std::vector<detail::Captured>> capturing;

Result<void> flush(std::vector<Queued> queued) {
    if (queued.empty()) return {};
    const size_t count = queued.size();

    auto client = stub();
    Result<TempoWorld::RunBatchResponse> response = [&]() -> Result<TempoWorld::RunBatchResponse> {
        if (!client) return std::move(client).error();
        TempoWorld::RunBatchRequest request;
        for (const auto& entry : queued) *request.add_ops() = entry.op;
        request.set_stop_on_failure(true);
        TempoWorld::RunBatchResponse answer;
        grpc::ClientContext context;
        grpc::Status status = client.value()->RunBatch(&context, request, &answer);
        if (!status.ok()) return TempoError::from_status(status);
        return answer;
    }();
    if (!response) {
        for (auto& entry : queued) {
            if (entry.resolve) entry.resolve(TempoError::batch(response.error().what()));
        }
        return std::move(response).error();
    }

    std::optional<std::string> first_failure;
    const auto& results = response.value().results();
    for (size_t index = 0; index < count; ++index) {
        Queued& entry = queued[index];
        Result<TempoWorld::BatchOpResult> outcome = [&]() -> Result<TempoWorld::BatchOpResult> {
            if (index >= static_cast<size_t>(results.size())) {
                return TempoError::protocol("the server answered the batch short");
            }
            const TempoWorld::BatchOpResult& result = results[static_cast<int>(index)];
            if (result.code() == 0) return result;
            std::string error = "batched op " + std::to_string(index) + " of " + std::to_string(count) +
                                " (" + entry.description + ") failed: " + result.error();
            // The op that failed, not the ones the server then declined to run.
            if (!first_failure && result.code() != kAborted) first_failure = error;
            return TempoError::batch(error);
        }();
        if (entry.resolve) entry.resolve(std::move(outcome));
    }
    if (first_failure) return TempoError::batch(*first_failure);
    return {};
}

// -- the registry -------------------------------------------------------------------------------------

std::mutex& registry_mutex() {
    static std::mutex mutex;
    return mutex;
}

std::vector<const Hierarchy*>& registry() {
    static std::vector<const Hierarchy*> registered;
    return registered;
}

}  // namespace

std::string_view label(EntryKind kind) {
    switch (kind) {
        case EntryKind::Class: return "class";
        case EntryKind::Struct: return "struct";
        case EntryKind::Enum: return "enum";
        case EntryKind::Asset: return "asset";
    }
    return "entry";
}

void register_classes(const Hierarchy& hierarchy) {
    std::lock_guard<std::mutex> lock(registry_mutex());
    auto& registered = registry();
    // Every handle made through a library registers it, so this runs constantly for the same table.
    if (std::find(registered.begin(), registered.end(), &hierarchy) == registered.end()) {
        registered.push_back(&hierarchy);
    }
}

std::vector<const Hierarchy*> hierarchies() {
    std::lock_guard<std::mutex> lock(registry_mutex());
    return registry();
}

std::optional<std::string_view> superclass_of(std::string_view class_path) {
    for (const Hierarchy* hierarchy : hierarchies()) {
        if (auto parent = hierarchy->superclass(class_path)) return parent;
    }
    return std::nullopt;
}

// -- ObjectRef ------------------------------------------------------------------------------------------

ObjectRef ObjectRef::actor(std::string name) {
    ObjectRef object;
    object.actor_ = std::move(name);
    return object;
}

ObjectRef ObjectRef::component(std::string actor, std::string component) {
    ObjectRef object;
    object.actor_ = std::move(actor);
    object.component_ = std::move(component);
    return object;
}

ObjectRef ObjectRef::subsystem(std::string class_path) {
    ObjectRef object;
    object.subsystem_ = std::move(class_path);
    return object;
}

ObjectRef ObjectRef::from_reference_name(std::string_view name) {
    const size_t colon = name.find(':');
    if (colon == std::string_view::npos) return actor(std::string(name));
    return component(std::string(name.substr(0, colon)), std::string(name.substr(colon + 1)));
}

ObjectRef ObjectRef::with_class(std::string class_path) const {
    ObjectRef object = *this;
    object.class_path_ = std::move(class_path);
    return object;
}

std::string ObjectRef::reference_name() const {
    return component_.empty() ? actor_ : actor_ + ":" + component_;
}

bool ObjectRef::is_a(std::string_view ancestor) const {
    if (class_path_.empty()) return false;
    std::string_view current = class_path_;
    for (int hops = 0; hops < 128; ++hops) {
        if (current == ancestor) return true;
        auto parent = superclass_of(current);
        if (!parent) return false;
        current = *parent;
    }
    return false;
}

Result<void> ObjectRef::call(std::string_view function, std::vector<TempoWorld::FunctionArg> args) const {
    auto response = call_raw(function, std::move(args));
    if (!response) return std::move(response).error();
    return {};
}

Result<TempoWorld::CallFunctionResponse> ObjectRef::call_raw(
    std::string_view function, std::vector<TempoWorld::FunctionArg> args) const {
    std::string description;
    TempoWorld::BatchOp op = detail::call_op(*this, function, args, &description);
    return detail::perform<TempoWorld::CallFunctionResponse>(
        std::move(op), std::move(description), detail::call_results,
        [&]() { return detail::call_function(*this, function, args); });
}

StructRef ObjectRef::struct_ref(std::string_view property) const {
    return StructRef(*this, std::string(property));
}

Result<void> ObjectRef::set_actor_ref(std::string_view property, const ObjectRef& value) const {
    TempoWorld::Value wire;
    wire.set_actor_value(value.reference_name());
    return detail::write(*this, property, std::move(wire));
}

Result<void> ObjectRef::set_component_ref(std::string_view property, const ObjectRef& value) const {
    TempoWorld::Value wire;
    wire.set_component_value(value.reference_name());
    return detail::write(*this, property, std::move(wire));
}

Result<void> ObjectRef::set_actor_refs(std::string_view property, const std::vector<ObjectRef>& values) const {
    std::vector<std::string> names;
    for (const auto& value : values) names.push_back(value.reference_name());
    return detail::write(*this, property, string_array(&TempoWorld::Value::mutable_actor_array_value, names));
}

Result<void> ObjectRef::set_component_refs(std::string_view property,
                                           const std::vector<ObjectRef>& values) const {
    std::vector<std::string> names;
    for (const auto& value : values) names.push_back(value.reference_name());
    return detail::write(*this, property,
                         string_array(&TempoWorld::Value::mutable_component_array_value, names));
}

TempoWorld::FunctionArg argument_actor(std::string_view name, const Handle& value) {
    TempoWorld::FunctionArg arg;
    arg.set_name(std::string(name));
    arg.mutable_value()->set_actor_value(value.object().reference_name());
    return arg;
}

TempoWorld::FunctionArg argument_component(std::string_view name, const Handle& value) {
    TempoWorld::FunctionArg arg;
    arg.set_name(std::string(name));
    arg.mutable_value()->set_component_value(value.object().reference_name());
    return arg;
}

// -- values -------------------------------------------------------------------------------------------

Result<double> ValueTraits<double>::from(const TempoWorld::Value& value) {
    if (value.value_case() != TempoWorld::Value::kFloatValue) return detail::wrong(value, "a float");
    return static_cast<double>(value.float_value());
}

TempoWorld::Value ValueTraits<double>::to(const double& v) {
    TempoWorld::Value value;
    value.set_float_value(static_cast<float>(v));
    return value;
}

Result<UnrealVector> ValueTraits<UnrealVector>::from(const TempoWorld::Value& value) {
    if (value.value_case() != TempoWorld::Value::kVectorValue) return detail::wrong(value, "a vector");
    const auto& v = value.vector_value();
    return UnrealVector{v.x(), v.y(), v.z()};
}

TempoWorld::Value ValueTraits<UnrealVector>::to(const UnrealVector& v) {
    TempoWorld::Value value;
    auto* out = value.mutable_vector_value();
    out->set_x(v.x);
    out->set_y(v.y);
    out->set_z(v.z);
    return value;
}

Result<UnrealVector2D> ValueTraits<UnrealVector2D>::from(const TempoWorld::Value& value) {
    if (value.value_case() != TempoWorld::Value::kVector2DValue) return detail::wrong(value, "a 2D vector");
    const auto& v = value.vector2d_value();
    return UnrealVector2D{v.x(), v.y()};
}

TempoWorld::Value ValueTraits<UnrealVector2D>::to(const UnrealVector2D& v) {
    TempoWorld::Value value;
    auto* out = value.mutable_vector2d_value();
    out->set_x(v.x);
    out->set_y(v.y);
    return value;
}

Result<UnrealIntVector> ValueTraits<UnrealIntVector>::from(const TempoWorld::Value& value) {
    if (value.value_case() != TempoWorld::Value::kIntVectorValue) return detail::wrong(value, "an int vector");
    const auto& v = value.int_vector_value();
    return UnrealIntVector{v.x(), v.y(), v.z()};
}

TempoWorld::Value ValueTraits<UnrealIntVector>::to(const UnrealIntVector& v) {
    TempoWorld::Value value;
    auto* out = value.mutable_int_vector_value();
    out->set_x(v.x);
    out->set_y(v.y);
    out->set_z(v.z);
    return value;
}

Result<UnrealIntPoint> ValueTraits<UnrealIntPoint>::from(const TempoWorld::Value& value) {
    if (value.value_case() != TempoWorld::Value::kIntPointValue) return detail::wrong(value, "an int point");
    const auto& v = value.int_point_value();
    return UnrealIntPoint{v.x(), v.y()};
}

TempoWorld::Value ValueTraits<UnrealIntPoint>::to(const UnrealIntPoint& v) {
    TempoWorld::Value value;
    auto* out = value.mutable_int_point_value();
    out->set_x(v.x);
    out->set_y(v.y);
    return value;
}

// Carried in a message whose fields are named for radians and right-handed degrees, because that is
// what every other RPC puts there. With native_units set the server does not convert, so these are
// Unreal's own degrees and r/p/y map straight onto roll/pitch/yaw.
Result<UnrealRotator> ValueTraits<UnrealRotator>::from(const TempoWorld::Value& value) {
    if (value.value_case() != TempoWorld::Value::kRotatorValue) return detail::wrong(value, "a rotator");
    const auto& v = value.rotator_value();
    return UnrealRotator{v.r(), v.p(), v.y()};
}

TempoWorld::Value ValueTraits<UnrealRotator>::to(const UnrealRotator& v) {
    TempoWorld::Value value;
    auto* out = value.mutable_rotator_value();
    out->set_r(v.roll);
    out->set_p(v.pitch);
    out->set_y(v.yaw);
    return value;
}

Result<UnrealQuat> ValueTraits<UnrealQuat>::from(const TempoWorld::Value& value) {
    if (value.value_case() != TempoWorld::Value::kQuatValue) return detail::wrong(value, "a quaternion");
    const auto& v = value.quat_value();
    return UnrealQuat{v.x(), v.y(), v.z(), v.w()};
}

TempoWorld::Value ValueTraits<UnrealQuat>::to(const UnrealQuat& v) {
    TempoWorld::Value value;
    auto* out = value.mutable_quat_value();
    out->set_x(static_cast<float>(v.x));
    out->set_y(static_cast<float>(v.y));
    out->set_z(static_cast<float>(v.z));
    out->set_w(static_cast<float>(v.w));
    return value;
}

Result<UnrealTransform> ValueTraits<UnrealTransform>::from(const TempoWorld::Value& value) {
    if (value.value_case() != TempoWorld::Value::kTransformValue) return detail::wrong(value, "a transform");
    const auto& t = value.transform_value();
    return UnrealTransform{UnrealVector{t.location().x(), t.location().y(), t.location().z()},
                           UnrealRotator{t.rotation().r(), t.rotation().p(), t.rotation().y()}};
}

TempoWorld::Value ValueTraits<UnrealTransform>::to(const UnrealTransform& v) {
    TempoWorld::Value value;
    auto* out = value.mutable_transform_value();
    out->mutable_location()->set_x(v.location.x);
    out->mutable_location()->set_y(v.location.y);
    out->mutable_location()->set_z(v.location.z);
    out->mutable_rotation()->set_r(v.rotation.roll);
    out->mutable_rotation()->set_p(v.rotation.pitch);
    out->mutable_rotation()->set_y(v.rotation.yaw);
    return value;
}

Result<UnrealColor> ValueTraits<UnrealColor>::from(const TempoWorld::Value& value) {
    if (value.value_case() != TempoWorld::Value::kColorValue) return detail::wrong(value, "a color");
    const auto& c = value.color_value();
    return UnrealColor{static_cast<std::uint8_t>(c.r()), static_cast<std::uint8_t>(c.g()),
                       static_cast<std::uint8_t>(c.b())};
}

TempoWorld::Value ValueTraits<UnrealColor>::to(const UnrealColor& v) {
    TempoWorld::Value value;
    auto* out = value.mutable_color_value();
    out->set_r(v.r);
    out->set_g(v.g);
    out->set_b(v.b);
    return value;
}

Result<ClassRef> ValueTraits<ClassRef>::from(const TempoWorld::Value& value) {
    if (value.value_case() != TempoWorld::Value::kClassValue) return detail::wrong(value, "a class reference");
    return ClassRef{value.class_value()};
}

TempoWorld::Value ValueTraits<ClassRef>::to(const ClassRef& v) {
    TempoWorld::Value value;
    value.set_class_value(v.path);
    return value;
}

Result<std::vector<bool>> ValueTraits<std::vector<bool>>::from(const TempoWorld::Value& value) {
    switch (value.value_case()) {
        case TempoWorld::Value::kBoolArrayValue:
            return std::vector<bool>(value.bool_array_value().values().begin(),
                                     value.bool_array_value().values().end());
        case TempoWorld::Value::kBoolSetValue:
            return std::vector<bool>(value.bool_set_value().values().begin(),
                                     value.bool_set_value().values().end());
        default: return detail::wrong(value, "a bool array");
    }
}

TempoWorld::Value ValueTraits<std::vector<bool>>::to(const std::vector<bool>& v) {
    TempoWorld::Value value;
    for (bool element : v) value.mutable_bool_array_value()->add_values(element);
    return value;
}

Result<std::vector<std::int32_t>> ValueTraits<std::vector<std::int32_t>>::from(const TempoWorld::Value& value) {
    switch (value.value_case()) {
        case TempoWorld::Value::kIntArrayValue:
            return std::vector<std::int32_t>(value.int_array_value().values().begin(),
                                             value.int_array_value().values().end());
        case TempoWorld::Value::kIntSetValue:
            return std::vector<std::int32_t>(value.int_set_value().values().begin(),
                                             value.int_set_value().values().end());
        default: return detail::wrong(value, "an int array");
    }
}

TempoWorld::Value ValueTraits<std::vector<std::int32_t>>::to(const std::vector<std::int32_t>& v) {
    TempoWorld::Value value;
    for (std::int32_t element : v) value.mutable_int_array_value()->add_values(element);
    return value;
}

Result<std::vector<std::int64_t>> ValueTraits<std::vector<std::int64_t>>::from(const TempoWorld::Value& value) {
    switch (value.value_case()) {
        case TempoWorld::Value::kInt64ArrayValue:
            return std::vector<std::int64_t>(value.int64_array_value().values().begin(),
                                             value.int64_array_value().values().end());
        case TempoWorld::Value::kInt64SetValue:
            return std::vector<std::int64_t>(value.int64_set_value().values().begin(),
                                             value.int64_set_value().values().end());
        default: return detail::wrong(value, "an int64 array");
    }
}

TempoWorld::Value ValueTraits<std::vector<std::int64_t>>::to(const std::vector<std::int64_t>& v) {
    TempoWorld::Value value;
    for (std::int64_t element : v) value.mutable_int64_array_value()->add_values(element);
    return value;
}

Result<std::vector<double>> ValueTraits<std::vector<double>>::from(const TempoWorld::Value& value) {
    const TempoWorld::FloatArray* array = nullptr;
    if (value.value_case() == TempoWorld::Value::kFloatArrayValue) array = &value.float_array_value();
    if (value.value_case() == TempoWorld::Value::kFloatSetValue) array = &value.float_set_value();
    if (array == nullptr) return detail::wrong(value, "a float array");
    return std::vector<double>(array->values().begin(), array->values().end());
}

TempoWorld::Value ValueTraits<std::vector<double>>::to(const std::vector<double>& v) {
    TempoWorld::Value value;
    for (double element : v) value.mutable_float_array_value()->add_values(static_cast<float>(element));
    return value;
}

Result<std::vector<std::string>> ValueTraits<std::vector<std::string>>::from(const TempoWorld::Value& value) {
    const TempoWorld::StringArray* array = nullptr;
    if (value.value_case() == TempoWorld::Value::kStringArrayValue) array = &value.string_array_value();
    if (value.value_case() == TempoWorld::Value::kStringSetValue) array = &value.string_set_value();
    if (array == nullptr) return detail::wrong(value, "a string array");
    return std::vector<std::string>(array->values().begin(), array->values().end());
}

TempoWorld::Value ValueTraits<std::vector<std::string>>::to(const std::vector<std::string>& v) {
    return string_array(&TempoWorld::Value::mutable_string_array_value, v);
}

Result<std::vector<ClassRef>> ValueTraits<std::vector<ClassRef>>::from(const TempoWorld::Value& value) {
    const TempoWorld::StringArray* array = nullptr;
    if (value.value_case() == TempoWorld::Value::kClassArrayValue) array = &value.class_array_value();
    if (value.value_case() == TempoWorld::Value::kClassSetValue) array = &value.class_set_value();
    if (array == nullptr) return detail::wrong(value, "a class array");
    std::vector<ClassRef> classes;
    for (const auto& path : array->values()) classes.push_back(ClassRef{path});
    return classes;
}

TempoWorld::Value ValueTraits<std::vector<ClassRef>>::to(const std::vector<ClassRef>& v) {
    std::vector<std::string> paths;
    for (const auto& element : v) paths.push_back(element.path);
    return string_array(&TempoWorld::Value::mutable_class_array_value, paths);
}

// -- struct values -------------------------------------------------------------------------------------

Result<StructFields> StructFields::of(const TempoWorld::Value& value, std::string_view name) {
    if (value.value_case() != TempoWorld::Value::kStructValue) return detail::wrong(value, "a struct");
    const auto& inner = value.struct_value();
    if (!inner.struct_name().empty() && inner.struct_name() != name) {
        return TempoError::protocol("expected a " + std::string(name) + " from the server, got a " +
                                    inner.struct_name());
    }
    return StructFields(&inner);
}

const TempoWorld::Value* StructFields::find(std::string_view name) const {
    for (const auto& field : value_->fields()) {
        if (field.name() == name && field.has_value()) return &field.value();
    }
    return nullptr;
}

StructBuilder::StructBuilder(std::string_view name) {
    value_.set_struct_name(std::string(name));
}

void StructBuilder::push(std::string_view name, TempoWorld::Value value) {
    TempoWorld::StructField* field = value_.add_fields();
    field->set_name(std::string(name));
    *field->mutable_value() = std::move(value);
}

void StructBuilder::push_actor(std::string_view name, const ObjectRef& object) {
    TempoWorld::Value value;
    value.set_actor_value(object.reference_name());
    push(name, std::move(value));
}

void StructBuilder::push_component(std::string_view name, const ObjectRef& object) {
    TempoWorld::Value value;
    value.set_component_value(object.reference_name());
    push(name, std::move(value));
}

TempoWorld::Value StructBuilder::finish() && {
    TempoWorld::Value value;
    *value.mutable_struct_value() = std::move(value_);
    return value;
}

// -- batching -----------------------------------------------------------------------------------------

Result<void> batch(const std::function<Result<void>()>& body) {
    auto outer = std::exchange(open_batch, std::vector<Queued>());
    Result<void> result = body();
    std::vector<Queued> mine = std::move(open_batch).value_or(std::vector<Queued>());
    open_batch = std::move(outer);
    if (!result) return result;
    return flush(std::move(mine));
}

namespace detail {

TempoError wrong(const TempoWorld::Value& value, std::string_view wanted) {
    const std::string got = value.value_case() == TempoWorld::Value::VALUE_NOT_SET
                                ? "nothing"
                                : "variant " + std::to_string(static_cast<int>(value.value_case()));
    return TempoError::protocol("expected " + std::string(wanted) + " from the server, got " + got);
}

bool batch_open() { return open_batch.has_value(); }

void enqueue(TempoWorld::BatchOp op, std::string description, Resolver resolve) {
    if (open_batch) open_batch->push_back(Queued{std::move(op), std::move(description), std::move(resolve)});
}

bool capture(std::shared_ptr<void> slot, const void* tag) {
    if (!capturing) return false;
    capturing->push_back(Captured{std::move(slot), tag});
    return true;
}

std::optional<std::vector<Captured>> swap_capture(std::optional<std::vector<Captured>> captured) {
    return std::exchange(capturing, std::move(captured));
}

Result<void> flush_open_batch() {
    if (!open_batch) return {};
    return flush(std::exchange(*open_batch, std::vector<Queued>()));
}

TempoWorld::BatchOp get_op(const ObjectRef& object, std::string_view property, std::string* description) {
    TempoWorld::BatchOp op;
    *op.mutable_get() = get_request(object, property);
    *description = "read " + where_of(object, property);
    return op;
}

TempoWorld::BatchOp call_op(const ObjectRef& object, std::string_view function,
                            std::vector<TempoWorld::FunctionArg> args, std::string* description) {
    TempoWorld::BatchOp op;
    *op.mutable_call() = call_request(object, function, std::move(args));
    *description = "call " + where_of(object, function);
    return op;
}

Result<TempoWorld::Value> read_value(const TempoWorld::BatchOpResult& result) {
    if (result.result_case() != TempoWorld::BatchOpResult::kValue) {
        return TempoError::protocol("a batched read came back without a value");
    }
    return result.value();
}

Result<TempoWorld::CallFunctionResponse> call_results(const TempoWorld::BatchOpResult& result) {
    if (result.result_case() != TempoWorld::BatchOpResult::kCall) {
        return TempoError::protocol("a batched call came back without results");
    }
    return result.call();
}

Result<TempoWorld::Value> returned(const TempoWorld::CallFunctionResponse& response) {
    // A Blueprint usually hands back more than one thing, and position would be guesswork, so the
    // return value is the result named "ReturnValue" -- which is what UHT calls it.
    for (const auto& result : response.results()) {
        if (result.name() == "ReturnValue" && result.has_typed_value()) return result.typed_value();
    }
    if (response.results_size() > 0 && response.results(0).has_typed_value()) {
        return response.results(0).typed_value();
    }
    return TempoError::protocol("the function returned no value");
}

Result<TempoWorld::Value> get_property(const ObjectRef& object, std::string_view property) {
    auto client = stub();
    if (!client) return std::move(client).error();
    TempoWorld::Value value;
    grpc::ClientContext context;
    grpc::Status status = client.value()->GetProperty(&context, get_request(object, property), &value);
    if (!status.ok()) return TempoError::from_status(status);
    return value;
}

Result<TempoWorld::CallFunctionResponse> call_function(const ObjectRef& object, std::string_view function,
                                                       std::vector<TempoWorld::FunctionArg> args) {
    auto client = stub();
    if (!client) return std::move(client).error();
    TempoWorld::CallFunctionResponse response;
    grpc::ClientContext context;
    grpc::Status status =
        client.value()->CallFunction(&context, call_request(object, function, std::move(args)), &response);
    if (!status.ok()) return TempoError::from_status(status);
    return response;
}

Result<void> write(const ObjectRef& object, std::string_view property, TempoWorld::Value value) {
    TempoWorld::SetPropertyRequest request;
    request.set_actor(object.actor_name());
    request.set_component(object.component_name());
    request.set_property(std::string(property));
    *request.mutable_value() = std::move(value);
    request.set_native_units(true);
    request.set_subsystem(object.subsystem_class());
    if (batch_open()) {
        TempoWorld::BatchOp op;
        *op.mutable_set() = std::move(request);
        enqueue(std::move(op), "write " + where_of(object, property));
        return {};
    }
    auto client = stub();
    if (!client) return std::move(client).error();
    TempoCore::Empty empty;
    grpc::ClientContext context;
    grpc::Status status = client.value()->SetProperty(&context, request, &empty);
    if (!status.ok()) return TempoError::from_status(status);
    return {};
}

Result<void> write_via(const ObjectRef& object, std::string_view setter, std::string_view argument,
                       TempoWorld::Value value) {
    std::vector<TempoWorld::FunctionArg> args(1);
    args.front().set_name(std::string(argument));
    *args.front().mutable_value() = std::move(value);
    if (batch_open()) {
        std::string description;
        TempoWorld::BatchOp op = call_op(object, setter, std::move(args), &description);
        enqueue(std::move(op), std::move(description));
        return {};
    }
    auto response = call_function(object, setter, std::move(args));
    if (!response) return std::move(response).error();
    return {};
}

Result<ServerStream<TempoWorld::Value>> stream_property(const ObjectRef& object, std::string_view property) {
    auto channel = TempoContext::instance().channel();
    if (!channel) return std::move(channel).error();
    auto client = TempoWorld::WorldControlService::NewStub(channel.value());
    auto context = std::make_unique<grpc::ClientContext>();
    auto reader = client->StreamProperty(context.get(), get_request(object, property));
    return ServerStream<TempoWorld::Value>(channel.value(), std::move(context), std::move(reader));
}

Result<std::string> asset_path(const TempoWorld::Value& value) {
    if (value.value_case() != TempoWorld::Value::kAssetValue) return wrong(value, "an asset reference");
    return value.asset_value();
}

TempoWorld::Value asset_value(const std::string& path) {
    TempoWorld::Value value;
    value.set_asset_value(path);
    return value;
}

Result<std::vector<std::string>> asset_paths(const TempoWorld::Value& value) {
    const TempoWorld::StringArray* array = nullptr;
    if (value.value_case() == TempoWorld::Value::kAssetArrayValue) array = &value.asset_array_value();
    if (value.value_case() == TempoWorld::Value::kAssetSetValue) array = &value.asset_set_value();
    if (array == nullptr) return wrong(value, "an asset array");
    return std::vector<std::string>(array->values().begin(), array->values().end());
}

TempoWorld::Value asset_array_value(const std::vector<std::string>& paths) {
    return string_array(&TempoWorld::Value::mutable_asset_array_value, paths);
}

Result<std::string> enum_name(const TempoWorld::Value& value, std::string_view enum_type) {
    if (value.value_case() != TempoWorld::Value::kEnumValue) {
        return wrong(value, "an enum (" + std::string(enum_type) + ")");
    }
    return value.enum_value();
}

TempoWorld::Value enum_value(std::string_view name) {
    TempoWorld::Value value;
    value.set_enum_value(std::string(name));
    return value;
}

Result<std::vector<std::string>> enum_names(const TempoWorld::Value& value) {
    const TempoWorld::StringArray* array = nullptr;
    if (value.value_case() == TempoWorld::Value::kEnumArrayValue) array = &value.enum_array_value();
    if (value.value_case() == TempoWorld::Value::kEnumSetValue) array = &value.enum_set_value();
    if (array == nullptr) return wrong(value, "an enum array");
    return std::vector<std::string>(array->values().begin(), array->values().end());
}

TempoWorld::Value enum_array_value(const std::vector<std::string>& names) {
    return string_array(&TempoWorld::Value::mutable_enum_array_value, names);
}

Result<ObjectRef> object_reference(const TempoWorld::Value& value) {
    switch (value.value_case()) {
        case TempoWorld::Value::kActorValue: return ObjectRef::actor(value.actor_value());
        case TempoWorld::Value::kComponentValue: return ObjectRef::from_reference_name(value.component_value());
        default: return wrong(value, "an actor or component reference");
    }
}

Result<std::vector<ObjectRef>> object_references(const TempoWorld::Value& value) {
    const TempoWorld::StringArray* array = nullptr;
    switch (value.value_case()) {
        case TempoWorld::Value::kActorArrayValue: array = &value.actor_array_value(); break;
        case TempoWorld::Value::kActorSetValue: array = &value.actor_set_value(); break;
        case TempoWorld::Value::kComponentArrayValue: array = &value.component_array_value(); break;
        case TempoWorld::Value::kComponentSetValue: array = &value.component_set_value(); break;
        default: return wrong(value, "an actor or component array");
    }
    std::vector<ObjectRef> objects;
    for (const auto& name : array->values()) objects.push_back(ObjectRef::from_reference_name(name));
    return objects;
}

Result<std::vector<TempoWorld::StructValue>> struct_values(const TempoWorld::Value& value) {
    if (value.value_case() != TempoWorld::Value::kStructArrayValue) return wrong(value, "an array of structs");
    return std::vector<TempoWorld::StructValue>(value.struct_array_value().values().begin(),
                                                value.struct_array_value().values().end());
}

TempoWorld::Value struct_array_value(std::vector<TempoWorld::Value> values) {
    TempoWorld::Value value;
    for (auto& element : values) {
        if (element.value_case() == TempoWorld::Value::kStructValue) {
            *value.mutable_struct_array_value()->add_values() = std::move(*element.mutable_struct_value());
        }
    }
    return value;
}

}  // namespace detail

}  // namespace tempo::unreal
