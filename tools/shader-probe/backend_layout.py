"""SPIR-V reflection and the bounded Phase 2d Xenos host layout."""

from spirv_parser import STORAGE_NAMES, decode_string, parse_spirv
from vertex_layout import DEFAULT_VERTEX_LAYOUT, profile_for


class LayoutError(ValueError):
    """The compiled module cannot be represented by the published host ABI."""


def _type_format(module, type_id, context):
    item = module.types.get(type_id)
    if item is None:
        raise LayoutError(f"{context} has an unknown SPIR-V type %{type_id}")
    if item.opcode == 22 and len(item.operands) == 2 and item.operands[1] == 32:
        return "float"
    if item.opcode == 23 and len(item.operands) == 3:
        scalar = module.types.get(item.operands[1])
        if scalar is not None and scalar.opcode == 22 and scalar.operands[1] == 32:
            width = item.operands[2]
            if 2 <= width <= 4:
                return "float" + str(width)
    raise LayoutError(f"{context} uses an unsupported interface type")


def _pointer_pointee(module, pointer_id, context):
    pointer = module.types.get(pointer_id)
    if pointer is None or pointer.opcode != 32 or len(pointer.operands) != 3:
        raise LayoutError(f"{context} has an unsupported pointer type")
    return pointer.operands[2]


def _member_names(module):
    names = {}
    for instruction in module.instructions:
        if instruction.opcode == 6:  # OpMemberName is not indexed by the parser.
            struct_id, member_index = instruction.operands[:2]
            names[(struct_id, member_index)] = decode_string(instruction.operands[2:])[0]
    return names


def _uniform_member(module, struct_id, index, member_type, names):
    name = names.get((struct_id, index))
    if not name:
        raise LayoutError(f"uniform struct %{struct_id} member {index} has no OpMemberName")
    decorations = module.member_decorations.get((struct_id, index), {})
    offset = decorations.get(35, (None,))[0]
    if type(offset) is not int:
        raise LayoutError(f"uniform member {name!r} has no valid Offset decoration")
    if offset < 0 or offset % 16:
        raise LayoutError(f"uniform member {name!r} offset must be nonnegative and vec4-aligned")

    type_item = module.types.get(member_type)
    major_decorations = [(decoration, major) for decoration, major in
                         ((4, "row_major"), (5, "column_major"))
                         if decoration in decorations]
    if type_item is None:
        raise LayoutError(f"uniform member {name!r} has an unknown SPIR-V type")
    if type_item.opcode == 24:
        if len(type_item.operands) != 3:
            raise LayoutError(f"uniform matrix {name!r} has a malformed type")
        column_type = module.types.get(type_item.operands[1])
        if (column_type is None or column_type.opcode != 23 or
                len(column_type.operands) != 3 or type_item.operands[2] != 4 or
                column_type.operands[2] != 4):
            raise LayoutError(f"uniform matrix {name!r} must be float4x4")
        scalar = module.types.get(column_type.operands[1])
        if scalar is None or scalar.opcode != 22 or scalar.operands[1] != 32:
            raise LayoutError(f"uniform matrix {name!r} must be float32")
        stride = decorations.get(7, (None,))[0]
        if len(major_decorations) != 1 or stride != 16:
            raise LayoutError(
                f"uniform matrix {name!r} requires exactly one major decoration and MatrixStride 16"
            )
        return ({"name": name, "type": "float4x4", "offset": offset, "size": 64},
                offset + 64)

    if major_decorations or 7 in decorations:
        raise LayoutError(f"non-matrix uniform member {name!r} has matrix decorations")
    fmt = _type_format(module, member_type, f"uniform member {name!r}")
    size = {"float": 4, "float2": 8, "float3": 12, "float4": 16}[fmt]
    return ({"name": name, "type": fmt, "offset": offset, "size": size}, offset + size)


def _reflect_uniform(module, variable, pointer_type, set_index, binding, names):
    name = module.names.get(variable) or f"%{variable}"
    if set_index != 0 or binding != 0:
        raise LayoutError(
            f"only constant buffer b0/set0 is supported; {name!r} has set={set_index}, binding={binding}"
        )
    struct_id = _pointer_pointee(module, pointer_type, f"constant buffer {name!r}")
    struct_type = module.types.get(struct_id)
    if struct_type is None or struct_type.opcode != 30:
        raise LayoutError(f"constant buffer {name!r} must point to a struct")
    name = module.names.get(variable) or module.names.get(struct_id)
    if not name:
        raise LayoutError(f"constant buffer %{variable} and its struct %{struct_id} have no OpName")
    members = []
    end = 0
    for index, member_type in enumerate(struct_type.operands[1:]):
        member, member_end = _uniform_member(module, struct_id, index, member_type, names)
        if member["offset"] < end:
            raise LayoutError(f"uniform member {member['name']!r} overlaps a previous member")
        members.append(member)
        end = member_end
    if not members:
        raise LayoutError(f"constant buffer {name!r} has no declared members")
    size = (end + 15) // 16 * 16
    register_count = size // 16
    if register_count > 256:
        raise LayoutError(f"constant buffer {name!r} exceeds 256 float4 registers")
    return ({"name": name, "kind": "constant_buffer", "set": set_index,
             "binding": binding, "size": size, "members": members}, register_count)


