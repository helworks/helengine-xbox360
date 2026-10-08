"""Bounded straight-line float SPIR-V lowering to the local Xenos assembler."""

from dataclasses import dataclass
import math
import struct

import xenos_asm
from spirv_parser import (
    DECORATION_NAMES, EXECUTION_MODELS, OP_NAMES, STORAGE_NAMES, SpirvError,
    parse_spirv,
)


class LoweringError(ValueError):
    """A valid SPIR-V module uses behavior outside this probe's subset."""


@dataclass(frozen=True)
class LoweredShader:
    assembly: str
    binary: bytes
    dwords: tuple
    metadata: dict


@dataclass(frozen=True)
class Lane:
    kind: str
    index: int
    component: int


@dataclass(frozen=True)
class Vector:
    lanes: tuple


@dataclass(frozen=True)
class Matrix:
    memory_vectors: tuple
    major: str
    stride: int
    binding: int
    offset: int


@dataclass(frozen=True)
class Pointer:
    variable: int
    offset: int
    type_id: int


@dataclass(frozen=True)
class ResourceHandle:
    kind: str
    variable: int
    image_type: int = 0


@dataclass(frozen=True)
class SampledHandle:
    texture_variable: int
    sampler_variable: int
    image_type: int


class _Lowerer:
    _DECLARATIONS = {0, 3, 4, 5, 6, 7, 8, 11, 14, 15, 16, 17,
                     19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30,
                     32, 33, 41, 42, 43, 44, 46, 54, 55, 56, 59, 71, 72}

    def __init__(self, module, stage, entrypoint, layout):
        self.m = module
        self.stage = stage
        self.layout = layout or {}
        matches = [ep for ep in module.entry_points if ep[2] == entrypoint]
        if len(matches) != 1:
            raise LoweringError(f"entry point {entrypoint!r} is missing or ambiguous")
        model, self.function_id, self.entry_name, interfaces = matches[0]
        actual_stage = EXECUTION_MODELS.get(model)
        if actual_stage != stage:
            raise LoweringError(
                f"entry point stage is {actual_stage or model}, requested {stage}"
            )
        self.interface_ids = set(interfaces)
        self.functions = []
        self.value = {}
        self.ptrs = {}
        self.ptr_layout = {}
        self.stores = []
        self.alu = []
        self.next_reg = 1 if stage == "vertex" else 0
        self.registers_used = set()
        self.reg_limit = int(self.layout.get("register_limit", 63))
        if self.reg_limit < 0 or self.reg_limit > 63:
            raise LoweringError("temporary register limit must be between 0 and 63")
        self.input_registers = {}
        self.input_bindings = self._map("vertex_inputs")
        self.uniform_bindings = self._map("uniforms")
        self.varying_bindings = self._map("varyings")
        self.texture_bindings = self._validate_texture_bindings()
        self.uniform_metadata = {}
        self.input_metadata = {}
        self.temp_metadata = []
        self.exports = []
        self.texture_metadata = []

    def run(self):
        self._validate_module_features()
        self._validate_function_stream()
        fetches = self._prepare_inputs()
        for ins in self.functions:
            self._evaluate(ins)
        exports = self._prepare_exports()
        if not exports:
            raise LoweringError("entry point has no supported output stores")

        lines = []
        if fetches:
            lines.extend(("exec", *["  " + x for x in fetches]))
        expressions = list(self.alu)
        positions = [text for kind, text in exports if kind == "position"]
        varyings = [text for kind, text in exports if kind == "varying"]
        colors = [text for kind, text in exports if kind == "color"]
        if self.stage == "vertex":
            if positions:
                lines.append("alloc position")
            lines.extend(self._exec_lines(expressions + positions))
            if varyings:
                max_varying = max(self._varying_export_index(s) for s in varyings)
                if max_varying >= 8:
                    raise LoweringError("Xenos interpolator allocation exceeds 8 outputs")
                lines.append(f"alloc interpolators, {max_varying + 1}")
                lines.extend(self._exec_lines(varyings))
        else:
            lines.append("alloc colors")
            lines.extend(self._exec_lines(expressions + colors))
        last_exec = max((i for i, line in enumerate(lines) if line in ("exec", "exece")), default=-1)
        if last_exec < 0:
            raise LoweringError("shader contains no executable instruction")
        lines[last_exec] = "exece"
        assembly = "\n".join(lines) + "\n"
        try:
            dwords, _listing = xenos_asm.assemble(assembly)
            binary = struct.pack(">%dI" % len(dwords), *dwords)
        except (xenos_asm.AsmError, struct.error) as error:
            raise LoweringError(f"generated Xenos assembly is invalid: {error}") from error
        metadata = {
            "stage": self.stage,
            "entrypoint": self.entry_name,
            # SQ_PROGRAM_CNTL stores allocation minus one. Constant-only
            # programs still reserve the minimum slot, even without a temp.
            "highest_register_index": max(self.registers_used, default=0),
            "register_count": max(self.registers_used, default=0) + 1,
            "sq_program_cntl_register_field": max(self.registers_used, default=0),
            "highest_temporary_register_index": max((r for r, _ in self.temp_metadata), default=None),
            "temporary_registers": [{"register": r, "purpose": purpose}
                                     for r, purpose in self.temp_metadata],
            "inputs": self.input_metadata,
            "input_map": self.input_metadata,
            "varying_map": {str(location): int(slot)
                            for location, slot in sorted(self.varying_bindings.items())},
            "uniforms": self.uniform_metadata,
            "textures": self.texture_metadata,
            "outputs": self.exports,
            "unsupported_features": [],
            "binary_dword_byte_order": "big_endian",
        }
        return LoweredShader(assembly, binary, tuple(dwords), metadata)

    def _validate_module_features(self):
        if len(self.m.entry_points) != 1:
            raise LoweringError("exactly one SPIR-V entry point is supported")
        capabilities = [ins.operands[0] for ins in self.m.instructions if ins.opcode == 17]
        if any(capability != 1 for capability in capabilities):
            raise LoweringError(f"unsupported SPIR-V capability set {capabilities}")
        allowed_decorations = {2, 11, 30, 33, 34}
        allowed_member_decorations = {4, 5, 7, 35}
        for target, decorations in self.m.decorations.items():
            unsupported = set(decorations) - allowed_decorations
            if unsupported:
                names = [DECORATION_NAMES.get(number, str(number)) for number in sorted(unsupported)]
                raise LoweringError(f"unsupported SPIR-V decoration(s) on %{target}: {names}")
        for (target, member), decorations in self.m.member_decorations.items():
            unsupported = set(decorations) - allowed_member_decorations
            if unsupported:
                names = [DECORATION_NAMES.get(number, str(number)) for number in sorted(unsupported)]
                raise LoweringError(
                    f"unsupported SPIR-V member decoration(s) on %{target}[{member}]: {names}"
                )
            if 4 in decorations and 5 in decorations:
                raise LoweringError(
                    f"uniform member %{target}[{member}] has both RowMajor and ColMajor"
                )
        function_ids = [ins.operands[1] for ins in self.m.instructions if ins.opcode == 54]
        if function_ids != [self.function_id]:
            raise LoweringError("helper or multiple SPIR-V functions are unsupported")
        uniform_buffers = []
        self.resource_variables = {}
        for variable, (_ptr, storage, _init) in self.m.variables.items():
            if storage == 2:
                decor = self.m.decorations.get(variable, {})
                binding = decor.get(33, (None,))[0]
                descriptor_set = decor.get(34, (None,))[0]
                if binding != 0 or descriptor_set != 0:
                    raise LoweringError(
                        f"only uniform buffer b0/set0 is supported; variable %{variable} "
                        f"has set={descriptor_set}, binding={binding}"
                    )
                uniform_buffers.append(variable)
            elif storage == 0:
                pointer_type = self.m.variables[variable][0]
                resource_type = self._pointee(pointer_type)
                type_ins = self.m.types.get(resource_type)
                if type_ins is None:
                    raise LoweringError(f"resource %{variable} has an unknown type")
                if type_ins.opcode == 25:
                    self._validate_image_type(variable, resource_type, type_ins)
                    kind = "texture"
                elif type_ins.opcode == 26:
                    kind = "sampler"
                elif type_ins.opcode in (28, 29):
                    raise LoweringError(
                        f"resource arrays are unsupported for variable %{variable}"
                    )
                else:
                    raise LoweringError(
                        f"unsupported UniformConstant resource type {type_ins.name} "
                        f"on variable %{variable}"
                    )
                self.resource_variables[variable] = (kind, resource_type)
            elif storage in (4, 6, 9, 12):
                raise LoweringError(
                    f"unsupported SPIR-V resource/storage class {STORAGE_NAMES.get(storage, storage)}"
                )
        if len(uniform_buffers) > 1:
            raise LoweringError("only one uniform buffer b0/set0 is supported")
        for ins in self.m.instructions:
            if ins.opcode == 16:
                mode = ins.operands[1]
                if self.stage != "pixel" or mode != 7:
                    raise LoweringError(
                        f"unsupported execution mode {mode} for stage {self.stage}"
                    )

    def _validate_image_type(self, variable, type_id, ins):
        a = ins.operands
        if len(a) != 8:
            raise LoweringError(f"malformed OpTypeImage for resource %{variable}")
        _result, sampled_type, dimension, depth, arrayed, multisample, sampled, image_format = a
        if dimension != 1:
            raise LoweringError("only 2D sampled images are supported")
        if depth != 0:
            raise LoweringError("depth/comparison images are unsupported")
        if arrayed:
            raise LoweringError("2D texture arrays are unsupported")
        if multisample:
            raise LoweringError("multisample textures are unsupported")
        if sampled != 1:
            raise LoweringError("storage or unknown image forms are unsupported")
        sample_type = self.m.types.get(sampled_type)
        if sample_type is None or sample_type.opcode != 22 or len(sample_type.operands) != 2 \
                or sample_type.operands[1] != 32:
            raise LoweringError("only Texture2D<float4> sampled images are supported")
        if image_format != 0:
            raise LoweringError("formatted/storage images are unsupported")

    def _validate_texture_bindings(self):
        raw = self.layout.get("texture_bindings", [])
        if not isinstance(raw, list):
            raise LoweringError("layout 'texture_bindings' must be a list")
        fields = {"texture_set", "texture_binding", "sampler_set",
                  "sampler_binding", "fetch_constant"}
        entries = {}
        slots = {}
        for index, item in enumerate(raw):
            if not isinstance(item, dict) or set(item) != fields:
                raise LoweringError(
                    f"texture_bindings entry {index} must contain exactly {sorted(fields)}"
                )
            for key, value in item.items():
                if type(value) is not int:
                    raise LoweringError(f"texture_bindings {key} must be an integer")
                maximum = 31 if key == "fetch_constant" else 0xFFFFFFFF
                if value < 0 or value > maximum:
                    bounds = "0..31" if key == "fetch_constant" else "0..4294967295"
                    raise LoweringError(f"texture_bindings {key} must be in range {bounds}")
            pair = (item["texture_set"], item["texture_binding"],
                    item["sampler_set"], item["sampler_binding"])
            if pair in entries:
                raise LoweringError("duplicate texture/sampler pair mapping")
            slot = item["fetch_constant"]
            if slot in slots and slots[slot] != pair:
                raise LoweringError(
                    f"texture fetch constant tf{slot} collision between distinct pairs"
                )
            entries[pair] = slot
            slots[slot] = pair
        return entries

    def _map(self, key):
        raw = self.layout.get(key, {})
        if not isinstance(raw, dict):
            raise LoweringError(f"layout {key!r} must be an object")
        try:
            return {int(k): v for k, v in raw.items()}
        except (TypeError, ValueError) as error:
            raise LoweringError(f"layout {key!r} keys must be integer locations") from error

    def _validate_function_stream(self):
        in_entry = False
        ended = False
        for ins in self.m.instructions:
            if ins.opcode == 54:
                in_entry = len(ins.operands) >= 2 and ins.operands[1] == self.function_id
                if in_entry:
                    if ins.operands[2] != 0:
                        raise LoweringError("function control masks are unsupported")
                    ended = False
                continue
            if in_entry:
                if ins.opcode == 56:
                    ended = True
                    in_entry = False
                elif ins.opcode in (8, 317):  # OpLine and OpNoLine are metadata only.
                    continue
                else:
                    self.functions.append(ins)
        if not ended:
            raise LoweringError("entry point function body is missing or unterminated")

        allowed = self._DECLARATIONS | {248, 61, 62, 65, 79, 80, 81, 82, 83,
                                        124, 129, 131, 133, 136, 142, 144,
                                        145, 146, 147, 148, 169, 253, 254, 255,
                                        12, 57, 249, 250, 245, *range(86, 100)}
        for ins in self.functions:
            if ins.opcode not in allowed:
                raise LoweringError(f"unsupported SPIR-V opcode {ins.opcode} ({ins.name})")
            if ins.opcode in (249, 250, 245, 57, *range(88, 99), 99, 136, 142, 146,
                              147, 148, 169, 254, 255):
                raise LoweringError(
                    f"unsupported SPIR-V operation {ins.name} (opcode {ins.opcode})"
                )

    def _prepare_inputs(self):
        used_varying_slots = list(self.varying_bindings.values())
        if (any(not isinstance(slot, int) or slot < 0 or slot > 7
                for slot in used_varying_slots)
                or len(set(used_varying_slots)) != len(used_varying_slots)):
            raise LoweringError("varying slots must be unique integers in the range 0..7")
        found = {}
        for variable, (ptr_type, storage, _init) in self.m.variables.items():
            if storage != 1 or variable not in self.interface_ids:
                continue
            decor = self.m.decorations.get(variable, {})
            if 30 not in decor:
                raise LoweringError(f"input %{variable} has no Location decoration")
            location = decor[30][0]
            if location in found:
                raise LoweringError(f"duplicate input location {location}")
            found[location] = (variable, self._pointee(ptr_type))

        fetches = []
        for location, (variable, type_id) in sorted(found.items()):
            width = self._vector_width(type_id)
            if self.stage == "pixel":
                if location not in self.varying_bindings:
                    raise LoweringError(f"pixel input location {location} has no varying map")
                reg = int(self.varying_bindings[location])
                self._check_reg(reg)
                self.next_reg = max(self.next_reg, reg + 1)
                self.registers_used.add(reg)
                self.input_metadata[str(location)] = {
                    "variable": self.m.names.get(variable, f"%{variable}"),
                    "register": reg, "kind": "interpolator",
                }
            else:
                binding = self.input_bindings.get(location)
                if binding is None:
                    raise LoweringError(f"vertex input location {location} has no fetch map")
                required = {"fetch_constant", "stride", "offset", "format"}
                if not required.issubset(binding):
                    raise LoweringError(f"vertex input location {location} map lacks {sorted(required - set(binding))}")
                fetch_constant = int(binding["fetch_constant"])
                if fetch_constant >= 96 or fetch_constant < 0:
                    raise LoweringError(f"vertex fetch constant {fetch_constant} is out of range")
                reg = self._new_reg(f"vertex input location {location}")
                fill = {1: "x001", 2: "xy01", 3: "xyz1", 4: "xyzw"}[width]
                fetches.append(
                    f"vfetch_full r{reg}.{fill}, r0.x, vf{fetch_constant}, "
                    f"stride={int(binding['stride'])}, offset={int(binding['offset'])}, "
                    f"format={binding['format']}"
                )
                self.input_metadata[str(location)] = {
                    "variable": self.m.names.get(variable, f"%{variable}"),
                    "register": reg, **binding,
                }
            self.input_registers[variable] = reg
            self.value[variable] = self._reg_vector(reg)
        return fetches

    def _evaluate(self, ins):
        op, a = ins.opcode, ins.operands
        if op in (0, 3, 4, 5, 6, 7, 8, 11, 14, 15, 16, 17, 19, 20, 21,
                  22, 23, 24, 25, 26, 27, 28, 29, 30, 32, 33, 41, 42, 43,
                  44, 46, 54, 55, 56, 59, 71, 72, 248, 253):
            return
        if op == 65:
            self._access_chain(a)
        elif op == 61:
            self._load(a)
        elif op == 86:
            self._sampled_image(a)
        elif op == 87:
            self._sample_implicit_lod(a)
        elif 88 <= op <= 98:
            raise LoweringError(
                f"unsupported texture sampling/image operation {OP_NAMES[op]}"
            )
        elif op == 62:
            if len(a) != 2:
                raise LoweringError("malformed OpStore")
            self.stores.append((a[0], a[1]))
        elif op == 80:
            self._composite_construct(a)
        elif op == 81:
            self._composite_extract(a)
        elif op == 79:
            self._vector_shuffle(a)
        elif op == 82:
            raise LoweringError("OpCompositeInsert is unsupported")
        elif op == 83:
            self.value[a[1]] = self.value[a[2]]
        elif op == 12:
            self._ext_inst(a)
        elif op == 124:
            raise LoweringError("OpBitcast is unsupported for floating-point lowering")
        elif op in (129, 131, 133):
            self._binary_float(op, a)
        elif op in (144, 145):
            self._matrix_times_vector(op, a)
        elif op in (128, 130, 136, 142):
            raise LoweringError(f"unsupported SPIR-V operation {OP_NAMES[op]} (opcode {op})")
        else:
            raise LoweringError(f"unsupported SPIR-V operation {OP_NAMES.get(op, op)} (opcode {op})")

    def _access_chain(self, a):
        if len(a) < 3:
            raise LoweringError("malformed OpAccessChain")
        result_type, result_id, base = a[:3]
        if base not in self.m.variables:
            raise LoweringError(f"OpAccessChain base %{base} is not a global variable")
        pointer_type, storage, _ = self.m.variables[base]
        pointee = self._pointee(pointer_type)
        offset = 0
        if storage in (2, 9, 12):
            if len(a) != 4:
                raise LoweringError("uniform access must select exactly one struct member")
            member = self._constant_index(a[3])
            decoration = self.m.member_decorations.get((pointee, member), {}).get(35)
            if decoration is None:
                raise LoweringError(f"uniform member %{pointee}[{member}] has no Offset")
            offset = decoration[0]
            member_decorations = self.m.member_decorations.get((pointee, member), {})
            majors = [name for number, name in ((4, "row_major"), (5, "column_major"))
                      if number in member_decorations]
            stride = member_decorations.get(7, (None,))[0]
        elif len(a) != 3:
            raise LoweringError("dynamic or nested AccessChain indexing is unsupported")
        else:
            majors, stride = [], None
        self.ptrs[result_id] = Pointer(base, offset, self._pointee(result_type))
        self.ptr_layout[result_id] = (majors, stride)

    def _load(self, a):
        if len(a) < 3:
            raise LoweringError("malformed OpLoad")
        if len(a) > 3:
            raise LoweringError("OpLoad memory access operands are unsupported")
        type_id, result_id, pointer_id = a[:3]
        if pointer_id in self.ptrs:
            ptr = self.ptrs[pointer_id]
            storage = self.m.variables[ptr.variable][1]
            if storage not in (2, 9, 12):
                raise LoweringError("only uniform-buffer AccessChain loads are supported")
            majors, stride = self.ptr_layout.get(pointer_id, ([], None))
            self.value[result_id] = self._uniform_value(
                ptr.variable, ptr.offset, type_id, majors, stride
            )
        elif pointer_id in self.resource_variables:
            kind, resource_type = self.resource_variables[pointer_id]
            expected_type = self._pointee(self.m.variables[pointer_id][0])
            if type_id != expected_type or resource_type != expected_type:
                raise LoweringError(f"malformed OpLoad of {kind} resource")
            self.value[result_id] = ResourceHandle(
                kind, pointer_id, resource_type if kind == "texture" else 0
            )
        elif pointer_id in self.input_registers:
            self.value[result_id] = self._reg_vector(self.input_registers[pointer_id], type_id)
        else:
            raise LoweringError(f"load from unsupported pointer %{pointer_id}")

    def _sampled_image(self, a):
        if len(a) != 4:
            raise LoweringError("malformed OpSampledImage operand length")
        result_type, result_id, image_id, sampler_id = a
        image = self.value.get(image_id)
        sampler = self.value.get(sampler_id)
        if not isinstance(image, ResourceHandle) or image.kind != "texture":
            raise LoweringError("OpSampledImage requires a loaded sampled texture")
        if not isinstance(sampler, ResourceHandle) or sampler.kind != "sampler":
            raise LoweringError("OpSampledImage requires a loaded SamplerState")
        type_ins = self.m.types.get(result_type)
        if type_ins is None or type_ins.opcode != 27 or len(type_ins.operands) != 2 \
                or type_ins.operands[1] != image.image_type:
            raise LoweringError("OpSampledImage has an unsupported sampled-image type")
        self.value[result_id] = SampledHandle(
            image.variable, sampler.variable, image.image_type
        )

    def _sample_implicit_lod(self, a):
        if len(a) != 4:
            raise LoweringError(
                "optional bias, offset, or other sampling operand is unsupported"
            )
        if self.stage != "pixel":
            raise LoweringError("texture sampling is supported only in the pixel stage")
        result_type, result_id, sampled_id, coordinate_id = a
        sampled = self.value.get(sampled_id)
        if not isinstance(sampled, SampledHandle):
            raise LoweringError("OpImageSampleImplicitLod requires OpSampledImage")
        coordinates = self._vector(self.value[coordinate_id])
        coordinate_type = self._result_type(coordinate_id)
        if self._shape(coordinate_type) != ("vector", 2):
            raise LoweringError("texture coordinates must be a float2 vector")
        if len(coordinates.lanes) != 2:
            raise LoweringError("texture coordinates must contain exactly two lanes")
        first = coordinates.lanes[0]
        if (not all(isinstance(lane, Lane) and lane.kind == "r"
                    and lane.index == first.index for lane in coordinates.lanes)
                or not isinstance(first, Lane)):
            raise LoweringError("texture coordinates must be in one temporary register")
        if self._shape(result_type) != ("vector", 4):
            raise LoweringError("sampled texture result must be float4")

        texture_decor = self.m.decorations.get(sampled.texture_variable, {})
        sampler_decor = self.m.decorations.get(sampled.sampler_variable, {})
        texture_binding = texture_decor.get(33, (None,))[0]
        sampler_binding = sampler_decor.get(33, (None,))[0]
        if texture_binding is None or sampler_binding is None:
            raise LoweringError("sampled texture and sampler require Binding decorations")
        texture_set = texture_decor.get(34, (0,))[0]
        sampler_set = sampler_decor.get(34, (0,))[0]
        pair = (texture_set, texture_binding, sampler_set, sampler_binding)
        fetch_constant = self.texture_bindings.get(pair)
        if fetch_constant is None:
            raise LoweringError(
                "sampled texture/sampler pair has no texture_bindings mapping"
            )

        register = self._new_reg("sampled Texture2D")
        coordinate_source = f"r{first.index}." + "".join(
            "xyzw"[lane.component] for lane in coordinates.lanes
        )
        self.alu.append(
            f"tfetch r{register}.xyzw, {coordinate_source}, tf{fetch_constant}"
        )
        self.value[result_id] = self._reg_vector(register, result_type)
        metadata = {
            "texture_set": texture_set,
            "texture_binding": texture_binding,
            "sampler_set": sampler_set,
            "sampler_binding": sampler_binding,
            "fetch_constant": fetch_constant,
            "stage": self.stage,
            "dimension": "2D",
        }
        if metadata not in self.texture_metadata:
            self.texture_metadata.append(metadata)

    def _result_type(self, result_id):
        for ins in self.functions:
            if len(ins.operands) >= 2 and ins.operands[1] == result_id:
                return ins.operands[0]
        variable = self.m.variables.get(result_id)
        if variable is not None:
            return self._pointee(variable[0])
        raise LoweringError(f"unknown SPIR-V result type for %{result_id}")

    def _uniform_value(self, variable, offset, type_id, majors, stride):
        binding_decor = self.m.decorations.get(variable, {})
        binding = binding_decor.get(33, (None,))[0]
        descriptor_set = binding_decor.get(34, (0,))[0]
        if binding is None:
            raise LoweringError(f"uniform variable %{variable} has no Binding decoration")
        if binding != 0 or descriptor_set != 0:
            raise LoweringError(
                f"only uniform buffer set=0 binding=0 is supported; got set={descriptor_set}, binding={binding}"
            )
        values, capacity = self._uniform_register_layout(binding)
        base = offset // 16
        if offset % 16:
            raise LoweringError(f"uniform member at byte offset {offset} is not vec4 aligned")
        shape = self._shape(type_id)
        width = shape[1] if shape[0] == "vector" else 1
        count = shape[2] if shape[0] == "matrix" else 1
        if shape[0] == "matrix":
            if shape[1:] != (4, 4):
                raise LoweringError("only float4x4 uniform matrices are supported")
            if len(majors) != 1 or stride != 16:
                raise LoweringError("float4x4 requires exactly one RowMajor/ColMajor and MatrixStride=16")
            memory_vectors = []
            for col in range(count):
                reg = base + col
                memory_vectors.append(self._constant_vector(
                    values, reg, binding, register_count=capacity
                ))
            major = majors[0]
            self._record_uniform_member(binding, descriptor_set, offset, values,
                                        major, stride, count, capacity)
            return Matrix(tuple(memory_vectors), major, stride, binding, offset)
        if width > 4:
            raise LoweringError("uniform vectors wider than float4 are unsupported")
        self._record_uniform_member(binding, descriptor_set, offset, values,
                                    None, None, 1, capacity)
        return self._constant_vector(values, base, binding, width,
                                     register_count=capacity)

    def _uniform_register_layout(self, binding):
        registers = self.uniform_bindings.get(binding)
        if registers is None:
            raise LoweringError(f"uniform binding {binding} has no upload value map")
        if isinstance(registers, dict) and "register_count" in registers:
            if "registers" in registers or "values" in registers:
                raise LoweringError(
                    f"uniform binding {binding} cannot combine register_count with registers or values"
                )
            count = registers["register_count"]
            if isinstance(count, bool) or not isinstance(count, int):
                raise LoweringError(
                    f"uniform binding {binding} register_count must be an integer"
                )
            if count < 1 or count > 256:
                raise LoweringError(
                    f"uniform binding {binding} register_count must be between 1 and 256"
                )
            return None, count
        if isinstance(registers, dict):
            registers = registers.get("registers", registers.get("values"))
        if not isinstance(registers, (tuple, list)):
            raise LoweringError(f"uniform binding {binding} values must be vec4 registers")
        return registers, None

    def _record_uniform_member(self, binding, descriptor_set, offset, values,
                              major, stride, register_count, capacity):
        entry = self.uniform_metadata.get(str(binding))
        if entry is None:
            entry = {
                "descriptor_set": descriptor_set,
                "binding": binding,
                "raw_memory_order": "consecutive_uploaded_vec4_registers",
            }
            if capacity is None:
                entry["register_values"] = [list(map(float, row)) for row in values]
            else:
                entry["register_count"] = capacity
            entry["members"] = []
            self.uniform_metadata[str(binding)] = entry
        member = {"offset_bytes": offset,
                  "registers": list(range(offset // 16, offset // 16 + register_count))}
        if major:
            member.update({"major": major, "matrix_stride_bytes": stride})
        if member not in entry["members"]:
            entry["members"].append(member)

    def _constant_vector(self, registers, index, binding, width=4,
                         register_count=None):
        if register_count is not None:
            if index >= register_count:
                raise LoweringError(
                    f"uniform binding {binding} declared register capacity {register_count} does not include c{index}"
                )
            return Vector(tuple(Lane("c", index, lane) for lane in range(width)))
        if index >= len(registers):
            raise LoweringError(f"uniform binding {binding} has no value for c{index}")
        row = registers[index]
        if not isinstance(row, (tuple, list)) or len(row) != 4:
            raise LoweringError(f"uniform binding {binding} register {index} must contain four floats")
        if not all(math.isfinite(float(x)) for x in row):
            raise LoweringError("uniform values must be finite floats")
        return Vector(tuple(Lane("c", index, lane) for lane in range(width)))

    def _binary_float(self, op, a):
        if len(a) != 4:
            raise LoweringError(f"malformed {OP_NAMES[op]}")
        type_id, result_id, left_id, right_id = a
        left, right = self._vector(self._value(left_id)), self._vector(self._value(right_id))
        width = self._vector_width(type_id)
        if len(left.lanes) == 1:
            left = Vector(left.lanes * width)
        if len(right.lanes) == 1:
            right = Vector(right.lanes * width)
        if len(left.lanes) != width or len(right.lanes) != width:
            raise LoweringError(f"{OP_NAMES[op]} operand width mismatch")
        dest = self._new_reg(OP_NAMES[op])
        opname = {133: "mul", 129: "add", 131: "add"}[op]
        src1 = self._source(left)
        src2 = self._source(right)
        if op == 131:
            src2 = self._source(right, negate=True)
        self.alu.append(f"{opname} r{dest}, {src1}, {src2}")
        self.value[result_id] = self._reg_vector(dest, type_id)

    def _ext_inst(self, a):
        if len(a) != 7:
            raise LoweringError("malformed OpExtInst")
        type_id, result_id, set_id, instruction, first_id, second_id, third_id = a
        import_name = self.m.ext_inst_imports.get(set_id)
        if import_name != "GLSL.std.450" or instruction != 50:
            raise LoweringError(
                f"unsupported extended instruction set={import_name!r} instruction={instruction}"
            )
        first = self._vector(self.value[first_id])
        second = self._vector(self.value[second_id])
        third = self._vector(self.value[third_id])
        width = self._vector_width(type_id)
        if any(len(v.lanes) != width for v in (first, second, third)):
            raise LoweringError("GLSL.std.450 Fma operand width mismatch")
        dest = self._new_reg("GLSL.std.450 Fma")
        self.alu.append(
            f"mad r{dest}, {self._source(first)}, {self._source(second)}, {self._source(third)}"
        )
        self.value[result_id] = self._reg_vector(dest, type_id)

    def _matrix_times_vector(self, op, a):
        if len(a) != 4:
            raise LoweringError(f"malformed {OP_NAMES[op]}")
        type_id, result_id = a[:2]
        if op == 144:
            vector_id, matrix_id = a[2:]
        else:
            matrix_id, vector_id = a[2:]
        matrix = self.value[matrix_id]
        vector = self._vector(self.value[vector_id])
        if not isinstance(matrix, Matrix) or len(matrix.memory_vectors) != 4 or len(vector.lanes) != 4:
            raise LoweringError("only a float4x4 times float4 transform is supported")
        expected_major = "row_major" if op == 144 else "column_major"
        if matrix.major != expected_major:
            if op == 145 and matrix.major == "row_major":
                # Row-major storage exposes matrix rows directly; each result
                # component is the dot product of one row with the vector.
                dest = self._new_reg("row-major matrix transform")
                if any(isinstance(lane, (int, float)) for lane in vector.lanes):
                    self._matrix_rows_with_literals(dest, matrix, vector)
                    self.value[result_id] = self._reg_vector(dest, type_id)
                    return
                vector_source = self._source(vector)
                for component, row in enumerate(matrix.memory_vectors):
                    self.alu.append(
                        f"dp4 r{dest}.{'xyzw'[component]}, "
                        f"{self._source(row)}, {vector_source}"
                    )
                self.value[result_id] = self._reg_vector(dest, type_id)
                return
            raise LoweringError(
                f"{OP_NAMES[op]} requires {expected_major} matrix storage in this lowering"
            )
        first = None
        for memory_vector, scalar in zip(matrix.memory_vectors, vector.lanes):
            if isinstance(scalar, (int, float)):
                if float(scalar) == 0.0:
                    continue
                if float(scalar) == 1.0:
                    if first is None:
                        first = self._new_reg("matrix position constant column")
                        self.alu.append(f"max r{first}, {self._source(memory_vector)}, {self._source(memory_vector)}")
                    else:
                        dest = self._new_reg("matrix transform accumulation")
                        self.alu.append(f"add r{dest}, {self._reg_name(first)}, {self._source(memory_vector)}")
                        first = dest
                    continue
                raise LoweringError("matrix transform literal scalar other than 0 or 1")
            if first is None:
                first = self._new_reg("matrix transform product")
                self.alu.append(f"mul r{first}, {self._source(memory_vector)}, {self._splat(scalar)}")
            else:
                dest = self._new_reg("matrix transform accumulation")
                self.alu.append(f"mad r{dest}, {self._source(memory_vector)}, {self._splat(scalar)}, {self._reg_name(first)}")
                first = dest
        if first is None:
            raise LoweringError("matrix transform is a zero vector, unsupported by this lowering")
        self.value[result_id] = self._reg_vector(first, type_id)

    def _matrix_rows_with_literals(self, dest, matrix, vector):
        # dp4 cannot encode literal lanes. Accumulate each row with a masked
        # destination instead: multiplying by 0 vanishes and by 1 is a copy.
        # Do not steal cbuffer registers to synthesize shader-owned constants.
        for lane in vector.lanes:
            if isinstance(lane, (int, float)) and lane not in (0, 1):
                raise LoweringError("matrix transform literal scalar other than 0 or 1")
        for component, row in enumerate(matrix.memory_vectors):
            mask = "xyzw"[component]
            target = f"r{dest}.{mask}"
            accumulated = f"r{dest}.{mask * 4}"
            started = False
            for matrix_lane, scalar in zip(row.lanes, vector.lanes):
                source = self._splat(matrix_lane)
                if isinstance(scalar, (int, float)):
                    if scalar == 0:
                        continue
                    instruction = (f"add {target}, {accumulated}, {source}" if started
                                   else f"max {target}, {source}, {source}")
                else:
                    product = f"{source}, {self._splat(scalar)}"
                    instruction = (f"mad {target}, {product}, {accumulated}" if started
                                   else f"mul {target}, {product}")
                self.alu.append(instruction)
                started = True
            if not started:
                raise LoweringError("matrix transform is a zero vector, unsupported by this lowering")

    def _prepare_exports(self):
        outputs = {}
        for variable, value_id in self.stores:
            if variable not in self.m.variables:
                raise LoweringError(f"store target %{variable} is not a global output")
            decorations = self.m.decorations.get(variable, {})
            storage = self.m.variables[variable][1]
            if storage != 3:
                raise LoweringError("only stores to output variables are supported")
            if 11 in decorations and decorations[11][0] == 0:
                key = ("position", 0)
            elif 30 in decorations:
                location = decorations[30][0]
                if self.stage == "vertex":
                    if location not in self.varying_bindings:
                        raise LoweringError(f"vertex output location {location} has no varying map")
                    key = ("varying", int(self.varying_bindings[location]))
                else:
                    key = ("color", location)
            else:
                raise LoweringError(f"output %{variable} has unsupported decoration")
            if key in outputs:
                raise LoweringError(f"multiple output variables map to the same Xenos export {key}")
            outputs[key] = self.value.get(value_id)
            if outputs[key] is None:
                raise LoweringError(f"output value %{value_id} has no supported definition")

        if self.stage == "vertex" and ("position", 0) not in outputs:
            raise LoweringError("vertex shader must write BuiltIn Position")

        emitted = []
        for (kind, index), value in sorted(outputs.items(), key=lambda item: (item[0][0], item[0][1])):
            if kind == "position":
                if self.stage != "vertex":
                    raise LoweringError("BuiltIn Position output requires a vertex shader")
                source = self._source(self._vector(value))
                text = f"max oPos, {source}, {source}"
                emitted.append(("position", text))
                self.exports.append({"kind": "position", "source": source})
            elif kind == "varying":
                text = self._export_copy(f"o{index}", value)
                emitted.append(("varying", text))
                self.exports.append({"kind": "varying", "location": index})
            else:
                if self.stage != "pixel":
                    raise LoweringError("pixel color output requires a pixel shader")
                text = self._export_copy(f"oC{index}", value)
                emitted.append(("color", text))
                self.exports.append({"kind": "color", "location": index})
        return emitted

    def _export_copy(self, dest, value):
        vector = self._vector(value)
        if not vector.lanes:
            raise LoweringError("empty output vector")
        source = self._source(vector)
        # Xenos has no plain move; max(x,x) is an exact float copy.
        return f"max {dest}, {source}, {source}"

    def _exec_lines(self, instructions):
        lines = []
        for instruction in instructions:
            if instruction.startswith("#"):
                continue
            lines.extend(("exec", "  serialize " + instruction))
        return lines

    @staticmethod
    def _varying_export_index(text):
        import re
        match = re.search(r"\bo(\d+)\b", text)
        return int(match.group(1)) if match else 0

    def _composite_construct(self, a):
        if len(a) < 2:
            raise LoweringError("malformed OpCompositeConstruct")
        type_id, result_id = a[:2]
        shape = self._shape(type_id)
        if shape[0] == "matrix":
            raise LoweringError("matrix OpCompositeConstruct is unsupported")
        else:
            lanes = []
            for operand in a[2:]:
                item = self.value[operand] if operand in self.value else self._constant(operand)
                if isinstance(item, Vector):
                    lanes.extend(item.lanes)
                elif isinstance(item, Lane):
                    lanes.append(item)
                elif isinstance(item, (int, float)):
                    lanes.append(item)
                else:
                    raise LoweringError("unsupported composite component type")
            if len(lanes) != self._vector_width(type_id):
                raise LoweringError("composite vector lane count mismatch")
            self.value[result_id] = Vector(tuple(lanes))

    def _composite_extract(self, a):
        if len(a) < 4:
            raise LoweringError("malformed OpCompositeExtract")
        _type_id, result_id, composite_id, component = a[:4]
        source = self.value[composite_id]
        if not isinstance(source, Vector) or component >= len(source.lanes):
            raise LoweringError("only one-level vector CompositeExtract is supported")
        self.value[result_id] = source.lanes[component]

    def _vector_shuffle(self, a):
        if len(a) < 5:
            raise LoweringError("malformed OpVectorShuffle")
        type_id, result_id, first_id, second_id, *indices = a
        first = self._vector(self.value[first_id]).lanes
        second = self._vector(self.value[second_id]).lanes
        combined = first + second
        if len(indices) != self._vector_width(type_id):
            raise LoweringError("VectorShuffle result width mismatch")
        lanes = []
        for index in indices:
            if index == 0xFFFFFFFF or index >= len(combined):
                raise LoweringError("undefined or out-of-range VectorShuffle lane")
            lanes.append(combined[index])
        self.value[result_id] = Vector(tuple(lanes))

    def _constant(self, value_id):
        if value_id not in self.m.constants:
            raise LoweringError(f"unknown SPIR-V value %{value_id}")
        raw = self.m.constants[value_id]
        type_id = self.m.constants[("type", value_id)]
        type_ins = self.m.types[type_id]
        if type_ins.opcode == 22:
            if len(raw) == 1:
                return struct.unpack("<f", struct.pack("<I", raw[0]))[0]
            if len(raw) == 2:
                return struct.unpack("<d", struct.pack("<II", *raw))[0]
        if type_ins.opcode == 21 and raw:
            return int(raw[0])
        raise LoweringError(f"SPIR-V constant %{value_id} is not a scalar float/int")

    def _value(self, value_id):
        if value_id in self.value:
            return self.value[value_id]
        if value_id not in self.m.constants:
            raise LoweringError(f"unknown SPIR-V value %{value_id}")
        raw = self.m.constants[value_id]
        type_id = self.m.constants[("type", value_id)]
        if self.m.types[type_id].opcode in (23, 24, 30):
            lanes = []
            for component_id in raw:
                component = self._value(component_id)
                if isinstance(component, Vector):
                    lanes.extend(component.lanes)
                elif isinstance(component, Lane):
                    lanes.append(component)
                else:
                    lanes.append(component)
            if len(lanes) != self._vector_width(type_id):
                raise LoweringError("constant composite lane count mismatch")
            return Vector(tuple(lanes))
        return self._constant(value_id)

    def _constant_index(self, value_id):
        value = self._constant(value_id)
        if not isinstance(value, int):
            raise LoweringError("uniform member index is not an integer constant")
        return value

    def _shape(self, type_id):
        ins = self.m.types.get(type_id)
        if ins is None:
            raise LoweringError(f"unknown SPIR-V type %{type_id}")
        a = ins.operands
        if ins.opcode == 22:
            if len(a) < 2 or a[1] != 32:
                raise LoweringError("only float32 scalar types are supported")
            return ("scalar", 1)
        if ins.opcode == 23:
            if self._shape(a[1])[0] != "scalar":
                raise LoweringError("only vectors of float scalars are supported")
            return ("vector", a[2])
        if ins.opcode == 24:
            vec_shape = self._shape(a[1])
            if vec_shape[0] != "vector":
                raise LoweringError("matrix columns must be float vectors")
            return ("matrix", vec_shape[1], a[2])
        if ins.opcode == 30:
            return ("struct", len(a) - 1)
        raise LoweringError(f"unsupported SPIR-V type {OP_NAMES.get(ins.opcode, ins.opcode)}")

    def _vector_width(self, type_id):
        shape = self._shape(type_id)
        if shape[0] == "scalar":
            return 1
        if shape[0] == "vector":
            return shape[1]
        raise LoweringError("expected a float scalar or vector")

    def _pointee(self, pointer_type):
        ins = self.m.types.get(pointer_type)
        if ins is None or ins.opcode != 32 or len(ins.operands) < 3:
            raise LoweringError(f"SPIR-V type %{pointer_type} is not a pointer")
        return ins.operands[2]

    def _vector(self, value):
        if isinstance(value, Vector):
            return value
        if isinstance(value, Lane):
            return Vector((value,))
        if isinstance(value, (int, float)):
            return Vector((value,))
        raise LoweringError("expected scalar/vector expression")

    def _reg_vector(self, register, type_id=None):
        width = 4 if type_id is None else self._vector_width(type_id)
        return Vector(tuple(Lane("r", register, component) for component in range(width)))

    def _source(self, vector, negate=False):
        vector = self._vector(vector)
        if not vector.lanes or not all(isinstance(x, Lane) for x in vector.lanes):
            raise LoweringError("literal scalar cannot be encoded as an ALU source")
        first = vector.lanes[0]
        if any(x.kind != first.kind or x.index != first.index for x in vector.lanes):
            raise LoweringError("mixed-register vector expression needs an unsupported shuffle")
        swizzle = "".join("xyzw"[x.component] for x in vector.lanes)
        if len(swizzle) == 1:
            swizzle *= 4
        elif len(swizzle) == 2:
            swizzle += swizzle[-1] * 2
        elif len(swizzle) == 3:
            swizzle += swizzle[-1]
        if len(swizzle) != 4:
            raise LoweringError("ALU source vectors must have at most four lanes")
        prefix = "-" if negate else ""
        return f"{prefix}{first.kind}{first.index}.{swizzle}"

    def _splat(self, scalar):
        if isinstance(scalar, (int, float)):
            raise LoweringError("literal multiplication requires an explicit constant register")
        return self._source(Vector((scalar,) * 4))

    @staticmethod
    def _reg_name(register):
        return f"r{register}"

    def _new_reg(self, purpose):
        reg = self.next_reg
        self._check_reg(reg)
        self.next_reg += 1
        self.registers_used.add(reg)
        self.temp_metadata.append((reg, purpose))
        return reg

    def _check_reg(self, reg):
        if reg < 0 or reg > self.reg_limit or reg > 63:
            raise LoweringError(
                f"temporary register r{reg} exceeds configured/hardware limit {min(self.reg_limit, 63)}"
            )


def lower_spirv(binary, stage, entrypoint, layout=None):
    """Lower the documented straight-line float subset and return probe artifacts."""
    if stage not in ("vertex", "pixel"):
        raise LoweringError(f"unsupported shader stage {stage!r}")
    try:
        module = parse_spirv(binary)
    except SpirvError as error:
        raise LoweringError(str(error)) from error
    lowerer = _Lowerer(module, stage, entrypoint, layout)
    return lowerer.run()
