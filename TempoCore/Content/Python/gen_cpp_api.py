# Copyright Tempo Simulation, LLC. All Rights Reserved
#
# Generate the Tempo C++ client API.
#
# Mirrors gen_rust_api.py: introspects the Python _pb2.py descriptors produced
# by gen_protos.py, renders Jinja templates for per-service wrapper code, then
# runs protoc and CMake to compile a static library that ships in
# Content/Cpp/API/. Gated by TEMPO_GEN_CPP_API.

import argparse
import copy
import importlib.util
import os
import platform
import re
import shutil
import subprocess
import sys
from pathlib import Path

import google.protobuf.descriptor as gpd
import jinja2

from gen_common import (
    INFRA_PACKAGE,
    gather_enums,
    gather_messages,
    gather_services,
    package_import_name,
    pascal_to_snake,
    project_package_name,
    protobuf_types_to_cpp_types,
)


SCALAR_TYPES = {
    gpd.FieldDescriptor.TYPE_DOUBLE,
    gpd.FieldDescriptor.TYPE_FLOAT,
    gpd.FieldDescriptor.TYPE_INT64,
    gpd.FieldDescriptor.TYPE_UINT64,
    gpd.FieldDescriptor.TYPE_INT32,
    gpd.FieldDescriptor.TYPE_FIXED64,
    gpd.FieldDescriptor.TYPE_FIXED32,
    gpd.FieldDescriptor.TYPE_BOOL,
    gpd.FieldDescriptor.TYPE_UINT32,
    gpd.FieldDescriptor.TYPE_SFIXED32,
    gpd.FieldDescriptor.TYPE_SFIXED64,
    gpd.FieldDescriptor.TYPE_SINT32,
    gpd.FieldDescriptor.TYPE_SINT64,
}


def host_platform_name() -> str:
    """Folder name under lib/ for the current build host."""
    system = platform.system()
    if system == "Darwin":
        return "Mac"
    if system == "Windows":
        return "Win64"
    if system == "Linux":
        return "Linux"
    raise RuntimeError(f"Unsupported host platform for C++ API build: {system}")


def descriptor_pbh_path(descriptor) -> str:
    """Return the protoc .pb.h path (relative to proto_gen/) for a descriptor."""
    parts = descriptor.module_name.split(".")
    if parts[-1].endswith("_pb2"):
        parts[-1] = parts[-1][: -len("_pb2")]
    return "/".join(parts) + ".pb.h"


def descriptor_grpc_pbh_path(descriptor) -> str:
    parts = descriptor.module_name.split(".")
    if parts[-1].endswith("_pb2"):
        parts[-1] = parts[-1][: -len("_pb2")]
    return "/".join(parts) + ".grpc.pb.h"


def cpp_type_for_field(field) -> str:
    """C++ type name for a single field (without const& or repeated wrapping).

    For message/enum fields, `field.field_type` after resolve_names() looks like
    "<Package>.<PythonModule>_pb2.<Type>" (the middle is the Python module that
    happened to define it, not part of the C++ namespace). Match the Rust
    generator: take parts[0] as the namespace, parts[-1] as the type name.
    """
    if field.proto_type in (gpd.FieldDescriptor.TYPE_MESSAGE,
                            gpd.FieldDescriptor.TYPE_ENUM):
        parts = field.field_type.split(".")
        if len(parts) >= 2:
            return f"{parts[0]}::{parts[-1]}"
        return field.field_type
    if field.proto_type == gpd.FieldDescriptor.TYPE_STRING:
        return "std::string"
    if field.proto_type == gpd.FieldDescriptor.TYPE_BYTES:
        return "std::string"
    return protobuf_types_to_cpp_types.get(field.proto_type, "/* unknown */")


def cpp_param_type(field) -> str:
    """C++ parameter type (with const& and vector wrapping for repeated)."""
    inner = cpp_type_for_field(field)
    is_repeated = field.label == "repeated"
    pass_by_ref = field.proto_type in (
        gpd.FieldDescriptor.TYPE_MESSAGE,
        gpd.FieldDescriptor.TYPE_STRING,
        gpd.FieldDescriptor.TYPE_BYTES,
    )
    if is_repeated:
        return f"const std::vector<{inner}>&"
    # Singular message fields have presence in proto3, so accept std::optional<T>:
    # callers can pass a T (implicit conversion) or std::nullopt to leave the
    # field unset (e.g. AddComponent with no transform for a non-scene component).
    if field.proto_type == gpd.FieldDescriptor.TYPE_MESSAGE:
        return f"const std::optional<{inner}>&"
    if pass_by_ref:
        return f"const {inner}&"
    return inner