def _reflect_resources(module, stage):
    resources = []
    uniform_register_count = None
    textures = []
    samplers = []
    member_names = _member_names(module)
    for variable, (pointer_type, storage, _initializer) in module.variables.items():
        decorations = module.decorations.get(variable, {})
        if storage == 2:
            if uniform_register_count is not None:
                raise LayoutError("only one uniform buffer b0/set0 is supported")
            set_index = decorations.get(34, (None,))[0]
            binding = decorations.get(33, (None,))[0]
            if type(set_index) is not int or type(binding) is not int:
                raise LayoutError("constant buffer requires DescriptorSet and Binding decorations")
            resource, uniform_register_count = _reflect_uniform(
                module, variable, pointer_type, set_index, binding, member_names
            )
            resources.append(resource)
        elif storage == 0:
            name = module.names.get(variable)
            if not name:
                raise LayoutError(f"resource variable %{variable} has no OpName")
            resource_type_id = _pointer_pointee(module, pointer_type, f"resource {name!r}")
            resource_type = module.types.get(resource_type_id)
            if resource_type is None:
                raise LayoutError(f"resource {name!r} has an unknown type")
            if resource_type.opcode in (28, 29):
                raise LayoutError(f"resource arrays are unsupported for {name!r}")
            if resource_type.opcode == 25:
                a = resource_type.operands
                if (len(a) != 8 or a[2] != 1 or a[3] != 0 or a[4] != 0 or
                        a[5] != 0 or a[6] != 1 or a[7] != 0):
                    raise LayoutError(f"resource {name!r} is not a sampled Texture2D<float4>")
                sample = module.types.get(a[1])
                if (sample is None or sample.opcode != 22 or
                        len(sample.operands) != 2 or sample.operands[1] != 32):
                    raise LayoutError(f"resource {name!r} is not a sampled Texture2D<float4>")
                kind = "texture2d"
            elif resource_type.opcode == 26:
                kind = "sampler"
            else:
                raise LayoutError(f"unsupported UniformConstant resource type on {name!r}")
            set_index = decorations.get(34, (0,))[0]
            binding = decorations.get(33, (None,))[0]
            if type(set_index) is not int or type(binding) is not int:
                raise LayoutError(f"resource {name!r} requires valid DescriptorSet and Binding decorations")
            if set_index != 0 or stage != "pixel":
                raise LayoutError(f"{kind} resources are supported only in pixel stage set 0")
            resource = {"name": name, "kind": kind, "set": set_index,
                        "binding": binding, "size": 0, "members": []}
            resources.append(resource)
            (textures if kind == "texture2d" else samplers).append(resource)
        elif storage not in (1, 3, 6, 7):
            storage_name = STORAGE_NAMES.get(storage, str(storage))
            raise LayoutError(
                f"unsupported SPIR-V storage class {storage_name} ({storage})"
            )

    if len(textures) > 1 or len(samplers) > 1:
        raise LayoutError("at most one Texture2D and one separate sampler are supported")
    if bool(textures) != bool(samplers):
        raise LayoutError("a Texture2D and separate sampler must both be present")
    if textures and samplers and stage != "pixel":
        raise LayoutError("Texture2D and sampler resources are pixel-stage only")
    resources.sort(key=lambda item: (item["set"], item["binding"], item["kind"], item["name"]))
    return resources, uniform_register_count, textures, samplers


