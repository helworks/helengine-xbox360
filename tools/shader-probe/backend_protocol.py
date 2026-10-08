#!/usr/bin/env python3
"""One-shot JSON-lines host for the bounded Xenos shader compiler."""

import argparse
import base64
import hashlib
import json
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

import shader_probe
from backend_layout import LayoutError, reflect_layout
from lower_xenos import LoweringError, lower_spirv
from vertex_layout import DEFAULT_VERTEX_LAYOUT, normalize_vertex_layout, profiles_abi


PROTOCOL_VERSION = 1
MAX_LINE_BYTES = 8 * 1024 * 1024
MAX_INCLUDE_REQUESTS = 4096
MAX_MICROCODE_DWORDS = 1024
ABI = {
    "protocol": 1,
    "layout": "x360-reflected-layout-v1",
    "vertex_layouts": profiles_abi(),
    "varying_locations": [0, 7],
    "uniform_registers": 256,
    "texture_fetch_constant": 1,
    "max_microcode_dwords": MAX_MICROCODE_DWORDS,
}
REQUEST_FIELDS = {
    "protocol", "type", "source", "source_name", "stage", "entrypoint",
    "program", "variant", "target", "shader_model", "defines", "options",
    "binding_policy", "vertex_layout",
}
OPTION_FIELDS = {"debug_info", "optimize", "warnings_as_errors"}
BINDING_POLICY_FIELDS = {
    "default_space", "constant_buffer_shift", "texture_shift", "sampler_shift",
    "storage_shift",
}


class ProtocolError(ValueError):
    """Malformed host request or include exchange."""


def _canonical_bytes(value):
    return json.dumps(value, ensure_ascii=False, sort_keys=True,
                      separators=(",", ":"), allow_nan=False).encode("utf-8")


def _sha256(data):
    return hashlib.sha256(data).hexdigest().upper()


def _read_line(stream):
    raw = stream.readline(MAX_LINE_BYTES + 1)
    if len(raw) > MAX_LINE_BYTES:
        raise ProtocolError(f"protocol line exceeds {MAX_LINE_BYTES} UTF-8 bytes")
    if not raw:
        raise ProtocolError("unexpected EOF while reading protocol message")
    try:
        text = raw.decode("utf-8")
    except UnicodeDecodeError as error:
        raise ProtocolError(f"protocol line is not valid UTF-8: {error}") from error
    try:
        return json.loads(text, object_pairs_hook=_unique_object)
    except (json.JSONDecodeError, ProtocolError) as error:
        raise ProtocolError(f"malformed protocol JSON: {error}") from error


def _write_protocol_line(stream, value):
    encoded = _canonical_bytes(value)
    if len(encoded) + 1 > MAX_LINE_BYTES:
        raise ProtocolError(f"protocol line exceeds {MAX_LINE_BYTES} UTF-8 bytes")
    stream.write(encoded + b"\n")
    stream.flush()


def _unique_object(pairs):
    value = {}
    for key, item in pairs:
        if key in value:
            raise ProtocolError(f"duplicate JSON field {key!r}")
        value[key] = item
    return value


def _object(value, label, fields, optional=()):
    if not isinstance(value, dict):
        raise ProtocolError(f"{label} must be a JSON object")
    missing = (fields - set(optional)) - value.keys()
    unknown = value.keys() - fields
    if missing:
        raise ProtocolError(f"{label} is missing required field(s): {sorted(missing)}")
    if unknown:
        raise ProtocolError(f"{label} has unsupported field(s): {sorted(unknown)}")
    return value


