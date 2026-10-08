import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
CLI = ROOT / "tools" / "shader-probe" / "shader_probe.py"
DEFAULT_SHADERC = Path(
    r"C:\dev\helworks\helengine-wiiu\builder\bin\Debug\net9.0-windows"
    r"\runtimes\win-x64\native\shaderc_shared.dll"
)


class ShaderFrontendTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.directory = Path(self.temp.name)
        self.shaderc = Path(os.environ.get("SHADERC_SHARED_LIBRARY", DEFAULT_SHADERC))

    def tearDown(self):
        self.temp.cleanup()

    def source(self, name, text):
        path = self.directory / name
        path.write_text(text, encoding="utf-8", newline="\n")
        return path

    def compile(
        self,
        source,
        stage,
        entry="main",
        output_name="out.spv",
        output_path=None,
        shaderc_path=None,
    ):
        output = Path(output_path) if output_path is not None else self.directory / output_name
        result = subprocess.run(
            [
                sys.executable,
                str(CLI),
                str(source),
                "--stage",
                stage,
                "--entry",
                entry,
                "--output",
                str(output),
                "--shaderc",
                str(shaderc_path or self.shaderc),
            ],
            capture_output=True,
            text=True,
            encoding="utf-8",
        )
        return result, output

    def assert_spirv(self, output):
        data = output.read_bytes()
        self.assertGreater(len(data), 20)
        self.assertEqual(data[:4], b"\x03\x02\x23\x07")
        self.assertEqual(data[4:8], b"\x00\x00\x01\x00")
        self.assertEqual(len(data) % 4, 0)
        return data

    def test_compiles_vertex_transform_from_authored_hlsl(self):
        source = self.source(
            "transform.hlsl",
            """struct VSIn { float3 position : POSITION; };
struct VSOut { float4 position : SV_Position; };
VSOut main(VSIn input) {
    VSOut output;
    output.position = float4(input.position.x + 0.25, input.position.yz, 1.0);
    return output;
}
""",
        )

        result, output = self.compile(source, "vertex")

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assert_spirv(output)

    def test_compiles_pixel_shader_with_color_output(self):
        source = self.source(
            "color.hlsl",
            "float4 main() : SV_Target0 { return float4(0.2, 0.4, 0.8, 1.0); }\n",
        )

        result, output = self.compile(source, "pixel")

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assert_spirv(output)
        self.assertIn("shader-probe 0.1.0", result.stdout)
        self.assertIn("target Vulkan 1.0 (env 0x00400000)", result.stdout)
        self.assertIn(hashlib.sha256(self.shaderc.read_bytes()).hexdigest(), result.stdout)

    def test_source_edit_changes_spirv(self):
        source = self.source(
            "editable.hlsl",
            "float4 main() : SV_Target0 { return float4(1, 0, 0, 1); }\n",
        )
        first, first_output = self.compile(source, "pixel", output_name="first.spv")
        self.assertEqual(first.returncode, 0, first.stderr)
        first_words = self.assert_spirv(first_output)

        source.write_text(
            "float4 main() : SV_Target0 { return float4(0, 1, 0, 1); }\n",
            encoding="utf-8",
            newline="\n",
        )
        second, second_output = self.compile(source, "pixel", output_name="second.spv")

        self.assertEqual(second.returncode, 0, second.stderr)
        self.assertNotEqual(first_words, self.assert_spirv(second_output))

    def test_invalid_hlsl_returns_compiler_diagnostics(self):
        source = self.source("invalid.hlsl", "float4 main( { definitely not HLSL\n")

        result, _ = self.compile(source, "pixel")

        self.assertNotEqual(result.returncode, 0)
        self.assertIn("error", result.stderr.lower())
        self.assertIn("invalid.hlsl", result.stderr)

    def test_unsupported_stage_is_rejected(self):
        source = self.source(
            "stage.hlsl", "float4 main() : SV_Target0 { return 1; }\n"
        )

        result, output = self.compile(source, "compute")

        self.assertNotEqual(result.returncode, 0)
        self.assertIn("unsupported stage", result.stderr.lower())
        self.assertFalse(output.exists())

    def test_utf8_source_is_compiled_using_its_byte_length(self):
        source = self.source(
            "utf8.hlsl",
            "// comentário UTF-8: ação, coração, π, 雪 ☃\n"
            "float4 main() : SV_Target0 { return float4(0.3, 0.6, 0.9, 1); }\n",
        )

        result, output = self.compile(source, "pixel")

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assert_spirv(output)

    def test_version_reports_probe_version_and_native_hash(self):
        preserved_output = self.directory / "version-output.spv"
        preserved_output.write_bytes(b"leave this output alone")
        result = subprocess.run(
            [
                sys.executable,
                str(CLI),
                "--version",
                "--shaderc",
                str(self.shaderc),
                "--output",
                str(preserved_output),
            ],
            capture_output=True,
            text=True,
            encoding="utf-8",
        )

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("shader-probe 0.1.0", result.stdout)
        self.assertNotIn("Silk.NET.Shaderc", result.stdout)
        self.assertIn(hashlib.sha256(self.shaderc.read_bytes()).hexdigest(), result.stdout)
        self.assertEqual(preserved_output.read_bytes(), b"leave this output alone")

    def test_failed_compile_removes_existing_output(self):
        source = self.source("invalid-existing-output.hlsl", "float4 main( { broken\n")
        output = self.directory / "stale.spv"
        output.write_bytes(b"previous successful SPIR-V")

        result, _ = self.compile(source, "pixel", output_path=output)

        self.assertNotEqual(result.returncode, 0)
        self.assertIn("error", result.stderr.lower())
        self.assertFalse(output.exists())

    def test_output_cannot_overwrite_source_or_shaderc_library(self):
        source = self.source(
            "protected.hlsl", "float4 main() : SV_Target0 { return 1; }\n"
        )
        source_before = source.read_bytes()
        source_result, _ = self.compile(source, "pixel", output_path=source)
        self.assertNotEqual(source_result.returncode, 0)
        self.assertEqual(source.read_bytes(), source_before)

        protected_library = self.directory / "shaderc_shared.dll"
        shutil.copyfile(self.shaderc, protected_library)
        library_before = protected_library.read_bytes()
        library_result, _ = self.compile(
            source,
            "pixel",
            output_path=protected_library,
            shaderc_path=protected_library,
        )
        self.assertNotEqual(library_result.returncode, 0)
        self.assertIn("output path must differ", library_result.stderr.lower())
        self.assertEqual(protected_library.read_bytes(), library_before)

    def test_missing_shaderc_removes_stale_output(self):
        source = self.source(
            "missing-library.hlsl", "float4 main() : SV_Target0 { return 1; }\n"
        )
        output = self.directory / "stale-on-missing-library.spv"
        output.write_bytes(b"old SPIR-V")
        missing_library = self.directory / "not-installed.dll"

        result, _ = self.compile(
            source, "pixel", output_path=output, shaderc_path=missing_library
        )

        self.assertNotEqual(result.returncode, 0)
        self.assertIn("shaderc library does not exist", result.stderr.lower())
        self.assertFalse(output.exists())

    def test_source_collision_is_rejected_before_missing_library_setup(self):
        source = self.source(
            "source-before-missing-library.hlsl",
            "float4 main() : SV_Target0 { return 1; }\n",
        )
        original = source.read_bytes()
        missing_library = self.directory / "not-installed.dll"

        result, _ = self.compile(
            source, "pixel", output_path=source, shaderc_path=missing_library
        )

        self.assertNotEqual(result.returncode, 0)
        self.assertIn("output path must differ", result.stderr.lower())
        self.assertEqual(source.read_bytes(), original)

    def test_missing_library_path_is_protected_as_output_too(self):
        source = self.source(
            "library-path-collision.hlsl",
            "float4 main() : SV_Target0 { return 1; }\n",
        )
        missing_library = self.directory / "missing-but-protected.dll"

        result, _ = self.compile(
            source,
            "pixel",
            output_path=missing_library,
            shaderc_path=missing_library,
        )

        self.assertNotEqual(result.returncode, 0)
        self.assertIn("output path must differ", result.stderr.lower())
        self.assertFalse(missing_library.exists())

    def test_invalid_native_library_clears_output_and_names_library(self):
        source = self.source(
            "invalid-library.hlsl", "float4 main() : SV_Target0 { return 1; }\n"
        )
        invalid_library = self.directory / "invalid-shaderc.dll"
        invalid_library.write_bytes(b"not a native library")
        output = self.directory / "old-output.spv"
        output.write_bytes(b"old SPIR-V")

        result, _ = self.compile(
            source, "pixel", output_path=output, shaderc_path=invalid_library
        )

        self.assertNotEqual(result.returncode, 0)
        self.assertIn("could not load shaderc native library", result.stderr.lower())
        self.assertIn(str(invalid_library).lower(), result.stderr.lower())
        self.assertFalse(output.exists())


if __name__ == "__main__":
    unittest.main()
