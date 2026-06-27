# Copyright Tempo Simulation, LLC. All Rights Reserved
#
# Generate Rust API wrappers from protobuf definitions.
# This is the Rust equivalent of gen_api.py for Python.

import google.protobuf.descriptor as gpd
import importlib.util
import jinja2
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

from gen_common import (
    INFRA_PACKAGE,
    classify_modules,
    gather_enums,
    gather_messages,
    gather_services,
    package_import_name,
    pascal_to_snake,
    project_package_name as project_crate_name,
    protobuf_types_to_rust_types,
)


TEMPO_INFRA_CRATE = "tempo_sim"  # Rust ident form of the tempo-sim crate name.


def _crate_prefix_for(target_module_pascal, current_owner, module_owners):
    """Pick the path root for referencing a proto module from inside `current_owner`'s crate.

    Returns "crate" for same-crate refs, the tempo-sim crate name for project→tempo refs.
    A tempo→project ref would mean a Tempo proto imports a project proto, which is a
    misconfiguration (the publishable crate can't depend on project code), so we raise.
    """
    target_owner = module_owners.get(target_module_pascal, "tempo")
    if target_owner == current_owner:
        return "crate"
    if target_owner == "tempo":
        return TEMPO_INFRA_CRATE
    raise RuntimeError(
        f"tempo-sim cannot reference project module {target_module_pascal!r}; "
        "did a Tempo plugin proto import a non-Tempo proto?"
    )


def _proto_type_path(target_module_pascal, type_name, current_owner, module_owners):
    prefix = _crate_prefix_for(target_module_pascal, current_owner, module_owners)
    return f"{prefix}::proto::{pascal_to_snake(target_module_pascal)}::{type_name}"


def get_rust_type(field, current_owner=None, module_owners=None):
    """Get the Rust type for a protobuf field.

    If `current_owner` and `module_owners` are supplied, message-type refs cross
    crate boundaries via TEMPO_INFRA_CRATE when needed; otherwise they resolve to
    same-crate `crate::` paths.
    """
    if field.proto_type == gpd.FieldDescriptor.TYPE_MESSAGE:
        parts = field.field_type.split('.')
        if len(parts) >= 2:
            if current_owner is not None and module_owners is not None:
                return _proto_type_path(parts[0], parts[-1], current_owner, module_owners)
            module = pascal_to_snake(parts[0])
            return f"crate::proto::{module}::{parts[-1]}"
        return field.field_type
    elif field.proto_type == gpd.FieldDescriptor.TYPE_ENUM:
        # In prost, protobuf enums are represented as i32
        return "i32"
    else:
        return protobuf_types_to_rust_types.get(field.proto_type, "unknown")


def needs_option_wrap(field):
    """Determine if a field needs to be wrapped in Some() when constructing the prost struct.

    In prost with proto3:
    - Message types are wrapped in Option<T>
    - Scalars (string, int, bool, etc.) are NOT wrapped in Option - they use default values
    - Repeated fields become Vec<T>, not Option
    """
    if field.label == "repeated":
        return False
    return field.proto_type == gpd.FieldDescriptor.TYPE_MESSAGE


def get_rust_default(field):
    """Get the Rust default value for a protobuf field."""
    if field.label == "repeated":
        return "Vec::new()"
    if field.proto_type == gpd.FieldDescriptor.TYPE_STRING:
        return 'String::new()'
    elif field.proto_type == gpd.FieldDescriptor.TYPE_BYTES:
        return 'Vec::new()'
    elif field.proto_type == gpd.FieldDescriptor.TYPE_BOOL:
        return 'false'
    elif field.proto_type in (gpd.FieldDescriptor.TYPE_DOUBLE, gpd.FieldDescriptor.TYPE_FLOAT):
        return '0.0'
    elif field.proto_type == gpd.FieldDescriptor.TYPE_MESSAGE:
        return 'Default::default()'
    elif field.proto_type == gpd.FieldDescriptor.TYPE_ENUM:
        return '0'  # Enums are i32 in prost
    else:
        return '0'


def snake_to_upper_camel(s):
    """Convert snake_case to UpperCamelCase using prost's heck-style rule.

    Differs from Python's ``str.title()`` for segments containing digits — heck
    only capitalizes the first letter of each underscore-separated segment, so
    ``vector2d_op`` becomes ``Vector2dOp``, not ``Vector2DOp``.
    """
    return ''.join(part[:1].upper() + part[1:].lower() for part in s.split('_'))


def proto_path_to_rust_module(proto_path):
    """Convert a protobuf package path to a Rust module path.

    e.g., "TempoCore.TempoCoreService" -> "tempo_core::TempoCoreService"
    """
    parts = proto_path.split('.')
    if len(parts) >= 2:
        module = pascal_to_snake(parts[0])
        rest = '.'.join(parts[1:])
        return f"{module}::{rest}"
    return proto_path