def setter_lines(field) -> list:
    """C++ statements that copy a parameter into the protobuf request struct.

    The generated request variable is named `request`; the parameter shares the
    field's name.
    """
    name = field.name
    is_repeated = field.label == "repeated"
    is_message = field.proto_type == gpd.FieldDescriptor.TYPE_MESSAGE

    if is_repeated and is_message:
        return [
            f"for (const auto& __item : {name}) {{",
            f"    *request.add_{name}() = __item;",
            f"}}",
        ]
    if is_repeated:
        return [
            f"for (const auto& __item : {name}) {{",
            f"    request.add_{name}(__item);",
            f"}}",
        ]
    if is_message:
        return [
            f"if ({name}.has_value()) {{",
            f"    *request.mutable_{name}() = *{name};",
            f"}}",
        ]
    return [f"request.set_{name}({name});"]


def cpp_builder_param_type(field) -> str:
    """C++ parameter type for a builder argument.

    Same as cpp_param_type except singular message fields are not wrapped in
    std::optional: a oneof variant either holds its message or is not selected at all,
    so there is no "set but absent" state to express.
    """
    inner = cpp_type_for_field(field)
    if field.label == "repeated":
        return f"const std::vector<{inner}>&"
    if field.proto_type in (gpd.FieldDescriptor.TYPE_MESSAGE,
                            gpd.FieldDescriptor.TYPE_STRING,
                            gpd.FieldDescriptor.TYPE_BYTES):
        return f"const {inner}&"
    return inner


def builder_setter_lines(field, target, proto_field=None) -> list:
    """C++ statements that copy a builder parameter into `target` (a pointer expression).

    `proto_field` overrides the protobuf field name when the parameter is named
    differently from the field it fills (a oneof variant taken as plain `value`).
    """
    name = field.name
    dest = proto_field or field.name
    is_repeated = field.label == "repeated"
    is_message = field.proto_type == gpd.FieldDescriptor.TYPE_MESSAGE

    if is_repeated and is_message:
        return [
            f"for (const auto& __item : {name}) {{",
            f"    *{target}->add_{dest}() = __item;",
            f"}}",
        ]
    if is_repeated:
        return [
            f"for (const auto& __item : {name}) {{",
            f"    {target}->add_{dest}(__item);",
            f"}}",
        ]
    if is_message:
        return [f"*{target}->mutable_{dest}() = {name};"]
    return [f"{target}->set_{dest}({name});"]


HEADER_TEMPLATE = """// Copyright Tempo Simulation, LLC. All Rights Reserved
//
// Auto-generated by gen_cpp_api.py. Do not edit.

#pragma once

#include <optional>
#include <string>
#include <vector>

#include "tempo/result.h"
{% if has_streaming -%}
#include "tempo/streaming.h"
{% endif -%}
{% for include in proto_includes %}
#include "{{ include }}"
{%- endfor %}

namespace tempo::{{ ns }} {

{% for rpc in rpcs %}
/// {{ rpc.original_name }}
{% if rpc.server_streaming -%}
Result<ServerStream<{{ rpc.response_cpp }}>> {{ rpc.name }}({% if rpc.fields %}
{%- for field in rpc.fields %}
    {{ field.cpp_param_type }} {{ field.name }}{{ "," if not loop.last else "" }}
{%- endfor %}
{% endif %});
{%- else -%}
Result<{{ rpc.response_cpp }}> {{ rpc.name }}({% if rpc.fields %}
{%- for field in rpc.fields %}
    {{ field.cpp_param_type }} {{ field.name }}{{ "," if not loop.last else "" }}
{%- endfor %}
{% endif %});
{%- endif %}

{% endfor %}
{%- if call_builder %}
/// CallFunction argument builder.
///
/// Stages typed arguments for one UFUNCTION invocation. Each `*_arg` method names a
/// parameter and appends its value, returning `*this` for fluent chaining; call
/// execute() to invoke. Every input parameter of the function must be supplied.
class Call {
public:
    Call(std::string actor, std::string component, std::string function);

{% for method in call_builder.methods %}
    Call& {{ method.name }}(const std::string& name{% for field in method.fields %}, {{ field.cpp_param_type }} {{ field.name }}{% endfor %});
{% endfor %}
    Result<{{ call_builder.response_cpp }}> execute();

private:
    {{ call_builder.request_cpp }} request_;
};

/// Create a builder for a CallFunction invocation.
Call call(std::string actor, std::string component, std::string function);

{% endif -%}
}  // namespace tempo::{{ ns }}
"""


