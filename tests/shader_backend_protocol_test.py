"""Real-shaderc tests for the reusable host protocol and platform layout."""

import base64
import hashlib
import json
import pathlib
import queue
import struct
import subprocess
import sys
import tempfile
import threading
import time
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools" / "shader-probe"))
PROTOCOL = ROOT / "tools" / "shader-probe" / "backend_protocol.py"
SHADERC = pathlib.Path(
    r"C:\dev\helworks\helengine-wiiu\builder\bin\Debug\net9.0-windows"
    r"\runtimes\win-x64\native\shaderc_shared.dll"
)
FIXTURES = ROOT / "tests" / "shaders" / "fixtures"


def request(source, stage, source_name="memory/main.hlsl", **changes):
    value = {
        "protocol": 1,
        "type": "compile",
        "source": source,
        "source_name": source_name,
        "stage": stage,
        "entrypoint": "main",
        "program": "test-program",
        "variant": "default",
        "target": "x360",
        "shader_model": "4.0",
        "defines": {},
        "options": {
            "debug_info": False,
            "optimize": False,
            "warnings_as_errors": False,
        },
        "binding_policy": {
            "default_space": 0,
            "constant_buffer_shift": 0,
            "texture_shift": 32,
            "sampler_shift": 64,
            "storage_shift": 96,
        },
    }
    value.update(changes)
    return value


def spirv_with_entry_point(*instructions):
    words = [0x07230203, 0x00010000, 0, 4, 0]
    all_instructions = [(15, (4, 1, 0x6E69616D, 0)), *instructions]
    for opcode, operands in all_instructions:
        words.append(((len(operands) + 1) << 16) | opcode)
        words.extend(operands)
    return struct.pack("<%dI" % len(words), *words)


def run_raw_protocol(data, shaderc=SHADERC):
    return subprocess.run(
        [sys.executable, "-u", str(PROTOCOL), "--shaderc", str(shaderc)],
        input=data,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        cwd=ROOT,
        timeout=20,
    )


def run_protocol(message, include_sources=None, shaderc=SHADERC, include_events=None,
                 response_id_delta=0, *, script_path=PROTOCOL, timeout_seconds=20.0,
                 process_observer=None):
    process = subprocess.Popen(
        [sys.executable, "-u", str(script_path), "--shaderc", str(shaderc)],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        cwd=ROOT,
    )
    if process_observer is not None:
        process_observer.append(process)
    output_lines = queue.Queue()

    def read_stdout():
        try:
            for output_line in process.stdout:
                output_lines.put(("line", output_line))
        except BaseException as error:
            output_lines.put(("error", error))
        finally:
            output_lines.put(("eof", None))

    reader = threading.Thread(target=read_stdout, daemon=True)
    reader.start()
    deadline = time.monotonic() + timeout_seconds
    value = None
    include_sources = include_sources or {}
    try:
        process.stdin.write(json.dumps(message, ensure_ascii=False).encode("utf-8") + b"\n")
        process.stdin.flush()
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError
            try:
                kind, line = output_lines.get(timeout=remaining)
            except queue.Empty as error:
                raise TimeoutError from error
            if kind == "error":
                raise line
            if kind == "eof":
                break
            value = json.loads(line.decode("utf-8"))
            if value.get("type") != "include":
                break
            if include_events is not None:
                include_events.append(value)
            include_name = value["include_name"]
            resolved = include_sources.get(include_name)
            response = {
                "protocol": 1,
                "type": "include_result",
                "id": value["id"] + response_id_delta,
            }
            if resolved is None:
                response["error"] = "test include not found"
            else:
                response["source_name"], response["source"] = resolved
            process.stdin.write(json.dumps(response).encode("utf-8") + b"\n")
            process.stdin.flush()
        process.stdin.close()
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise TimeoutError
        try:
            code = process.wait(timeout=remaining)
        except subprocess.TimeoutExpired as error:
            raise TimeoutError from error
        stderr = process.stderr.read().decode("utf-8", errors="replace")
        reader.join(timeout=1)
        return code, value, stderr
    except TimeoutError as error:
        raise TimeoutError(
            f"protocol deadline of {timeout_seconds:.3f}s exceeded"
        ) from error
    finally:
        if process.stdin is not None and not process.stdin.closed:
            process.stdin.close()
        if process.poll() is None:
            process.kill()
            try:
                process.wait(timeout=2)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
        reader.join(timeout=1)
        process.stdout.close()
        process.stderr.close()


