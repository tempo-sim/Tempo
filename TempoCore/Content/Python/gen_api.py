# Copyright Tempo Simulation, LLC. All Rights Reserved

import importlib
import jinja2
import json
import os
import re
import sys
from pathlib import Path
from typing import List

from prebuild_cache import PrebuildCache, find_files_filtered

from gen_common import (
    INFRA_PACKAGE,
    gather_enums,
    gather_messages,
    gather_services,
    package_import_name,
    pascal_to_snake,
    project_package_name,
)

# Non-generated files that live alongside the generated wrappers in each package
# dir and must be preserved by the stale-file sweep. The tempo_sim package also
# carries the hand-written infra; the project package only owns its __init__.py.
TEMPO_SIM_KEEP = {
    ".gitignore", "_tempo_context.py", "TempoImageUtils.py",
    "TempoLidarUtils.py", "__init__.py",
}
PROJECT_PKG_KEEP = {".gitignore", "__init__.py"}


def generate_tempo_api(tempo_sim_dir, project_pkg_dir, project_import_name):
    """Generate per-service wrapper modules across the two distributions.

    Tempo-owned services land in `tempo_sim/<snake>.py`; project services land in
    `<project_pkg>/<snake>.py`. pb2 module names are namespace-qualified
    (`tempo_sim.TempoSensors.Sensors_pb2`, `tempo_sample.Greeter.*`), so wrappers
    reference proto types by their nested dotted path. Both packages import the
    runtime infra from `tempo_sim` — for the project package that's satisfied by
    its `tempo-sim` install dependency.

    Args:
        tempo_sim_dir: the tempo_sim package dir (.../API/tempo_sim).
        project_pkg_dir: the project package dir, or None if there are no project
            modules (in which case no project wrappers are emitted).
        project_import_name: the project package's import name (e.g. tempo_sample).
    """
    all_enums = {}
    all_messages = {}
    all_services = {}

    # (package dir, namespace, non-generated files to keep) per distribution.
    roots = [(tempo_sim_dir, INFRA_PACKAGE, TEMPO_SIM_KEEP)]
    if project_pkg_dir is not None:
        roots.append((project_pkg_dir, project_import_name, PROJECT_PKG_KEEP))
    ns_dir = {namespace: root for root, namespace, _ in roots}

    # Per-namespace set of top-level files that may be stale (generated wrappers
    # we didn't re-emit this run). Seeded with everything but the known keepers.
    potentially_stale = {}
    for root_dir, namespace, keep in roots:
        potentially_stale[namespace] = set(
            name for name in os.listdir(root_dir)
            if os.path.isfile(os.path.join(root_dir, name)) and name not in keep
        )

    for root_dir, namespace, _ in roots:
        module_dir_names = [name for name in os.listdir(root_dir)
                            if os.path.isdir(os.path.join(root_dir, name))]
        for module_dir_name in module_dir_names:
            module_root = os.path.join(root_dir, module_dir_name)
            contains_services = False
            for path, _, files in os.walk(module_root):
                for filename in files:
                    if filename.endswith("_pb2.py"):
                        file_path = os.path.join(path, filename)
                        rel_path = os.path.relpath(file_path, module_root)
                        # Fully namespace-qualified module name, e.g.
                        # tempo_sim.TempoCore.Empty_pb2 — matches the nested
                        # location and the imports protoc emitted inside the pb2.
                        module_name = "{}.{}.{}".format(
                            namespace, module_dir_name,
                            os.path.splitext(rel_path)[0].replace(os.sep, '.'))
                        module = importlib.import_module(module_name)
                        if hasattr(module, "DESCRIPTOR"):
                            module_descriptor = module.DESCRIPTOR
                            all_enums = all_enums | gather_enums(module_name, module_descriptor)
                            all_messages = all_messages | gather_messages(module_name, module_descriptor)
                            services = gather_services(module_name, module_descriptor)
                            if len(services) > 0:
                                contains_services = True
                            module_rpcs = set()
                            for service in services.values():
                                for rpc in service.rpcs:
                                    rpc_name = rpc.name
                                    if rpc_name in module_rpcs:
                                        raise Exception(f"Module {module_dir_name} has two rpcs named {rpc_name}. "
                                                        f"This is not allowed! Please rename one.")
                                    module_rpcs.add(rpc_name)
                            all_services = all_services | services
            if contains_services:
                output_file = f"{pascal_to_snake(module_dir_name)}.py"
                potentially_stale[namespace].discard(output_file)
                with open(os.path.join(root_dir, output_file), 'w') as f:
                    f.write("# Warning: Autogenerated code do not edit\n\n")
                    f.write(f"from {INFRA_PACKAGE}._tempo_context import tempo_context\n")
                    f.write(f"from {INFRA_PACKAGE} import run_async\n")
                    f.write("from curio.meta import awaitable\n")
                    f.write("import asyncio\n")
                    f.write("\n\n")

    for root_dir, namespace, _ in roots:
        for stale_file in potentially_stale[namespace]:
            os.remove(os.path.join(root_dir, stale_file))

    # The Protobuf-provided "descriptor" objects use the Protobuf names for classes, but we want the names of the
    # generated Python classes. The all_* dictionaries can provide that mapping, so we go through all the messages
    # once more to "resolve" all the types of message fields here.
    for tempo_message_descriptor in all_messages.values():
        tempo_message_descriptor.resolve_names(all_messages | all_enums)

    # Every RPC gets sync and async versions. The correct one will automatically be chosen by context.
    simple_rpc_template = \
        "import " \
        "{% for import in imports %}" \
        "{{ import }}{% if not loop.last %}, {% endif %}" \
        "{% endfor %}" \
        "\n" \
        "async def _{{ name }}(\n" \
        "{% for field in request.fields %}" \
        "    {{ field.name }}: {{ field.field_type }} = {{ field.default }}{% if not loop.last %},{% endif %}\n" \
        "{% endfor %}" \
        ") -> {{ response.full_name }}:\n" \
        "    stub = await tempo_context().get_stub({{ service.module_name }}_grpc.{{ service.object_name }}Stub)\n" \
        "    request = {{ request.full_name }}(\n" \
        "{% for field in request.fields %}" \
        "        {{ field.name }}={{ field.name }}{% if not loop.last %},{% endif %}\n" \
        "{% endfor %}" \
        "    )\n" \
        "    return await stub.{{ rpc.name }}(request)\n" \
        "\n\n" \
        "def {{ name }}(\n" \
        "{% for field in request.fields %}" \
        "    {{ field.name }}: {{ field.field_type }} = {{ field.default }}{% if not loop.last %},{% endif %}\n" \
        "{% endfor %}" \
        ") -> {{ response.full_name }}:\n" \
        "    return run_async(_{{ name }}(\n" \
        "{% for field in request.fields %}" \
        "        {{ field.name }}={{ field.name }}{% if not loop.last %},{% endif %}\n" \
        "{% endfor %}" \
        "    ))\n" \
        "\n\n" \
        "@awaitable({{ name }})\n" \
        "async def {{ name }}(\n" \
        "{% for field in request.fields %}" \
        "    {{ field.name }}: {{ field.field_type }} = {{ field.default }}{% if not loop.last %},{% endif %}\n" \
        "{% endfor %}" \
        ") -> {{ response.full_name }}:\n" \
        "    return await _{{ name }}(\n" \
        "{% for field in request.fields %}" \
        "        {{ field.name }}={{ field.name }}{% if not loop.last %},{% endif %}\n" \
        "{% endfor %}" \
        "    )\n" \
        "\n\n" \

    streaming_rpc_template = \
        "import " \
        "{% for import in imports %}" \
        "{{ import }}{% if not loop.last %}, {% endif %}" \
        "{% endfor %}" \
        "\n" \
        "async def _{{ name }}(\n" \
        "{% for field in request.fields %}" \
        "    {{ field.name }}: {{ field.field_type }} = {{ field.default }}{% if not loop.last %},{% endif %}\n" \
        "{% endfor %}" \
        ") -> {{ response.full_name }}:\n" \
        "    stub = await tempo_context().get_stub({{ service.module_name }}_grpc.{{ service.object_name }}Stub)\n" \
        "    request = {{ request.full_name }}(\n" \
        "{% for field in request.fields %}" \
        "        {{ field.name }}={{ field.name }}{% if not loop.last %},{% endif %}\n" \
        "{% endfor %}" \
        "    )\n" \
        "    async for response in stub.{{ rpc.name }}(request):\n" \
        "        yield response\n" \
        "\n\n" \
        "def {{ name }}(\n" \
        "{% for field in request.fields %}" \
        "    {{ field.name }}: {{ field.field_type }} = {{ field.default }}{% if not loop.last %},{% endif %}\n" \
        "{% endfor %}" \
        ") -> {{ response.full_name }}:\n" \
        "    async_gen = _{{ name }}(\n" \
        "{% for field in request.fields %}" \
        "        {{ field.name }}={{ field.name }}{% if not loop.last %},{% endif %}\n" \
        "{% endfor %}" \
        "    )\n" \
        "    while True:\n" \
        "        try:\n" \
        "            yield run_async(async_gen.__anext__())\n" \
        "        except StopAsyncIteration:\n" \
        "            break\n" \
        "\n\n" \
        "@awaitable({{ name }})\n" \
        "async def {{ name }}(\n" \
        "{% for field in request.fields %}" \
        "    {{ field.name }}: {{ field.field_type }} = {{ field.default }}{% if not loop.last %},{% endif %}\n" \
        "{% endfor %}" \
        ") -> {{ response.full_name }}:\n" \
        "    async for response in _{{ name }}(\n" \
        "{% for field in request.fields %}" \
        "        {{ field.name }}={{ field.name }}{% if not loop.last %},{% endif %}\n" \
        "{% endfor %}" \
        "    ):\n" \
        "        yield response\n" \
        "\n\n" \

    # Template for a Batch class that wraps a oneof-of-Set*PropertyRequest into
    # a fluent builder, so users don't have to construct SetPropertyOp messages
    # themselves. One method per oneof variant; execute() / execute_async() send
    # one SetProperties RPC for the staged ops.
    batch_template = \
        "import " \
        "{% for import in imports %}" \
        "{{ import }}{% if not loop.last %}, {% endif %}" \
        "{% endfor %}" \
        "\n\n\n" \
        "class Batch:\n" \
        '    """Stages property-set operations and submits them in one SetProperties RPC.\n' \
        "\n" \
        "    Methods mirror the singular ``set_*_property`` free functions; each appends an op\n" \
        "    to the batch and returns ``self`` for fluent chaining. Call :meth:`execute` (sync)\n" \
        "    or ``await`` :meth:`execute_async` to send.\n" \
        '    """\n' \
        "\n" \
        "    def __init__(self):\n" \
        "        self._ops = []\n" \
        "\n" \
        "{% for method in methods %}" \
        "    def {{ method.name }}(\n" \
        "        self,\n" \
        "{% for field in method.fields %}" \
        "        {{ field.name }}: {{ field.field_type }} = {{ field.default }}{% if not loop.last %},{% endif %}\n" \
        "{% endfor %}" \
        "    ) -> 'Batch':\n" \
        "        self._ops.append({{ pb2 }}.SetPropertyOp(\n" \
        "            {{ method.oneof_field }}={{ pb2 }}.{{ method.request_type }}(\n" \
        "{% for field in method.fields %}" \
        "                {{ field.name }}={{ field.name }}{% if not loop.last %},{% endif %}\n" \
        "{% endfor %}" \
        "            )\n" \
        "        ))\n" \
        "        return self\n" \
        "\n" \
        "{% endfor %}" \
        "    async def _execute(self) -> {{ pb2 }}.SetPropertiesResponse:\n" \
        "        stub = await tempo_context().get_stub({{ pb2_grpc }}.{{ service_object }}Stub)\n" \
        "        request = {{ pb2 }}.SetPropertiesRequest(ops=self._ops)\n" \
        "        return await stub.SetProperties(request)\n" \
        "\n" \
        "    def execute(self) -> {{ pb2 }}.SetPropertiesResponse:\n" \
        "        return run_async(self._execute())\n" \
        "\n" \
        "    async def execute_async(self) -> {{ pb2 }}.SetPropertiesResponse:\n" \
        "        return await self._execute()\n" \
        "\n\n" \
        "def batch() -> Batch:\n" \
        '    """Create a new property-set batch builder."""\n' \
        "    return Batch()\n" \
        "\n\n"

    j2_environment = jinja2.Environment()
    for service_name, tempo_service_descriptor in all_services.items():
        namespace = tempo_service_descriptor.module_name.split(".")[0]
        tempo_module_name = tempo_service_descriptor.module_name.split(".")[1]
        with open(os.path.join(ns_dir[namespace], f"{pascal_to_snake(tempo_module_name)}.py"), 'a') as f:
            for rpc_descriptor in tempo_service_descriptor.rpcs:
                tempo_request_descriptor = all_messages[rpc_descriptor.request_type]
                tempo_response_descriptor = all_messages[rpc_descriptor.response_type]
                templates = []
                if rpc_descriptor.client_streaming:
                    raise Exception("Client streaming RPCs are not yet supported")
                if rpc_descriptor.server_streaming:
                    templates.append(j2_environment.from_string(streaming_rpc_template))
                else:
                    templates.append(j2_environment.from_string(simple_rpc_template))
                imports = set()
                imports.add("{}_grpc".format(tempo_service_descriptor.module_name))
                imports.add(tempo_request_descriptor.module_name)
                imports.add(tempo_response_descriptor.module_name)
                for field in tempo_request_descriptor.fields:
                    if field.module_name:
                        imports.add(field.module_name)
                for template in templates:
                    f.write(template.render(
                        name=pascal_to_snake(rpc_descriptor.name),
                        rpc=rpc_descriptor,
                        service=tempo_service_descriptor,
                        request=tempo_request_descriptor,
                        response=tempo_response_descriptor,
                        imports=imports
                    ))

    # Emit a Batch builder class for any message in the proto graph that has the
    # canonical name "SetPropertyOp" and exposes its variants via a oneof named "op".
    # Each variant must be a TYPE_MESSAGE whose request type is also in all_messages.
    for proto_full_name, set_property_op_desc in list(all_messages.items()):
        if set_property_op_desc.object_name != "SetPropertyOp":
            continue
        op_variants = set_property_op_desc.oneofs.get("op")
        if not op_variants:
            continue
        # Find the service in the same proto module that exposes SetProperties.
        proto_package = proto_full_name.rsplit(".", 1)[0]
        owning_service = None
        for service in all_services.values():
            if service.module_name.split(".")[:2] != set_property_op_desc.module_name.split(".")[:2]:
                continue
            if any(rpc.name == "SetProperties" for rpc in service.rpcs):
                owning_service = service
                break
        if owning_service is None:
            continue

        methods = []
        for entry in op_variants:
            request_desc = all_messages.get(entry["message_proto_full_name"])
            if request_desc is None:
                continue
            # bool_op -> set_bool_property; int_array_op -> set_int_array_property
            method_name = "set_{}_property".format(
                entry["name"][:-3] if entry["name"].endswith("_op") else entry["name"]
            )
            methods.append({
                "name": method_name,
                "oneof_field": entry["name"],
                "request_type": request_desc.object_name,
                "fields": request_desc.fields,
            })

        pb2_module = set_property_op_desc.module_name        # e.g. TempoWorld.WorldControl_pb2
        pb2_grpc_module = owning_service.module_name + "_grpc"
        imports = sorted({pb2_module, pb2_grpc_module})

        namespace = set_property_op_desc.module_name.split(".")[0]
        tempo_module_name = set_property_op_desc.module_name.split(".")[1]
        output_file = os.path.join(ns_dir[namespace], f"{pascal_to_snake(tempo_module_name)}.py")
        with open(output_file, 'a') as f:
            f.write(j2_environment.from_string(batch_template).render(
                methods=methods,
                pb2=pb2_module,
                pb2_grpc=pb2_grpc_module,
                service_object=owning_service.object_name,
                imports=imports,
            ))


