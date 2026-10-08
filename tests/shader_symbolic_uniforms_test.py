"""Real-shaderc coverage for symbolic uniform register layouts."""

import copy
import pathlib
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools" / "shader-probe"))
sys.path.insert(0, str(ROOT / "tools"))

import shader_probe  # noqa: E402
from lower_xenos import LoweringError, lower_spirv  # noqa: E402


SHADERC = pathlib.Path(
    r"C:\dev\helworks\helengine-wiiu\builder\bin\Debug\net9.0-windows"
    r"\runtimes\win-x64\native\shaderc_shared.dll"
)
FIXTURES = ROOT / "tests" / "shaders" / "fixtures"


class SymbolicUniformTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.library, _path = shader_probe.load_shaderc(SHADERC)

    def compile(self, filename, stage):
        return shader_probe.compile_hlsl(
            self.library, FIXTURES / filename, stage, "main"
        )

    def test_transform_symbolic_layout_matches_value_lowering_and_metadata(self):
        import json

        spirv = self.compile("transform_vs.hlsl", "vertex")
        value_layout = json.loads(
            (FIXTURES / "xenos_runtime_layout.json").read_text(encoding="utf-8")
        )
        symbolic_layout = copy.deepcopy(value_layout)
        symbolic_layout["uniforms"] = {"0": {"register_count": 5}}

        value_result = lower_spirv(spirv, "vertex", "main", value_layout)
        symbolic_result = lower_spirv(spirv, "vertex", "main", symbolic_layout)
        self.assertEqual(symbolic_result.assembly, value_result.assembly)
        self.assertEqual(symbolic_result.binary, value_result.binary)

        value_metadata = copy.deepcopy(value_result.metadata)
        symbolic_metadata = copy.deepcopy(symbolic_result.metadata)
        value_uniform = value_metadata.pop("uniforms")["0"]
        symbolic_uniform = symbolic_metadata.pop("uniforms")["0"]
        self.assertEqual(symbolic_metadata, value_metadata)
        self.assertEqual(symbolic_uniform, {
            "descriptor_set": 0,
            "binding": 0,
            "raw_memory_order": "consecutive_uploaded_vec4_registers",
            "register_count": 5,
            "members": value_uniform["members"],
        })
        self.assertNotIn("register_values", symbolic_uniform)
        self.assertEqual(symbolic_uniform["members"], [
            {"offset_bytes": 0, "registers": [0, 1, 2, 3],
             "major": "row_major", "matrix_stride_bytes": 16},
            {"offset_bytes": 64, "registers": [4]},
        ])

    def test_pixel_color_symbolic_layout_matches_value_lowering(self):
        spirv = self.compile("uniform_color_ps.hlsl", "pixel")
        value_layout = {"uniforms": {"0": [[.2, .4, .6, 1.0]]}}
        symbolic_layout = {"uniforms": {"0": {"register_count": 1}}}
        value_result = lower_spirv(spirv, "pixel", "main", value_layout)
        symbolic_result = lower_spirv(spirv, "pixel", "main", symbolic_layout)
        self.assertEqual(symbolic_result.assembly, value_result.assembly)
        self.assertEqual(symbolic_result.binary, value_result.binary)
        self.assertEqual(symbolic_result.metadata["uniforms"]["0"]["register_count"], 1)
        self.assertNotIn("register_values", symbolic_result.metadata["uniforms"]["0"])

    def test_uniform_free_shader_needs_no_uniform_mapping(self):
        with tempfile.TemporaryDirectory() as directory:
            source = pathlib.Path(directory) / "uniform_free.hlsl"
            source.write_text(
                "struct I { [[vk::location(0)]] float4 color : COLOR0; };\n"
                "float4 main(I input) : SV_Target0 { return input.color; }\n",
                encoding="utf-8",
            )
            spirv = shader_probe.compile_hlsl(self.library, source, "pixel", "main")
        result = lower_spirv(
            spirv, "pixel", "main", {"varyings": {"0": 0}},
        )
        self.assertEqual(result.metadata["uniforms"], {})

    def test_matrix_and_tint_registers_must_fit_declared_capacity(self):
        spirv = self.compile("transform_vs.hlsl", "vertex")
        base = {
            "vertex_inputs": {
                "0": {"fetch_constant": 0, "stride": 12, "offset": 0,
                      "format": "32_32_32_FLOAT"},
                "2": {"fetch_constant": 0, "stride": 12, "offset": 6,
                      "format": "32_32_FLOAT"},
                "3": {"fetch_constant": 0, "stride": 12, "offset": 8,
                      "format": "32_32_32_32_FLOAT"},
            },
            "varyings": {"0": 0, "1": 1},
        }
        for capacity, missing in ((3, "c3"), (4, "c4")):
            layout = {**base, "uniforms": {"0": {"register_count": capacity}}}
            with self.subTest(capacity=capacity), self.assertRaisesRegex(
                    LoweringError, rf"capacity {capacity}.*{missing}"):
                lower_spirv(spirv, "vertex", "main", layout)

    def test_symbolic_count_validation_and_ambiguous_forms(self):
        spirv = self.compile("uniform_color_ps.hlsl", "pixel")
        invalid = (
            (0, "between 1 and 256"),
            (-1, "between 1 and 256"),
            (True, "integer"),
            (1.5, "integer"),
            ("1", "integer"),
            (257, "between 1 and 256"),
            ({"register_count": 1, "registers": []}, "cannot combine"),
            ({"register_count": 1, "values": []}, "cannot combine"),
        )
        for spec, message in invalid:
            if isinstance(spec, dict):
                entry = spec
            else:
                entry = {"register_count": spec}
            with self.subTest(spec=spec), self.assertRaisesRegex(
                    LoweringError, message):
                lower_spirv(spirv, "pixel", "main", {"uniforms": {"0": entry}})

    def test_capacity_256_is_allowed_and_257_is_rejected(self):
        spirv = self.compile("uniform_color_ps.hlsl", "pixel")
        result = lower_spirv(
            spirv, "pixel", "main", {"uniforms": {"0": {"register_count": 256}}}
        )
        self.assertEqual(result.metadata["uniforms"]["0"]["register_count"], 256)
        with self.assertRaisesRegex(LoweringError, "between 1 and 256"):
            lower_spirv(
                spirv, "pixel", "main",
                {"uniforms": {"0": {"register_count": 257}}},
            )

    def test_legacy_malformed_and_nonfinite_values_still_fail(self):
        spirv = self.compile("uniform_color_ps.hlsl", "pixel")
        invalid = (
            ([], "no value"),
            ([[1, 2, 3]], "four floats"),
            ([[1, 2, float("inf"), 4]], "finite floats"),
        )
        for values, message in invalid:
            with self.subTest(values=values), self.assertRaisesRegex(
                    LoweringError, message):
                lower_spirv(spirv, "pixel", "main", {"uniforms": {"0": values}})


if __name__ == "__main__":
    unittest.main()
