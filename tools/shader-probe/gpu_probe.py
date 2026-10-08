#!/usr/bin/env python3
"""Generate deterministic host data for the bounded X360 GPU diagnostic."""

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import re
import struct
import sys
from typing import NamedTuple

sys.path.insert(0, str(Path(__file__).parent))
sys.path.insert(0, str(Path(__file__).parents[1]))

import shader_probe
from lower_xenos import LoweringError, lower_spirv
from spirv_parser import parse_spirv


PROBE_VS_CONSTANTS = (
    (0.65, 0.10, 0.0, 0.0),
    (0.20, 0.65, 0.0, 0.0),
    (0.0, 0.0, 1.0, 0.0),
    (0.0, 0.0, 0.0, 1.0),
    (1.0, 0.75, 0.5, 1.0),
)
OUTPUT_NAMES = (
    "probe_shaders.h", "probe_manifest.json",
    "vertex.bin", "vertex.xasm", "vertex.json",
    "pixel.bin", "pixel.xasm", "pixel.json",
)
RUNTIME_VERTEX_INPUTS = {
    "0": {"fetch_constant": 0, "stride": 12, "offset": 0, "format": "32_32_32_FLOAT"},
    "3": {"fetch_constant": 0, "stride": 12, "offset": 8, "format": "32_32_32_32_FLOAT"},
    "2": {"fetch_constant": 0, "stride": 12, "offset": 6, "format": "32_32_FLOAT"},
}


class ProbeError(ValueError):
    """Input pair or generated host data is outside the supported diagnostic."""


class InterfaceItem(NamedTuple):
    location: int | None
    builtin: int | None
    scalar: str
    width: int
    variable: str


class Interface(NamedTuple):
    stage: str
    inputs: tuple
    outputs: tuple


class Stage(NamedTuple):
    source_bytes: bytes
    spirv: bytes
    layout_bytes: bytes
    layout: dict
    lowered: object
    interface: Interface


class Pair(NamedTuple):
    vertex: Stage
    pixel: Stage
    shaderc_sha256: str


def _sha256(data):
    return hashlib.sha256(data).hexdigest()


def _path_identity(path):
    return Path(path).resolve()


def _interface_item(module, stage, variable, storage, pointer_type):
    pointer = module.types.get(pointer_type)
    if pointer is None or pointer.opcode != 32 or len(pointer.operands) != 3:
        raise ProbeError(f"{stage} interface variable %{variable} has unsupported pointer type")
    type_id = pointer.operands[2]
    type_ins = module.types.get(type_id)
    if type_ins is None:
        raise ProbeError(f"{stage} interface variable %{variable} has unknown type")
    if type_ins.opcode == 22:
        scalar_id, width = type_id, 1
    elif type_ins.opcode == 23 and len(type_ins.operands) == 3:
        scalar_id, width = type_ins.operands[1], type_ins.operands[2]
    else:
        raise ProbeError(f"{stage} interface variable %{variable} uses an unsupported interface form")
    scalar_ins = module.types.get(scalar_id)
    if scalar_ins is None or scalar_ins.opcode not in (21, 22):
        raise ProbeError(f"{stage} interface variable %{variable} has an unsupported scalar type")
    scalar = "float32" if scalar_ins.opcode == 22 and scalar_ins.operands[1] == 32 else "other"
    decorations = module.decorations.get(variable, {})
    location = decorations.get(30, (None,))[0]
    builtin = decorations.get(11, (None,))[0]
    if (location is None) == (builtin is None):
        raise ProbeError(f"{stage} interface variable %{variable} must have exactly one Location or BuiltIn")
    return InterfaceItem(location, builtin, scalar, width,
                         module.names.get(variable, f"%{variable}"))


def _read_interface(spirv, stage, entrypoint):
    module = parse_spirv(spirv)
    matches = [entry for entry in module.entry_points if entry[2] == entrypoint]
    if len(matches) != 1:
        raise ProbeError(f"{stage} SPIR-V entry point {entrypoint!r} is missing or ambiguous")
    model, _function, _name, interface_ids = matches[0]
    expected_model = 0 if stage == "vertex" else 4
    if model != expected_model:
        raise ProbeError(f"{stage} SPIR-V entry point has the wrong execution model")
    items = {1: [], 3: []}
    for variable in interface_ids:
        entry = module.variables.get(variable)
        if entry is None:
            raise ProbeError(f"{stage} entry point interface %{variable} is not a variable")
        pointer_type, storage, _initializer = entry
        if storage in items:
            items[storage].append(_interface_item(module, stage, variable, storage, pointer_type))
    for values, label in ((items[1], "input"), (items[3], "output")):
        keys = [(x.location, x.builtin) for x in values]
        if len(keys) != len(set(keys)):
            raise ProbeError(f"{stage} has duplicate {label} locations or builtins")
    return Interface(stage, tuple(items[1]), tuple(items[3]))


