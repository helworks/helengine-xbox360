"""Real-shaderc tests for explicit physical Xenos vertex layouts."""

import base64
import hashlib
import pathlib
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools" / "shader-probe"))
from backend_protocol import ProtocolError, compile_request
from shader_backend_protocol_test import SHADERC, request, run_protocol

SHARED_SOURCE = pathlib.Path(
    r"C:\dev\helworks\helengine\engine\helengine.editor"
    r"\shaders\builtin\SpriteBatchShader.hlsl"
)


class VertexLayoutProtocolTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.shared_source = SHARED_SOURCE.read_text(encoding="utf-8")

    def _compile_vertex(self, source, *, profile=None):
        changes = {"entrypoint": "VS", "variant": "Textured"}
        if profile is not None:
            changes["vertex_layout"] = profile
        code, result, stderr = run_protocol(request(source, "vertex", **changes))
        self.assertEqual(code, 0, stderr + str(result))
        self.assertTrue(result["success"], result["diagnostics"])
        return result

    def test_shared_textured_vertex_uses_explicit_batch_layout_with_both_optimization_modes(self):
        expected_inputs = [(0, "float4"), (1, "float4"), (2, "float4")]
        expected_offsets = {"0": 0, "1": 4, "2": 8}
        for optimize in (False, True):
            with self.subTest(optimize=optimize):
                options = {
                    "debug_info": False,
                    "optimize": optimize,
                    "warnings_as_errors": False,
                }
                code, result, stderr = run_protocol(request(
                    self.shared_source,
                    "vertex",
                    source_name=str(SHARED_SOURCE),
                    entrypoint="VS",
                    variant="Textured",
                    defines={},
                    options=options,
                    vertex_layout="batch2d-v1",
                ))
                self.assertEqual(code, 0, stderr + str(result))
                self.assertTrue(result["success"], result["diagnostics"])
                self.assertEqual(result["metadata"]["vertex_layout"], "batch2d-v1")
                self.assertEqual(
                    [(item["location"], item["format"])
                     for item in result["reflection"]["inputs"]],
                    expected_inputs,
                )
                self.assertEqual(set(result["metadata"]["inputs"]), {"0", "1", "2"})
                for location, item in result["metadata"]["inputs"].items():
                    self.assertEqual(item["fetch_constant"], 0)
                    self.assertEqual(item["stride"], 24)
                    self.assertEqual(item["offset"], expected_offsets[location])
                    self.assertEqual(item["format"], "32_32_32_32_FLOAT")
                camera = next(
                    item for item in result["reflection"]["bindings"]
                    if item["kind"] == "constant_buffer"
                )
                self.assertEqual(camera["name"], "BatchCameraBuffer")
                self.assertEqual(camera["members"][0]["name"], "projection")

    def test_shared_textured_vertex_defaults_to_mesh_and_rejects_float4_position(self):
        code, result, stderr = run_protocol(request(
            self.shared_source,
            "vertex",
            source_name=str(SHARED_SOURCE),
            entrypoint="VS",
            variant="Textured",
            defines={},
        ))
        self.assertNotEqual(code, 0, stderr + str(result))
        self.assertFalse(result["success"])
        self.assertNotIn("binary_base64", result)
        self.assertRegex(result["diagnostics"], r"vertex input location 0 must be float3, got float4")

    def test_invalid_profiles_fail_request_validation_before_shaderc_load(self):
        invalid_profiles = (
            (None, "vertex_layout must be a string"),
            (True, "vertex_layout must be a string"),
            (17, "vertex_layout must be a string"),
            ("batch2d-v2", "unsupported vertex_layout"),
        )
        for profile, expected in invalid_profiles:
            with self.subTest(profile=profile):
                value = request("", "vertex", vertex_layout=profile)
                with self.assertRaisesRegex(ProtocolError, expected):
                    compile_request(value, ROOT / "missing-shaderc.dll")

    def test_sparse_batch_inputs_keep_physical_offsets_and_full_stride(self):
        source = (
            "struct Input {\n"
            " [[vk::location(1)]] float4 texLocal : TEXCOORD0;\n"
            " [[vk::location(4)]] float4 borderColor : COLOR1;\n"
            "};\n"
            "struct Output { float4 position : SV_Position; };\n"
            "Output VS(Input input) { Output output;\n"
            " output.position = input.texLocal + input.borderColor;\n"
            " return output; }\n"
        )
        result = self._compile_vertex(source, profile="batch2d-v1")
        self.assertEqual(set(result["metadata"]["inputs"]), {"1", "4"})
        self.assertEqual(
            {location: (item["offset"], item["stride"])
             for location, item in result["metadata"]["inputs"].items()},
            {"1": (4, 24), "4": (16, 24)},
        )

    def test_batch_profile_rejects_wrong_width_and_out_of_profile_location(self):
        cases = (
            (
                "struct Input { [[vk::location(0)]] float3 value : POSITION0; };\n"
                "float4 main(Input input) : SV_Position { return float4(input.value, 1); }\n",
                r"vertex input location 0 must be float4, got float3",
            ),
            (
                "struct Input { [[vk::location(6)]] float4 value : TEXCOORD6; };\n"
                "float4 main(Input input) : SV_Position { return input.value; }\n",
                r"vertex input location 6 is outside the published ABI",
            ),
        )
        for source, diagnostic in cases:
            with self.subTest(diagnostic=diagnostic):
                code, result, stderr = run_protocol(request(
                    source, "vertex", entrypoint="main", vertex_layout="batch2d-v1"
                ))
                self.assertNotEqual(code, 0, stderr + str(result))
                self.assertFalse(result["success"])
                self.assertRegex(result["diagnostics"], diagnostic)

    def test_sparse_mesh_fixture_keeps_legacy_offsets_stride_and_binary_hash(self):
        source = (ROOT / "tests" / "shaders" / "fixtures" / "transform_vs.hlsl").read_text(
            encoding="utf-8"
        )
        code, result, stderr = run_protocol(request(source, "vertex"))
        self.assertEqual(code, 0, stderr + str(result))
        self.assertTrue(result["success"], result["diagnostics"])
        binary = base64.b64decode(result["binary_base64"], validate=True)
        self.assertEqual(
            hashlib.sha256(binary).hexdigest().upper(),
            "40C01033D5B097352583DE0F004FF1D37D5640C456BCAADF1941D9B7CEC284EF",
        )
        self.assertEqual(result["metadata"]["vertex_layout"], "mesh3d-v1")
        self.assertEqual(
            {location: (item["offset"], item["stride"])
             for location, item in result["metadata"]["inputs"].items()},
            {"0": (0, 12), "2": (6, 12), "3": (8, 12)},
        )

    def test_compile_identity_tracks_profile_for_pixel_and_normalizes_mesh_default(self):
        source = (ROOT / "tests" / "shaders" / "fixtures" / "texture_ps.hlsl").read_text(
            encoding="utf-8"
        )
        omitted = request(source, "pixel")
        explicit_mesh = request(source, "pixel", vertex_layout="mesh3d-v1")
        batch = request(source, "pixel", vertex_layout="batch2d-v1")
        results = []
        for value in (omitted, explicit_mesh, batch):
            code, result, stderr = run_protocol(value)
            self.assertEqual(code, 0, stderr + str(result))
            self.assertTrue(result["success"], result["diagnostics"])
            results.append(result)
        self.assertEqual(results[0]["compile_identity"], results[1]["compile_identity"])
        self.assertNotEqual(results[0]["compile_identity"], results[2]["compile_identity"])
        self.assertEqual(
            [item["binary_base64"] for item in results],
            [results[0]["binary_base64"]] * 3,
        )
        self.assertEqual(
            [item["metadata"]["vertex_layout"] for item in results],
            ["mesh3d-v1", "mesh3d-v1", "batch2d-v1"],
        )
        self.assertEqual(
            [item["reflection"]["bindings"] for item in results],
            [item["reflection"]["bindings"] for item in results[:1]] * 3,
        )


if __name__ == "__main__":
    unittest.main()
