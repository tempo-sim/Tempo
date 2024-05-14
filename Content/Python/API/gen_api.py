# Copyright Tempo Simulation, LLC. All Rights Reserved

import google.protobuf.descriptor as gpd
import importlib
import jinja2
import os
import re
import sys


protobuf_types_to_python_types = {
    gpd.FieldDescriptor.TYPE_DOUBLE: "float",
    gpd.FieldDescriptor.TYPE_FLOAT: "float",
    gpd.FieldDescriptor.TYPE_INT64: "int",
    gpd.FieldDescriptor.TYPE_UINT64: "int",
    gpd.FieldDescriptor.TYPE_INT32: "int",
    gpd.FieldDescriptor.TYPE_FIXED64: "int",
    gpd.FieldDescriptor.TYPE_FIXED32: "int",
    gpd.FieldDescriptor.TYPE_BOOL: "bool",
    gpd.FieldDescriptor.TYPE_STRING: "str",
    gpd.FieldDescriptor.TYPE_GROUP: None,
    gpd.FieldDescriptor.TYPE_MESSAGE: None,
    gpd.FieldDescriptor.TYPE_BYTES: "bytearray",
    gpd.FieldDescriptor.TYPE_UINT32: "int",
    gpd.FieldDescriptor.TYPE_ENUM: None, 
    gpd.FieldDescriptor.TYPE_SFIXED32: "int",
    gpd.FieldDescriptor.TYPE_SFIXED64: "int",
    gpd.FieldDescriptor.TYPE_SINT32: "int",
    gpd.FieldDescriptor.TYPE_SINT64: "int",
    gpd.FieldDescriptor.MAX_TYPE: None
}


protobuf_labels_to_python_labels = {
    gpd.FieldDescriptor.LABEL_OPTIONAL: "optional",
    gpd.FieldDescriptor.LABEL_REPEATED: "repeated",
    gpd.FieldDescriptor.LABEL_REQUIRED: "required",
    gpd.FieldDescriptor: None
}


class TempoObjectDescriptor(object):
    def __init__(self, module_name):
        self.module_name = module_name
        self.object_name = None

    @property
    def full_name(self):
        return f"{self.module_name}.{self.object_name}"


class TempoEnumDescriptor(TempoObjectDescriptor):
    def __init__(self, module_name, enum_descriptor):
        super().__init__(module_name)
        self.object_name = enum_descriptor.name
        

class TempoMessageDescriptor(TempoObjectDescriptor):
    class FieldDescriptor(object):
        def __init__(self, field_descriptor):
            if field_descriptor.type == gpd.FieldDescriptor.TYPE_MESSAGE:
                self.field_type = field_descriptor.message_type.full_name
            elif field_descriptor.type == gpd.FieldDescriptor.TYPE_ENUM:
                self.field_type = field_descriptor.enum_type.full_name
            else:
                self.field_type = protobuf_types_to_python_types[field_descriptor.type]
            self.label = protobuf_labels_to_python_labels[field_descriptor.label]
            self.default = field_descriptor.default_value
            self.name = field_descriptor.name

    def __init__(self, module_name, message_descriptor):
        super().__init__(module_name)
        self.object_name = message_descriptor.name
        self.fields = []
        for field_descriptor in message_descriptor.fields_by_name.values():
            self.fields.append(self.FieldDescriptor(field_descriptor))

    def resolve_names(self, all_messages_and_enums):
        for field in self.fields:
            if field.field_type in all_messages_and_enums:
                field.field_type = all_messages_and_enums[field.field_type].full_name


class TempoServiceDescriptor(TempoObjectDescriptor):
    class RPCDescriptor(object):
        def __init__(self, rpc_descriptor):
            self.name = rpc_descriptor.name
            self.server_streaming = rpc_descriptor.server_streaming
            self.client_streaming = rpc_descriptor.client_streaming
            self.request_type = rpc_descriptor.input_type.full_name
            self.response_type = rpc_descriptor.output_type.full_name

    def __init__(self, module_name, service_descriptor):
        super().__init__(module_name)
        self.object_name = service_descriptor.name
        self.rpcs = []

        for rpc_descriptor in service_descriptor.methods_by_name.values():
            self.rpcs.append(self.RPCDescriptor(rpc_descriptor))


def gather_enums(module_name, module_descriptor):
    enums = {}
    for enum_descriptor in module_descriptor.enum_types_by_name.values():
        enums[enum_descriptor.full_name] = TempoEnumDescriptor(module_name, enum_descriptor)
    return enums


def gather_messages(module_name, module_descriptor):
    messages = {}
    for message_descriptor in module_descriptor.message_types_by_name.values():
        messages[message_descriptor.full_name] = TempoMessageDescriptor(module_name, message_descriptor)
    return messages


def gather_services(module_name, module_descriptor):
    services = {}
    for service_descriptor in module_descriptor.services_by_name.values():
        services[service_descriptor.full_name] = TempoServiceDescriptor(module_name, service_descriptor)
    return services


