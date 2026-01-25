# Copyright Tempo Simulation, LLC. All Rights Reserved
#
# Generate Rust API wrappers from protobuf definitions.
# This is the Rust equivalent of gen_api.py for Python.

import google.protobuf.descriptor as gpd
import importlib.util
import jinja2
import os
import sys

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
        # e.g., "TempoScripting.Empty" -> "crate::proto::tempo_scripting::Empty"
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
    let mut client = {{ client_type }}::new(channel);

    let request = {{ request_rust_type }} {
{%- for field in request.fields %}
        {{ field.name }},
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
    let rt = tokio::runtime::Runtime::new()?;
    rt.block_on({{ name }}_async(
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
    let mut client = {{ client_type }}::new(channel);

    let request = {{ request_rust_type }} {
{%- for field in request.fields %}
        {{ field.name }},
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
    let rt = tokio::runtime::Runtime::new()?;
    let stream = rt.block_on({{ name }}_async(
{%- for field in request.fields %}
        {{ field.name }},
{%- endfor %}
    ))?;

    Ok(SyncStreamIterator::new(rt, stream))
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
                    print(f"Warning: Skipping client-streaming RPC {rpc_descriptor.name} (not supported)")
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

            print(f"Generated {output_file}")

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
            f.write(f'    tonic::include_proto!("{module}");\n')
            f.write("}\n\n")

    print(f"Generated {proto_rs_path}")


def update_lib_rs(rust_root_dir, generated_modules):
    """Update lib.rs with module declarations."""
    lib_rs_path = os.path.join(rust_root_dir, "lib.rs")

    with open(lib_rs_path, 'w') as f:
        f.write('''// Copyright Tempo Simulation, LLC. All Rights Reserved

//! Tempo Simulation Rust Client API
//!
//! This crate provides a Rust client for interacting with Tempo simulation servers
//! via gRPC. It supports both synchronous and asynchronous operation using tokio.
//!
//! # Quick Start
//!
//! ```rust,no_run
//! use tempo::set_server;
//!
//! fn main() -> Result<(), tempo::TempoError> {
//!     // Connect to the Tempo server
//!     set_server("localhost", 10001)?;
//!
//!     // Use the API modules (e.g., tempo_core, tempo_sensors)
//!     Ok(())
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

    print(f"Updated {lib_rs_path}")


if __name__ == "__main__":
    if sys.version_info[0] < 3 or sys.version_info[1] < 9:
        raise Exception("This script requires Python 3.9 or greater (found {}.{}.{})"
                        .format(sys.version_info[0], sys.version_info[1], sys.version_info[2]))

    script_dir = os.path.dirname(os.path.realpath(__file__))
    python_root_dir = os.path.join(script_dir, "API", "tempo")
    rust_root_dir = os.path.join(script_dir, "..", "Rust", "API", "src")

    # Add paths for imports
    sys.path.append(script_dir)
    # Add the tempo directory so pb2 imports can find other pb2 files
    sys.path.append(python_root_dir)

    if not os.path.exists(rust_root_dir):
        os.makedirs(rust_root_dir)

    generated = generate_rust_api(python_root_dir, rust_root_dir)
    print(f"Generated {len(generated)} Rust API modules")
