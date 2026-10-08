import os
import hashlib
from pathlib import Path
import sys
import unittest


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools" / "shader-probe"))

import shader_probe


DEFAULT_SHADERC = Path(
    r"C:\dev\helworks\helengine-wiiu\builder\bin\Debug\net9.0-windows"
    r"\runtimes\win-x64\native\shaderc_shared.dll"
)


class ShaderRequestFrontendTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        path = Path(os.environ.get("SHADERC_SHARED_LIBRARY", DEFAULT_SHADERC))
        cls.library, _ = shader_probe.load_shaderc(path)

    def compile_source(self, source, **kwargs):
        return shader_probe.compile_hlsl_from_source(
            self.library, source, "virtual/main.hlsl", "vertex", "chosen", **kwargs
        )

    def test_entrypoint_and_define_value_change_real_spirv(self):
        source = b"""float4 chosen() : SV_Position {
    return float4(FACTOR, 0.0, 0.0, 1.0);
}
float4 unused() : SV_Position { return 0; }
"""

        low = self.compile_source(source, defines={"FACTOR": "0.25"})
        high = self.compile_source(source, defines={"FACTOR": "0.75"})

        self.assertTrue(low.succeeded, low.diagnostics)
        self.assertTrue(high.succeeded, high.diagnostics)
        self.assertNotEqual(low.spirv, high.spirv)
        self.assertEqual(low.included_sources, ())

    def test_default_in_memory_output_matches_existing_transform_fixture(self):
        fixtures = (
            ("transform_vs.hlsl", "vertex"),
            ("texture_ps.hlsl", "pixel"),
        )
        for filename, stage in fixtures:
            with self.subTest(filename=filename):
                fixture = ROOT / "tests" / "shaders" / "fixtures" / filename
                original_api = shader_probe.compile_hlsl(
                    self.library, fixture, stage, "main"
                )
                in_memory = shader_probe.compile_hlsl_from_source(
                    self.library, fixture.read_bytes(), str(fixture), stage, "main"
                )

                self.assertTrue(in_memory.succeeded, in_memory.diagnostics)
                self.assertEqual(in_memory.spirv, original_api)

    def test_nested_include_uses_resolved_requester_and_reports_identities(self):
        source = b'#include "dir/one.hlsl"\nfloat4 chosen() : SV_Position { return included(); }\n'
        included = {
            ("virtual/main.hlsl", "dir/one.hlsl"): (
                "virtual/dir/one.hlsl",
                b'#include "../two.hlsl"\nfloat4 included() { return nested_value(); }\n',
            ),
            ("virtual/dir/one.hlsl", "../two.hlsl"): (
                "virtual/two.hlsl",
                b"float4 nested_value() { return float4(0.2, 0.3, 0.4, 1); }\n",
            ),
        }
        requests = []

        def resolve(requesting_source, include_name):
            requests.append((requesting_source, include_name))
            return included[(requesting_source, include_name)]

        first = self.compile_source(source, include_resolver=resolve)
        included[("virtual/dir/one.hlsl", "../two.hlsl")] = (
            "virtual/two.hlsl",
            b"float4 nested_value() { return float4(0.8, 0.3, 0.4, 1); }\n",
        )
        second = self.compile_source(source, include_resolver=resolve)

        self.assertTrue(first.succeeded, first.diagnostics)
        self.assertTrue(second.succeeded, second.diagnostics)
        self.assertEqual(
            first.included_sources,
            (
                shader_probe.IncludedSource(
                    "virtual/dir/one.hlsl",
                    hashlib.sha256(included[("virtual/main.hlsl", "dir/one.hlsl")][1]).hexdigest(),
                ),
                shader_probe.IncludedSource(
                    "virtual/two.hlsl",
                    hashlib.sha256(b"float4 nested_value() { return float4(0.2, 0.3, 0.4, 1); }\n").hexdigest(),
                ),
            ),
        )
        self.assertIn(("virtual/dir/one.hlsl", "../two.hlsl"), requests)
        self.assertNotEqual(first.spirv, second.spirv)

    def test_inactive_include_does_not_call_resolver(self):
        source = b"""#if 0
#include "inactive.hlsl"
#endif
float4 chosen() : SV_Position { return float4(0, 0, 0, 1); }
"""
        requests = []

        result = self.compile_source(
            source,
            include_resolver=lambda requesting, name: requests.append((requesting, name)),
        )

        self.assertTrue(result.succeeded, result.diagnostics)
        self.assertEqual(requests, [])

    def test_missing_include_and_resolver_exception_are_compile_diagnostics(self):
        source = b'#include "missing.hlsl"\nfloat4 chosen() : SV_Position { return 0; }\n'
        missing = self.compile_source(source, include_resolver=lambda requester, name: None)
        failed = self.compile_source(
            source,
            include_resolver=lambda requester, name: (_ for _ in ()).throw(
                RuntimeError("resolver backend offline")
            ),
        )

        self.assertFalse(missing.succeeded)
        self.assertIn("missing.hlsl", missing.diagnostics)
        self.assertFalse(failed.succeeded)
        self.assertIn("resolver backend offline", failed.diagnostics)
        self.assertNotIn("Traceback", failed.diagnostics)

    def test_recursive_includes_fail_at_a_bounded_depth(self):
        source = b'#include "loop.hlsl"\nfloat4 chosen() : SV_Position { return loop_value(); }\n'

        def resolve(requester, name):
            return "virtual/loop.hlsl", b'#include "loop.hlsl"\nfloat4 loop_value() { return 0; }\n'

        result = self.compile_source(source, include_resolver=resolve)

        self.assertFalse(result.succeeded)
        self.assertIn("depth", result.diagnostics.lower())

    def test_embedded_nul_is_rejected_in_all_native_string_payloads(self):
        cases = (
            (b"float4 chosen() : SV_Position { return 0; }\0", "virtual/main.hlsl", "chosen", {}, None),
            (b"float4 chosen() : SV_Position { return 0; }", "virtual\0main.hlsl", "chosen", {}, None),
            (b"float4 chosen() : SV_Position { return 0; }", "virtual/main.hlsl", "chosen\0x", {}, None),
            (b"float4 chosen() : SV_Position { return FACTOR; }", "virtual/main.hlsl", "chosen", {"BAD\0NAME": "1"}, None),
            (b"float4 chosen() : SV_Position { return FACTOR; }", "virtual/main.hlsl", "chosen", {"FACTOR": "1\0x"}, None),
        )
        for source, name, entry, defines, resolver in cases:
            with self.subTest(source=source, name=name, entry=entry, defines=defines):
                with self.assertRaisesRegex(ValueError, "NUL"):
                    shader_probe.compile_hlsl_from_source(
                        self.library, source, name, "vertex", entry,
                        defines=defines, include_resolver=resolver,
                    )

        for response in (
            ("virtual/include.hlsl\0bad", b"float4 helper() { return 0; }"),
            ("virtual/include.hlsl", b"float4 helper() { return 0; }\0"),
        ):
            with self.subTest(response=response):
                result = self.compile_source(
                    b'#include "include.hlsl"\nfloat4 chosen() : SV_Position { return helper(); }\n',
                    include_resolver=lambda requester, name, response=response: response,
                )
                self.assertFalse(result.succeeded)
                self.assertIn("NUL", result.diagnostics)

    def test_callback_state_is_not_reused_after_success_or_failure(self):
        with_include = b'#include "value.hlsl"\nfloat4 chosen() : SV_Position { return helper(); }\n'
        success = self.compile_source(
            with_include,
            include_resolver=lambda requester, name: ("virtual/value.hlsl", b"float4 helper() { return 1; }"),
        )
        failure = self.compile_source(with_include, include_resolver=lambda requester, name: None)
        plain = self.compile_source(b"float4 chosen() : SV_Position { return 1; }\n")

        self.assertTrue(success.succeeded, success.diagnostics)
        self.assertFalse(failure.succeeded)
        self.assertTrue(plain.succeeded, plain.diagnostics)
        self.assertEqual(plain.included_sources, ())

    def test_debug_optimization_and_warnings_options_have_observable_effects(self):
        source = b"float4 chosen() : SV_Position { float x = 0.25; return float4(x, 0, 0, 1); }\n"
        default = self.compile_source(source)
        debug = self.compile_source(source, debug_info=True)
        optimized = self.compile_source(source, optimize=True)
        warning_error = self.compile_source(
            b"#warning request warning\nfloat4 chosen() : SV_Position { return 0; }\n",
            warnings_as_errors=True,
        )

        for result in (default, debug, optimized):
            self.assertTrue(result.succeeded, result.diagnostics)
        self.assertNotEqual(default.spirv, debug.spirv)
        self.assertNotEqual(default.spirv, optimized.spirv)
        self.assertFalse(warning_error.succeeded)
        self.assertIn("warning", warning_error.diagnostics.lower())

    def test_warnings_as_errors_binding_matches_native_one_argument_signature(self):
        setter = self.library.shaderc_compile_options_set_warnings_as_errors

        self.assertEqual(setter.argtypes, [shader_probe.ctypes.c_void_p])


if __name__ == "__main__":
    unittest.main()
