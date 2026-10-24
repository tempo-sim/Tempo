# Copyright Tempo Simulation, LLC. All Rights Reserved

import copy
import google.protobuf.descriptor as gpd
import importlib
import jinja2
import os
import sys
from pathlib import Path
from typing import List

from gen_project_package import PYTHON_METADATA_FILE, write_python_package
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
# carries the hand-written infra; the project package only owns its __init__.py and py.typed.
TEMPO_SIM_KEEP = {
    ".gitignore", "_tempo_context.py", "_tempo_context.pyi", "TempoImageUtils.py",
    "TempoLidarUtils.py", "unreal.py", "unreal.pyi", "world.py", "py.typed", "__init__.py",
}
PROJECT_PKG_KEEP = {".gitignore", "__init__.py", "py.typed"}


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
        "    {{ field.name }}: {{ field.py_annotation }} = {{ field.py_default }}{% if not loop.last %},{% endif %}\n" \
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
        "    {{ field.name }}: {{ field.py_annotation }} = {{ field.py_default }}{% if not loop.last %},{% endif %}\n" \
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
        "    {{ field.name }}: {{ field.py_annotation }} = {{ field.py_default }}{% if not loop.last %},{% endif %}\n" \
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
        "    {{ field.name }}: {{ field.py_annotation }} = {{ field.py_default }}{% if not loop.last %},{% endif %}\n" \
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
        "    {{ field.name }}: {{ field.py_annotation }} = {{ field.py_default }}{% if not loop.last %},{% endif %}\n" \
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
        "    {{ field.name }}: {{ field.py_annotation }} = {{ field.py_default }}{% if not loop.last %},{% endif %}\n" \
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
        "        {{ field.name }}: {{ field.py_annotation }} = {{ field.py_default }}{% if not loop.last %},{% endif %}\n" \
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

    # Template for a Call class that stages typed CallFunction arguments. One method per
    # `Value` oneof variant, so users never construct Value or FunctionArg messages themselves.
    call_template = \
        "import " \
        "{% for import in imports %}" \
        "{{ import }}{% if not loop.last %}, {% endif %}" \
        "{% endfor %}" \
        "\n\n\n" \
        "class Call:\n" \
        '    """Stages typed arguments and invokes a UFUNCTION in one CallFunction RPC.\n' \
        "\n" \
        "    Each ``*_arg`` method names one parameter and appends its value, returning ``self``\n" \
        "    for fluent chaining. Call :meth:`execute` (sync) or ``await`` :meth:`execute_async`\n" \
        "    to invoke. Every input parameter of the function must be supplied.\n" \
        '    """\n' \
        "\n" \
        "    def __init__(self, actor: str = '', component: str = '', function: str = ''):\n" \
        "        self._actor = actor\n" \
        "        self._component = component\n" \
        "        self._function = function\n" \
        "        self._args = []\n" \
        "\n" \
        "{% for method in methods %}" \
        "    def {{ method.name }}(\n" \
        "        self,\n" \
        "        name: str = '',\n" \
        "{% for field in method.fields %}" \
        "        {{ field.name }}: {{ field.py_annotation }} = {{ field.py_default }}{% if not loop.last %},{% endif %}\n" \
        "{% endfor %}" \
        "    ) -> 'Call':\n" \
        "        self._args.append({{ pb2 }}.FunctionArg(\n" \
        "            name=name,\n" \
        "            value={{ pb2 }}.Value(\n" \
        "                {{ method.oneof_field }}=" \
        "{% if method.wrapper_type %}{{ method.wrapper_type }}(" \
        "{% for field in method.fields %}{{ field.name }}={{ field.name }}" \
        "{% if not loop.last %}, {% endif %}{% endfor %})" \
        "{% else %}{{ method.fields[0].name }}{% endif %}\n" \
        "            )\n" \
        "        ))\n" \
        "        return self\n" \
        "\n" \
        "{% endfor %}" \
        "    async def _execute(self) -> {{ response_type }}:\n" \
        "        stub = await tempo_context().get_stub({{ pb2_grpc }}.{{ service_object }}Stub)\n" \
        "        request = {{ pb2 }}.CallFunctionRequest(\n" \
        "            actor=self._actor,\n" \
        "            component=self._component,\n" \
        "            function=self._function,\n" \
        "            args=self._args\n" \
        "        )\n" \
        "        return await stub.CallFunction(request)\n" \
        "\n" \
        "    def execute(self) -> {{ response_type }}:\n" \
        "        return run_async(self._execute())\n" \
        "\n" \
        "    async def execute_async(self) -> {{ response_type }}:\n" \
        "        return await self._execute()\n" \
        "\n\n" \
        "def call(actor: str = '', component: str = '', function: str = '') -> Call:\n" \
        '    """Create a builder for a CallFunction invocation."""\n' \
        "    return Call(actor, component, function)\n" \
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

    # Emit a Call builder class for any message in the proto graph named "Value" that exposes
    # its variants via a oneof named "value", alongside a CallFunction RPC.
    for proto_full_name, value_desc in list(all_messages.items()):
        if value_desc.object_name != "Value":
            continue
        if not value_desc.oneofs.get("value"):
            continue
        owning_service = None
        for service in all_services.values():
            if service.module_name.split(".")[:2] != value_desc.module_name.split(".")[:2]:
                continue
            if any(rpc.name == "CallFunction" for rpc in service.rpcs):
                owning_service = service
                break
        if owning_service is None:
            continue

        fields_by_name = {field.name: field for field in value_desc.fields}
        pb2_module = value_desc.module_name
        pb2_grpc_module = owning_service.module_name + "_grpc"
        imports = {pb2_module, pb2_grpc_module}

        methods = []
        for entry in value_desc.oneofs["value"]:
            variant_field = fields_by_name[entry["name"]]
            # bool_value -> bool_arg; int_vector_value -> int_vector_arg
            method_name = "{}_arg".format(
                entry["name"][:-6] if entry["name"].endswith("_value") else entry["name"]
            )
            wrapper_desc = all_messages.get(entry["message_proto_full_name"])
            # Flatten a wrapper message's fields into the method signature when they are all
            # scalars, mirroring how set_vector_property takes x/y/z rather than a Vector. A
            # wrapper with message-typed fields (Transform) is passed through whole instead.
            if wrapper_desc is not None and all(
                    f.proto_type != gpd.FieldDescriptor.TYPE_MESSAGE for f in wrapper_desc.fields):
                method_fields = wrapper_desc.fields
                wrapper_type = wrapper_desc.full_name
                imports.add(wrapper_desc.module_name)
            else:
                # Pass the variant through whole, under the same `value` parameter name the
                # singular set_*_property functions use.
                value_field = copy.copy(variant_field)
                value_field.name = "value"
                method_fields = [value_field]
                wrapper_type = None
                if variant_field.module_name:
                    imports.add(variant_field.module_name)
            methods.append({
                "name": method_name,
                "oneof_field": entry["name"],
                "wrapper_type": wrapper_type,
                "fields": method_fields,
            })

        call_rpc = next(rpc for rpc in owning_service.rpcs if rpc.name == "CallFunction")
        response_desc = all_messages[call_rpc.response_type]
        imports.add(response_desc.module_name)

        namespace = value_desc.module_name.split(".")[0]
        tempo_module_name = value_desc.module_name.split(".")[1]
        output_file = os.path.join(ns_dir[namespace], f"{pascal_to_snake(tempo_module_name)}.py")
        with open(output_file, 'a') as f:
            f.write(j2_environment.from_string(call_template).render(
                methods=methods,
                pb2=pb2_module,
                pb2_grpc=pb2_grpc_module,
                service_object=owning_service.object_name,
                response_type=response_desc.full_name,
                imports=sorted(imports),
            ))


def collect_input_files(project_root: Path, script_path: Path) -> List[Path]:
    """Collect all files that affect API generation."""
    files = [script_path]  # gen_api.py itself
    files.append((script_path.parent / "gen_common.py").resolve())
    files.append((script_path.parent / "gen_project_package.py").resolve())
    files.extend(find_files_filtered(project_root, {'.proto', '.Build.cs'}))
    # The project metadata file drives the generated pyproject.toml.
    metadata = project_root / "Content" / "Python" / "API" / PYTHON_METADATA_FILE
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
        write_python_package(project_root)

    # Update cache after successful generation
    output_files = collect_output_files(package_dirs, cache_project_api_dir, cache_project_pkg_dir)
    cache.update("gen_api", input_files, output_files,
                 input_base=project_root, output_base=project_root)