SOURCE_TEMPLATE = """// Copyright Tempo Simulation, LLC. All Rights Reserved
//
// Auto-generated by gen_cpp_api.py. Do not edit.

#include "tempo/{{ ns }}.h"

#include <memory>
#include <utility>

#include <grpcpp/client_context.h>

#include "tempo/context.h"
#include "tempo/error.h"
{% for include in grpc_includes %}
#include "{{ include }}"
{%- endfor %}

namespace tempo::{{ ns }} {

{% for rpc in rpcs %}
{% if rpc.server_streaming -%}
Result<ServerStream<{{ rpc.response_cpp }}>> {{ rpc.name }}({% if rpc.fields %}
{%- for field in rpc.fields %}
    {{ field.cpp_param_type }} {{ field.name }}{{ "," if not loop.last else "" }}
{%- endfor %}
{% endif %}) {
    auto channel_result = TempoContext::instance().channel();
    if (!channel_result) return std::move(channel_result).error();
    auto channel = std::move(channel_result).value();

    auto stub = {{ rpc.client_cpp }}::NewStub(channel);

    {{ rpc.request_cpp }} request;
{%- for field in rpc.fields %}
{%- for line in field.setter_lines %}
    {{ line }}
{%- endfor %}
{%- endfor %}

    auto context = std::make_unique<grpc::ClientContext>();
    auto reader = stub->{{ rpc.original_name }}(context.get(), request);
    return ServerStream<{{ rpc.response_cpp }}>(
        std::move(channel), std::move(context), std::move(reader));
}
{%- else -%}
Result<{{ rpc.response_cpp }}> {{ rpc.name }}({% if rpc.fields %}
{%- for field in rpc.fields %}
    {{ field.cpp_param_type }} {{ field.name }}{{ "," if not loop.last else "" }}
{%- endfor %}
{% endif %}) {
    auto channel_result = TempoContext::instance().channel();
    if (!channel_result) return std::move(channel_result).error();
    auto channel = std::move(channel_result).value();

    auto stub = {{ rpc.client_cpp }}::NewStub(channel);

    {{ rpc.request_cpp }} request;
{%- for field in rpc.fields %}
{%- for line in field.setter_lines %}
    {{ line }}
{%- endfor %}
{%- endfor %}

    {{ rpc.response_cpp }} response;
    grpc::ClientContext ctx;
    auto status = stub->{{ rpc.original_name }}(&ctx, request, &response);
    if (!status.ok()) return TempoError::from_status(status);
    return response;
}
{%- endif %}

{% endfor %}
{%- if call_builder %}
Call::Call(std::string actor, std::string component, std::string function) {
    request_.set_actor(std::move(actor));
    request_.set_component(std::move(component));
    request_.set_function(std::move(function));
}

{% for method in call_builder.methods %}
Call& Call::{{ method.name }}(const std::string& name{% for field in method.fields %}, {{ field.cpp_param_type }} {{ field.name }}{% endfor %}) {
    auto* arg = request_.add_args();
    arg->set_name(name);
{%- if method.wrapper_cpp %}
    auto* wrapper = arg->mutable_value()->mutable_{{ method.oneof_field }}();
{%- endif %}
{%- for line in method.setter_lines %}
    {{ line }}
{%- endfor %}
    return *this;
}

{% endfor %}
Result<{{ call_builder.response_cpp }}> Call::execute() {
    auto channel_result = TempoContext::instance().channel();
    if (!channel_result) return std::move(channel_result).error();
    auto channel = std::move(channel_result).value();

    auto stub = {{ call_builder.client_cpp }}::NewStub(channel);

    {{ call_builder.response_cpp }} response;
    grpc::ClientContext ctx;
    auto status = stub->CallFunction(&ctx, request_, &response);
    if (!status.ok()) return TempoError::from_status(status);
    return response;
}

Call call(std::string actor, std::string component, std::string function) {
    return Call(std::move(actor), std::move(component), std::move(function));
}

{% endif -%}
}  // namespace tempo::{{ ns }}
"""


UMBRELLA_TEMPLATE = """// Copyright Tempo Simulation, LLC. All Rights Reserved
//
// Auto-generated by gen_cpp_api.py. Do not edit.
//
// Umbrella header — include this and you're done.

#pragma once

#include "tempo/context.h"
#include "tempo/error.h"
#include "tempo/result.h"
#include "tempo/streaming.h"

{% for module in modules -%}
#include "tempo/{{ module }}.h"
{% endfor -%}
"""


