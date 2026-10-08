"""Behavioral tests for the bounded GPU probe host generator."""

import hashlib
from dataclasses import replace
import json
import pathlib
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
TOOLS = ROOT / "tools" / "shader-probe"
FIXTURES = ROOT / "tests" / "shaders" / "fixtures"
SHADERC = pathlib.Path(
    r"C:\dev\helworks\helengine-wiiu\builder\bin\Debug\net9.0-windows"
    r"\runtimes\win-x64\native\shaderc_shared.dll"
)
sys.path.insert(0, str(TOOLS))

import gpu_probe  # noqa: E402


class ShaderGpuProbeTests(unittest.TestCase):
    def args(self, directory, *, pixel_layout=None):
        temp = pathlib.Path(directory)
        return [
            "--vertex", str(FIXTURES / "transform_vs.hlsl"),
            "--pixel", str(FIXTURES / "texture_ps.hlsl"),
            "--vertex-layout", str(FIXTURES / "gpu_probe_vs_layout.json"),
            "--pixel-layout", str(pixel_layout or FIXTURES / "texture_runtime_layout.json"),
            "--shaderc", str(SHADERC),
            "--output-dir", str(temp / "out"),
        ]

    def run_cli(self, args):
        return subprocess.run([sys.executable, str(TOOLS / "gpu_probe.py"), *args],
                              cwd=ROOT, capture_output=True, text=True, check=False)

    def test_real_hlsl_pair_emits_deterministic_header_manifest_and_evidence(self):
        with tempfile.TemporaryDirectory() as directory:
            args = self.args(directory)
            first = self.run_cli(args)
            self.assertEqual(first.returncode, 0, first.stderr)
            out = pathlib.Path(args[-1])
            header = (out / "probe_shaders.h").read_bytes()
            manifest_bytes = (out / "probe_manifest.json").read_bytes()
            manifest = json.loads(manifest_bytes)
            for stem in ("vertex", "pixel"):
                self.assertTrue((out / f"{stem}.bin").is_file())
                self.assertTrue((out / f"{stem}.xasm").is_file())
                self.assertTrue((out / f"{stem}.json").is_file())
            self.assertIn(b"#include <stdint.h>", header)
            for symbol in (b"hel_probe_vs[]", b"hel_probe_ps[]",
                           b"hel_probe_vs_constants[]", b"hel_probe_ps_constants[]"):
                self.assertIn(symbol, header)
            self.assertIn("HEL_PROBE_VS_DWORDS", header.decode("ascii"))
            self.assertIn("HEL_PROBE_PS_REGISTER_FIELD", header.decode("ascii"))
            self.assertEqual(manifest["programs"]["vertex"]["binary_byte_order"], "big_endian")
            self.assertEqual(manifest["programs"]["pixel"]["binary_byte_order"], "big_endian")
            self.assertEqual(manifest["upload_dwords"], int(
                next(line.split()[-1] for line in header.decode().splitlines()
                     if line.startswith("#define HEL_PROBE_UPLOAD_DWORDS ")).removesuffix("u")))
            self.assertLessEqual(manifest["upload_dwords"], 1024)
            cost = manifest["upload_cost_definition"]
            self.assertEqual(cost["program_packet_overhead_dwords"], 6)
            self.assertFalse(cost["includes_sq_state_writes"])
            self.assertEqual(manifest["upload_dwords"],
                             cost["program_microcode_dwords"]
                             + cost["program_packet_overhead_dwords"]
                             + cost["constant_payload_dwords"]
                             + cost["constant_packet_header_dwords"])

            self.assertEqual(self.run_cli(args).returncode, 0)
            self.assertEqual((out / "probe_shaders.h").read_bytes(), header)
            self.assertEqual((out / "probe_manifest.json").read_bytes(), manifest_bytes)

            changed = pathlib.Path(directory) / "changed_vs.hlsl"
            changed.write_bytes((FIXTURES / "transform_vs.hlsl").read_bytes() + b"\n// identity\n")
            args[1] = str(changed)
            self.assertEqual(self.run_cli(args).returncode, 0)
            self.assertNotEqual((out / "probe_shaders.h").read_bytes(), header)

    def test_header_words_constants_and_register_fields_match_evidence(self):
        with tempfile.TemporaryDirectory() as directory:
            args = self.args(directory)
            result = self.run_cli(args)
            self.assertEqual(result.returncode, 0, result.stderr)
            out = pathlib.Path(args[-1])
            header = (out / "probe_shaders.h").read_text(encoding="ascii")
            manifest = json.loads((out / "probe_manifest.json").read_text())
            for stem, prefix in (("vertex", "VS"), ("pixel", "PS")):
                raw = (out / f"{stem}.bin").read_bytes()
                words = struct.unpack(f">{len(raw) // 4}I", raw)
                array = gpu_probe.parse_header_array(header, f"hel_probe_{prefix.lower()}")
                self.assertEqual(array, words)
                meta = json.loads((out / f"{stem}.json").read_text())
                self.assertEqual(meta["sq_program_cntl_register_field"],
                                 manifest["programs"][stem]["register_field"])
            self.assertEqual(gpu_probe.parse_header_array(header, "hel_probe_vs_constants"),
                             tuple(struct.unpack("<I", struct.pack("<f", value))[0]
                                   for vector in gpu_probe.PROBE_VS_CONSTANTS for value in vector))

    def test_finite_vertex_constant_changes_emitted_words_and_header_identity(self):
        with tempfile.TemporaryDirectory() as directory:
            args = self.args(directory)
            first = self.run_cli(args)
            self.assertEqual(first.returncode, 0, first.stderr)
            out = pathlib.Path(args[-1])
            first_header = (out / "probe_shaders.h").read_text(encoding="ascii")
            first_constants = gpu_probe.parse_header_array(first_header, "hel_probe_vs_constants")
            first_manifest = json.loads((out / "probe_manifest.json").read_text())

            layout = json.loads((FIXTURES / "gpu_probe_vs_layout.json").read_text())
            layout["uniforms"]["0"][0][0] = 0.70
            changed_layout = pathlib.Path(directory) / "changed_vs_layout.json"
            changed_layout.write_text(json.dumps(layout), encoding="utf-8")
            args[args.index("--vertex-layout") + 1] = str(changed_layout)
            changed = self.run_cli(args)
            self.assertEqual(changed.returncode, 0, changed.stderr)
            changed_header = (out / "probe_shaders.h").read_text(encoding="ascii")
            changed_constants = gpu_probe.parse_header_array(changed_header, "hel_probe_vs_constants")
            changed_manifest = json.loads((out / "probe_manifest.json").read_text())
            self.assertNotEqual(changed_constants, first_constants)
            self.assertNotEqual(changed_manifest["probe_id"], first_manifest["probe_id"])

    def test_pair_interface_validation_uses_compiled_spirv_types(self):
        with tempfile.TemporaryDirectory() as directory:
            pair = gpu_probe.compile_pair(
                FIXTURES / "transform_vs.hlsl",
                FIXTURES / "texture_ps.hlsl",
                FIXTURES / "gpu_probe_vs_layout.json",
                FIXTURES / "texture_runtime_layout.json", SHADERC)
            gpu_probe.validate_pair(pair)
            pixel_input = next(x for x in pair.pixel.interface.inputs if x.location == 0)
            broken = pair._replace(pixel=pair.pixel._replace(interface=pair.pixel.interface._replace(
                inputs=tuple(x._replace(width=3) if x.location == 0 else x
                             for x in pair.pixel.interface.inputs))))
            with self.assertRaisesRegex(gpu_probe.ProbeError, "width"):
                gpu_probe.validate_pair(broken)
            broken = pair._replace(pixel=pair.pixel._replace(interface=pair.pixel.interface._replace(
                inputs=tuple(x._replace(location=7) if x.location == 0 else x
                             for x in pair.pixel.interface.inputs))))
            with self.assertRaisesRegex(gpu_probe.ProbeError, "matching vertex output"):
                gpu_probe.validate_pair(broken)
            pixel_meta = dict(pair.pixel.lowered.metadata)
            pixel_meta["inputs"] = {key: dict(value)
                                    for key, value in pixel_meta["inputs"].items()}
            pixel_meta["inputs"]["0"]["register"] = 2
            with self.assertRaisesRegex(gpu_probe.ProbeError, "slot mismatch"):
                gpu_probe.validate_pair(pair._replace(pixel=pair.pixel._replace(
                    lowered=replace(pair.pixel.lowered, metadata=pixel_meta))))
            self.assertEqual(pixel_input.width, 4)

    def test_rejects_texture_vertex_fetch_collision(self):
        with tempfile.TemporaryDirectory() as directory:
            pair = gpu_probe.compile_pair(
                FIXTURES / "transform_vs.hlsl", FIXTURES / "texture_ps.hlsl",
                FIXTURES / "gpu_probe_vs_layout.json", FIXTURES / "texture_runtime_layout.json", SHADERC)
            metadata = dict(pair.vertex.lowered.metadata)
            metadata["inputs"] = {key: dict(value) for key, value in metadata["inputs"].items()}
            metadata["inputs"]["0"]["fetch_constant"] = 3
            altered_lowering = replace(pair.vertex.lowered, metadata=metadata)
            with self.assertRaisesRegex(gpu_probe.ProbeError, "alias"):
                gpu_probe.validate_pair(pair._replace(
                    vertex=pair.vertex._replace(lowered=altered_lowering)))

    def test_failure_clears_only_owned_outputs_and_never_deletes_input_alias(self):
        with tempfile.TemporaryDirectory() as directory:
            args = self.args(directory)
            out = pathlib.Path(args[-1])
            out.mkdir()
            names = ["probe_shaders.h", "probe_manifest.json", "vertex.bin",
                     "vertex.xasm", "vertex.json", "pixel.bin", "pixel.xasm", "pixel.json"]
            for name in names:
                (out / name).write_text("stale", encoding="ascii")
            args[args.index("--vertex-layout") + 1] = str(pathlib.Path(directory) / "missing_layout.json")
            failed = self.run_cli(args)
            self.assertNotEqual(failed.returncode, 0)
            self.assertTrue(all(not (out / name).exists() for name in names))

            alias_args = self.args(directory)
            alias_args[alias_args.index("--output-dir") + 1] = str(FIXTURES)
            alias = self.run_cli(alias_args)
            self.assertNotEqual(alias.returncode, 0)
            self.assertTrue((FIXTURES / "transform_vs.hlsl").exists())

    def test_output_file_alias_of_source_is_rejected_without_deleting_source(self):
        with tempfile.TemporaryDirectory() as directory:
            output_dir = pathlib.Path(directory) / "out"
            output_dir.mkdir()
            source_alias = output_dir / "vertex.bin"
            source_bytes = (FIXTURES / "transform_vs.hlsl").read_bytes()
            source_alias.write_bytes(source_bytes)
            args = self.args(directory)
            args[args.index("--vertex") + 1] = str(source_alias)
            args[args.index("--output-dir") + 1] = str(output_dir)
            result = self.run_cli(args)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("aliases", result.stderr.lower())
            self.assertEqual(source_alias.read_bytes(), source_bytes)

    def test_source_layout_slots_constants_registers_and_upload_budget_are_bounded(self):
        with tempfile.TemporaryDirectory() as directory:
            args = self.args(directory)
            cases = []
            wrong = pathlib.Path(directory) / "wrong_layout.json"
            wrong.write_text(json.dumps({"vertex_inputs": {}}), encoding="utf-8")
            changed = args.copy(); changed[changed.index("--vertex-layout") + 1] = str(wrong); cases.append((changed, "runtime"))
            for mutator, expected in (
                (lambda layout: layout["vertex_inputs"]["3"].update(fetch_constant=1), "runtime"),
                (lambda layout: layout["uniforms"]["0"][0].__setitem__(0, float("nan")), "finite"),
                (lambda layout: layout["uniforms"]["0"].extend([[0, 0, 0, 0]] * 252), "256|register|budget"),
            ):
                layout = json.loads((FIXTURES / "gpu_probe_vs_layout.json").read_text())
                mutator(layout)
                path = pathlib.Path(directory) / f"bad_{len(cases)}.json"
                path.write_text(json.dumps(layout), encoding="utf-8")
                altered = args.copy(); altered[altered.index("--vertex-layout") + 1] = str(path); cases.append((altered, expected))
            for altered, message in cases:
                with self.subTest(message=message):
                    result = self.run_cli(altered)
                    self.assertNotEqual(result.returncode, 0)
                    self.assertRegex(result.stderr.lower(), message.lower())

    def test_rejects_register_field_and_upload_budget_overflow_from_compiled_pair(self):
        pair = gpu_probe.compile_pair(
            FIXTURES / "transform_vs.hlsl", FIXTURES / "texture_ps.hlsl",
            FIXTURES / "gpu_probe_vs_layout.json", FIXTURES / "texture_runtime_layout.json", SHADERC)
        metadata = dict(pair.pixel.lowered.metadata)
        metadata["sq_program_cntl_register_field"] = 64
        with self.assertRaisesRegex(gpu_probe.ProbeError, "range 0\.\.63"):
            gpu_probe.validate_pair(pair._replace(pixel=pair.pixel._replace(
                lowered=replace(pair.pixel.lowered, metadata=metadata))))

        oversized = replace(pair.vertex.lowered, binary=b"\0" * (1100 * 4))
        with self.assertRaisesRegex(gpu_probe.ProbeError, "upload budget.*maximum is 1024"):
            gpu_probe._build_outputs(pair._replace(
                vertex=pair.vertex._replace(lowered=oversized)))


if __name__ == "__main__":
    unittest.main()