class BackendProtocolTests(unittest.TestCase):
    def test_spirv_line_metadata_accepts_valid_exact_arities(self):
        from spirv_parser import parse_spirv

        for opcode, operands in ((8, (2, 10, 4)), (317, ())):
            with self.subTest(opcode=opcode):
                module = parse_spirv(spirv_with_entry_point((opcode, operands)))
                self.assertEqual(module.instructions[-1].opcode, opcode)

    def test_spirv_line_rejects_extra_operands(self):
        from spirv_parser import SpirvError, parse_spirv

        malformed = spirv_with_entry_point((8, (2, 10, 4, 99)))
        with self.assertRaisesRegex(SpirvError, "Line.*expected exactly 3 operands"):
            parse_spirv(malformed)

    def test_spirv_noline_rejects_any_operands(self):
        from spirv_parser import SpirvError, parse_spirv

        malformed = spirv_with_entry_point((317, (1,)))
        with self.assertRaisesRegex(SpirvError, "NoLine.*expected exactly 0 operands"):
            parse_spirv(malformed)

    def test_protocol_harness_deadline_kills_its_own_hung_child(self):
        with tempfile.TemporaryDirectory() as directory:
            hanging_script = pathlib.Path(directory) / "hanging_protocol.py"
            hanging_script.write_text("import time\ntime.sleep(30)\n", encoding="utf-8")
            owned_processes = []
            started = time.monotonic()
            with self.assertRaisesRegex(TimeoutError, "protocol deadline"):
                run_protocol(
                    request("", "pixel"),
                    script_path=hanging_script,
                    timeout_seconds=0.2,
                    process_observer=owned_processes,
                )
            self.assertLess(time.monotonic() - started, 3.0)
            self.assertEqual(len(owned_processes), 1)
            self.assertIsNotNone(owned_processes[0].poll())

    def test_reflection_rejects_unknown_storage_classes_but_skips_nonresources(self):
        from backend_layout import LayoutError, _reflect_resources
        from spirv_parser import SpirvModule

        for storage in (1, 3, 6, 7):
            module = SpirvModule(0x00010000, 0, 100, 0,
                                 variables={1: (2, storage, ())})
            self.assertEqual(_reflect_resources(module, "pixel"), ([], None, [], []))

        for storage in (4, 5, 8, 9, 10, 11, 12, 13):
            module = SpirvModule(0x00010000, 0, 100, 0,
                                 variables={1: (2, storage, ())})
            with self.subTest(storage=storage), self.assertRaisesRegex(
                    LayoutError, rf"unsupported SPIR-V storage class.*{storage}"):
                _reflect_resources(module, "pixel")

    def test_transform_round_trip_reflects_real_interfaces_and_uniform_members(self):
        source = (FIXTURES / "transform_vs.hlsl").read_text(encoding="utf-8")
        code, result, stderr = run_protocol(request(source, "vertex"))

        self.assertEqual(code, 0, stderr + str(result))
        self.assertTrue(result["success"], result["diagnostics"])
        binary = base64.b64decode(result["binary_base64"], validate=True)
        self.assertEqual(
            hashlib.sha256(binary).hexdigest().upper(),
            "40C01033D5B097352583DE0F004FF1D37D5640C456BCAADF1941D9B7CEC284EF",
        )
        self.assertEqual(
            [(x["location"], x["format"]) for x in result["reflection"]["inputs"]],
            [(0, "float3"), (2, "float2"), (3, "float4")],
        )
        uniform = next(x for x in result["reflection"]["bindings"]
                       if x["kind"] == "constant_buffer")
        self.assertEqual(uniform["name"], "Transform")
        self.assertEqual(uniform["set"], 0)
        self.assertEqual(uniform["binding"], 0)
        self.assertEqual(uniform["size"], 80)
        self.assertEqual(uniform["members"], [
            {"name": "clipFromObject", "type": "float4x4", "offset": 0, "size": 64},
            {"name": "tint", "type": "float4", "offset": 64, "size": 16},
        ])
        self.assertEqual(result["metadata"]["uniforms"]["0"]["register_count"], 5)
        matrix = result["metadata"]["uniforms"]["0"]["members"][0]
        self.assertEqual(matrix["major"], "row_major")
        self.assertEqual(matrix["matrix_stride_bytes"], 16)
        self.assertTrue(any(item["builtin"] == 0 for item in result["reflection"]["outputs"]))

    def test_textured_pixel_round_trip_uses_actual_texture_pair_at_tf1(self):
        source = (FIXTURES / "texture_ps.hlsl").read_text(encoding="utf-8")
        code, result, stderr = run_protocol(request(source, "pixel"))

        self.assertEqual(code, 0, stderr)
        self.assertTrue(result["success"], result["diagnostics"])
        binary = base64.b64decode(result["binary_base64"], validate=True)
        self.assertEqual(
            hashlib.sha256(binary).hexdigest().upper(),
            "D70B3ADAFE4D38E2DEE8C77E038B06B712836C58F28E48CE850A2795F547016A",
        )
        resources = {item["kind"]: item for item in result["reflection"]["bindings"]}
        self.assertEqual(resources["texture2d"]["name"], "texture0")
        self.assertEqual(resources["sampler"]["name"], "sampler0")
        self.assertEqual((resources["texture2d"]["set"], resources["texture2d"]["binding"]), (0, 0))
        self.assertEqual((resources["sampler"]["set"], resources["sampler"]["binding"]), (0, 0))
        self.assertEqual(result["metadata"]["textures"][0]["fetch_constant"], 1)

    def test_optimized_textured_spritebatch_reflection_preserves_resource_names(self):
        source_path = pathlib.Path(
            r"C:\dev\helworks\helengine\engine\helengine.editor"
            r"\shaders\builtin\SpriteBatchShader.hlsl"
        )
        source = source_path.read_text(encoding="utf-8")
        for debug_info in (False, True):
            with self.subTest(debug_info=debug_info):
                code, result, stderr = run_protocol(request(
                    source,
                    "pixel",
                    source_name=str(source_path),
                    entrypoint="PS",
                    variant="Textured",
                    defines={},
                    options={
                        "optimize": True,
                        "debug_info": debug_info,
                        "warnings_as_errors": False,
                    },
                ))

                self.assertEqual(code, 0, stderr + str(result))
                self.assertTrue(result["success"], result["diagnostics"])
                resources = {
                    item["name"] for item in result["reflection"]["bindings"]
                }
                self.assertIn("BatchTexture", resources)
                self.assertIn("BatchSampler", resources)

    def test_optimized_reflection_preserves_uniform_member_names(self):
        source = (FIXTURES / "transform_vs.hlsl").read_text(encoding="utf-8")
        for debug_info in (False, True):
            with self.subTest(debug_info=debug_info):
                code, result, stderr = run_protocol(request(
                    source,
                    "vertex",
                    options={
                        "optimize": True,
                        "debug_info": debug_info,
                        "warnings_as_errors": False,
                    },
                ))

                self.assertEqual(code, 0, stderr + str(result))
                self.assertTrue(result["success"], result["diagnostics"])
                uniform = next(
                    item for item in result["reflection"]["bindings"]
                    if item["kind"] == "constant_buffer"
                )
                self.assertEqual(uniform["name"], "Transform")
                self.assertEqual(
                    [member["name"] for member in uniform["members"]],
                    ["clipFromObject", "tint"],
                )

    def test_optimized_includes_preserve_resource_reflection_names(self):
        source = (
            '#include "resources.hlsli"\n'
            'float4 main(float2 uv : TEXCOORD0) : SV_Target0 {\n'
            '    return IncludedTexture.Sample(IncludedSampler, uv);\n'
            '}\n'
        )
        include = (
            "Texture2D IncludedTexture : register(t0);\n"
            "SamplerState IncludedSampler : register(s0);\n"
        )
        code, result, stderr = run_protocol(
            request(source, "pixel", options={
                "optimize": True,
                "debug_info": False,
                "warnings_as_errors": False,
            }),
            {"resources.hlsli": ("shared/resources.hlsli", include)},
        )

        self.assertEqual(code, 0, stderr + str(result))
        self.assertTrue(result["success"], result["diagnostics"])
        self.assertEqual(
            {item["name"] for item in result["reflection"]["bindings"]},
            {"IncludedTexture", "IncludedSampler"},
        )
        self.assertEqual(result["includes"], [{
            "source_name": "shared/resources.hlsli",
            "sha256": hashlib.sha256(include.encode("utf-8")).hexdigest(),
        }])

    def test_required_reflection_names_are_rejected_when_absent(self):
        from backend_layout import LayoutError, _reflect_resources
        from spirv_parser import Instruction, SpirvModule

        unnamed_resource = SpirvModule(
            0x00010000, 0, 100, 0, variables={17: (2, 0, ())}
        )
        with self.assertRaisesRegex(LayoutError, "resource variable %17 has no OpName"):
            _reflect_resources(unnamed_resource, "pixel")

        unnamed_member = SpirvModule(
            0x00010000,
            0,
            100,
            0,
            types={
                2: Instruction(32, (2, 2, 3), 0),
                3: Instruction(30, (3, 4), 0),
                4: Instruction(22, (32, 0), 0),
            },
            variables={1: (2, 2, ())},
            decorations={1: {33: (0,), 34: (0,)}},
            member_decorations={(3, 0): {35: (0,)}},
            names={1: "NamedBuffer"},
        )
        with self.assertRaisesRegex(LayoutError, "member 0 has no OpMemberName"):
            _reflect_resources(unnamed_member, "vertex")

    def test_debug_line_metadata_does_not_hide_unsupported_runtime_operations(self):
        source = (
            "float4 main(float4 color : COLOR0) : SV_Target0 {\n"
            "    discard;\n"
            "    return color;\n"
            "}\n"
        )
        code, result, stderr = run_protocol(request(source, "pixel", options={
            "optimize": False,
            "debug_info": True,
            "warnings_as_errors": False,
        }))

        self.assertNotEqual(code, 0, stderr + str(result))
        self.assertFalse(result["success"])
        self.assertIn(
            "unsupported SPIR-V opcode 252",
            result["diagnostics"],
        )

    def test_includes_use_the_live_protocol_and_hash_supplied_contents(self):
        source = (
            '#include "active.hlsli"\n'
            'struct Input { [[vk::location(0)]] float4 color : COLOR0; };\n'
            'float4 main(Input input) : SV_Target0 { return includedColor(input.color); }\n'
        )
        include = "float4 includedColor(float4 color) { return color * color; }\n"
        code, result, stderr = run_protocol(
            request(source, "pixel", source_name="arbitrary/source.hlsl"),
            {"active.hlsli": ("virtual/active.hlsli", include)},
        )

        self.assertEqual(code, 0, stderr + str(result))
        self.assertTrue(result["success"], result["diagnostics"])
        self.assertEqual(result["includes"], [{
            "source_name": "virtual/active.hlsli",
            "sha256": hashlib.sha256(include.encode("utf-8")).hexdigest(),
        }])

    def test_nested_conditional_includes_request_only_active_files(self):
        source = (
            '#if ENABLED\n#include "outer.hlsli"\n'
            '#else\n#include "inactive.hlsli"\n#endif\n'
            'struct Input { [[vk::location(0)]] float4 color : COLOR0; };\n'
            'float4 main(Input input) : SV_Target0 { return includedColor(input.color); }\n'
        )
        includes = {
            "outer.hlsli": ("virtual/outer.hlsli", '#include "inner.hlsli"\n'),
            "inner.hlsli": (
                "virtual/inner.hlsli",
                "float4 includedColor(float4 color) { return color * color; }\n",
            ),
        }
        events = []
        code, result, stderr = run_protocol(
            request(source, "pixel", defines={"ENABLED": "1"}),
            includes,
            include_events=events,
        )

        self.assertEqual(code, 0, stderr + str(result))
        self.assertTrue(result["success"], result["diagnostics"])
        self.assertEqual([event["include_name"] for event in events], ["outer.hlsli", "inner.hlsli"])
        self.assertEqual([event["id"] for event in events], [1, 2])
        self.assertEqual([item["source_name"] for item in result["includes"]],
                         ["virtual/outer.hlsli", "virtual/inner.hlsli"])

    def test_wrong_include_id_fails_without_emitting_binary(self):
        source = '#include "active.hlsli"\nfloat4 main() : SV_Target0 { return input.color; }\n'
        code, result, _stderr = run_protocol(
            request(source, "pixel"),
            {"active.hlsli": ("virtual/active.hlsli", "")},
            response_id_delta=1,
        )
        self.assertNotEqual(code, 0)
        self.assertFalse(result["success"])
        self.assertNotIn("binary_base64", result)
        self.assertIn("ID mismatch", result["diagnostics"])

    def test_missing_include_is_reported_and_never_looked_up_implicitly(self):
        source = (
            '#include "not-provided.hlsli"\n'
            'struct Input { [[vk::location(0)]] float4 color : COLOR0; };\n'
            'float4 main(Input input) : SV_Target0 { return includedColor(input.color); }\n'
        )
        code, result, _stderr = run_protocol(
            request(source, "pixel", source_name="virtual/main.hlsl"), {}
        )
        self.assertNotEqual(code, 0)
        self.assertFalse(result["success"])
        self.assertNotIn("binary_base64", result)
        self.assertIn("test include not found", result["diagnostics"])

    def test_changed_supported_expression_and_variant_change_identity(self):
        prefix = "struct Input { [[vk::location(0)]] float4 color : COLOR0; };\n"
        first_source = prefix + "float4 main(Input i) : SV_Target0 { return i.color * i.color; }\n"
        second_source = prefix + "float4 main(Input i) : SV_Target0 { return i.color + i.color; }\n"
        code_a, first, stderr_a = run_protocol(request(first_source, "pixel"))
        code_b, second, stderr_b = run_protocol(request(second_source, "pixel"))
        self.assertEqual((code_a, code_b), (0, 0), stderr_a + stderr_b)
        self.assertNotEqual(first["binary_base64"], second["binary_base64"])
        self.assertNotEqual(first["compile_identity"], second["compile_identity"])

        variant = request(first_source, "pixel", variant="alternate")
        code_c, third, stderr_c = run_protocol(variant)
        self.assertEqual(code_c, 0, stderr_c)
        self.assertEqual(first["binary_base64"], third["binary_base64"])
        self.assertNotEqual(first["compile_identity"], third["compile_identity"])
        self.assertEqual(first["tool_identity"], third["tool_identity"])

        invalid = request(first_source, "pixel")
        invalid["target"] = "other"
        failed_code, failed, _stderr = run_protocol(invalid)
        self.assertNotEqual(failed_code, 0)
        self.assertFalse(failed["success"])
        self.assertNotIn("binary_base64", failed)
        fresh_code, fresh, fresh_stderr = run_protocol(request(first_source, "pixel"))
        self.assertEqual(fresh_code, 0, fresh_stderr)
        self.assertTrue(fresh["success"])
        self.assertEqual(fresh["binary_base64"], first["binary_base64"])

    def test_include_contents_and_options_change_compile_identity(self):
        source = (
            '#include "color.hlsli"\n'
            'struct Input { [[vk::location(0)]] float4 color : COLOR0; };\n'
            'float4 main(Input input) : SV_Target0 { return includedColor(input.color); }\n'
        )
        first_include = "float4 includedColor(float4 color) { return color * color; }\n"
        second_include = first_include.rstrip() + " // comment changes include bytes\n"
        first_request = request(source, "pixel")
        first_request["options"]["warnings_as_errors"] = False
        code_a, first, stderr_a = run_protocol(
            first_request, {"color.hlsli": ("virtual/color.hlsli", first_include)}
        )
        code_b, second, stderr_b = run_protocol(
            first_request, {"color.hlsli": ("virtual/color.hlsli", second_include)}
        )
        self.assertEqual((code_a, code_b), (0, 0), stderr_a + stderr_b)
        self.assertEqual(first["binary_base64"], second["binary_base64"])
        self.assertNotEqual(first["includes"][0]["sha256"], second["includes"][0]["sha256"])
        self.assertNotEqual(first["compile_identity"], second["compile_identity"])

        option_request = request(source, "pixel")
        option_request["options"]["warnings_as_errors"] = True
        code_c, third, stderr_c = run_protocol(
            option_request, {"color.hlsli": ("virtual/color.hlsli", first_include)}
        )
        self.assertEqual(code_c, 0, stderr_c)
        self.assertEqual(first["binary_base64"], third["binary_base64"])
        self.assertNotEqual(first["compile_identity"], third["compile_identity"])

    def test_malformed_protocol_and_wrong_interface_fail_without_binary(self):
        malformed = run_raw_protocol(b"{not json}\n")
        self.assertNotEqual(malformed.returncode, 0)
        malformed_result = json.loads(malformed.stdout.decode("utf-8"))
        self.assertFalse(malformed_result["success"])
        self.assertNotIn("binary_base64", malformed_result)

        source = (
            "struct Input { [[vk::location(8)]] float4 color : COLOR0; };\n"
            "float4 main(Input input) : SV_Target0 { return input.color; }\n"
        )
        code, result, stderr = run_protocol(request(source, "pixel"))
        self.assertNotEqual(code, 0)
        self.assertFalse(result["success"], stderr)
        self.assertNotIn("binary_base64", result)
        self.assertIn("location must be in 0..7", result["diagnostics"])

    def test_line_and_microcode_limits_reject_invalid_sizes(self):
        import io

        from backend_protocol import MAX_LINE_BYTES, _check_microcode, _write_protocol_line
        from lower_xenos import LoweringError

        process = subprocess.Popen(
            [sys.executable, "-u", str(PROTOCOL), "--shaderc", str(SHADERC)],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            cwd=ROOT,
        )
        stdout, stderr = process.communicate(b" " * (MAX_LINE_BYTES + 1) + b"\n", timeout=10)
        self.assertNotEqual(process.returncode, 0)
        result = json.loads(stdout.decode("utf-8"))
        self.assertFalse(result["success"])
        self.assertIn("exceeds", result["diagnostics"])
        self.assertEqual(stderr, b"")

        _check_microcode(b"\0" * (1024 * 4))
        for bad in (b"", b"\0", b"\0" * (1025 * 4)):
            with self.subTest(size=len(bad)), self.assertRaises(LoweringError):
                _check_microcode(bad)
        with self.assertRaisesRegex(ValueError, "exceeds"):
            _write_protocol_line(io.BytesIO(), {"value": "x" * MAX_LINE_BYTES})

    def test_tool_identity_sources_follow_bundled_shader_tools_layout(self):
        from backend_protocol import _tool_source_paths

        bundle_root = pathlib.Path("C:/builder/output") / "shader-tools"
        protocol_path = bundle_root / "shader-probe" / "backend_protocol.py"
        frontend_path = bundle_root / "shader-probe" / "shader_probe.py"
        sources = _tool_source_paths(protocol_path, frontend_path)
        self.assertIn(
            ("vertex_layout.py", bundle_root / "shader-probe" / "vertex_layout.py"),
            sources,
        )
        self.assertEqual(sources[-1][1], bundle_root / "xenos_asm.py")

    def test_invalid_request_and_missing_shaderc_return_failure_without_binary(self):
        malformed = request("float4 main() : SV_Target0 { return 1; }", "pixel")
        malformed["binding_policy"]["default_space"] = 1
        code, result, _stderr = run_protocol(malformed)
        self.assertNotEqual(code, 0)
        self.assertIsNotNone(result)
        self.assertFalse(result["success"])
        self.assertNotIn("binary_base64", result)

        code, result, _stderr = run_protocol(
            request("float4 main() : SV_Target0 { return 1; }", "pixel"),
            shaderc=ROOT / "missing-shaderc.dll",
        )
        self.assertNotEqual(code, 0)
        self.assertIsNotNone(result)
        self.assertFalse(result["success"])
        self.assertNotIn("binary_base64", result)


if __name__ == "__main__":
    unittest.main()