class CppApiGenerator:
    def __init__(self, plugin_root: Path, tool_dir: Path):
        self.plugin_root = plugin_root.resolve()
        self.tool_dir = tool_dir.resolve()

        # pb2 descriptors are nested under their package namespaces:
        # tempo_sim/<Module> under the plugin, plus <project>/<Module> in the
        # project tree. The C++ wrapper consumes all of them. (The proto
        # compilation itself uses the bare-path proto export in proto_dir.)
        self.api_root = self.plugin_root / "Content/Python/API"
        self.tempo_sim_dir = self.api_root / INFRA_PACKAGE
        # self.plugin_root is the TempoCore plugin dir
        # (<project>/Plugins/Tempo/TempoCore), so parents[2] is the project root.
        project_root = self.plugin_root.parents[2]
        self.project_py_import = package_import_name(project_package_name(project_root))
        self.project_pb2_dir = (project_root / "Content" / "Python" / "API"
                                / self.project_py_import)
        self.proto_dir = self.plugin_root / "Content/Cpp/API/proto"
        self.template_dir = self.plugin_root / "Content/Python/cpp_api_template"

        # Final ship dir.
        self.api_dir = self.plugin_root / "Content/Cpp/API"
        self.include_dir = self.api_dir / "include"
        self.lib_dir = self.api_dir / "lib" / host_platform_name()

        # Build dir (gitignored).
        self.build_dir = self.api_dir / "build"
        self.source_dir = self.build_dir / "source"
        self.cmake_build_dir = self.build_dir / "cmake"

        self.protoc = self.tool_dir / (
            "protoc.exe" if platform.system() == "Windows" else "protoc")
        self.grpc_cpp_plugin = self.tool_dir / (
            "grpc_cpp_plugin.exe" if platform.system() == "Windows" else "grpc_cpp_plugin")

        self.grpc_third_party = self.plugin_root / "Source/ThirdParty/gRPC"

        self.j2 = jinja2.Environment(trim_blocks=False, lstrip_blocks=False)

    def check_prereqs(self):
        if not self.tempo_sim_dir.exists():
            raise RuntimeError(
                f"Python API not found at {self.tempo_sim_dir}. Run gen_protos.py first.")
        if not self.proto_dir.exists():
            raise RuntimeError(
                f"Decorated protos not found at {self.proto_dir}. "
                f"Run gen_protos.py with TEMPO_GEN_CPP_API=1 first.")
        if not self.template_dir.exists():
            raise RuntimeError(f"cpp_api_template not found at {self.template_dir}")
        if not self.protoc.exists():
            raise RuntimeError(f"protoc not found at {self.protoc}")
        if not self.grpc_cpp_plugin.exists():
            raise RuntimeError(f"grpc_cpp_plugin not found at {self.grpc_cpp_plugin}")
        if not self.grpc_third_party.exists():
            raise RuntimeError(f"Vendored gRPC not found at {self.grpc_third_party}")
        if shutil.which("cmake") is None:
            raise RuntimeError(
                "cmake not found on PATH. Install CMake 3.20+ "
                "(https://cmake.org/) to use TEMPO_GEN_CPP_API.")

    def gather(self):
        """Walk _pb2.py files and gather services/messages/enums."""
        all_enums = {}
        all_messages = {}
        all_services = {}

        # Module dirs across both packages: tempo_sim/<Module> and
        # <project>/<Module>. {module: (namespace, dir)}.
        module_dirs = {}
        for pb2_root, namespace in ((self.tempo_sim_dir, INFRA_PACKAGE),
                                    (self.project_pb2_dir, self.project_py_import)):
            if not pb2_root.exists():
                continue
            for d in sorted(pb2_root.iterdir()):
                if d.is_dir() and d.name != "__pycache__":
                    if d.name in module_dirs:
                        raise RuntimeError(
                            f"Module name collision: {d.name!r} exists in both the "
                            f"tempo_sim and project ({self.project_py_import}) packages. "
                            "Module names must be unique across plugin and project.")
                    module_dirs[d.name] = (namespace, d)

        for tempo_module_name, (namespace, tempo_module_root) in module_dirs.items():
            for path, _, files in os.walk(tempo_module_root):
                for filename in files:
                    if not filename.endswith("_pb2.py"):
                        continue
                    file_path = Path(path) / filename
                    rel_path = file_path.relative_to(tempo_module_root)
                    rel_module = os.path.splitext(str(rel_path))[0].replace(os.sep, ".")
                    # Descriptor keys stay un-prefixed (bare Module paths, as the
                    # C++ pass expects); the import is namespace-qualified so each
                    # nested pb2 is loaded once under its canonical name.
                    module_name = "{}.{}".format(tempo_module_name, rel_module)
                    qualified_name = "{}.{}.{}".format(namespace, tempo_module_name, rel_module)
                    module = importlib.import_module(qualified_name)
                    if not hasattr(module, "DESCRIPTOR"):
                        continue
                    descriptor = module.DESCRIPTOR
                    all_enums.update(gather_enums(module_name, descriptor))
                    all_messages.update(
                        gather_messages(module_name, descriptor, protobuf_types_to_cpp_types))
                    all_services.update(gather_services(module_name, descriptor))

        for msg in all_messages.values():
            msg.resolve_names(all_messages | all_enums)

        return all_enums, all_messages, all_services

    @staticmethod
    def _call_builder_render(tempo_module, services, all_messages, proto_includes):
        """Gather the Call builder rendering for a module, or None if it has no Value oneof."""
        value_desc = next(
            (m for m in all_messages.values()
             if m.object_name == "Value" and m.module_name.split(".")[0] == tempo_module),
            None,
        )
        call_service = next(
            (s for s in services if any(rpc.name == "CallFunction" for rpc in s.rpcs)),
            None,
        )
        if not value_desc or not call_service or not value_desc.oneofs.get("value"):
            return None

        request_desc = all_messages[f"{tempo_module}.CallFunctionRequest"]
        call_rpc = next(rpc for rpc in call_service.rpcs if rpc.name == "CallFunction")
        response_desc = all_messages[call_rpc.response_type]
        proto_includes.add(descriptor_pbh_path(request_desc))
        proto_includes.add(descriptor_pbh_path(response_desc))

        fields_by_name = {field.name: field for field in value_desc.fields}
        methods = []
        for entry in value_desc.oneofs["value"]:
            variant_field = fields_by_name[entry["name"]]
            wrapper_desc = all_messages.get(entry["message_proto_full_name"])
            # Flatten a wrapper message's fields into the method signature when they are all
            # scalars, mirroring how set_vector_property takes x/y/z rather than a Vector. A
            # wrapper with message-typed fields (Transform) is passed through whole instead.
            if wrapper_desc is not None and all(
                    f.proto_type != gpd.FieldDescriptor.TYPE_MESSAGE for f in wrapper_desc.fields):
                proto_includes.add(descriptor_pbh_path(wrapper_desc))
                fields = [_FieldRender(name=f.name, cpp_param_type=cpp_builder_param_type(f),
                                       setter_lines=[]) for f in wrapper_desc.fields]
                setter_lines = []
                for f in wrapper_desc.fields:
                    setter_lines.extend(builder_setter_lines(f, "wrapper"))
                wrapper_cpp = "::".join(
                    [wrapper_desc.module_name.split(".")[0], wrapper_desc.object_name])
            else:
                # Name the parameter `value`, as the singular set_*_property wrappers do.
                value_field = copy.copy(variant_field)
                value_field.name = "value"
                fields = [_FieldRender(name="value",
                                       cpp_param_type=cpp_builder_param_type(variant_field),
                                       setter_lines=[])]
                setter_lines = builder_setter_lines(
                    value_field, "arg->mutable_value()", proto_field=entry["name"])
                wrapper_cpp = None
                if variant_field.proto_type == gpd.FieldDescriptor.TYPE_MESSAGE:
                    # resolve_names() rewrote field_type to the descriptor's full_name, which is
                    # the Python module path rather than the proto full name all_messages is keyed by.
                    variant_desc = next(
                        (m for m in all_messages.values() if m.full_name == variant_field.field_type),
                        None)
                    if variant_desc is not None:
                        proto_includes.add(descriptor_pbh_path(variant_desc))
            methods.append(_CallMethodRender(
                # bool_value -> bool_arg; int_vector_value -> int_vector_arg
                name="{}_arg".format(
                    entry["name"][:-6] if entry["name"].endswith("_value") else entry["name"]),
                oneof_field=entry["name"],
                wrapper_cpp=wrapper_cpp,
                fields=fields,
                setter_lines=setter_lines,
            ))

        return _CallBuilderRender(
            request_cpp=f"{tempo_module}::CallFunctionRequest",
            response_cpp="::".join(
                [response_desc.module_name.split(".")[0], response_desc.object_name]),
            client_cpp="::".join(
                call_service.module_name.split(".")[:1] + [call_service.object_name]),
            methods=methods,
        )

    def render_wrappers(self, all_messages, all_services):
        """Render per-service wrapper headers and sources into self.source_dir.

        Returns a list of generated module names (snake_case) for the umbrella.
        """
        # Group services by tempo module.
        by_module = {}
        for service in all_services.values():
            tempo_module = service.module_name.split(".")[0]
            by_module.setdefault(tempo_module, []).append(service)

        include_out = self.source_dir / "include" / "tempo"
        src_out = self.source_dir / "src"
        include_out.mkdir(parents=True, exist_ok=True)
        src_out.mkdir(parents=True, exist_ok=True)

        generated_modules = []
        header_t = self.j2.from_string(HEADER_TEMPLATE)
        source_t = self.j2.from_string(SOURCE_TEMPLATE)

        for tempo_module, services in sorted(by_module.items()):
            ns = pascal_to_snake(tempo_module)
            rpc_renderings = []
            proto_includes = set()
            grpc_includes = set()
            has_streaming = False

            for service in services:
                grpc_includes.add(descriptor_grpc_pbh_path(service))
                proto_includes.add(descriptor_pbh_path(service))

                client_cpp = "::".join(service.module_name.split(".")[:1] + [service.object_name])
                # Service namespace is derived from the proto package, which after
                # decoration is just the tempo module name.

                for rpc in service.rpcs:
                    if rpc.client_streaming:
                        # Out of scope for v1 — neither Python nor Rust wrappers
                        # generate client-streaming sync APIs.
                        continue

                    request_desc = all_messages[rpc.request_type]
                    response_desc = all_messages[rpc.response_type]

                    proto_includes.add(descriptor_pbh_path(request_desc))
                    proto_includes.add(descriptor_pbh_path(response_desc))

                    request_cpp = "::".join(
                        [request_desc.module_name.split(".")[0], request_desc.object_name])
                    response_cpp = "::".join(
                        [response_desc.module_name.split(".")[0], response_desc.object_name])

                    fields = []
                    for field in request_desc.fields:
                        fields.append(_FieldRender(
                            name=field.name,
                            cpp_param_type=cpp_param_type(field),
                            setter_lines=setter_lines(field),
                        ))

                    if rpc.server_streaming:
                        has_streaming = True
                        # Pull the response message header in too — ServerStream<T>
                        # consumers need the full type definition.
                        proto_includes.add(descriptor_pbh_path(response_desc))

                    rpc_renderings.append(_RpcRender(
                        name=pascal_to_snake(rpc.name),
                        original_name=rpc.name,
                        request_cpp=request_cpp,
                        response_cpp=response_cpp,
                        client_cpp=client_cpp,
                        server_streaming=rpc.server_streaming,
                        fields=fields,
                    ))

            if not rpc_renderings:
                continue

            # If this module defines a `Value` message with a `value` oneof and a
            # CallFunction RPC, emit a fluent Call builder so users don't have to
            # construct Value or FunctionArg messages by hand.
            call_builder = self._call_builder_render(
                tempo_module, services, all_messages, proto_includes)

            generated_modules.append(ns)
            ctx = {
                "ns": ns,
                "rpcs": rpc_renderings,
                "call_builder": call_builder,
                "has_streaming": has_streaming,
                "proto_includes": sorted(proto_includes),
                "grpc_includes": sorted(grpc_includes),
            }
            (include_out / f"{ns}.h").write_text(header_t.render(**ctx))
            (src_out / f"{ns}.cc").write_text(source_t.render(**ctx))

        umbrella_t = self.j2.from_string(UMBRELLA_TEMPLATE)
        umbrella_out = self.source_dir / "include" / "tempo.h"
        umbrella_out.write_text(umbrella_t.render(modules=sorted(generated_modules)))

        return generated_modules

    def copy_template(self):
        """Copy hand-written headers, sources, and CMakeLists.txt into the build tree."""
        # include/tempo/*.h (hand-written: context, error, result, streaming)
        for h in (self.template_dir / "include/tempo").glob("*.h"):
            dest = self.source_dir / "include/tempo" / h.name
            dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(h, dest)

        # src/*.cc (hand-written: context, error)
        for cc in (self.template_dir / "src").glob("*.cc"):
            dest = self.source_dir / "src" / cc.name
            dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(cc, dest)

        # CMakeLists.txt (root of build tree)
        shutil.copy2(self.template_dir / "CMakeLists.txt",
                     self.source_dir / "CMakeLists.txt")

    def run_protoc(self):
        """Run protoc + grpc_cpp_plugin against decorated protos in self.proto_dir.

        Outputs flow into <source>/proto_gen/. Generated layout matches each
        proto's path relative to self.proto_dir (e.g.,
        TempoCore/TempoCore.proto → proto_gen/TempoCore/TempoCore.pb.{h,cc}
        and .grpc.pb.{h,cc}).
        """
        proto_gen = self.source_dir / "proto_gen"
        if proto_gen.exists():
            shutil.rmtree(proto_gen)
        proto_gen.mkdir(parents=True)

        if platform.system() == "Linux":
            for tool in (self.protoc, self.grpc_cpp_plugin):
                if tool.exists():
                    tool.chmod(tool.stat().st_mode | 0o111)

        proto_files = sorted(self.proto_dir.rglob("*.proto"))
        if not proto_files:
            raise RuntimeError(f"No .proto files found in {self.proto_dir}")

        service_re = re.compile(r"\bservice\s+\w+\s*\{")

        for proto_file in proto_files:
            with open(proto_file, "r", encoding="utf-8") as f:
                content = f.read()
            has_service = bool(service_re.search(content))

            cmd = [
                str(self.protoc),
                f"-I{self.proto_dir}",
                f"--cpp_out={proto_gen}",
                str(proto_file),
            ]
            if has_service:
                cmd.insert(2, f"--grpc_out={proto_gen}")
                cmd.insert(3, f"--plugin=protoc-gen-grpc={self.grpc_cpp_plugin}")

            subprocess.run(cmd, check=True)

    def run_cmake(self):
        """Configure and build libtempo via CMake."""
        if self.cmake_build_dir.exists():
            shutil.rmtree(self.cmake_build_dir)
        self.cmake_build_dir.mkdir(parents=True, exist_ok=True)

        configure_cmd = [
            "cmake",
            "-S", str(self.source_dir),
            "-B", str(self.cmake_build_dir),
            f"-DTEMPO_GRPC_DIR={self.grpc_third_party}",
            "-DCMAKE_BUILD_TYPE=Release",
        ]
        # Prefer Ninja on Windows when available so we get a single-config build
        # (otherwise CMake defaults to Visual Studio's multi-config generator).
        if platform.system() == "Windows" and shutil.which("ninja"):
            configure_cmd.extend(["-G", "Ninja"])

        print(f"[Tempo Prebuild] Configuring C++ wrapper: {' '.join(configure_cmd)}", flush=True)
        subprocess.run(configure_cmd, check=True)

        build_cmd = ["cmake", "--build", str(self.cmake_build_dir),
                     "--config", "Release", "--parallel"]
        print(f"[Tempo Prebuild] Building C++ wrapper", flush=True)
        subprocess.run(build_cmd, check=True)

    def copy_artifacts(self, generated_modules):
        """Copy headers, library, README, and decorated protos to Content/Cpp/API/."""
        # Wipe & recreate include/, lib/<platform>/
        if self.include_dir.exists():
            shutil.rmtree(self.include_dir)
        self.include_dir.mkdir(parents=True)
        (self.include_dir / "tempo").mkdir()
        (self.include_dir / "proto").mkdir()

        # Wrapper + hand-written headers from build/source/include/tempo/
        for h in (self.source_dir / "include/tempo").glob("*.h"):
            shutil.copy2(h, self.include_dir / "tempo" / h.name)
        # Umbrella tempo.h
        shutil.copy2(self.source_dir / "include/tempo.h",
                     self.include_dir / "tempo.h")

        # Public proto headers from proto_gen/
        for pbh in (self.source_dir / "proto_gen").rglob("*.pb.h"):
            rel = pbh.relative_to(self.source_dir / "proto_gen")
            dest = self.include_dir / "proto" / rel
            dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(pbh, dest)

        # Library archive(s)
        if self.lib_dir.exists():
            shutil.rmtree(self.lib_dir)
        self.lib_dir.mkdir(parents=True)
        # CMake puts archives under <build>/lib/ (we set ARCHIVE_OUTPUT_DIRECTORY).
        # Multi-config generators (MSVC) insert <Config>/, so we glob.
        archive_root = self.cmake_build_dir / "lib"
        archives = list(archive_root.rglob("*.a")) + list(archive_root.rglob("*.lib"))
        if not archives:
            raise RuntimeError(
                f"No archive produced under {archive_root}. CMake build may have failed silently.")
        for arc in archives:
            shutil.copy2(arc, self.lib_dir / arc.name)

        # README (static; copied verbatim)
        shutil.copy2(self.template_dir / "README.md", self.api_dir / "README.md")

        print(f"[Tempo Prebuild] C++ API written to {self.api_dir}")
        print(f"[Tempo Prebuild]   modules: {', '.join(generated_modules)}")
        print(f"[Tempo Prebuild]   archive: {self.lib_dir}")

    def run(self):
        self.check_prereqs()

        # Reset build tree
        if self.build_dir.exists():
            shutil.rmtree(self.build_dir)
        self.source_dir.mkdir(parents=True)

        _enums, all_messages, all_services = self.gather()
        if not all_services:
            print("[Tempo Prebuild] No services found; skipping C++ API generation.")
            return

        self.copy_template()
        generated_modules = self.render_wrappers(all_messages, all_services)
        self.run_protoc()
        self.run_cmake()
        self.copy_artifacts(generated_modules)


