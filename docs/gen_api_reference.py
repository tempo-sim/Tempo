# Copyright Tempo Simulation, LLC. All Rights Reserved
#
# Generates the gRPC API reference pages from Tempo's canonical .proto files.
#
# Run by mkdocs-gen-files on every docs build, so the reference cannot drift from
# the protos the server actually serves. Nothing it writes lands on disk - the
# pages exist only in the built site.
#
# The pipeline mirrors what Tempo's own prebuild (gen_protos.py) does, so the
# names here are the names the generated clients expose:
#   1. Stage each module's protos under <ModuleName>/... so the
#      "ModuleName/File.proto" import convention resolves.
#   2. Append `package <ModuleName>;` to any proto that declares none, exactly as
#      the prebuild does - the package is the wire identity.
#   3. Compile to a FileDescriptorSet (with source info, so proto comments carry
#      through) and render Markdown from it.
#
# Client-facing identifiers come from gen_naming.pascal_to_snake, the same helper
# the Python/Rust/C++ generators use, so a naming-rule change updates the docs too.

import re
import shutil
import subprocess
import sys
import tempfile
from importlib import util as importlib_util
from pathlib import Path

import mkdocs_gen_files

PLUGIN_ROOT = Path(__file__).resolve().parent.parent
API_DIR = "reference/api"

# Plugin order on the reference index, and the module order within it. Modules not
# listed here still appear, after these, in discovery order.
MODULE_ORDER = [
    "TempoCore",
    "TempoCoreEditor",
    "TempoWorld",
    "TempoSensors",
    "TempoMovement",
    "TempoAgents",
    "TempoAgentsEditor",
    "TempoGeographic",
]

# One-line orientation for each module, shown at the top of its page and on the index.
MODULE_BLURBS = {
    "TempoCore": "Simulation lifecycle and time control - load levels, pause, step, quit.",
    "TempoCoreEditor": "Editor-only control: start and stop Play In Editor.",
    "TempoWorld": "Query and control the simulated world - actors, components, properties, functions.",
    "TempoSensors": "Discover sensors and stream their measurements.",
    "TempoMovement": "Command vehicles and pawns, and configure trajectory following.",
    "TempoAgents": "Query the lane graph and traffic controls of a ZoneGraph map.",
    "TempoAgentsEditor": "Editor-only agents utilities.",
    "TempoGeographic": "Geographic reference and date/time of day.",
}

# The plugin each module documents against, for the "defined in" line.
_PLUGIN_DOC_PAGE = {
    "TempoCore": "tempo-core",
    "TempoCoreEditor": "tempo-core",
    "TempoWorld": "tempo-world",
    "TempoSensors": "tempo-sensors",
    "TempoMovement": "tempo-movement",
    "TempoAgents": "tempo-agents",
    "TempoAgentsEditor": "tempo-agents",
    "TempoGeographic": "tempo-geographic",
}


def _load_gen_naming():
    """Import Tempo's own naming helpers so generated names match the shipped clients."""
    path = PLUGIN_ROOT / "TempoCore" / "Content" / "Python" / "gen_naming.py"
    spec = importlib_util.spec_from_file_location("tempo_gen_naming", path)
    module = importlib_util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


gen_naming = _load_gen_naming()
pascal_to_snake = gen_naming.pascal_to_snake


def discover_protos():
    """Return {module_name: [proto paths]} for Tempo's canonical, hand-written protos.

    Only `<Plugin>/Source/<Module>/{Public,Private}/**/*.proto` counts. Generated
    copies staged under Content/{Cpp,Rust}/API, vendored third-party protos, and
    the Tests vendor tree are all excluded - they are outputs, not sources.
    """
    modules = {}
    for plugin_dir in sorted(PLUGIN_ROOT.iterdir()):
        source_dir = plugin_dir / "Source"
        if not source_dir.is_dir():
            continue
        for module_dir in sorted(source_dir.iterdir()):
            if not module_dir.is_dir() or module_dir.name == "ThirdParty":
                continue
            protos = sorted(
                p for visibility in ("Public", "Private")
                for p in (module_dir / visibility).rglob("*.proto")
                if (module_dir / visibility).is_dir()
            )
            if protos:
                modules[module_dir.name] = protos
    return modules


