# Copyright Tempo Simulation, LLC. All Rights Reserved
#
# Common utilities shared between Python and Rust API generation.

import google.protobuf.descriptor as gpd
import re


# Protobuf type mappings for Python
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


# Protobuf type mappings for Rust
protobuf_types_to_rust_types = {
    gpd.FieldDescriptor.TYPE_DOUBLE: "f64",
    gpd.FieldDescriptor.TYPE_FLOAT: "f32",
    gpd.FieldDescriptor.TYPE_INT64: "i64",
    gpd.FieldDescriptor.TYPE_UINT64: "u64",
    gpd.FieldDescriptor.TYPE_INT32: "i32",
    gpd.FieldDescriptor.TYPE_FIXED64: "u64",
    gpd.FieldDescriptor.TYPE_FIXED32: "u32",
    gpd.FieldDescriptor.TYPE_BOOL: "bool",
    gpd.FieldDescriptor.TYPE_STRING: "String",
    gpd.FieldDescriptor.TYPE_GROUP: None,
    gpd.FieldDescriptor.TYPE_MESSAGE: None,
    gpd.FieldDescriptor.TYPE_BYTES: "Vec<u8>",
    gpd.FieldDescriptor.TYPE_UINT32: "u32",
    gpd.FieldDescriptor.TYPE_ENUM: None,
    gpd.FieldDescriptor.TYPE_SFIXED32: "i32",
    gpd.FieldDescriptor.TYPE_SFIXED64: "i64",
    gpd.FieldDescriptor.TYPE_SINT32: "i32",
    gpd.FieldDescriptor.TYPE_SINT64: "i64",
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
        def __init__(self, field_descriptor, type_mapping=None):
            if type_mapping is None:
                type_mapping = protobuf_types_to_python_types
            self.module_name = None
            if field_descriptor.type == gpd.FieldDescriptor.TYPE_MESSAGE:
                self.field_type = field_descriptor.message_type.full_name
            elif field_descriptor.type == gpd.FieldDescriptor.TYPE_ENUM:
                self.field_type = field_descriptor.enum_type.full_name
            else:
                self.field_type = type_mapping[field_descriptor.type]
            self.label = protobuf_labels_to_python_labels[field_descriptor.label]
            # Special case for str fields. Default ("") will render as nothing, so we add some extra quotes
            self.default = field_descriptor.default_value if self.field_type != "str" else "''"
            self.name = field_descriptor.name
            # Store the raw protobuf type for Rust generation
            self.proto_type = field_descriptor.type

    def __init__(self, module_name, message_descriptor, type_mapping=None):
        super().__init__(module_name)
        self.object_name = message_descriptor.name
        self.fields = []
        for field_descriptor in message_descriptor.fields_by_name.values():
            self.fields.append(self.FieldDescriptor(field_descriptor, type_mapping))

    def resolve_names(self, all_messages_and_enums):
        for field in self.fields:
            if field.field_type in all_messages_and_enums:
                object = all_messages_and_enums[field.field_type]
                field.field_type = object.full_name
                field.module_name = object.module_name


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


def gather_messages(module_name, module_descriptor, type_mapping=None):
    messages = {}
    for message_descriptor in module_descriptor.message_types_by_name.values():
        messages[message_descriptor.full_name] = TempoMessageDescriptor(module_name, message_descriptor, type_mapping)
    return messages


def gather_services(module_name, module_descriptor):
    services = {}
    for service_descriptor in module_descriptor.services_by_name.values():
        services[service_descriptor.full_name] = TempoServiceDescriptor(module_name, service_descriptor)
    return services


def pascal_to_snake(string):
    return re.sub(r'(?<!^)(?=[A-Z])', '_', string).lower()
