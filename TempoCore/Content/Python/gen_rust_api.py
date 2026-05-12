# Copyright Tempo Simulation, LLC. All Rights Reserved
#
# Generate Rust API wrappers from protobuf definitions.
# This is the Rust equivalent of gen_api.py for Python.

import google.protobuf.descriptor as gpd
import importlib.util
import jinja2
import os
import re
import subprocess
import sys
from pathlib import Path

from gen_common import (
    gather_enums,
    gather_messages,
    gather_services,
    pascal_to_snake,
    protobuf_types_to_rust_types,
)


def load_pb2_module(file_path, module_name):
    """Load a pb2 module directly without importing parent packages.

    This avoids triggering imports of dependencies like curio that may not be installed.
    """
    spec = importlib.util.spec_from_file_location(module_name, file_path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[module_name] = module
    spec.loader.exec_module(module)
    return module


def get_rust_type(field):
    """Get the Rust type for a protobuf field."""
    if field.proto_type == gpd.FieldDescriptor.TYPE_MESSAGE:
        # Convert protobuf full name to Rust module path
        # e.g., "TempoCore.Empty" -> "crate::proto::tempo_core::Empty"
        parts = field.field_type.split('.')
        if len(parts) >= 2:
            module = pascal_to_snake(parts[0])
            type_name = parts[-1]
            return f"crate::proto::{module}::{type_name}"
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


def generate_rust_api(python_root_dir, rust_root_dir):
    """Generate Rust API wrapper modules from protobuf definitions."""
    all_enums = {}
    all_messages = {}
    all_services = {}

    # Track which modules have services (and thus need wrapper generation)
    modules_with_services = set()
    # Track all modules that have proto files
    all_proto_modules = set()

    # Find all tempo module directories
    tempo_module_names = [name for name in os.listdir(python_root_dir)
                         if os.path.isdir(os.path.join(python_root_dir, name))]

    for tempo_module_name in tempo_module_names:
        tempo_module_root = os.path.join(python_root_dir, tempo_module_name)
        for path, _, files in os.walk(tempo_module_root):
            for filename in files:
                if filename.endswith("_pb2.py"):
                    file_path = os.path.join(path, filename)
                    rel_path = os.path.relpath(file_path, tempo_module_root)
                    module_name = "{}.{}".format(
                        tempo_module_name, os.path.splitext(rel_path)[0].replace(os.sep, '.'))
                    # Load module directly to avoid triggering parent package imports
                    module = load_pb2_module(file_path, f"tempo.{module_name}")
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

    # Template for unary RPC (non-streaming)
    unary_rpc_template = '''
/// {{ rpc.name }}
pub async fn {{ name }}_async(
{%- for field in request.fields %}
    {{ field.name }}: {{ field.rust_type }},
{%- endfor %}
) -> Result<{{ response_rust_type }}, crate::TempoError> {
    let mut ctx = crate::context::CONTEXT.write().await;
    let channel = ctx.channel().await?;
    let mut client = {{ client_type }}::new(channel)
        .max_decoding_message_size(crate::context::MAX_MESSAGE_SIZE)
        .max_encoding_message_size(crate::context::MAX_MESSAGE_SIZE);

    let request = {{ request_rust_type }} {
{%- for field in request.fields %}
{%- if field.needs_option_wrap %}
        {{ field.name }}: Some({{ field.name }}),
{%- else %}
        {{ field.name }},
{%- endif %}
{%- endfor %}
    };

    let response = client.{{ method_name }}(request).await?;
    Ok(response.into_inner())
}

/// Synchronous version of {{ name }}
pub fn {{ name }}(
{%- for field in request.fields %}
    {{ field.name }}: {{ field.rust_type }},
{%- endfor %}
) -> Result<{{ response_rust_type }}, crate::TempoError> {
    crate::context::RUNTIME.block_on({{ name }}_async(
{%- for field in request.fields %}
        {{ field.name }},
{%- endfor %}
    ))
}

'''

    # Template for server-streaming RPC
    streaming_rpc_template = '''
/// {{ rpc.name }} (server streaming)
pub async fn {{ name }}_async(
{%- for field in request.fields %}
    {{ field.name }}: {{ field.rust_type }},
{%- endfor %}
) -> Result<tonic::Streaming<{{ response_rust_type }}>, crate::TempoError> {
    let mut ctx = crate::context::CONTEXT.write().await;
    let channel = ctx.channel().await?;
    let mut client = {{ client_type }}::new(channel)
        .max_decoding_message_size(crate::context::MAX_MESSAGE_SIZE)
        .max_encoding_message_size(crate::context::MAX_MESSAGE_SIZE);

    let request = {{ request_rust_type }} {
{%- for field in request.fields %}
{%- if field.needs_option_wrap %}
        {{ field.name }}: Some({{ field.name }}),
{%- else %}
        {{ field.name }},
{%- endif %}
{%- endfor %}
    };

    let response = client.{{ method_name }}(request).await?;
    Ok(response.into_inner())
}

/// Synchronous iterator version of {{ name }}
pub fn {{ name }}(
{%- for field in request.fields %}
    {{ field.name }}: {{ field.rust_type }},
{%- endfor %}
) -> Result<impl Iterator<Item = Result<{{ response_rust_type }}, tonic::Status>>, crate::TempoError> {
    let stream = crate::context::RUNTIME.block_on({{ name }}_async(
{%- for field in request.fields %}
        {{ field.name }},
{%- endfor %}
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
                    {{ field.name }}: Some({{ field.name }}),
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
    ) -> Result<{{ response_rust_type }}, crate::TempoError> {
        let mut ctx = crate::context::CONTEXT.write().await;
        let channel = ctx.channel().await?;
        let mut client = {{ client_type }}::new(channel)
            .max_decoding_message_size(crate::context::MAX_MESSAGE_SIZE)
            .max_encoding_message_size(crate::context::MAX_MESSAGE_SIZE);

        let request = {{ request_rust_type }} { ops: self.ops };
        let response = client.set_properties(request).await?;
        Ok(response.into_inner())
    }

    pub fn execute(
        self,
    ) -> Result<{{ response_rust_type }}, crate::TempoError> {
        crate::context::RUNTIME.block_on(self.execute_async())
    }
}

'''

    # Template for module file header
    module_header_template = '''// Copyright Tempo Simulation, LLC. All Rights Reserved
//
// Auto-generated by gen_rust_api.py. Do not edit.

//! High-level API wrappers for {{ module_name }}.

{% if has_streaming %}
use crate::streaming::SyncStreamIterator;
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

    # Generate wrapper module for each module that has services
    generated_modules = set()

    for tempo_module_name, service_descriptors in services_by_module.items():
        rust_module_name = pascal_to_snake(tempo_module_name)
        output_file = os.path.join(rust_root_dir, f"{rust_module_name}.rs")

        # Track imports needed
        imports = set()
        has_streaming = False

        # Collect all RPCs from all services in this module
        rpc_code = []

        for tempo_service_descriptor in service_descriptors:
            for rpc_descriptor in tempo_service_descriptor.rpcs:
                if rpc_descriptor.client_streaming:
                    continue

                tempo_request_descriptor = all_messages[rpc_descriptor.request_type]
                tempo_response_descriptor = all_messages[rpc_descriptor.response_type]

                # Build Rust type paths
                request_module = pascal_to_snake(tempo_request_descriptor.module_name.split(".")[0])
                response_module = pascal_to_snake(tempo_response_descriptor.module_name.split(".")[0])
                service_module = pascal_to_snake(tempo_service_descriptor.module_name.split(".")[0])

                request_rust_type = f"crate::proto::{request_module}::{tempo_request_descriptor.object_name}"
                response_rust_type = f"crate::proto::{response_module}::{tempo_response_descriptor.object_name}"

                # Client type path
                client_type = f"crate::proto::{service_module}::{pascal_to_snake(tempo_service_descriptor.object_name)}_client::{tempo_service_descriptor.object_name}Client"

                # Add Rust type info to fields
                for field in tempo_request_descriptor.fields:
                    field.rust_type = get_rust_type(field)
                    field.needs_option_wrap = needs_option_wrap(field)
                    if field.label == "repeated":
                        field.rust_type = f"Vec<{field.rust_type}>"

                # Choose template based on streaming
                if rpc_descriptor.server_streaming:
                    has_streaming = True
                    template = j2_environment.from_string(streaming_rpc_template)
                else:
                    template = j2_environment.from_string(unary_rpc_template)

                rpc_code.append(template.render(
                    name=pascal_to_snake(rpc_descriptor.name),
                    rpc=rpc_descriptor,
                    request=tempo_request_descriptor,
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
            rust_proto_module = pascal_to_snake(set_property_op_desc.module_name.split(".")[0])
            op_module = pascal_to_snake(set_property_op_desc.object_name)  # set_property_op
            op_type = f"crate::proto::{rust_proto_module}::SetPropertyOp"
            op_enum_path = f"crate::proto::{rust_proto_module}::{op_module}::Op"
            request_rust_type_batch = f"crate::proto::{rust_proto_module}::SetPropertiesRequest"
            response_rust_type_batch = f"crate::proto::{rust_proto_module}::SetPropertiesResponse"
            client_type_batch = (
                f"crate::proto::{rust_proto_module}::"
                f"{pascal_to_snake(owning_service.object_name)}_client::"
                f"{owning_service.object_name}Client"
            )

            methods = []
            for entry in set_property_op_desc.oneofs["op"]:
                request_desc = all_messages.get(entry["message_proto_full_name"])
                if request_desc is None:
                    continue
                method_fields = []
                for field in request_desc.fields:
                    field.rust_type = get_rust_type(field)
                    field.needs_option_wrap = needs_option_wrap(field)
                    if field.label == "repeated":
                        field.rust_type = f"Vec<{field.rust_type}>"
                    method_fields.append(field)
                # bool_op -> set_bool_property; int_array_op -> set_int_array_property
                method_name = "set_{}_property".format(
                    entry["name"][:-3] if entry["name"].endswith("_op") else entry["name"]
                )
                methods.append({
                    "name": method_name,
                    "variant": snake_to_upper_camel(entry["name"]),
                    "request_rust_type": (
                        f"crate::proto::{rust_proto_module}::{request_desc.object_name}"
                    ),
                    "fields": method_fields,
                })

            batch_tpl = j2_environment.from_string(batch_template)
            rpc_code.append(batch_tpl.render(
                methods=methods,
                op_type=op_type,
                op_enum_path=op_enum_path,
                request_rust_type=request_rust_type_batch,
                response_rust_type=response_rust_type_batch,
                client_type=client_type_batch,
            ))

        # Write the module file
        if rpc_code:
            generated_modules.add(rust_module_name)
            header_template = j2_environment.from_string(module_header_template)

            with open(output_file, 'w') as f:
                f.write(header_template.render(
                    module_name=tempo_module_name,
                    imports=sorted(imports),
                    has_streaming=has_streaming,
                ))
                for code in rpc_code:
                    f.write(code)

    # Generate the proto module include file (include all proto modules, not just those with services)
    generate_proto_includes(rust_root_dir, all_proto_modules)

    # Update lib.rs with module declarations
    update_lib_rs(rust_root_dir, generated_modules)

    return generated_modules


def generate_proto_includes(rust_root_dir, modules):
    """Generate the proto.rs file with tonic::include_proto! macros."""
    proto_rs_path = os.path.join(rust_root_dir, "proto.rs")

    with open(proto_rs_path, 'w') as f:
        f.write("// Copyright Tempo Simulation, LLC. All Rights Reserved\n")
        f.write("//\n")
        f.write("// Auto-generated by gen_rust_api.py. Do not edit.\n\n")
        f.write("//! Generated protobuf modules.\n\n")

        for module in sorted(modules):
            rust_module = pascal_to_snake(module)
            # tonic generates modules based on the proto package name
            # The package is "ModuleName" or "ModuleName.SubPackage"
            f.write(f"pub mod {rust_module} {{\n")
            f.write(f'    tonic::include_proto!("{rust_module}");\n')
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


def _read_crate_version(cargo_toml: Path) -> str:
    """Pull the package version out of Cargo.toml without taking a TOML dep."""
    text = cargo_toml.read_text(encoding="utf-8")
    match = re.search(r'^\s*version\s*=\s*"([^"]+)"', text, re.MULTILINE)
    if not match:
        raise RuntimeError(f"Could not find package version in {cargo_toml}")
    return match.group(1)


def _collect_rust_inputs(script_dir: str, python_root_dir: str, rust_crate_dir: Path):
    """Files whose contents drive Rust wrapper generation and the cargo build."""
    inputs = [
        Path(__file__).resolve(),
        (Path(script_dir) / "gen_common.py").resolve(),
    ]
    # All pb2 modules feed the wrapper templates.
    for path, _, files in os.walk(python_root_dir):
        for filename in files:
            if filename.endswith("_pb2.py"):
                inputs.append(Path(path) / filename)
    # Manifest, build script, hand-written + generated sources, and protos.
    for rel in ("Cargo.toml", "build.rs"):
        p = rust_crate_dir / rel
        if p.exists():
            inputs.append(p)
    for sub in ("src", "proto"):
        sub_dir = rust_crate_dir / sub
        if not sub_dir.exists():
            continue
        for path, _, files in os.walk(sub_dir):
            for filename in files:
                if filename.endswith((".rs", ".proto")):
                    inputs.append(Path(path) / filename)
    return inputs


if __name__ == "__main__":
    if sys.version_info[0] < 3 or sys.version_info[1] < 9:
        raise Exception("This script requires Python 3.9 or greater (found {}.{}.{})"
                        .format(sys.version_info[0], sys.version_info[1], sys.version_info[2]))

    script_dir = os.path.dirname(os.path.realpath(__file__))
    python_root_dir = os.path.join(script_dir, "API", "tempo")
    rust_crate_dir = Path(script_dir, "..", "Rust", "API").resolve()
    rust_root_dir = str(rust_crate_dir / "src")
    plugin_root = Path(script_dir).parent.parent.resolve()

    # Add paths for imports
    sys.path.append(script_dir)
    # Add the tempo directory so pb2 imports can find other pb2 files
    sys.path.append(python_root_dir)

    from prebuild_cache import PrebuildCache

    if not os.path.exists(rust_root_dir):
        os.makedirs(rust_root_dir)

    cargo_toml = rust_crate_dir / "Cargo.toml"
    crate_version = _read_crate_version(cargo_toml)
    crate_artifact = rust_crate_dir / "target" / "package" / f"tempo-sim-{crate_version}.crate"

    cache = PrebuildCache(plugin_root / ".tempo_prebuild_cache.json")
    input_files = _collect_rust_inputs(script_dir, python_root_dir, rust_crate_dir)
    output_files = [crate_artifact]

    if cache.is_valid("gen_rust_api", input_files, output_files):
        print("[Tempo Prebuild]  Skipping Rust API generation (no changes detected)", flush=True)
        sys.exit(0)

    print("[Tempo Prebuild] Generating Rust API", flush=True)
    generate_rust_api(python_root_dir, rust_root_dir)

    # cargo package's verify step compiles the extracted crate, so this doubles
    # as a build sanity check on the generated code.
    print("[Tempo Prebuild] Packaging Rust crate", flush=True)
    subprocess.run(
        ["cargo", "package", "--allow-dirty", "--quiet",
         "--manifest-path", str(cargo_toml)],
        check=True,
    )

    # Re-collect inputs since generation rewrote the wrapper modules.
    input_files = _collect_rust_inputs(script_dir, python_root_dir, rust_crate_dir)
    cache.update("gen_rust_api", input_files, [crate_artifact])

    print(f"[Tempo Prebuild] Rust crate at {crate_artifact}", flush=True)