# Filename of the project-owned publish-metadata override (mirror of the Rust
# crate's crate_info.json). Lives in the project's Content/Python/API dir.
PROJECT_METADATA_FILE = "project_info.json"


def collect_input_files(project_root: Path, script_path: Path) -> List[Path]:
    """Collect all files that affect API generation."""
    files = [script_path]  # gen_api.py itself
    files.append((script_path.parent / "gen_common.py").resolve())
    files.extend(find_files_filtered(project_root, {'.proto', '.Build.cs'}))
    # The project metadata file drives the generated pyproject.toml.
    metadata = project_root / "Content" / "Python" / "API" / PROJECT_METADATA_FILE
    if metadata.exists():
        files.append(metadata)
    return files


def collect_output_files(package_dirs, project_api_dir=None, project_pkg_dir=None) -> List[Path]:
    """Collect generated API wrapper files across the given package dirs.

    Generated files are snake_case .py files at a package dir's top level,
    excluding the non-generated keepers and any pb2 modules.

    When the project package is present, its generated metadata files
    (pyproject.toml, .gitignore, __init__.py) are tracked too — they're
    overwritten every run, so listing them as outputs makes the cache
    regenerate if any is deleted. Listing a path that doesn't exist yet simply
    invalidates the cache (the desired behavior on first run).
    """
    keep = TEMPO_SIM_KEEP | PROJECT_PKG_KEEP
    files = []
    for package_dir in package_dirs:
        if not package_dir.exists():
            continue
        for f in package_dir.iterdir():
            if f.is_file() and f.suffix == ".py" and f.name not in keep and "_pb2" not in f.name:
                files.append(f)
    if project_api_dir is not None and project_pkg_dir is not None:
        files += [project_api_dir / "pyproject.toml",
                  project_api_dir / ".gitignore",
                  project_pkg_dir / "__init__.py"]
    return files