def generate_rust_api(module_dirs, tempo_root_dir, project_root_dir, module_owners):
    """Generate Rust API wrapper modules from protobuf definitions.

    Args:
        module_dirs: dict mapping module name (PascalCase, e.g. "TempoCore" or "Greeter")
            to (namespace, dir) — `namespace` is the Python package the module's pb2 nest
            under (e.g. "tempo_sim", "tempo_sample"), `dir` is the module's pb2 directory.
        tempo_root_dir: src/ dir of the tempo-sim crate (always written).
        project_root_dir: src/ dir of the project crate, or None if there are no project
            modules (in which case no project crate is emitted).
        module_owners: dict mapping module name to "tempo" or "project" — drives crate
            placement and cross-crate type paths.
    """
    all_enums = {}
    all_messages = {}
    all_services = {}

    # Track which modules have services (and thus need wrapper generation)
    modules_with_services = set()
    # Track all modules that have proto files
    all_proto_modules = set()

    for tempo_module_name, (namespace, module_dir) in module_dirs.items():
        tempo_module_root = str(module_dir)
        for path, _, files in os.walk(tempo_module_root):
            for filename in files:
                if filename.endswith("_pb2.py"):
                    file_path = os.path.join(path, filename)
                    rel_path = os.path.relpath(file_path, tempo_module_root)
                    rel_module = os.path.splitext(rel_path)[0].replace(os.sep, '.')
                    # Descriptor keys stay un-prefixed (TempoCore.Empty_pb2) so the Rust
                    # module derivation (split(".")[0]) sees the PascalCase module name;
                    # the import uses the namespace-qualified path so each nested pb2 is
                    # loaded once under its canonical name (no double registration).
                    module_name = "{}.{}".format(tempo_module_name, rel_module)
                    qualified_name = "{}.{}.{}".format(namespace, tempo_module_name, rel_module)
                    module = importlib.import_module(qualified_name)
                    if hasattr(module, "DESCRIPTOR"):
                        module_descriptor = module.DESCRIPTOR
                        all_proto_modules.add(tempo_module_name)
                        all_enums = all_enums | gather_enums(module_name, module_descriptor)
                        all_messages = all_messages | gather_messages(module_name, module_descriptor)
                        services = gather_services(module_name, module_descriptor)
                        if len(services) > 0:
                            modules_with_services.add(tempo_module_name)
                        all_services = all_services | services

    # Resolve type names
    for tempo_message_descriptor in all_messages.values():
        tempo_message_descriptor.resolve_names(all_messages | all_enums)

    # Jinja2 templates for Rust code generation

    # Shared signature/body fragments. `sig_fields` are the function parameters (one
    # per proto field, oneof members typed Option<T>); `plain_fields` become direct
    # prost struct fields; each `oneof_groups` entry collapses its members into the
    # single Option<Enum> field prost emits for the oneof.
    sig_params_fragment = '''
{%- for field in sig_fields %}
    {{ field.name }}: {{ field.rust_type }},
{%- endfor %}'''

    oneof_build_fragment = '''
{%- for oneof in oneof_groups %}
    let {{ oneof.name }} = {% for variant in oneof.variants %}if let Some(value) = {{ variant.param }} {
        Some({{ oneof.enum_path }}::{{ variant.variant }}(value))
    } else {% endfor %}{
        None
    };
{%- endfor %}'''

    struct_init_fragment = '''
{%- for field in plain_fields %}
{%- if field.needs_option_wrap %}
        {{ field.name }}: {{ field.name }}.into(),
{%- else %}
        {{ field.name }},
{%- endif %}
{%- endfor %}
{%- for oneof in oneof_groups %}
        {{ oneof.name }},
{%- endfor %}'''

    call_args_fragment = '''
{%- for field in sig_fields %}
        {{ field.name }},
{%- endfor %}'''

    # Template for unary RPC (non-streaming)
    unary_rpc_template = '''
/// {{ rpc.name }}
pub async fn {{ name }}_async(''' + sig_params_fragment + '''
) -> Result<{{ response_rust_type }}, {{ infra }}::TempoError> {
    let channel = {{ infra }}::context::connected_channel().await?;
    let mut client = {{ client_type }}::new(channel)
        .max_decoding_message_size({{ infra }}::context::MAX_MESSAGE_SIZE)
        .max_encoding_message_size({{ infra }}::context::MAX_MESSAGE_SIZE);
''' + oneof_build_fragment + '''
    let request = {{ request_rust_type }} {''' + struct_init_fragment + '''
    };

    let response = client.{{ method_name }}(request).await?;
    Ok(response.into_inner())
}

/// Synchronous version of {{ name }}
pub fn {{ name }}(''' + sig_params_fragment + '''
) -> Result<{{ response_rust_type }}, {{ infra }}::TempoError> {
    {{ infra }}::context::RUNTIME.block_on({{ name }}_async(''' + call_args_fragment + '''
    ))
}

'''

    # Template for server-streaming RPC
    streaming_rpc_template = '''
/// {{ rpc.name }} (server streaming)
pub async fn {{ name }}_async(''' + sig_params_fragment + '''
) -> Result<tonic::Streaming<{{ response_rust_type }}>, {{ infra }}::TempoError> {
    let channel = {{ infra }}::context::connected_channel().await?;
    let mut client = {{ client_type }}::new(channel)
        .max_decoding_message_size({{ infra }}::context::MAX_MESSAGE_SIZE)
        .max_encoding_message_size({{ infra }}::context::MAX_MESSAGE_SIZE);
''' + oneof_build_fragment + '''
    let request = {{ request_rust_type }} {''' + struct_init_fragment + '''
    };

    let response = client.{{ method_name }}(request).await?;
    Ok(response.into_inner())
}

/// Synchronous iterator version of {{ name }}
pub fn {{ name }}(''' + sig_params_fragment + '''
) -> Result<impl Iterator<Item = Result<{{ response_rust_type }}, tonic::Status>>, {{ infra }}::TempoError> {
    let stream = {{ infra }}::context::RUNTIME.block_on({{ name }}_async(''' + call_args_fragment + '''
    ))?;

    Ok(SyncStreamIterator::new(stream))
}

'''

    # Template for a Batch struct that wraps a oneof-of-Set*PropertyRequest into
    # a fluent builder. Each method appends an op and returns self; execute /
    # execute_async send one SetProperties RPC for the staged ops.
    batch_template = '''
/// SetProperties batch builder.
///
/// Stages property-set operations and submits them in one SetProperties RPC.
/// Each method appends an op and returns `self` for fluent chaining; call
/// [`Batch::execute`] (sync) or [`Batch::execute_async`] (async) to send.
pub struct Batch {
    ops: Vec<{{ op_type }}>,
}

pub fn batch() -> Batch {
    Batch { ops: Vec::new() }
}

impl Batch {
{%- for method in methods %}
    pub fn {{ method.name }}(
        mut self,
{%- for field in method.fields %}
        {{ field.name }}: {{ field.rust_type }},
{%- endfor %}
    ) -> Self {
        self.ops.push({{ op_type }} {
            op: Some({{ op_enum_path }}::{{ method.variant }}(
                {{ method.request_rust_type }} {
{%- for field in method.fields %}
{%- if field.needs_option_wrap %}
                    {{ field.name }}: {{ field.name }}.into(),
{%- else %}
                    {{ field.name }},
{%- endif %}
{%- endfor %}
                }
            )),
        });
        self
    }

{%- endfor %}

    pub async fn execute_async(
        self,
    ) -> Result<{{ response_rust_type }}, {{ infra }}::TempoError> {
        let channel = {{ infra }}::context::connected_channel().await?;
        let mut client = {{ client_type }}::new(channel)
            .max_decoding_message_size({{ infra }}::context::MAX_MESSAGE_SIZE)
            .max_encoding_message_size({{ infra }}::context::MAX_MESSAGE_SIZE);

        let request = {{ request_rust_type }} { ops: self.ops };
        let response = client.set_properties(request).await?;
        Ok(response.into_inner())
    }

    pub fn execute(
        self,
    ) -> Result<{{ response_rust_type }}, {{ infra }}::TempoError> {
        {{ infra }}::context::RUNTIME.block_on(self.execute_async())
    }
}

'''

    # Template for module file header
    module_header_template = '''// Copyright Tempo Simulation, LLC. All Rights Reserved
//
// Auto-generated by gen_rust_api.py. Do not edit.

//! High-level API wrappers for {{ module_name }}.

{% if has_streaming %}
use {{ infra }}::streaming::SyncStreamIterator;
{% endif %}

'''

    j2_environment = jinja2.Environment()

    # Group services by module first
    services_by_module = {}
    for service_name, tempo_service_descriptor in all_services.items():
        tempo_module_name = tempo_service_descriptor.module_name.split(".")[0]
        if tempo_module_name not in services_by_module:
            services_by_module[tempo_module_name] = []
        services_by_module[tempo_module_name].append(tempo_service_descriptor)

    # Bucket modules by owner so each crate only sees its own proto modules + services.
    tempo_proto_modules = {m for m in all_proto_modules if module_owners.get(m, "tempo") == "tempo"}
    project_proto_modules = {m for m in all_proto_modules if module_owners.get(m) == "project"}

    def _crate_path(owner, target_module_pascal, suffix):
        prefix = _crate_prefix_for(target_module_pascal, owner, module_owners)
        return f"{prefix}::proto::{pascal_to_snake(target_module_pascal)}::{suffix}"

    def _render_owner(owner, owner_root_dir, owner_modules):
        """Render wrapper .rs files plus proto.rs/lib.rs for one crate's owned modules."""
        infra = "crate" if owner == "tempo" else TEMPO_INFRA_CRATE
        generated = set()
        for tempo_module_name, service_descriptors in services_by_module.items():
            if tempo_module_name not in owner_modules:
                continue
            rust_module_name = pascal_to_snake(tempo_module_name)
            output_file = os.path.join(owner_root_dir, f"{rust_module_name}.rs")

            has_streaming = False
            rpc_code = []

            for tempo_service_descriptor in service_descriptors:
                for rpc_descriptor in tempo_service_descriptor.rpcs:
                    if rpc_descriptor.client_streaming:
                        continue

                    tempo_request_descriptor = all_messages[rpc_descriptor.request_type]
                    tempo_response_descriptor = all_messages[rpc_descriptor.response_type]

                    request_module_pascal = tempo_request_descriptor.module_name.split(".")[0]
                    response_module_pascal = tempo_response_descriptor.module_name.split(".")[0]
                    service_module_pascal = tempo_service_descriptor.module_name.split(".")[0]

                    request_rust_type = _crate_path(owner, request_module_pascal,
                                                    tempo_request_descriptor.object_name)
                    response_rust_type = _crate_path(owner, response_module_pascal,
                                                     tempo_response_descriptor.object_name)
                    client_type = _crate_path(
                        owner, service_module_pascal,
                        f"{pascal_to_snake(tempo_service_descriptor.object_name)}_client::"
                        f"{tempo_service_descriptor.object_name}Client",
                    )

                    # Build the function signature, the plain prost struct fields,
                    # and one collapsed Option<Enum> assignment per oneof. prost emits
                    # a oneof as a single field named after the oneof; members become
                    # enum variants, so they're function params (Option<T>) but not
                    # direct struct fields.
                    sig_fields = []
                    plain_fields = []
                    oneof_groups = {}
                    request_module_pascal = tempo_request_descriptor.module_name.split(".")[0]
                    request_msg_snake = pascal_to_snake(tempo_request_descriptor.object_name)
                    for field in tempo_request_descriptor.fields:
                        field.rust_type = get_rust_type(field, owner, module_owners)
                        field.needs_option_wrap = needs_option_wrap(field)
                        if field.label == "repeated":
                            field.rust_type = f"Vec<{field.rust_type}>"
                        if field.containing_oneof is not None:
                            sig_fields.append({
                                "name": field.name,
                                "rust_type": f"Option<{field.rust_type}>",
                            })
                            group = oneof_groups.setdefault(field.containing_oneof, {
                                "name": field.containing_oneof,
                                "enum_path": _crate_path(
                                    owner, request_module_pascal,
                                    f"{request_msg_snake}::"
                                    f"{snake_to_upper_camel(field.containing_oneof)}",
                                ),
                                "variants": [],
                            })
                            group["variants"].append({
                                "param": field.name,
                                "variant": snake_to_upper_camel(field.name),
                            })
                        else:
                            # Message fields are Option<T> in prost, so accept
                            # `impl Into<Option<T>>`: callers can pass a bare `T`,
                            # `Some(T)`, or `None` (e.g. a component with no transform).
                            if field.needs_option_wrap:
                                param_type = f"impl Into<Option<{field.rust_type}>>"
                            else:
                                param_type = field.rust_type
                            sig_fields.append({"name": field.name, "rust_type": param_type})
                            plain_fields.append(field)

                    if rpc_descriptor.server_streaming:
                        has_streaming = True
                        template = j2_environment.from_string(streaming_rpc_template)
                    else:
                        template = j2_environment.from_string(unary_rpc_template)

                    rpc_code.append(template.render(
                        infra=infra,
                        name=pascal_to_snake(rpc_descriptor.name),
                        rpc=rpc_descriptor,
                        request=tempo_request_descriptor,
                        sig_fields=sig_fields,
                        plain_fields=plain_fields,
                        oneof_groups=list(oneof_groups.values()),
                        response=tempo_response_descriptor,
                        request_rust_type=request_rust_type,
                        response_rust_type=response_rust_type,
                        client_type=client_type,
                        method_name=pascal_to_snake(rpc_descriptor.name),
                    ))

            # If this module defines a `SetPropertyOp` message with an `op` oneof and a
            # corresponding `SetProperties` RPC, emit a fluent Batch builder so users
            # don't have to construct SetPropertyOp messages by hand.
            set_property_op_desc = next(
                (m for m in all_messages.values()
                 if m.object_name == "SetPropertyOp"
                 and m.module_name.split(".")[0] == tempo_module_name),
                None,
            )
            owning_service = next(
                (s for s in service_descriptors
                 if any(rpc.name == "SetProperties" for rpc in s.rpcs)),
                None,
            )
            if set_property_op_desc and owning_service and set_property_op_desc.oneofs.get("op"):
                op_module_pascal = set_property_op_desc.module_name.split(".")[0]
                op_module_snake = pascal_to_snake(set_property_op_desc.object_name)  # set_property_op
                op_type = _crate_path(owner, op_module_pascal, "SetPropertyOp")
                op_enum_path = _crate_path(owner, op_module_pascal, f"{op_module_snake}::Op")
                request_rust_type_batch = _crate_path(owner, op_module_pascal, "SetPropertiesRequest")
                response_rust_type_batch = _crate_path(owner, op_module_pascal, "SetPropertiesResponse")
                client_type_batch = _crate_path(
                    owner, op_module_pascal,
                    f"{pascal_to_snake(owning_service.object_name)}_client::"
                    f"{owning_service.object_name}Client",
                )

                methods = []
                for entry in set_property_op_desc.oneofs["op"]:
                    request_desc = all_messages.get(entry["message_proto_full_name"])
                    if request_desc is None:
                        continue
                    method_fields = []
                    for field in request_desc.fields:
                        field.rust_type = get_rust_type(field, owner, module_owners)
                        field.needs_option_wrap = needs_option_wrap(field)
                        if field.label == "repeated":
                            field.rust_type = f"Vec<{field.rust_type}>"
                        elif field.needs_option_wrap:
                            # See the unary-RPC signature note: accept
                            # `impl Into<Option<T>>` so `None` is expressible.
                            field.rust_type = f"impl Into<Option<{field.rust_type}>>"
                        method_fields.append(field)
                    # bool_op -> set_bool_property; int_array_op -> set_int_array_property
                    method_name = "set_{}_property".format(
                        entry["name"][:-3] if entry["name"].endswith("_op") else entry["name"]
                    )
                    methods.append({
                        "name": method_name,
                        "variant": snake_to_upper_camel(entry["name"]),
                        "request_rust_type": _crate_path(owner, op_module_pascal, request_desc.object_name),
                        "fields": method_fields,
                    })

                batch_tpl = j2_environment.from_string(batch_template)
                rpc_code.append(batch_tpl.render(
                    infra=infra,
                    methods=methods,
                    op_type=op_type,
                    op_enum_path=op_enum_path,
                    request_rust_type=request_rust_type_batch,
                    response_rust_type=response_rust_type_batch,
                    client_type=client_type_batch,
                ))

            if rpc_code:
                generated.add(rust_module_name)
                header_template = j2_environment.from_string(module_header_template)
                with open(output_file, 'w') as f:
                    f.write(header_template.render(
                        infra=infra,
                        module_name=tempo_module_name,
                        has_streaming=has_streaming,
                    ))
                    for code in rpc_code:
                        f.write(code)
        return generated

    tempo_generated = _render_owner("tempo", tempo_root_dir, tempo_proto_modules)
    generate_proto_includes(tempo_root_dir, tempo_proto_modules)
    update_lib_rs(tempo_root_dir, tempo_generated)
    # Sweep stale wrappers: anything that's not infra (context/error/streaming),
    # not lib.rs/proto.rs, and not in the current generated set.
    _tempo_keep = {"context.rs", "error.rs", "streaming.rs", "lib.rs", "proto.rs"}
    _tempo_keep |= {f"{m}.rs" for m in tempo_generated}
    for entry in os.listdir(tempo_root_dir):
        full = os.path.join(tempo_root_dir, entry)
        if os.path.isfile(full) and entry.endswith(".rs") and entry not in _tempo_keep:
            os.remove(full)

    if project_root_dir is not None and project_proto_modules:
        # Project crate's src/ is fully generated, so wipe stale wrappers but keep
        # the codegen-populated proto/ subdir (already refreshed by _run_codegen).
        if os.path.isdir(project_root_dir):
            for entry in os.listdir(project_root_dir):
                full = os.path.join(project_root_dir, entry)
                if os.path.isfile(full) and entry.endswith(".rs"):
                    os.remove(full)
        os.makedirs(project_root_dir, exist_ok=True)
        project_generated = _render_owner("project", project_root_dir, project_proto_modules)
        generate_proto_includes(project_root_dir, project_proto_modules)
        update_project_lib_rs(project_root_dir, project_generated)

    return tempo_generated