class _RpcRender:
    __slots__ = ("name", "original_name", "request_cpp", "response_cpp",
                 "client_cpp", "server_streaming", "fields")

    def __init__(self, name, original_name, request_cpp, response_cpp,
                 client_cpp, server_streaming, fields):
        self.name = name
        self.original_name = original_name
        self.request_cpp = request_cpp
        self.response_cpp = response_cpp
        self.client_cpp = client_cpp
        self.server_streaming = server_streaming
        self.fields = fields


class _FieldRender:
    __slots__ = ("name", "cpp_param_type", "setter_lines")

    def __init__(self, name, cpp_param_type, setter_lines):
        self.name = name
        self.cpp_param_type = cpp_param_type
        self.setter_lines = setter_lines


class _CallMethodRender:
    def __init__(self, name, oneof_field, wrapper_cpp, fields, setter_lines):
        self.name = name
        self.oneof_field = oneof_field
        self.wrapper_cpp = wrapper_cpp
        self.fields = fields
        self.setter_lines = setter_lines


class _CallBuilderRender:
    def __init__(self, request_cpp, response_cpp, client_cpp, methods):
        self.request_cpp = request_cpp
        self.response_cpp = response_cpp
        self.client_cpp = client_cpp
        self.methods = methods


def _collect_inputs(plugin_root: Path) -> list:
    """Files that drive C++ wrapper generation and the cmake build."""
    inputs = [
        Path(__file__).resolve(),
        (Path(__file__).parent / "gen_common.py").resolve(),
    ]

    template_dir = plugin_root / "Content/Python/cpp_api_template"
    if template_dir.exists():
        for path, _, files in os.walk(template_dir):
            for filename in files:
                inputs.append(Path(path) / filename)

    project_root = plugin_root.parents[2]
    project_py_import = package_import_name(project_package_name(project_root))
    pb2_roots = [
        plugin_root / "Content/Python/API" / INFRA_PACKAGE,
        project_root / "Content" / "Python" / "API" / project_py_import,
    ]
    for pb2_root in pb2_roots:
        if pb2_root.exists():
            for path, _, files in os.walk(pb2_root):
                for filename in files:
                    if filename.endswith("_pb2.py"):
                        inputs.append(Path(path) / filename)

    proto_dir = plugin_root / "Content/Cpp/API/proto"
    if proto_dir.exists():
        for path, _, files in os.walk(proto_dir):
            for filename in files:
                if filename.endswith(".proto"):
                    inputs.append(Path(path) / filename)

    return inputs