def read_project_metadata(project_api_dir: Path) -> dict:
    """Read project-owned publish metadata from project_info.json, if present.

    Mirror of gen_rust_api's read_project_metadata: the project's hook for
    setting the generated pyproject.toml's publish fields (version, license,
    urls, extra dependencies) WITHOUT editing this Tempo-owned script or the
    generated pyproject.toml — both overwritten every run. Every field is
    optional; an absent file returns {} and the generator falls back to
    placeholders.
    """
    path = project_api_dir / PROJECT_METADATA_FILE
    if not path.exists():
        return {}
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as e:
        raise RuntimeError(f"Invalid JSON in {path}: {e}") from e
    if not isinstance(data, dict):
        raise RuntimeError(f"{path} must contain a JSON object, got {type(data).__name__}")
    return data


def _read_package_version(pyproject_path: Path) -> str:
    """Pull the project.version out of tempo-sim's pyproject.toml (no TOML dep)."""
    text = pyproject_path.read_text(encoding="utf-8")
    match = re.search(r'^\s*version\s*=\s*"([^"]+)"', text, re.MULTILINE)
    if not match:
        raise RuntimeError(f"Could not find project version in {pyproject_path}")
    return match.group(1)


def _toml_str(value) -> str:
    """Render a Python value as a TOML basic string, escaping backslashes/quotes."""
    return '"' + str(value).replace("\\", "\\\\").replace('"', '\\"') + '"'