def generate_proto_includes(rust_root_dir, modules):
    """Generate the proto.rs file that pulls in pre-compiled proto modules.

    The codegen binary writes `<rust_module>.rs` files into `<rust_root_dir>/proto/`
    ahead of time, so consumers don't need protoc at build time.
    """
    proto_rs_path = os.path.join(rust_root_dir, "proto.rs")

    with open(proto_rs_path, 'w') as f:
        f.write("// Copyright Tempo Simulation, LLC. All Rights Reserved\n")
        f.write("//\n")
        f.write("// Auto-generated by gen_rust_api.py. Do not edit.\n\n")
        f.write("//! Generated protobuf modules.\n\n")

        for module in sorted(modules):
            rust_module = pascal_to_snake(module)
            f.write(f"pub mod {rust_module} {{\n")
            f.write(f'    include!("proto/{rust_module}.rs");\n')
            f.write("}\n\n")


def update_lib_rs(rust_root_dir, generated_modules):
    """Update lib.rs with module declarations."""
    lib_rs_path = os.path.join(rust_root_dir, "lib.rs")

    with open(lib_rs_path, 'w') as f:
        f.write('''// Copyright Tempo Simulation, LLC. All Rights Reserved
//
// Auto-generated by gen_rust_api.py. Do not edit.

//! Tempo Simulation Rust Client API
//!
//! This crate provides a Rust client for interacting with Tempo simulation servers
//! via gRPC. It supports both synchronous and asynchronous operation using tokio.
//!
//! # Quick Start
//!
//! ```rust,no_run
//! use tempo_sim::set_server;
//!
//! fn main() {
//!     // Connect to the Tempo server
//!     set_server("localhost", 10001);
//!
//!     // Use the API modules (e.g., tempo_core, tempo_sensors)
//! }
//! ```

pub mod context;
pub mod error;
pub mod proto;
pub mod streaming;

// Re-export commonly used items
pub use context::{set_server, set_server_async, tempo_context, TempoContext};
pub use error::TempoError;
pub use streaming::SyncStreamIterator;

''')

        # Add generated wrapper modules
        for module in sorted(generated_modules):
            f.write(f"pub mod {module};\n")