def build_descriptor_set(modules):
    """Stage the protos the way the prebuild does, compile them, return the FileDescriptorSet."""
    try:
        from grpc_tools import protoc
    except ImportError as exc:  # pragma: no cover - a missing build dep should be loud
        raise SystemExit(
            "gen_api_reference.py needs grpcio-tools. Install the docs requirements:\n"
            "    pip install -r docs/requirements.txt"
        ) from exc
    from google.protobuf import descriptor_pb2

    staging = Path(tempfile.mkdtemp(prefix="tempo-docs-protos-"))
    try:
        staged_to_source = {}
        for module_name, protos in modules.items():
            for proto in protos:
                # <Module>/<Public|Private>/<rel>/<name>.proto -> <Module>/<rel>/<name>.proto
                visibility_root = next(
                    parent for parent in proto.parents if parent.name in ("Public", "Private")
                )
                staged = staging / module_name / proto.relative_to(visibility_root)
                staged.parent.mkdir(parents=True, exist_ok=True)
                content = proto.read_text()
                if not re.search(r"^\s*package\s+", content, re.MULTILINE):
                    content += f"\npackage {module_name};\n"
                staged.write_text(content)
                staged_to_source[str(staged.relative_to(staging))] = proto

        descriptor_path = staging / "descriptor.pb"
        args = [
            "protoc",
            f"--proto_path={staging}",
            f"--proto_path={Path(protoc.__file__).parent / '_proto'}",
            f"--descriptor_set_out={descriptor_path}",
            "--include_source_info",
            "--include_imports",
            *sorted(staged_to_source),
        ]
        if protoc.main(args) != 0:
            raise SystemExit("protoc failed while generating the Tempo API reference")

        file_set = descriptor_pb2.FileDescriptorSet()
        file_set.ParseFromString(descriptor_path.read_bytes())
        return file_set, staged_to_source
    finally:
        shutil.rmtree(staging, ignore_errors=True)


# --- SourceCodeInfo -----------------------------------------------------------
# Field numbers in FileDescriptorProto, used to address elements in source info.
_MESSAGE, _ENUM, _SERVICE = 4, 5, 6
_MESSAGE_FIELD, _MESSAGE_NESTED, _MESSAGE_ENUM = 2, 3, 4
_ENUM_VALUE, _SERVICE_METHOD = 2, 2


def collect_comments(file_proto):
    """Map a source-info path tuple to its cleaned-up leading comment."""
    comments = {}
    for location in file_proto.source_code_info.location:
        text = location.leading_comments or location.trailing_comments
        if not text:
            continue
        lines = [line[1:] if line.startswith(" ") else line for line in text.split("\n")]
        cleaned = "\n".join(lines).strip()
        if cleaned:
            comments[tuple(location.path)] = cleaned
    return comments


# --- Type rendering -----------------------------------------------------------
from google.protobuf import descriptor_pb2 as _dpb  # noqa: E402  (needs the import above)

_SCALARS = {
    _dpb.FieldDescriptorProto.TYPE_DOUBLE: ("double", "float"),
    _dpb.FieldDescriptorProto.TYPE_FLOAT: ("float", "float"),
    _dpb.FieldDescriptorProto.TYPE_INT64: ("int64", "int"),
    _dpb.FieldDescriptorProto.TYPE_UINT64: ("uint64", "int"),
    _dpb.FieldDescriptorProto.TYPE_INT32: ("int32", "int"),
    _dpb.FieldDescriptorProto.TYPE_FIXED64: ("fixed64", "int"),
    _dpb.FieldDescriptorProto.TYPE_FIXED32: ("fixed32", "int"),
    _dpb.FieldDescriptorProto.TYPE_BOOL: ("bool", "bool"),
    _dpb.FieldDescriptorProto.TYPE_STRING: ("string", "str"),
    _dpb.FieldDescriptorProto.TYPE_BYTES: ("bytes", "bytes"),
    _dpb.FieldDescriptorProto.TYPE_UINT32: ("uint32", "int"),
    _dpb.FieldDescriptorProto.TYPE_SFIXED32: ("sfixed32", "int"),
    _dpb.FieldDescriptorProto.TYPE_SFIXED64: ("sfixed64", "int"),
    _dpb.FieldDescriptorProto.TYPE_SINT32: ("sint32", "int"),
    _dpb.FieldDescriptorProto.TYPE_SINT64: ("sint64", "int"),
}