def write_project_pyproject(project_api_dir: Path, dist_name: str, import_name: str,
                            tempo_sim_version: str, metadata: dict):
    """Write the project package's pyproject.toml.

    Always overwritten — it's a generated artifact, like the Rust crate's
    Cargo.toml. `tempo-sim==<version>` is pinned to the single-sourced tempo-sim
    version so the project package always matches the plugin it was generated
    against. Publish metadata (version, license, urls, extra deps) comes from
    project_info.json; absent keys fall back to placeholders.
    """
    metadata = metadata or {}
    version = metadata.get("version", "0.1.0")
    description = metadata.get(
        "description",
        "Project-specific Tempo client API; layers project protos on top of tempo-sim.")

    deps = [f"tempo-sim=={tempo_sim_version}"]
    extra_deps = metadata.get("dependencies", [])
    if not isinstance(extra_deps, list):
        raise RuntimeError(f'{PROJECT_METADATA_FILE} "dependencies" must be a JSON array')
    deps.extend(extra_deps)

    lines = [
        "# Auto-generated by Tempo's gen_api.py. Do not edit.",
        f"# Publish metadata (license, urls, extra deps) comes from {PROJECT_METADATA_FILE}",
        "# in this directory — edit that file, not this one.",
        "",
        "[build-system]",
        'requires = ["setuptools>=61.0"]',
        'build-backend = "setuptools.build_meta"',
        "",
        "[project]",
        f"name = {_toml_str(dist_name)}",
        f"version = {_toml_str(version)}",
        f"description = {_toml_str(description)}",
        'requires-python = ">=3.9"',
    ]

    # PEP 621 table form for broad setuptools compatibility (the bundled UE
    # Python ships setuptools 65, which predates the PEP 639 bare-string form).
    if "license" in metadata:
        lines.append(f"license = {{ text = {_toml_str(metadata['license'])} }}")
    else:
        lines += [
            f"# TODO: set \"license\" in {PROJECT_METADATA_FILE} before publishing — \"Apache-2.0\"",
            "# is a placeholder so the build is quiet; replace it with this project's actual license.",
            'license = { text = "Apache-2.0" }',
        ]
    if "readme" in metadata:
        lines.append(f"readme = {_toml_str(metadata['readme'])}")

    lines.append("dependencies = [")
    lines += [f"    {_toml_str(d)}," for d in deps]
    lines.append("]")

    # Optional project URLs from metadata (repository, homepage, documentation).
    url_keys = [k for k in ("homepage", "repository", "documentation") if k in metadata]
    if url_keys:
        lines += ["", "[project.urls]"]
        label = {"homepage": "Homepage", "repository": "Repository",
                 "documentation": "Documentation"}
        for k in url_keys:
            lines.append(f"{label[k]} = {_toml_str(metadata[k])}")

    lines += [
        "",
        "[tool.setuptools.packages.find]",
        f'include = [{_toml_str(import_name)}, {_toml_str(import_name + ".*")}]',
    ]

    (project_api_dir / "pyproject.toml").write_text("\n".join(lines) + "\n", encoding="utf-8")