def validate_request(value):
    value = _object(value, "compile request", REQUEST_FIELDS, optional={"vertex_layout"})
    try:
        value["vertex_layout"] = normalize_vertex_layout(
            value.get("vertex_layout", DEFAULT_VERTEX_LAYOUT)
        )
    except ValueError as error:
        raise ProtocolError(str(error)) from error
    if type(value["protocol"]) is not int or value["protocol"] != PROTOCOL_VERSION:
        raise ProtocolError("unsupported protocol version; expected 1")
    if value["type"] != "compile":
        raise ProtocolError("unsupported message type; expected 'compile'")
    for field in ("source", "source_name", "stage", "entrypoint", "program",
                  "variant", "target", "shader_model"):
        if not isinstance(value[field], str):
            raise ProtocolError(f"{field} must be a string")
    for field in ("source_name", "entrypoint", "program", "variant"):
        if not value[field]:
            raise ProtocolError(f"{field} must be nonempty")
    if value["stage"] not in ("vertex", "pixel"):
        raise ProtocolError("unsupported stage; expected 'vertex' or 'pixel'")
    if value["target"] != "x360":
        raise ProtocolError("unsupported target; expected 'x360'")
    if value["shader_model"] != "4.0":
        raise ProtocolError("unsupported shader_model; expected '4.0'")
    if not isinstance(value["defines"], dict):
        raise ProtocolError("defines must be an object mapping names to string values")
    for name, define_value in value["defines"].items():
        if not isinstance(name, str) or not isinstance(define_value, str):
            raise ProtocolError("defines must map string names to string values")
    options = _object(value["options"], "options", OPTION_FIELDS)
    for name, flag in options.items():
        if type(flag) is not bool:
            raise ProtocolError(f"options.{name} must be boolean")
    policy = _object(value["binding_policy"], "binding_policy", BINDING_POLICY_FIELDS)
    for name, number in policy.items():
        if type(number) is not int or number < 0:
            raise ProtocolError(f"binding_policy.{name} must be a nonnegative integer")
    if policy["default_space"] != 0:
        raise ProtocolError("binding_policy.default_space must be 0")
    return value


def _tool_source_paths(protocol_path=None, shader_probe_path=None):
    protocol_path = pathlib.Path(protocol_path or __file__).resolve()
    shader_probe_path = pathlib.Path(shader_probe_path or shader_probe.__file__).resolve()
    probe_directory = protocol_path.parent
    tools_directory = protocol_path.parents[1]
    return [
        ("backend_protocol.py", protocol_path),
        ("backend_layout.py", probe_directory / "backend_layout.py"),
        ("vertex_layout.py", probe_directory / "vertex_layout.py"),
        ("shader_probe.py", shader_probe_path),
        ("lower_xenos.py", probe_directory / "lower_xenos.py"),
        ("spirv_parser.py", probe_directory / "spirv_parser.py"),
        ("xenos_asm.py", tools_directory / "xenos_asm.py"),
    ]


def _tool_identity(shaderc_path):
    source_paths = _tool_source_paths()
    digest = hashlib.sha256()
    digest.update(_canonical_bytes(ABI))
    for label, path in source_paths:
        data = path.read_bytes()
        digest.update(_canonical_bytes({"file": label, "size": len(data)}))
        digest.update(data)
    library = pathlib.Path(shaderc_path).read_bytes()
    digest.update(_canonical_bytes({"file": "shaderc_shared.dll", "size": len(library)}))
    digest.update(library)
    return digest.hexdigest().upper()


def _read_include_response(stream, expected_id):
    value = _read_line(stream)
    if not isinstance(value, dict):
        raise ProtocolError("include response must be a JSON object")
    if (type(value.get("protocol")) is not int or
            value["protocol"] != PROTOCOL_VERSION or value.get("type") != "include_result"):
        raise ProtocolError("include response protocol/type mismatch")
    if type(value.get("id")) is not int or value["id"] != expected_id:
        raise ProtocolError(f"include response ID mismatch; expected {expected_id}")
    if "error" in value:
        if set(value) != {"protocol", "type", "id", "error"} or not isinstance(value["error"], str):
            raise ProtocolError("include error response must contain only a string error")
        raise ProtocolError(f"include resolution failed: {value['error'] or 'unspecified error'}")
    expected = {"protocol", "type", "id", "source_name", "source"}
    if set(value) != expected:
        raise ProtocolError("successful include response has missing or unsupported fields")
    if not isinstance(value["source_name"], str) or not isinstance(value["source"], str):
        raise ProtocolError("include source_name and source must be strings")
    if not value["source_name"]:
        raise ProtocolError("include source_name must be nonempty")
    return value["source_name"], value["source"].encode("utf-8")


def _check_microcode(binary):
    if not binary or len(binary) % 4:
        raise LoweringError("lowered microcode is empty or not dword-aligned")
    dword_count = len(binary) // 4
    if dword_count > MAX_MICROCODE_DWORDS:
        raise LoweringError(
            f"lowered microcode has {dword_count} dwords; stage limit is {MAX_MICROCODE_DWORDS}"
        )


