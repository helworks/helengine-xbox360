"""Small, strict reader for the SPIR-V 1.0 subset used by shader_probe."""

from dataclasses import dataclass, field
import struct


MAGIC = 0x07230203
OPCODE_GRAMMAR_SOURCE = (
    "https://raw.githubusercontent.com/KhronosGroup/SPIRV-Headers/main/"
    "include/spirv/unified1/spirv.core.grammar.json"
)
OPCODE_GRAMMAR_VERSION = "SPIR-V 1.6 revision 7"
GLSL450_GRAMMAR_SOURCE = (
    "https://raw.githubusercontent.com/KhronosGroup/SPIRV-Headers/main/"
    "include/spirv/unified1/extinst.glsl.std.450.grammar.json"
)
GLSL450_GRAMMAR_VERSION = "GLSL.std.450 version 100 revision 2"

# Parsing accepts the standard declarations seen in real shaderc modules. The
# lowerer still rejects semantic instructions outside its implemented subset.
OP_NAMES = {
    0: "Nop", 1: "Undef", 3: "Source", 4: "SourceExtension", 5: "Name",
    6: "MemberName", 7: "String", 8: "Line", 11: "ExtInstImport",
    14: "MemoryModel", 15: "EntryPoint", 16: "ExecutionMode",
    17: "Capability", 19: "TypeVoid", 20: "TypeBool", 21: "TypeInt",
    22: "TypeFloat", 23: "TypeVector", 24: "TypeMatrix", 25: "TypeImage",
    26: "TypeSampler", 27: "TypeSampledImage", 28: "TypeArray",
    29: "TypeRuntimeArray", 30: "TypeStruct", 32: "TypePointer",
    33: "TypeFunction", 41: "ConstantTrue", 42: "ConstantFalse",
    43: "Constant", 44: "ConstantComposite", 46: "ConstantNull",
    54: "Function", 55: "FunctionParameter", 56: "FunctionEnd",
    12: "ExtInst", 57: "FunctionCall", 59: "Variable", 61: "Load", 62: "Store",
    65: "AccessChain", 71: "Decorate", 72: "MemberDecorate",
    79: "VectorShuffle", 80: "CompositeConstruct", 81: "CompositeExtract",
    82: "CompositeInsert", 83: "CopyObject", 86: "SampledImage",
    87: "ImageSampleImplicitLod", 88: "ImageSampleExplicitLod",
    89: "ImageSampleDrefImplicitLod", 90: "ImageSampleDrefExplicitLod",
    91: "ImageSampleProjImplicitLod", 92: "ImageSampleProjExplicitLod",
    93: "ImageSampleProjDrefImplicitLod", 94: "ImageSampleProjDrefExplicitLod",
    95: "ImageFetch", 96: "ImageGather", 97: "ImageDrefGather",
    98: "ImageRead", 99: "ImageWrite",
    124: "Bitcast", 128: "IAdd", 129: "FAdd", 130: "ISub",
    131: "FSub", 133: "FMul",
    136: "FDiv", 140: "FRem", 142: "VectorTimesScalar",
    144: "VectorTimesMatrix", 145: "MatrixTimesVector",
    146: "MatrixTimesMatrix", 147: "OuterProduct", 148: "Dot",
    169: "Select", 245: "Phi", 248: "Label", 249: "Branch",
    250: "BranchConditional", 253: "Return", 254: "ReturnValue",
    255: "Unreachable", 317: "NoLine",
}

DECORATION_NAMES = {
    2: "Block", 3: "BufferBlock", 6: "ArrayStride", 11: "BuiltIn",
    4: "RowMajor", 5: "ColMajor", 7: "MatrixStride",
    30: "Location", 33: "Binding", 34: "DescriptorSet", 35: "Offset",
}
DECORATION_LITERAL_COUNTS = {
    2: 0, 3: 0, 4: 0, 5: 0, 6: 1, 7: 1, 11: 1,
    30: 1, 33: 1, 34: 1, 35: 1,
}
STORAGE_NAMES = {0: "UniformConstant", 1: "Input", 2: "Uniform",
                 3: "Output", 4: "Workgroup", 6: "Private", 7: "Function",
                 9: "PushConstant", 12: "StorageBuffer"}