def main():
    if sys.version_info < (3, 9):
        raise RuntimeError(
            f"This script requires Python 3.9+ (found "
            f"{sys.version_info[0]}.{sys.version_info[1]}.{sys.version_info[2]})")

    parser = argparse.ArgumentParser(description="Generate Tempo C++ client API")
    parser.add_argument("plugin_root", help="Plugin root directory")
    parser.add_argument("tool_dir", help="Path to gRPC Binaries dir (contains protoc + grpc_cpp_plugin)")
    args = parser.parse_args()

    plugin_root = Path(args.plugin_root).resolve()
    tool_dir = Path(args.tool_dir).resolve()

    sys.path.append(str(plugin_root / "Content/Python"))
    # API dirs (parents of the package dirs) so the namespace-qualified nested
    # pb2 (tempo_sim.*, <project>.*) and their cross-imports resolve.
    sys.path.append(str(plugin_root / "Content/Python/API"))
    project_root = plugin_root.parents[2]
    project_py_import = package_import_name(project_package_name(project_root))
    if (project_root / "Content" / "Python" / "API" / project_py_import).exists():
        sys.path.append(str(project_root / "Content" / "Python" / "API"))

    from prebuild_cache import PrebuildCache  # noqa: E402

    api_dir = plugin_root / "Content/Cpp/API"
    lib_dir = api_dir / "lib" / host_platform_name()
    archive_paths = list(lib_dir.glob("*.a")) + list(lib_dir.glob("*.lib"))

    cache = PrebuildCache(plugin_root / ".tempo_prebuild_cache.json")
    inputs = _collect_inputs(plugin_root)
    if archive_paths and cache.is_valid("gen_cpp_api", inputs, archive_paths):
        print("[Tempo Prebuild]  Skipping C++ API generation (no changes detected)", flush=True)
        return 0

    print("[Tempo Prebuild] Generating C++ API", flush=True)
    generator = CppApiGenerator(plugin_root, tool_dir)
    generator.run()

    # Re-collect outputs after the build
    archive_paths = list(lib_dir.glob("*.a")) + list(lib_dir.glob("*.lib"))
    if not archive_paths:
        raise RuntimeError(f"No archive at {lib_dir} after generation.")
    cache.update("gen_cpp_api", inputs, archive_paths)

    return 0


if __name__ == "__main__":
    sys.exit(main())