_LABEL_REPEATED = _dpb.FieldDescriptorProto.LABEL_REPEATED


def anchor_for(full_name):
    """Deterministic heading anchor for a fully-qualified proto type."""
    return re.sub(r"[^a-z0-9]+", "-", full_name.lower()).strip("-")


def page_slug(module_name):
    return pascal_to_snake(module_name).replace("_", "-")


class Registry:
    """Where every declared type lives, so cross-module references can link."""

    def __init__(self):
        self.module_of = {}   # full proto name -> module name
        self.kind_of = {}     # full proto name -> "message" | "enum"

    def add(self, full_name, module_name, kind):
        self.module_of[full_name] = module_name
        self.kind_of[full_name] = kind

    def link(self, full_name, from_module):
        """Markdown link to a type's heading, or plain text if it isn't ours."""
        module = self.module_of.get(full_name)
        if module is None:
            return f"`{full_name}`"
        target = "" if module == from_module else f"{page_slug(module)}.md"
        return f"[`{full_name}`]({target}#{anchor_for(full_name)})"


def field_type(field, registry, from_module):
    """(proto type as markdown, python type hint) for one field."""
    if field.type in (_dpb.FieldDescriptorProto.TYPE_MESSAGE,
                      _dpb.FieldDescriptorProto.TYPE_ENUM):
        full_name = field.type_name.lstrip(".")
        proto_type = registry.link(full_name, from_module)
        py_type = full_name.rsplit(".", 1)[-1]
    else:
        proto_name, py_name = _SCALARS.get(field.type, (str(field.type), "Any"))
        proto_type = f"`{proto_name}`"
        py_type = py_name
    if field.label == _LABEL_REPEATED:
        return f"{proto_type} (repeated)", f"list[{py_type}]"
    return proto_type, py_type


def field_rows(message, comments, path, registry, from_module):
    """Markdown table rows for a message's fields."""
    rows = []
    for index, field in enumerate(message.field):
        proto_type, _ = field_type(field, registry, from_module)
        notes = []
        if field.proto3_optional:
            notes.append("optional")
        elif field.HasField("oneof_index"):
            notes.append(f"oneof `{message.oneof_decl[field.oneof_index].name}`")
        description = comments.get(path + (_MESSAGE_FIELD, index), "")
        description = description.replace("\n", " ").replace("|", r"\|")
        if notes:
            description = f"*({', '.join(notes)})* {description}".strip()
        rows.append(f"| `{field.name}` | {proto_type} | {field.number} | {description} |")
    return rows


def render_message(out, message, full_name, comments, path, registry, module_name, level=3):
    heading = "#" * level
    out.append(f"{heading} `{full_name}` {{ #{anchor_for(full_name)} }}")
    out.append("")
    description = comments.get(path)
    if description:
        out.append(description)
        out.append("")
    if message.field:
        out.append("| Field | Type | # | Description |")
        out.append("| --- | --- | --- | --- |")
        out.extend(field_rows(message, comments, path, registry, module_name))
    else:
        out.append("*No fields.*")
    out.append("")

    for index, nested in enumerate(message.nested_type):
        if nested.options.map_entry:
            continue
        render_message(out, nested, f"{full_name}.{nested.name}", comments,
                       path + (_MESSAGE_NESTED, index), registry, module_name, level + 1)
    for index, enum in enumerate(message.enum_type):
        render_enum(out, enum, f"{full_name}.{enum.name}", comments,
                    path + (_MESSAGE_ENUM, index), level + 1)


def render_enum(out, enum, full_name, comments, path, level=3):
    heading = "#" * level
    out.append(f"{heading} `{full_name}` {{ #{anchor_for(full_name)} }}")
    out.append("")
    description = comments.get(path)
    if description:
        out.append(description)
        out.append("")
    out.append("| Value | # | Description |")
    out.append("| --- | --- | --- |")
    for index, value in enumerate(enum.value):
        note = comments.get(path + (_ENUM_VALUE, index), "").replace("\n", " ").replace("|", r"\|")
        out.append(f"| `{value.name}` | {value.number} | {note} |")
    out.append("")