def pascal_to_snake(string):
    return re.sub(r'(?<!^)(?=[A-Z])', '_', string).lower()


def generate_tempo_api(root_dir):
    all_enums = {}
    all_messages = {}
    all_services = {}
    potentially_stale_files = set([name for name in os.listdir(root_dir) 
                                   if os.path.isfile(os.path.join(root_dir, name))])
    # Non-generated files
    potentially_stale_files.remove(".gitignore")
    potentially_stale_files.remove("_tempo_context.py")
    tempo_module_names = [name for name in os.listdir(root_dir) if os.path.isdir(os.path.join(root_dir, name))]
    for tempo_module_name in tempo_module_names:
        tempo_module_root = os.path.join(root_dir, tempo_module_name)
        contains_services = False
        for path, _, files in os.walk(tempo_module_root):
            for filename in files:
                if filename.endswith("_pb2.py"):
                    file_path = os.path.join(path, filename)
                    rel_path = os.path.relpath(file_path, tempo_module_root)
                    module_name = "{}.{}".format(
                        tempo_module_name, os.path.splitext(rel_path)[0].replace(os.sep, '.'))
                    module = importlib.import_module(f"tempo.{module_name}")
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
                                    raise Exception(f"Module {tempo_module_name} has two rpcs named {rpc_name}. "
                                                    f"This is not allowed! Please rename one.")
                                module_rpcs.add(rpc_name)
                        all_services = all_services | services
        if contains_services:
            output_file = f"{pascal_to_snake(tempo_module_name)}.py"
            potentially_stale_files.discard(output_file)
            with open(os.path.join(root_dir, output_file), 'w') as f:
                f.write("# Warning: Autogenerated code do not edit\n\n")
                f.write("from tempo._tempo_context import TempoContext\n")
                f.write("from curio.meta import awaitable\n")
                f.write("import asyncio\n")
                f.write("\n\n")
                
    for stale_file in potentially_stale_files:
        os.remove(os.path.join(root_dir, stale_file))

    # The Protobuf-provided "descriptor" objects use the Protobuf names for classes, but we want the names of the
    # generated Python classes. The all_* dictionaries can provide that mapping, so we go through all the messages
    # once more to "resolve" all the types of message fields here.
    for tempo_message_descriptor in all_messages.values():
        tempo_message_descriptor.resolve_names(all_messages | all_enums)

    # Every RPC gets sync and async versions. The correct one will automatically be chosen by context.
    simple_rpc_template = \
        "import {{ service.module_name }}_grpc, {{ request.module_name }}, {{ response.module_name }}\n" \
        "async def _{{ name }}(\n" \
        "{% for field in request.fields %}" \
        "    {{ field.name }}: {{ field.field_type }} = {{ field.default }}{% if not loop.last %},{% endif %}\n" \
        "{% endfor %}" \
        ") -> {{ response.full_name }}:\n" \
        "    context = TempoContext() # Singleton\n" \
        "    stub = context.get_stub({{ service.module_name }}_grpc.{{ service.object_name }}Stub)\n" \
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
        "    return asyncio.run(_{{ name }}(\n" \
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
        "import {{ service.module_name }}_grpc, {{ request.module_name }}, {{ response.module_name }}\n" \
        "async def _{{ name }}(\n" \
        "{% for field in request.fields %}" \
        "    {{ field.name }}: {{ field.field_type }} = {{ field.default }}{% if not loop.last %},{% endif %}\n" \
        "{% endfor %}" \
        ") -> {{ response.full_name }}:\n" \
        "    context = TempoContext() # Singleton\n" \
        "    stub = context.get_stub({{ service.module_name }}_grpc.{{ service.object_name }}Stub)\n" \
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
        "    loop = asyncio.get_event_loop()\n" \
        "    while True:\n" \
        "        try:\n" \
        "            yield loop.run_until_complete(async_gen.__anext__())\n" \
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

    j2_environment = jinja2.Environment()
    for service_name, tempo_service_descriptor in all_services.items():
        tempo_module_name = tempo_service_descriptor.module_name.split(".")[0]
        with open(os.path.join(root_dir, f"{pascal_to_snake(tempo_module_name)}.py"), 'a') as f:
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
                for template in templates:
                    f.write(template.render(
                        name=pascal_to_snake(rpc_descriptor.name),
                        rpc=rpc_descriptor,
                        service=tempo_service_descriptor,
                        request=tempo_request_descriptor,
                        response=tempo_response_descriptor,
                    ))        


if __name__ == "__main__":
    if sys.version_info[0] < 3 or sys.version_info[1] < 9:
        raise Exception("This script requires Python 3.9 or greater (found {}.{}.{})"
                        .format(sys.version_info[0], sys.version_info[1], sys.version_info[2]))
    root_dir = os.path.join(os.path.dirname(os.path.realpath(__file__)), "tempo")
    generate_tempo_api(root_dir)