EXECUTION_MODELS = {0: "vertex", 4: "pixel"}

MIN_OPERANDS = {
    0: 0, 3: 2, 4: 1, 5: 2, 6: 3, 7: 2, 8: 3, 11: 2, 12: 4,
    14: 2, 15: 3, 16: 2, 17: 1, 19: 1, 20: 1, 21: 3, 22: 2,
    23: 3, 24: 3, 25: 8, 26: 1, 27: 2, 28: 3, 29: 2, 30: 1,
    32: 3, 33: 2, 41: 2, 42: 2, 43: 3, 44: 3, 46: 2, 54: 4,
    55: 2, 56: 0, 57: 3, 59: 3, 61: 3, 62: 2, 65: 4, 71: 2,
    72: 3, 79: 6, 80: 3, 81: 4, 82: 5, 83: 3, 86: 4, 87: 4,
    88: 5, 89: 5, 90: 6, 91: 4, 92: 5, 93: 5, 94: 6,
    95: 4, 96: 4, 97: 5, 98: 4,
    99: 3, 124: 3, 128: 4, 129: 4, 130: 4, 131: 4, 133: 4,
    136: 4, 140: 4, 142: 4, 144: 4, 145: 4, 146: 4, 147: 4,
    148: 4, 169: 5, 245: 4, 248: 1, 249: 1, 250: 3, 253: 0,
    254: 1, 255: 0, 317: 0,
}
EXACT_OPERANDS = {8: 3, 317: 0}


class SpirvError(ValueError):
    """Malformed module or an instruction outside the parser's safe subset."""


@dataclass(frozen=True)
class Instruction:
    opcode: int
    operands: tuple
    word_offset: int

    @property
    def name(self):
        return OP_NAMES.get(self.opcode, f"Op{self.opcode}")


@dataclass
class SpirvModule:
    version: int
    generator: int
    bound: int
    schema: int
    instructions: list = field(default_factory=list)
    types: dict = field(default_factory=dict)
    constants: dict = field(default_factory=dict)
    variables: dict = field(default_factory=dict)
    decorations: dict = field(default_factory=dict)
    member_decorations: dict = field(default_factory=dict)
    names: dict = field(default_factory=dict)
    entry_points: list = field(default_factory=list)
    ext_inst_imports: dict = field(default_factory=dict)


def decode_string(words):
    raw = struct.pack("<%dI" % len(words), *words)
    end = raw.find(b"\0")
    if end < 0:
        raise SpirvError("SPIR-V string is not null terminated")
    try:
        return raw[:end].decode("utf-8"), (end + 1 + 3) // 4
    except UnicodeDecodeError as error:
        raise SpirvError("SPIR-V string is not valid UTF-8") from error