def render_rpc(out, method, service, comments, path, registry, module_name, client_module):
    """One RPC: its client-facing name, signature, and request/response types."""
    name = pascal_to_snake(method.name)
    request_name = method.input_type.lstrip(".")
    response_name = method.output_type.lstrip(".")
    request = registry.messages.get(request_name)

    out.append(f"### `{name}` {{ #{anchor_for(f'{service.name}.{method.name}')} }}")
    out.append("")
    if method.server_streaming:
        out.append(f"*Streaming RPC. gRPC method `{service.name}.{method.name}`.*")
    else:
        out.append(f"*gRPC method `{service.name}.{method.name}`.*")
    out.append("")
    description = comments.get(path)
    if description:
        out.append(description)
        out.append("")

    # The client wrappers flatten the request message's fields into the argument
    # list, so the signature is the request's fields - not a request object.
    params = []
    if request is not None:
        for field in request.field:
            _, py_type = field_type(field, registry, module_name)
            params.append(f"{field.name}: {py_type}")
    joined = ", ".join(params)
    if len(joined) > 76:
        rendered = "(\n    " + ",\n    ".join(params) + ",\n)"
    else:
        rendered = f"({joined})"
    returns = response_name.rsplit(".", 1)[-1]
    if method.server_streaming:
        returns = f"Iterator[{returns}]"
    out.append("```python")
    out.append(f"{client_module}.{name}{rendered} -> {returns}")
    out.append("```")
    out.append("")
    out.append(f"**Request:** {registry.link(request_name, module_name)} &nbsp;&nbsp; "
               f"**Response:** {registry.link(response_name, module_name)}")
    out.append("")