def compile_request(request, shaderc_path, input_stream=None, output_stream=None):
    request = validate_request(request)
    input_stream = input_stream or sys.stdin.buffer
    output_stream = output_stream or sys.stdout.buffer
    library, loaded_path = shader_probe.load_shaderc(shaderc_path)
    tool_identity = _tool_identity(loaded_path)
    include_count = 0

    def resolve_include(requesting_source, include_name):
        nonlocal include_count
        include_count += 1
        if include_count > MAX_INCLUDE_REQUESTS:
            raise ProtocolError(f"include request limit {MAX_INCLUDE_REQUESTS} exceeded")
        request_source = requesting_source
        request_name = include_name
        if not isinstance(request_source, str) or not isinstance(request_name, str):
            raise ProtocolError("shaderc include names must be strings")
        message = {
            "protocol": PROTOCOL_VERSION,
            "type": "include",
            "id": include_count,
            "requesting_source": request_source,
            "include_name": request_name,
        }
        _write_protocol_line(output_stream, message)
        return _read_include_response(input_stream, include_count)

    opts = request["options"]
    try:
        compiled = shader_probe.compile_hlsl_from_source(
            library,
            request["source"].encode("utf-8"),
            request["source_name"],
            request["stage"],
            request["entrypoint"],
            defines=request["defines"],
            include_resolver=resolve_include,
            debug_info=opts["debug_info"],
            optimize=opts["optimize"],
            warnings_as_errors=opts["warnings_as_errors"],
            preserve_reflection_names=opts["optimize"],
        )
    except (ProtocolError, OSError, TypeError, ValueError, RuntimeError) as error:
        return {"success": False, "diagnostics": str(error)}, tool_identity
    if not compiled.succeeded:
        return {"success": False, "diagnostics": compiled.diagnostics}, tool_identity

    reflection, layout = reflect_layout(
        compiled.spirv, request["stage"], request["entrypoint"], request["vertex_layout"]
    )
    lowered = lower_spirv(compiled.spirv, request["stage"], request["entrypoint"], layout)
    lowered.metadata["vertex_layout"] = request["vertex_layout"]
    binary = lowered.binary
    _check_microcode(binary)

    includes = [{"source_name": item.source_name, "sha256": item.sha256}
                for item in compiled.included_sources]
    binary_base64 = base64.b64encode(binary).decode("ascii")
    complete = {
        "request": request,
        "includes": includes,
        "tool_identity": tool_identity,
        "binary_base64": binary_base64,
        "metadata": lowered.metadata,
        "reflection": reflection,
    }
    result = {
        "success": True,
        "binary_base64": binary_base64,
        "metadata": lowered.metadata,
        "reflection": reflection,
        "diagnostics": compiled.diagnostics,
        "includes": includes,
        "tool_identity": tool_identity,
        "compile_identity": _sha256(_canonical_bytes(complete)),
    }
    return result, tool_identity


def _failure(diagnostics):
    return {"protocol": PROTOCOL_VERSION, "type": "result", "success": False,
            "diagnostics": str(diagnostics)}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--shaderc", help="absolute path to shaderc_shared.dll")
    args = parser.parse_args(argv)
    try:
        if not args.shaderc:
            raise ProtocolError("--shaderc ABSOLUTE_DLL_PATH is required")
        if not pathlib.Path(args.shaderc).is_absolute():
            raise ProtocolError("--shaderc must be an absolute DLL path")
        request = _read_line(sys.stdin.buffer)
        payload, tool_identity = compile_request(request, args.shaderc)
        payload.update({"protocol": PROTOCOL_VERSION, "type": "result"})
        if payload["success"]:
            payload["tool_identity"] = tool_identity
    except (ProtocolError, OSError, RuntimeError, TypeError, ValueError,
            LayoutError, LoweringError) as error:
        payload = _failure(error)
    try:
        _write_protocol_line(sys.stdout.buffer, payload)
    except ProtocolError as error:
        payload = _failure(error)
        _write_protocol_line(sys.stdout.buffer, payload)
    return 0 if payload["success"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