def update_project_lib_rs(project_root_dir, generated_modules):
    """Write the project crate's lib.rs.

    The project crate has no infra of its own — it re-exports tempo-sim's runtime
    helpers (`set_server`, `TempoError`, ...) so consumers can `use <project>::*`
    without separately depending on `tempo-sim`.
    """
    lib_rs_path = os.path.join(project_root_dir, "lib.rs")
    with open(lib_rs_path, 'w') as f:
        f.write('''// Copyright Tempo Simulation, LLC. All Rights Reserved
//
// Auto-generated by gen_rust_api.py. Do not edit.

//! Project-specific Rust client API.
//!
//! Re-exports the Tempo runtime helpers from `tempo_sim` and adds the
//! project's own service wrappers and proto types.

pub mod proto;

pub use tempo_sim::{set_server, set_server_async, tempo_context, TempoContext, TempoError, SyncStreamIterator};

''')
        for module in sorted(generated_modules):
            f.write(f"pub mod {module};\n")


PROJECT_METADATA_FILE = "crate_info.json"


def read_project_metadata(project_crate_dir: Path) -> dict:
    """Read project-owned publish metadata from crate_info.json, if present.

    This file is the project's hook for filling in the generated Cargo.toml's
    publish fields (license, repository, homepage, ...) WITHOUT editing this
    Tempo-owned script or the generated Cargo.toml — both of which are
    overwritten on every run. Every field is optional; an absent file returns
    {} and the generator falls back to placeholders, preserving prior behavior.
    """
    metadata_path = project_crate_dir / PROJECT_METADATA_FILE
    if not metadata_path.exists():
        return {}
    try:
        data = json.loads(metadata_path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as e:
        raise RuntimeError(f"Invalid JSON in {metadata_path}: {e}") from e
    if not isinstance(data, dict):
        raise RuntimeError(f"{metadata_path} must contain a JSON object, got {type(data).__name__}")
    return data


def _toml_str(value) -> str:
    """Render a Python value as a TOML basic string, escaping backslashes/quotes."""
    return '"' + str(value).replace("\\", "\\\\").replace('"', '\\"') + '"'


def _toml_str_array(values) -> str:
    if not isinstance(values, list):
        raise RuntimeError(f"Expected a JSON array, got {type(values).__name__}: {values!r}")
    return "[" + ", ".join(_toml_str(v) for v in values) + "]"


def write_project_cargo_toml(project_crate_dir: Path, crate_name: str, tempo_crate_dir: Path,
                             tempo_crate_version: str, metadata: dict = None):
    """Write the project crate's Cargo.toml.

    Always overwritten — it's a generated artifact, like lib.rs.

    By default `tempo-sim` is a path+version dependency: the path lets local
    changes propagate without a publish step, and the version lets `cargo
    package` accept it (cargo strips the path on publish and falls back to the
    crates.io version). Set "tempo_sim_source": "registry" in crate_info.json
    to depend on the published crates.io crate only (no local path) — useful
    for building the project crate in isolation or against a released tempo-sim.

    Publish metadata (version, license, repository, homepage, ...) comes from
    `metadata` — the project's crate_info.json (see read_project_metadata).
    The `name`, `edition`, and the rest of the `[dependencies]` block stay
    generator-owned so the tempo-sim version pin is always re-derived;
    everything else is the project's to set. Absent keys fall back to
    placeholders so `cargo build` stays quiet.
    """
    metadata = metadata or {}
    rel_tempo = os.path.relpath(tempo_crate_dir, project_crate_dir)

    # How to depend on tempo-sim: "path" (default) keeps the local path for
    # development and a version pin for publishing; "registry" drops the path
    # and depends on the published crates.io crate only.
    tempo_sim_source = metadata.get("tempo_sim_source", "path")
    if tempo_sim_source == "path":
        tempo_sim_dep = (f"tempo-sim = {{ path = {_toml_str(rel_tempo)}, "
                         f"version = {_toml_str(tempo_crate_version)} }}")
    elif tempo_sim_source == "registry":
        # Default to the local crate's version; let the project pin a different
        # published requirement (e.g. "0.1") via tempo_sim_version.
        registry_version = metadata.get("tempo_sim_version", tempo_crate_version)
        tempo_sim_dep = f"tempo-sim = {_toml_str(registry_version)}"
    else:
        raise RuntimeError(
            f'{PROJECT_METADATA_FILE} "tempo_sim_source" must be "path" or "registry", '
            f'got {tempo_sim_source!r}')

    version = metadata.get("version", "0.1.0")
    rust_version = metadata.get("rust-version", "1.85")
    description = metadata.get(
        "description",
        "Project-specific Rust client API; layers project protos on top of tempo-sim.")

    lines = [
        "# Auto-generated by Tempo's gen_rust_api.py. Do not edit.",
        f"# Publish metadata (license, repository, homepage, ...) comes from {PROJECT_METADATA_FILE}",
        "# in this directory — edit that file, not this one.",
        "",
        "[package]",
        f"name = {_toml_str(crate_name)}",
        f"version = {_toml_str(version)}",
        'edition = "2021"',
        f"rust-version = {_toml_str(rust_version)}",
        f"description = {_toml_str(description)}",
    ]

    if "license" in metadata:
        lines.append(f"license = {_toml_str(metadata['license'])}")
    elif "license-file" in metadata:
        lines.append(f"license-file = {_toml_str(metadata['license-file'])}")
    else:
        lines += [
            f"# TODO: set \"license\" in {PROJECT_METADATA_FILE} before publishing — `Apache-2.0` is a",
            "# placeholder so `cargo build` is quiet; replace it with this project's actual license.",
            'license = "Apache-2.0"',
        ]

    # Optional single-value metadata, emitted only when the project sets it.
    for key in ("repository", "homepage", "documentation", "readme"):
        if key in metadata:
            lines.append(f"{key} = {_toml_str(metadata[key])}")
    for key in ("keywords", "categories"):
        if key in metadata:
            lines.append(f"{key} = {_toml_str_array(metadata[key])}")

    # Explicit include so generated files (which .gitignore excludes) ship in the .crate.
    # Whitelist any project-provided readme/license file so it lands in the .crate too.
    include = ["Cargo.toml", "src/**/*.rs", "proto/**/*.proto"]
    for key in ("readme", "license-file"):
        if key in metadata and metadata[key] not in include:
            include.insert(1, metadata[key])

    lines += [
        "",
        "# Explicit include so generated files (which .gitignore excludes) ship in the .crate.",
        "include = [",
    ]
    lines += [f"    {_toml_str(p)}," for p in include]
    lines += [
        "]",
        "",
        "[dependencies]",
        tempo_sim_dep,
        'tonic = "0.13"',
        'prost = "0.13"',
        'tokio = { version = "1", features = ["rt-multi-thread", "sync", "macros", "time"] }',
        'tokio-stream = "0.1"',
    ]

    cargo_toml = project_crate_dir / "Cargo.toml"
    cargo_toml.write_text("\n".join(lines) + "\n", encoding="utf-8")


def write_project_gitignore(project_crate_dir: Path):
    """Write the project crate's .gitignore. The whole src/ and proto/ trees are
    generated, so they stay out of git; consumers regenerate them locally."""
    (project_crate_dir / ".gitignore").write_text('''# Build artifacts
/target/
Cargo.lock

# Generated proto sources (populated by gen_protos.py)
/proto/

# Tempo proto sources staged here so protoc can resolve project imports.
/tempo_proto_includes/

# Generated Rust sources (populated by gen_rust_api.py)
/src/
''', encoding="utf-8")


def _read_crate_version(cargo_toml: Path) -> str:
    """Pull the package version out of Cargo.toml without taking a TOML dep."""
    text = cargo_toml.read_text(encoding="utf-8")
    match = re.search(r'^\s*version\s*=\s*"([^"]+)"', text, re.MULTILINE)
    if not match:
        raise RuntimeError(f"Could not find package version in {cargo_toml}")
    return match.group(1)


def _collect_rust_inputs(script_dir: str, pb2_roots, rust_crate_dir: Path,
                         codegen_crate_dir: Path):
    """Files whose contents drive Rust wrapper generation and the cargo build."""
    inputs = [
        Path(__file__).resolve(),
        (Path(script_dir) / "gen_common.py").resolve(),
    ]
    # All pb2 modules (across the tempo_sim and project package dirs) feed the
    # wrapper templates.
    for pb2_root in pb2_roots:
        for path, _, files in os.walk(pb2_root):
            for filename in files:
                if filename.endswith("_pb2.py"):
                    inputs.append(Path(path) / filename)
    # Manifest and hand-written + generated sources, and protos.
    cargo_toml = rust_crate_dir / "Cargo.toml"
    if cargo_toml.exists():
        inputs.append(cargo_toml)
    for sub in ("src", "proto"):
        sub_dir = rust_crate_dir / sub
        if not sub_dir.exists():
            continue
        for path, _, files in os.walk(sub_dir):
            for filename in files:
                if filename.endswith((".rs", ".proto")):
                    inputs.append(Path(path) / filename)
    # Codegen tool source: changes here change the generated proto code.
    codegen_cargo = codegen_crate_dir / "Cargo.toml"
    if codegen_cargo.exists():
        inputs.append(codegen_cargo)
    codegen_src = codegen_crate_dir / "src"
    if codegen_src.exists():
        for path, _, files in os.walk(codegen_src):
            for filename in files:
                if filename.endswith(".rs"):
                    inputs.append(Path(path) / filename)
    return inputs


def _collect_project_crate_inputs(project_crate_dir: Path):
    """Files in the project crate whose contents drive cache validity."""
    inputs = []
    # crate_info.json drives the generated Cargo.toml, so a change to it must
    # invalidate the cache even before Cargo.toml is regenerated.
    for fname in ("Cargo.toml", PROJECT_METADATA_FILE):
        path = project_crate_dir / fname
        if path.exists():
            inputs.append(path)
    for sub in ("src", "proto"):
        sub_dir = project_crate_dir / sub
        if not sub_dir.exists():
            continue
        for path, _, files in os.walk(sub_dir):
            for filename in files:
                if filename.endswith((".rs", ".proto")):
                    inputs.append(Path(path) / filename)
    return inputs


def _run_codegen(codegen_crate_dir: Path, proto_dir: Path, out_dir: Path,
                 include_dirs=(), extern_paths=()):
    """Build (if needed) and invoke the codegen binary to pre-compile protos.

    Wipes `out_dir` first so a renamed proto package doesn't leave a stale
    `.rs` file behind.
    """
    if out_dir.exists():
        shutil.rmtree(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)

    # cargo will skip when up-to-date; first run pays the tonic-build compile.
    subprocess.run(
        ["cargo", "build", "--release", "--quiet",
         "--manifest-path", str(codegen_crate_dir / "Cargo.toml")],
        check=True,
    )

    bin_path = codegen_crate_dir / "target" / "release" / "tempo-sim-codegen"
    cmd = [str(bin_path), "--proto-dir", str(proto_dir), "--out-dir", str(out_dir)]
    for inc in include_dirs:
        cmd += ["--include-dir", str(inc)]
    for proto_path, rust_path in extern_paths:
        cmd += ["--extern-path", f"{proto_path}={rust_path}"]
    subprocess.run(cmd, check=True)


if __name__ == "__main__":
    if sys.version_info[0] < 3 or sys.version_info[1] < 9:
        raise Exception("This script requires Python 3.9 or greater (found {}.{}.{})"
                        .format(sys.version_info[0], sys.version_info[1], sys.version_info[2]))

    script_dir = os.path.dirname(os.path.realpath(__file__))
    api_dir = os.path.join(script_dir, "API")
    tempo_sim_pb2_dir = os.path.join(api_dir, INFRA_PACKAGE)
    tempo_crate_dir = Path(script_dir, "..", "Rust", "API").resolve()
    codegen_crate_dir = Path(script_dir, "..", "Rust", "Codegen").resolve()
    tempo_src_dir = str(tempo_crate_dir / "src")
    tempo_proto_out_dir = tempo_crate_dir / "src" / "proto"
    # script_dir is <project>/Plugins/Tempo/TempoCore/Content/Python.
    # tempo_core_dir is the TempoCore plugin dir (where the prebuild cache lives);
    # tempo_root is Plugins/Tempo, the collection of Tempo plugins (TempoCore,
    # TempoSensors, ...) that classify_modules scans, not a plugin itself.
    tempo_core_dir = Path(script_dir).parent.parent.resolve()
    tempo_root = tempo_core_dir.parent.resolve()  # Plugins/Tempo
    project_root = tempo_root.parent.parent.resolve()  # the Unreal project root
    project_py_import = package_import_name(project_crate_name(project_root))
    project_pb2_dir = project_root / "Content" / "Python" / "API" / project_py_import
    project_crate_dir = (project_root / "Content" / "Rust" / "API").resolve()
    project_src_dir = project_crate_dir / "src"
    project_proto_out_dir = project_src_dir / "proto"
    project_proto_src_dir = project_crate_dir / "proto"
    # Sibling of proto/, so protoc -I picks it up via --include-dir without also
    # walking it as a compile source (which would double-define types).
    project_tempo_includes_dir = project_crate_dir / "tempo_proto_includes"

    # Add paths for imports
    sys.path.append(script_dir)
    # Add the API dirs (parents of the package dirs) so the namespace-qualified
    # nested pb2 (tempo_sim.*, <project>.*) and their cross-imports resolve.
    sys.path.append(api_dir)
    if project_pb2_dir.exists():
        sys.path.append(str(project_root / "Content" / "Python" / "API"))

    from prebuild_cache import PrebuildCache

    if not os.path.exists(tempo_src_dir):
        os.makedirs(tempo_src_dir)

    cargo_toml = tempo_crate_dir / "Cargo.toml"
    crate_version = _read_crate_version(cargo_toml)
    crate_artifact = tempo_crate_dir / "target" / "package" / f"tempo-sim-{crate_version}.crate"

    # Discover modules by inspecting the nested pb2 trees: each subdir of a
    # package dir is one module. {module: (namespace, dir)}.
    module_dirs = {}
    for name in sorted(os.listdir(tempo_sim_pb2_dir)):
        d = os.path.join(tempo_sim_pb2_dir, name)
        if os.path.isdir(d) and name != "__pycache__":
            module_dirs[name] = (INFRA_PACKAGE, Path(d))
    if project_pb2_dir.exists():
        for name in sorted(os.listdir(project_pb2_dir)):
            d = project_pb2_dir / name
            if d.is_dir() and name != "__pycache__":
                if name in module_dirs:
                    raise RuntimeError(
                        f"Module name collision: {name!r} exists in both the "
                        f"tempo_sim and project ({project_py_import}) packages. "
                        "Module names must be unique across plugin and project.")
                module_dirs[name] = (project_py_import, d)
    module_names = list(module_dirs)
    module_owners = classify_modules(tempo_root, module_names)
    project_modules_present = any(o == "project" for o in module_owners.values())

    pb2_roots = [tempo_sim_pb2_dir] + ([str(project_pb2_dir)] if project_pb2_dir.exists() else [])

    cache = PrebuildCache(tempo_core_dir / ".tempo_prebuild_cache.json")
    input_files = _collect_rust_inputs(script_dir, pb2_roots, tempo_crate_dir, codegen_crate_dir)
    if project_modules_present:
        input_files += _collect_project_crate_inputs(project_crate_dir)
    output_files = [crate_artifact]

    if cache.is_valid("gen_rust_api", input_files, output_files):
        print("[Tempo Prebuild]  Skipping Rust API generation (no changes detected)", flush=True)
        sys.exit(0)

    print("[Tempo Prebuild] Compiling Tempo protos", flush=True)
    _run_codegen(codegen_crate_dir, tempo_crate_dir / "proto", tempo_proto_out_dir)

    if project_modules_present:
        # Project protos may `import "TempoCore/Foo.proto"`. Stage a copy of the Tempo
        # proto tree into the project crate so protoc can resolve those includes whether
        # tempo-sim is consumed as a path dep or a published crate.
        if project_tempo_includes_dir.exists():
            shutil.rmtree(project_tempo_includes_dir)
        project_tempo_includes_dir.parent.mkdir(parents=True, exist_ok=True)
        shutil.copytree(tempo_crate_dir / "proto", project_tempo_includes_dir)

        tempo_modules_for_extern = sorted(m for m, o in module_owners.items() if o == "tempo")
        extern_paths = [(f".{m}", f"::{TEMPO_INFRA_CRATE}::proto::{pascal_to_snake(m)}")
                        for m in tempo_modules_for_extern]

        print("[Tempo Prebuild] Compiling project protos", flush=True)
        _run_codegen(
            codegen_crate_dir,
            project_proto_src_dir,
            project_proto_out_dir,
            include_dirs=[project_tempo_includes_dir],
            extern_paths=extern_paths,
        )

    print("[Tempo Prebuild] Generating Rust API", flush=True)
    generate_rust_api(
        module_dirs,
        tempo_src_dir,
        str(project_src_dir) if project_modules_present else None,
        module_owners,
    )

    if project_modules_present:
        project_name = project_crate_name(project_root)
        project_metadata = read_project_metadata(project_crate_dir)
        write_project_cargo_toml(project_crate_dir, project_name, tempo_crate_dir, crate_version,
                                 project_metadata)
        write_project_gitignore(project_crate_dir)

    # cargo package's verify step compiles the extracted crate, so this doubles
    # as a build sanity check on the generated code.
    print("[Tempo Prebuild] Packaging Rust crate", flush=True)
    subprocess.run(
        ["cargo", "package", "--allow-dirty", "--quiet",
         "--manifest-path", str(cargo_toml)],
        check=True,
    )

    if project_modules_present:
        # `cargo build` against the local path dep is the sanity check. We don't
        # `cargo package` here because cargo strips `path` from tempo-sim during
        # packaging and then can't resolve the version on crates.io (where it
        # isn't published). Package.sh enumerates ship-files via `cargo package
        # --list --no-verify`, which works without resolving deps.
        print("[Tempo Prebuild] Building project Rust crate", flush=True)
        subprocess.run(
            ["cargo", "build", "--quiet",
             "--manifest-path", str(project_crate_dir / "Cargo.toml")],
            check=True,
        )

    # Re-collect inputs since generation rewrote the wrapper modules.
    input_files = _collect_rust_inputs(script_dir, pb2_roots, tempo_crate_dir, codegen_crate_dir)
    if project_modules_present:
        input_files += _collect_project_crate_inputs(project_crate_dir)
    cache.update("gen_rust_api", input_files, [crate_artifact])

    print(f"[Tempo Prebuild] Rust crate at {crate_artifact}", flush=True)
    if project_modules_present:
        print(f"[Tempo Prebuild] Project Rust crate at {project_crate_dir}", flush=True)