def parse_spirv(binary):
    if not isinstance(binary, (bytes, bytearray, memoryview)):
        raise SpirvError("SPIR-V input must be bytes")
    if len(binary) < 20 or len(binary) % 4:
        raise SpirvError("SPIR-V module has a truncated or unaligned header")
    words = struct.unpack("<%dI" % (len(binary) // 4), binary)
    if words[0] != MAGIC:
        raise SpirvError("invalid SPIR-V magic number")
    version, generator, bound, schema = words[1:5]
    if version < 0x00010000 or version >= 0x00020000:
        raise SpirvError(f"unsupported SPIR-V version 0x{version:08X}")
    if not bound or schema:
        raise SpirvError("invalid SPIR-V id bound or schema")

    module = SpirvModule(version, generator, bound, schema)
    index = 5
    while index < len(words):
        first = words[index]
        word_count, opcode = first >> 16, first & 0xFFFF
        if word_count < 1 or index + word_count > len(words):
            raise SpirvError(
                f"instruction at word {index} has invalid word count {word_count}"
            )
        if opcode not in OP_NAMES:
            raise SpirvError(f"unsupported SPIR-V opcode {opcode} at word {index}")
        operands = tuple(words[index + 1:index + word_count])
        exact = EXACT_OPERANDS.get(opcode)
        if exact is not None and len(operands) != exact:
            raise SpirvError(
                f"malformed {OP_NAMES[opcode]} at word {index}: expected exactly "
                f"{exact} operands, got {len(operands)}"
            )
        minimum = MIN_OPERANDS.get(opcode)
        if minimum is not None and len(operands) < minimum:
            raise SpirvError(
                f"malformed {OP_NAMES[opcode]} at word {index}: expected at least "
                f"{minimum} operands, got {len(operands)}"
            )
        ins = Instruction(opcode, operands, index)
        module.instructions.append(ins)
        _index_instruction(module, ins)
        index += word_count

    if not module.entry_points:
        raise SpirvError("SPIR-V module has no entry point")
    return module


def _index_instruction(module, ins):
    op, a = ins.opcode, ins.operands
    if op == 15:  # execution model, function, string, interface IDs
        if len(a) < 3:
            raise SpirvError(f"malformed OpEntryPoint at word {ins.word_offset}")
        name, used = decode_string(a[2:])
        module.entry_points.append((a[0], a[1], name, tuple(a[2 + used:])))
    elif op == 5:
        if len(a) < 2:
            raise SpirvError(f"malformed OpName at word {ins.word_offset}")
        module.names[a[0]] = decode_string(a[1:])[0]
    elif op == 11:
        if len(a) < 2:
            raise SpirvError(f"malformed OpExtInstImport at word {ins.word_offset}")
        _check_id(module, a[0], ins)
        module.ext_inst_imports[a[0]] = decode_string(a[1:])[0]
    elif op == 71:
        if len(a) < 2:
            raise SpirvError(f"malformed OpDecorate at word {ins.word_offset}")
        _validate_decoration_arity(ins, a[1], a[2:])
        module.decorations.setdefault(a[0], {})[a[1]] = tuple(a[2:])
    elif op == 72:
        if len(a) < 3:
            raise SpirvError(f"malformed OpMemberDecorate at word {ins.word_offset}")
        _validate_decoration_arity(ins, a[2], a[3:])
        module.member_decorations.setdefault((a[0], a[1]), {})[a[2]] = tuple(a[3:])
    elif 19 <= op <= 33 and op not in (31,):
        if not a:
            raise SpirvError(f"malformed {ins.name} at word {ins.word_offset}")
        result_id = a[0]
        _check_id(module, result_id, ins)
        module.types[result_id] = ins
    elif op in (41, 42, 43, 44, 46):
        if len(a) < 2:
            raise SpirvError(f"malformed {ins.name} at word {ins.word_offset}")
        result_type, result_id = a[:2]
        _check_id(module, result_id, ins)
        if op == 41:
            module.constants[result_id] = True
        elif op == 42:
            module.constants[result_id] = False
        elif op == 43:
            module.constants[result_id] = tuple(a[2:])
        elif op == 44:
            module.constants[result_id] = tuple(a[2:])
        else:
            module.constants[result_id] = 0
        module.constants.setdefault(("type", result_id), result_type)
    elif op == 59:
        if len(a) < 3:
            raise SpirvError(f"malformed OpVariable at word {ins.word_offset}")
        _check_id(module, a[1], ins)
        module.variables[a[1]] = (a[0], a[2], tuple(a[3:]))


def _check_id(module, value, ins):
    if value == 0 or value >= module.bound:
        raise SpirvError(
            f"invalid result id %{value} in {ins.name} at word {ins.word_offset}"
        )


def _validate_decoration_arity(ins, decoration, literals):
    expected = DECORATION_LITERAL_COUNTS.get(decoration)
    if expected is None:
        return
    if len(literals) != expected:
        name = DECORATION_NAMES.get(decoration, str(decoration))
        raise SpirvError(
            f"malformed {ins.name} {name} decoration at word {ins.word_offset}: "
            f"expected {expected} literal operand(s), got {len(literals)}"
        )