def main():
    modules = discover_protos()
    file_set, staged_to_source = build_descriptor_set(modules)

    # Index every declared type first, so a reference can link across modules.
    registry = Registry()
    registry.messages = {}
    files_by_module = {}
    for file_proto in file_set.file:
        source = staged_to_source.get(file_proto.name)
        if source is None:
            continue  # an imported well-known type, not one of ours
        module_name = file_proto.name.split("/")[0]
        files_by_module.setdefault(module_name, []).append((file_proto, source))
        package = file_proto.package

        def index_message(message, prefix):
            full_name = f"{prefix}.{message.name}"
            registry.add(full_name, module_name, "message")
            registry.messages[full_name] = message
            for nested in message.nested_type:
                if not nested.options.map_entry:
                    index_message(nested, full_name)
            for enum in message.enum_type:
                registry.add(f"{full_name}.{enum.name}", module_name, "enum")

        for message in file_proto.message_type:
            index_message(message, package)
        for enum in file_proto.enum_type:
            registry.add(f"{package}.{enum.name}", module_name, "enum")

    ordered = [m for m in MODULE_ORDER if m in files_by_module]
    ordered += [m for m in sorted(files_by_module) if m not in MODULE_ORDER]

    index_rows = []
    summary = ["* [Overview](index.md)"]

    for module_name in ordered:
        client_module = pascal_to_snake(module_name)
        slug = page_slug(module_name)
        out = [
            f"# {module_name}",
            "",
            MODULE_BLURBS.get(module_name, ""),
            "",
            "!!! info \"Client modules\"",
            "",
            f"    === \"Python\"",
            "",
            f"        ```python",
            f"        from tempo_sim import {client_module}",
            f"        ```",
            "",
            f"    === \"Rust\"",
            "",
            f"        ```rust",
            f"        use tempo_sim::{client_module};",
            f"        ```",
            "",
            f"    === \"C++\"",
            "",
            f"        ```cpp",
            f"        #include <tempo.h>  // tempo::{client_module}",
            f"        ```",
            "",
        ]
        doc_page = _PLUGIN_DOC_PAGE.get(module_name)
        if doc_page:
            out += [f"See [the {module_name} guide](../../plugins/{doc_page}.md) for the "
                    f"narrative documentation behind these RPCs.", ""]

        out += [
            "Every RPC below has a synchronous and an asynchronous form with the same",
            "signature - see [Client APIs](../../clients/index.md). Units follow",
            "[Tempo's conventions](../../concepts/conventions.md): meters, radians, and a",
            "right-handed frame.",
            "",
        ]

        services, messages, enums = [], [], []
        for file_proto, source in sorted(files_by_module[module_name], key=lambda p: p[0].name):
            comments = collect_comments(file_proto)
            package = file_proto.package
            rel_source = source.relative_to(PLUGIN_ROOT)
            for index, service in enumerate(file_proto.service):
                services.append((service, comments, (_SERVICE, index), rel_source))
            for index, message in enumerate(file_proto.message_type):
                messages.append((message, f"{package}.{message.name}", comments,
                                 (_MESSAGE, index)))
            for index, enum in enumerate(file_proto.enum_type):
                enums.append((enum, f"{package}.{enum.name}", comments, (_ENUM, index)))

        if services:
            out += ["## RPCs", ""]
            for service, comments, path, rel_source in services:
                service_doc = comments.get(path)
                if len(services) > 1 or service_doc:
                    out += [f"Defined by `{service.name}` in "
                            f"[`{rel_source}`](https://github.com/tempo-sim/Tempo/blob/main/{rel_source})."]
                    if service_doc:
                        out += ["", service_doc]
                    out += [""]
                for index, method in enumerate(service.method):
                    render_rpc(out, method, service, comments,
                               path + (_SERVICE_METHOD, index), registry, module_name,
                               client_module)
                    index_rows.append(
                        f"| [`{client_module}.{pascal_to_snake(method.name)}`]"
                        f"({slug}.md#{anchor_for(f'{service.name}.{method.name}')}) "
                        f"| {module_name} | {'stream' if method.server_streaming else 'unary'} "
                        f"| {(comments.get(path + (_SERVICE_METHOD, index), '').split(chr(10))[0]).replace('|', chr(92) + '|')} |"
                    )

        if messages:
            out += ["## Messages", ""]
            for message, full_name, comments, path in messages:
                render_message(out, message, full_name, comments, path, registry, module_name)

        if enums:
            out += ["## Enums", ""]
            for enum, full_name, comments, path in enums:
                render_enum(out, enum, full_name, comments, path)

        page = f"{API_DIR}/{slug}.md"
        with mkdocs_gen_files.open(page, "w") as handle:
            handle.write("\n".join(out).rstrip() + "\n")
        # Point the "edit this page" button at the protos, not the generated Markdown.
        first_source = files_by_module[module_name][0][1].relative_to(PLUGIN_ROOT)
        mkdocs_gen_files.set_edit_path(page, f"../{first_source}")

        summary.append(f"* [{module_name}]({slug}.md)")

    overview = [
        "# gRPC API Reference",
        "",
        "Generated from Tempo's `.proto` files on every documentation build, so it always",
        "matches the services the server exposes.",
        "",
        "Names are shown as the generated clients expose them - `tempo_world.spawn_actor`",
        "rather than `WorldControlService.SpawnActor`. The same name is used in Python, Rust",
        "and C++; Rust adds an `_async` suffix for the asynchronous form. Request fields are",
        "flattened into the wrapper's arguments, which is why each signature below takes the",
        "request's fields rather than a request message.",
        "",
        "!!! tip \"Looking for how something behaves, not what it is called?\"",
        "",
        "    The [plugin guides](../../plugins/index.md) explain the semantics - partial",
        "    transform updates, label tables, time modes - that a field table cannot.",
        "",
        "## Every RPC",
        "",
        "| RPC | Module | Kind | Description |",
        "| --- | --- | --- | --- |",
        *index_rows,
        "",
        "## By module",
        "",
        "| Module | Covers |",
        "| --- | --- |",
    ]
    for module_name in ordered:
        overview.append(f"| [{module_name}]({page_slug(module_name)}.md) "
                        f"| {MODULE_BLURBS.get(module_name, '')} |")

    with mkdocs_gen_files.open(f"{API_DIR}/index.md", "w") as handle:
        handle.write("\n".join(overview).rstrip() + "\n")

    with mkdocs_gen_files.open(f"{API_DIR}/SUMMARY.md", "w") as handle:
        handle.write("\n".join(summary) + "\n")


main()