def _load_json(path, label):
    try:
        raw = Path(path).read_bytes()
        value = json.loads(raw.decode("utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise ProbeError(f"could not read {label}: {error}") from error
    if not isinstance(value, dict):
        raise ProbeError(f"{label} JSON root must be an object")
    return raw, value


def _compile_stage(source_path, layout_path, library, stage):
    source_path, layout_path = Path(source_path), Path(layout_path)
    try:
        source_bytes = source_path.read_bytes()
    except OSError as error:
        raise ProbeError(f"could not read {stage} source: {error}") from error
    layout_bytes, layout = _load_json(layout_path, f"{stage} layout")
    if stage == "vertex" and layout.get("vertex_inputs") != RUNTIME_VERTEX_INPUTS:
        raise ProbeError("vertex layout must exactly match the runtime vf0 position/color/UV layout")
    try:
        spirv = shader_probe.compile_hlsl(library, source_path, stage, "main")
        lowered = lower_spirv(spirv, stage, "main", layout)
        interface = _read_interface(spirv, stage, "main")
    except (OSError, RuntimeError, LoweringError, ValueError) as error:
        raise ProbeError(f"{stage}: {error}") from error
    return Stage(source_bytes, spirv, layout_bytes, layout, lowered, interface)


def compile_pair(vertex_path, pixel_path, vertex_layout_path, pixel_layout_path, shaderc_path):
    try:
        library, library_path = shader_probe.load_shaderc(shaderc_path)
        compiler_hash = _sha256(library_path.read_bytes())
    except (OSError, RuntimeError) as error:
        raise ProbeError(f"shaderc: {error}") from error
    pair = Pair(
        _compile_stage(vertex_path, vertex_layout_path, library, "vertex"),
        _compile_stage(pixel_path, pixel_layout_path, library, "pixel"),
        compiler_hash,
    )
    validate_pair(pair)
    return pair


def _positive_int(value, label, maximum):
    if isinstance(value, bool) or not isinstance(value, int) or not 0 <= value <= maximum:
        raise ProbeError(f"{label} must be an integer in range 0..{maximum}")
    return value


def _validate_registers(stage, stage_name):
    meta = stage.lowered.metadata
    field = _positive_int(meta.get("sq_program_cntl_register_field"),
                          f"{stage_name} program register field", 63)
    count = _positive_int(meta.get("register_count"), f"{stage_name} register count", 64)
    if count == 0 or field != count - 1:
        raise ProbeError(f"{stage_name} program register field/count are inconsistent")
    uniforms = meta.get("uniforms", {})
    if not isinstance(uniforms, dict) or set(uniforms) - {"0"}:
        raise ProbeError(f"{stage_name} supports only uniform binding b0")
    for binding, entry in uniforms.items():
        values = entry.get("register_values")
        members = entry.get("members")
        if not isinstance(values, list) or not isinstance(members, list):
            raise ProbeError(f"{stage_name} uniform metadata is malformed")
        if len(values) > 256:
            raise ProbeError(f"{stage_name} uniform register count exceeds 256")
        used = []
        for member in members:
            if not isinstance(member, dict) or not isinstance(member.get("registers"), list):
                raise ProbeError(f"{stage_name} uniform member metadata is malformed")
            used.extend(member["registers"])
        if used and sorted(set(used)) != list(range(max(used) + 1)):
            raise ProbeError(f"{stage_name} uniform registers are not contiguous from c0")
        for index, row in enumerate(values):
            if not isinstance(row, list) or len(row) != 4:
                raise ProbeError(f"{stage_name} constant c{index} must be a vec4")
            if any(isinstance(value, bool) or not isinstance(value, (int, float))
                   or not math.isfinite(value) for value in row):
                raise ProbeError(f"{stage_name} constant c{index} values must be finite numbers")
    return field


def validate_pair(pair):
    vs, ps = pair.vertex, pair.pixel
    if vs.interface.stage != "vertex" or ps.interface.stage != "pixel":
        raise ProbeError("host pair requires vertex and pixel stages")
    position = [x for x in vs.interface.outputs if x.builtin == 0]
    color = [x for x in ps.interface.outputs if x.location == 0]
    if len(position) != 1 or position[0].scalar != "float32" or position[0].width != 4:
        raise ProbeError("vertex shader must output float32x4 Position")
    if len(color) != 1 or color[0].scalar != "float32" or color[0].width != 4:
        raise ProbeError("pixel shader must output float32x4 color0")
    if len(ps.interface.outputs) != 1 or ps.interface.outputs[0] != color[0]:
        raise ProbeError("pixel shader may output only color0")

    vs_outputs = {x.location: x for x in vs.interface.outputs if x.location is not None}
    vs_map = vs.lowered.metadata.get("varying_map", {})
    ps_inputs = {x.location: x for x in ps.interface.inputs if x.location is not None}
    ps_input_meta = ps.lowered.metadata.get("inputs", {})
    for location, pixel_item in ps_inputs.items():
        vertex_item = vs_outputs.get(location)
        if vertex_item is None:
            raise ProbeError(f"pixel varying location {location} has no matching vertex output")
        if (pixel_item.scalar != "float32" or vertex_item.scalar != "float32"
                or pixel_item.width != vertex_item.width):
            raise ProbeError(f"pixel varying location {location} float32 width does not match vertex output")
        try:
            vertex_slot = int(vs_map[str(location)])
            pixel_slot = int(ps_input_meta[str(location)]["register"])
        except (KeyError, TypeError, ValueError) as error:
            raise ProbeError(f"varying location {location} has no physical interpolator slot") from error
        if vertex_slot != pixel_slot or not 0 <= vertex_slot <= 7:
            raise ProbeError(f"varying location {location} physical interpolator slot mismatch")
    for location in vs_outputs:
        if str(location) not in vs_map:
            raise ProbeError(f"vertex output location {location} has no physical interpolator slot")
    if any(x.location is None for x in vs.interface.inputs + vs.interface.outputs
           if x.builtin not in (0, None)):
        raise ProbeError("unsupported vertex interface builtin")
    if any(x.location is None for x in ps.interface.inputs + ps.interface.outputs
           if x.builtin not in (0, None)):
        raise ProbeError("unsupported pixel interface builtin")
    if vs.lowered.metadata.get("textures"):
        raise ProbeError("vertex texture fetches are unsupported")
    textures = ps.lowered.metadata.get("textures", [])
    if len(textures) != 1 or textures[0].get("stage") != "pixel" or textures[0].get("fetch_constant") != 1:
        raise ProbeError("pixel shader must use exactly one sampled pair at tf1")
    for texture in textures:
        fetch = _positive_int(texture.get("fetch_constant"), "texture fetch slot", 31)
        for item in vs.lowered.metadata.get("inputs", {}).values():
            vf = _positive_int(item.get("fetch_constant"), "vertex fetch slot", 95)
            if 3 * fetch <= vf <= 3 * fetch + 2:
                raise ProbeError(f"tf{fetch} texture fetch aliases vertex fetch vf{vf}")

    actual_inputs = {str(x.location): x for x in vs.interface.inputs if x.location is not None}
    if set(actual_inputs) != set(RUNTIME_VERTEX_INPUTS):
        raise ProbeError("vertex inputs must exactly match the runtime vf0 position/color/UV layout")
    supplied = vs.layout.get("vertex_inputs")
    if not isinstance(supplied, dict) or set(supplied) != set(RUNTIME_VERTEX_INPUTS):
        raise ProbeError("vertex layout must exactly match the runtime vf0 position/color/UV layout")
    for location, expected in RUNTIME_VERTEX_INPUTS.items():
        actual = supplied[location]
        if (not isinstance(actual, dict) or set(actual) != set(expected)
                or any(type(actual[key]) is not int for key in ("fetch_constant", "stride", "offset"))
                or actual != expected):
            raise ProbeError("vertex layout fields and slots must exactly match runtime vf0 layout")
    widths = {"0": 3, "3": 4, "2": 2}
    if any(actual_inputs[key].scalar != "float32" or actual_inputs[key].width != widths[key]
           for key in widths):
        raise ProbeError("compiled vertex inputs do not match float32 position/color/UV widths")
    input_meta = vs.lowered.metadata.get("inputs", {})
    if set(input_meta) != set(widths) or any(
            input_meta[key].get("fetch_constant") != 0
            or input_meta[key].get("stride") != 12
            or input_meta[key].get("offset") != RUNTIME_VERTEX_INPUTS[key]["offset"]
            or input_meta[key].get("format") != RUNTIME_VERTEX_INPUTS[key]["format"]
            for key in widths):
        raise ProbeError("lowered vertex fetch slots do not match runtime vf0 layout")

    texture_bindings = ps.layout.get("texture_bindings")
    if (not isinstance(texture_bindings, list) or len(texture_bindings) != 1
            or not isinstance(texture_bindings[0], dict)
            or texture_bindings[0].get("fetch_constant") != 1):
        raise ProbeError("pixel layout must declare exactly one sampled pair at tf1")
    if _validate_registers(vs, "vertex") > 63 or _validate_registers(ps, "pixel") > 63:
        raise ProbeError("program register field exceeds hardware bounds")
    return pair


def _constant_words(stage):
    entry = stage.lowered.metadata.get("uniforms", {}).get("0")
    if entry is None:
        return ()
    values = entry["register_values"]
    return tuple(struct.unpack("<I", struct.pack("<f", float(value)))[0]
                 for row in values for value in row)


def _array(name, values):
    words = list(values) or [0]
    lines = [f"static const uint32_t {name}[] = {{"]
    for start in range(0, len(words), 6):
        lines.append("    " + ", ".join(f"0x{word:08X}u" for word in words[start:start + 6]) + ",")
    lines.append("};")
    return "\n".join(lines)


def parse_header_array(header, name):
    match = re.search(rf"static const uint32_t {re.escape(name)}\[\] = \{{(.*?)\}};",
                      header, flags=re.S)
    if not match:
        raise ValueError(f"header array {name} is missing")
    return tuple(int(value, 16) for value in re.findall(r"0x([0-9A-Fa-f]{8})u", match.group(1)))


def _build_outputs(pair):
    validate_pair(pair)
    vs = struct.unpack(f">{len(pair.vertex.lowered.binary) // 4}I", pair.vertex.lowered.binary)
    ps = struct.unpack(f">{len(pair.pixel.lowered.binary) // 4}I", pair.pixel.lowered.binary)
    vsc = _constant_words(pair.vertex)
    psc = _constant_words(pair.pixel)
    vs_field = _validate_registers(pair.vertex, "vertex")
    ps_field = _validate_registers(pair.pixel, "pixel")
    allocated_slots = [int(item["location"]) for item in pair.vertex.lowered.metadata["outputs"]
                       if item.get("kind") == "varying"]
    varying_field = max(allocated_slots, default=0)
    program_packet_overhead = 3 * 2  # PM4_LOAD_IMMEDIATE header, shader type, and count per stage.
    constant_packet_overhead = (2 if vsc else 0) + (2 if psc else 0)
    upload_dwords = (len(vs) + len(ps) + program_packet_overhead
                     + len(vsc) + len(psc) + constant_packet_overhead)
    if upload_dwords > 1024:
        raise ProbeError(f"probe upload budget is {upload_dwords} dwords; maximum is 1024")

    identity_material = {
        "vertex_source": _sha256(pair.vertex.source_bytes),
        "pixel_source": _sha256(pair.pixel.source_bytes),
        "vertex_spirv": _sha256(pair.vertex.spirv),
        "pixel_spirv": _sha256(pair.pixel.spirv),
        "vertex_layout": _sha256(pair.vertex.layout_bytes),
        "pixel_layout": _sha256(pair.pixel.layout_bytes),
        "shaderc": pair.shaderc_sha256,
        "vertex_microcode": _sha256(pair.vertex.lowered.binary),
        "pixel_microcode": _sha256(pair.pixel.lowered.binary),
    }
    probe_id = _sha256(json.dumps(identity_material, sort_keys=True, separators=(",", ":")).encode())
    body = ["#ifndef HEL_PROBE_SHADERS_H", "#define HEL_PROBE_SHADERS_H", "",
            "#include <stdint.h>", "",
            _array("hel_probe_vs", vs), _array("hel_probe_ps", ps),
            _array("hel_probe_vs_constants", vsc),
            _array("hel_probe_ps_constants", psc), "",
            f"#define HEL_PROBE_VS_DWORDS {len(vs)}u",
            f"#define HEL_PROBE_PS_DWORDS {len(ps)}u",
            f"#define HEL_PROBE_VS_CONSTANT_DWORDS {len(vsc)}u",
            f"#define HEL_PROBE_PS_CONSTANT_DWORDS {len(psc)}u",
            f"#define HEL_PROBE_VS_REGISTER_FIELD {vs_field}u",
            f"#define HEL_PROBE_PS_REGISTER_FIELD {ps_field}u",
            f"#define HEL_PROBE_INTERPOLATOR_FIELD {varying_field}u",
            f"#define HEL_PROBE_UPLOAD_DWORDS {upload_dwords}u",
            f'#define HEL_PROBE_ID "{probe_id}"', "", "#endif", ""]
    header = "\n".join(body).encode("ascii")
    identity_material["header"] = _sha256(header)
    manifest = {
        "probe_id": probe_id,
        "artifact_status": "generated_header_bridge_not_final_artifact_abi",
        "hashes": identity_material,
        "upload_dwords": upload_dwords,
        "upload_budget_dwords": 1024,
        "upload_cost_definition": {
            "program_microcode_dwords": len(vs) + len(ps),
            "program_packet_overhead_dwords": program_packet_overhead,
            "constant_payload_dwords": len(vsc) + len(psc),
            "constant_packet_header_dwords": constant_packet_overhead,
            "includes_sq_state_writes": False,
        },
        "programs": {
            "vertex": {"dwords": len(vs), "register_field": vs_field,
                       "binary_byte_order": "big_endian"},
            "pixel": {"dwords": len(ps), "register_field": ps_field,
                      "binary_byte_order": "big_endian"},
        },
        "constants": {"vertex_dwords": len(vsc), "pixel_dwords": len(psc),
                      "byte_order": "ieee754_word_values"},
    }
    files = {
        "probe_shaders.h": header,
        "probe_manifest.json": (json.dumps(manifest, sort_keys=True, indent=2) + "\n").encode(),
        "vertex.bin": pair.vertex.lowered.binary,
        "vertex.xasm": pair.vertex.lowered.assembly.encode("utf-8"),
        "vertex.json": (json.dumps(pair.vertex.lowered.metadata, sort_keys=True, indent=2) + "\n").encode(),
        "pixel.bin": pair.pixel.lowered.binary,
        "pixel.xasm": pair.pixel.lowered.assembly.encode("utf-8"),
        "pixel.json": (json.dumps(pair.pixel.lowered.metadata, sort_keys=True, indent=2) + "\n").encode(),
    }
    return files


def _validate_paths(inputs, shaderc_path, output_dir):
    output_dir = Path(output_dir)
    outputs = [output_dir / name for name in OUTPUT_NAMES]
    protected = [Path(item) for item in inputs]
    protected += [Path(__file__), Path(__file__).with_name("shader_probe.py"),
                  Path(__file__).with_name("lower_xenos.py"),
                  Path(__file__).with_name("spirv_parser.py"),
                  Path(__file__).parents[1] / "xenos_asm.py"]
    if shaderc_path:
        protected.append(Path(shaderc_path))
    resolved = [path.resolve() for path in outputs]
    if len(set(resolved)) != len(resolved):
        raise ProbeError("output filenames resolve to duplicate paths")
    protected_resolved = {_path_identity(path) for path in protected}
    resolved_dir = output_dir.resolve()
    if any(resolved_dir == path.parent.resolve() for path in protected_resolved):
        raise ProbeError(f"output directory aliases a protected source, layout, tool, or DLL directory: {resolved_dir}")
    alias = next((path for path in resolved if path in protected_resolved), None)
    if alias is not None:
        raise ProbeError(f"output path aliases a source, layout, tool, or Shaderc DLL: {alias}")
    return outputs


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--vertex", required=True)
    parser.add_argument("--pixel", required=True)
    parser.add_argument("--vertex-layout", required=True)
    parser.add_argument("--pixel-layout", required=True)
    parser.add_argument("--shaderc", required=True)
    parser.add_argument("--output-dir", required=True)
    args = parser.parse_args(argv)
    inputs = [args.vertex, args.pixel, args.vertex_layout, args.pixel_layout]
    try:
        outputs = _validate_paths(inputs, args.shaderc, args.output_dir)
    except (OSError, ProbeError) as error:
        print(f"gpu-probe: {error}", file=sys.stderr)
        return 1
    for output in outputs:
        output.unlink(missing_ok=True)
    try:
        pair = compile_pair(args.vertex, args.pixel, args.vertex_layout,
                            args.pixel_layout, args.shaderc)
        files = _build_outputs(pair)
        outputs[0].parent.mkdir(parents=True, exist_ok=True)
        for output, name in zip(outputs, OUTPUT_NAMES):
            output.write_bytes(files[name])
    except (OSError, RuntimeError, ValueError, ProbeError) as error:
        for output in outputs:
            output.unlink(missing_ok=True)
        print(f"gpu-probe: {error}", file=sys.stderr)
        return 1
    print(f"gpu-probe: wrote {len(outputs)} artifacts to {outputs[0].parent}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