def write_project_init(project_pkg_dir: Path):
    """Write the project package's __init__.py.

    The project package has no infra of its own — it re-exports tempo-sim's
    runtime helpers so consumers can `from <project> import set_server` without
    separately importing tempo_sim (mirror of the Rust project crate's lib.rs
    re-export). Satisfied at runtime by the `tempo-sim` install dependency.
    """
    (project_pkg_dir / "__init__.py").write_text(
        "# Warning: Autogenerated code do not edit\n\n"
        f"from {INFRA_PACKAGE} import set_server, run_async  # noqa: F401\n",
        encoding="utf-8",
    )


def write_project_gitignore(project_api_dir: Path, import_name: str):
    """Write the project package's .gitignore.

    The whole package dir is generated (pb2 + wrappers + __init__), so it stays
    out of git; consumers regenerate it locally. The generated pyproject.toml and
    the user's project_info.json are tracked.
    """
    (project_api_dir / ".gitignore").write_text(
        "# Build artifacts\n"
        "/build/\n"
        "/dist/\n"
        "*.egg-info/\n"
        "\n"
        "# Generated Python package (pb2 + wrappers, populated by gen_protos.py / gen_api.py)\n"
        f"/{import_name}/\n",
        encoding="utf-8",
    )


if __name__ == "__main__":
    if sys.version_info[0] < 3 or sys.version_info[1] < 9:
        raise Exception("This script requires Python 3.9 or greater (found {}.{}.{})"
                        .format(sys.version_info[0], sys.version_info[1], sys.version_info[2]))

    if len(sys.argv) < 3:
        print("Usage: gen_api.py <project_root> <plugin_root>", file=sys.stderr)
        sys.exit(1)

    project_root = Path(sys.argv[1])
    plugin_root = Path(sys.argv[2])
    api_dir = Path(os.path.dirname(os.path.realpath(__file__))) / "API"
    tempo_sim_dir = api_dir / INFRA_PACKAGE

    project_import_name = package_import_name(project_package_name(project_root))
    project_api_dir = project_root / "Content" / "Python" / "API"
    project_pkg_dir = project_api_dir / project_import_name
    # gen_protos.py only creates the project package when project modules exist.
    has_project_pkg = project_pkg_dir.exists()

    # Insert the API dirs (parents of the package dirs) at the front so the nested
    # generated pb2 shadow any stale copy in the venv's site-packages, and so the
    # namespace-qualified imports protoc emits (e.g. `from tempo_sim.TempoCore
    # import Empty_pb2`) resolve to the local tree — each pb2 module is loaded
    # exactly once and registers its descriptor once. Project pb2 importing Tempo
    # pb2 need both API dirs on the path.
    sys.path.insert(0, str(api_dir))
    if has_project_pkg:
        sys.path.insert(0, str(project_api_dir))

    package_dirs = [tempo_sim_dir] + ([project_pkg_dir] if has_project_pkg else [])

    # Generated project metadata to track in the cache (only when the project
    # package exists).
    cache_project_api_dir = project_api_dir if has_project_pkg else None
    cache_project_pkg_dir = project_pkg_dir if has_project_pkg else None

    # Check cache
    cache = PrebuildCache(plugin_root / ".tempo_prebuild_cache.json")
    input_files = collect_input_files(project_root, Path(__file__))
    output_files = collect_output_files(package_dirs, cache_project_api_dir, cache_project_pkg_dir)

    if cache.is_valid("gen_api", input_files, output_files,
                      input_base=project_root, output_base=project_root):
        print("[Tempo Prebuild]  Skipping Python API generation (no changes detected)", flush=True)
        sys.exit(0)

    print("[Tempo Prebuild] Generating Python API", flush=True)
    generate_tempo_api(
        str(tempo_sim_dir),
        str(project_pkg_dir) if has_project_pkg else None,
        project_import_name,
    )

    if has_project_pkg:
        tempo_sim_version = _read_package_version(api_dir / "pyproject.toml")
        project_metadata = read_project_metadata(project_api_dir)
        write_project_pyproject(project_api_dir, project_package_name(project_root),
                                project_import_name, tempo_sim_version, project_metadata)
        write_project_init(project_pkg_dir)
        write_project_gitignore(project_api_dir, project_import_name)

    # Update cache after successful generation
    output_files = collect_output_files(package_dirs, cache_project_api_dir, cache_project_pkg_dir)
    cache.update("gen_api", input_files, output_files,
                 input_base=project_root, output_base=project_root)