def reflect_layout(binary, stage, entrypoint, vertex_layout=DEFAULT_VERTEX_LAYOUT):
    """Return actual SPIR-V reflection and its deterministic lowerer layout."""
    if stage not in ("vertex", "pixel"):
        raise LayoutError(f"unsupported shader stage {stage!r}")
    try:
        selected_layout = profile_for(vertex_layout)
    except ValueError as error:
        raise LayoutError(str(error)) from error
    module = parse_spirv(binary)
    matches = [entry for entry in module.entry_points if entry[2] == entrypoint]
    if len(matches) != 1:
        raise LayoutError(f"{stage} entry point {entrypoint!r} is missing or ambiguous")
    model, _function, _name, interface_ids = matches[0]
    if model != (0 if stage == "vertex" else 4):
        raise LayoutError(f"{stage} entry point has the wrong execution model")

    interfaces = {"inputs": [], "outputs": []}
    for variable in interface_ids:
        entry = module.variables.get(variable)
        if entry is None:
            raise LayoutError(f"entry point interface %{variable} is not a variable")
        pointer_type, storage, _initializer = entry
        if storage not in (1, 3):
            continue
        type_id = _pointer_pointee(module, pointer_type, f"interface variable %{variable}")
        fmt = _type_format(module, type_id, f"interface variable %{variable}")
        decorations = module.decorations.get(variable, {})
        location = decorations.get(30, (None,))[0]
        builtin = decorations.get(11, (None,))[0]
        if (location is None) == (builtin is None):
            raise LayoutError(f"interface variable %{variable} must have exactly one Location or BuiltIn")
        if location is not None and type(location) is not int:
            raise LayoutError(f"interface variable %{variable} has an invalid Location")
        if builtin is not None and type(builtin) is not int:
            raise LayoutError(f"interface variable %{variable} has an invalid BuiltIn")
        key = "inputs" if storage == 1 else "outputs"
        interfaces[key].append({
            "name": module.names.get(variable, f"%{variable}"),
            "location": location,
            "builtin": builtin,
            "format": fmt,
        })

    for key, values in interfaces.items():
        locations = [item["location"] for item in values if item["location"] is not None]
        builtins = [item["builtin"] for item in values if item["builtin"] is not None]
        if len(locations) != len(set(locations)) or len(builtins) != len(set(builtins)):
            raise LayoutError(f"duplicate {key[:-1]} location or builtin")
        values.sort(key=lambda item: (
            item["location"] is None,
            item["location"] if item["location"] is not None else item["builtin"],
        ))

    _validate_interfaces(interfaces, stage)
    resources, register_count, textures, samplers = _reflect_resources(module, stage)

    layout = {"vertex_inputs": {}, "varyings": {}, "uniforms": {}, "texture_bindings": []}
    if stage == "vertex":
        for item in interfaces["inputs"]:
            location = item["location"]
            fmt = item["format"]
            attribute = selected_layout.inputs.get(location)
            if attribute is None:
                raise LayoutError(f"vertex input location {location} is outside the published ABI")
            if fmt != attribute.format:
                raise LayoutError(
                    f"vertex input location {location} must be {attribute.format}, got {fmt}"
                )
            layout["vertex_inputs"][str(location)] = {
                "fetch_constant": 0,
                "stride": selected_layout.stride,
                "offset": attribute.offset,
                "format": attribute.hardware_format,
            }
        layout["varyings"] = {
            str(item["location"]): item["location"]
            for item in interfaces["outputs"] if item["location"] is not None
        }
    else:
        layout["varyings"] = {
            str(item["location"]): item["location"]
            for item in interfaces["inputs"] if item["location"] is not None
        }
    if register_count is not None:
        layout["uniforms"]["0"] = {"register_count": register_count}
    if textures:
        layout["texture_bindings"] = [{
            "texture_set": textures[0]["set"],
            "texture_binding": textures[0]["binding"],
            "sampler_set": samplers[0]["set"],
            "sampler_binding": samplers[0]["binding"],
            "fetch_constant": 1,
        }]
    return {**interfaces, "bindings": resources}, layout


def _validate_interfaces(interfaces, stage):
    for direction in ("inputs", "outputs"):
        for item in interfaces[direction]:
            if item["builtin"] is not None:
                if stage == "vertex" and direction == "outputs" and item["builtin"] == 0:
                    if item["format"] != "float4":
                        raise LayoutError("vertex BuiltIn Position must be float4")
                    continue
                raise LayoutError(f"{stage} {direction[:-1]} BuiltIn {item['builtin']} is unsupported")
            if not 0 <= item["location"] <= 7:
                raise LayoutError(f"{stage} {direction[:-1]} location must be in 0..7")
    if stage == "vertex":
        positions = [item for item in interfaces["outputs"] if item["builtin"] == 0]
        if len(positions) != 1:
            raise LayoutError("vertex shader must output BuiltIn Position as float4")
    else:
        outputs = interfaces["outputs"]
        if len(outputs) != 1 or outputs[0]["location"] != 0 or outputs[0]["format"] != "float4":
            raise LayoutError("pixel shader must have exactly one float4 output at location 0")
