#!/usr/bin/env python3
"""Compile one authored HLSL entrypoint to SPIR-V using an installed Shaderc DLL."""

import argparse
import ctypes
from dataclasses import dataclass
import hashlib
import os
from pathlib import Path
import re
import sys
from collections.abc import Mapping


PROBE_VERSION = "0.1.0"
STAGES = {"vertex": 0, "pixel": 1}
SHADERC_ENV_VERSION_VULKAN_1_0 = 1 << 22
MAX_INCLUDE_DEPTH = 64


class _ShadercIncludeResult(ctypes.Structure):
    _fields_ = [
        ("source_name", ctypes.c_void_p),
        ("source_name_length", ctypes.c_size_t),
        ("content", ctypes.c_void_p),
        ("content_length", ctypes.c_size_t),
        ("user_data", ctypes.c_void_p),
    ]


@dataclass(frozen=True)
class IncludedSource:
    """Identity of one resolved include payload passed to shaderc."""

    source_name: str
    sha256: str


@dataclass(frozen=True)
class ShaderCompileResult:
    """SPIR-V and diagnostics from one in-memory shaderc compilation."""

    spirv: bytes | None
    diagnostics: str
    included_sources: tuple[IncludedSource, ...]

    @property
    def succeeded(self):
        return self.spirv is not None


class _IncludeCallbackState:
    def __init__(self, resolver):
        self.resolver = resolver
        self.results = {}
        self.sources = []
        self.errors = []

    def allocate(self, source_name, content):
        source_buffer = ctypes.create_string_buffer(source_name)
        content_buffer = ctypes.create_string_buffer(content)
        result = _ShadercIncludeResult(
            ctypes.cast(source_buffer, ctypes.c_void_p), len(source_name),
            ctypes.cast(content_buffer, ctypes.c_void_p), len(content), None,
        )
        pointer = ctypes.pointer(result)
        self.results[ctypes.addressof(result)] = (result, source_buffer, content_buffer)
        return pointer

    def release(self, address):
        if address:
            self.results.pop(address, None)

    def resolve(self, requested_bytes, requesting_bytes, depth):
        requested = requested_bytes.decode("utf-8", errors="replace")
        requesting = requesting_bytes.decode("utf-8", errors="replace")
        try:
            if depth > MAX_INCLUDE_DEPTH:
                raise ValueError(
                    f"include depth limit ({MAX_INCLUDE_DEPTH}) exceeded for {requested!r}"
                )
            if self.resolver is None:
                raise ValueError(f"no include resolver configured for {requested!r}")
            resolved = self.resolver(requesting, requested)
            if resolved is None:
                raise ValueError(f"include not found: {requested!r}")
            if not isinstance(resolved, (tuple, list)) or len(resolved) != 2:
                raise TypeError("include resolver must return (resolved_source_name, source_bytes)")
            resolved_name, content = resolved
            if not isinstance(resolved_name, str):
                raise TypeError("resolved include source name must be str")
            if not isinstance(content, bytes):
                raise TypeError("resolved include source must be bytes")
            if "\0" in resolved_name or b"\0" in content:
                raise ValueError("NUL byte in resolved include source name or payload")
            name_bytes = resolved_name.encode("utf-8")
            if b"\0" in name_bytes:
                raise ValueError("NUL byte in resolved include source name")
            self.sources.append(IncludedSource(resolved_name, hashlib.sha256(content).hexdigest()))
            return self.allocate(name_bytes, content)
        except BaseException as error:
            message = f"Include resolution failed for {requested!r} from {requesting!r}: {type(error).__name__}: {error}"
            self.errors.append(message)
            error_name = b"shader-probe-include-error"
            error_content = message.encode("utf-8", errors="replace")
            return self.allocate(error_name, error_content)


def load_shaderc(library_path):
    path = Path(library_path)
    if not path.is_file():
        raise OSError(f"Shaderc library does not exist: {path}")
    try:
        library = ctypes.WinDLL(str(path)) if os.name == "nt" else ctypes.CDLL(str(path))
    except OSError as error:
        raise OSError(f"Could not load Shaderc native library '{path}': {error}") from error

    pointer = ctypes.c_void_p
    library.shaderc_compiler_initialize.argtypes = []
    library.shaderc_compiler_initialize.restype = pointer
    library.shaderc_compiler_release.argtypes = [pointer]
    library.shaderc_compiler_release.restype = None
    library.shaderc_compile_options_initialize.argtypes = []
    library.shaderc_compile_options_initialize.restype = pointer
    library.shaderc_compile_options_release.argtypes = [pointer]
    library.shaderc_compile_options_release.restype = None
    library.shaderc_compile_options_set_source_language.argtypes = [pointer, ctypes.c_int]
    library.shaderc_compile_options_set_source_language.restype = None
    library.shaderc_compile_options_set_target_env.argtypes = [pointer, ctypes.c_int, ctypes.c_uint]
    library.shaderc_compile_options_set_target_env.restype = None
    library.shaderc_compile_options_add_macro_definition.argtypes = [
        pointer, ctypes.c_char_p, ctypes.c_size_t, ctypes.c_char_p, ctypes.c_size_t,
    ]
    library.shaderc_compile_options_add_macro_definition.restype = None
    library.shaderc_compile_options_set_include_callbacks.argtypes = [
        pointer, ctypes.c_void_p, ctypes.c_void_p, pointer,
    ]
    library.shaderc_compile_options_set_include_callbacks.restype = None
    library.shaderc_compile_options_set_generate_debug_info.argtypes = [pointer]
    library.shaderc_compile_options_set_generate_debug_info.restype = None
    library.shaderc_compile_options_set_optimization_level.argtypes = [pointer, ctypes.c_int]
    library.shaderc_compile_options_set_optimization_level.restype = None
    library.shaderc_compile_options_set_warnings_as_errors.argtypes = [pointer]
    library.shaderc_compile_options_set_warnings_as_errors.restype = None
    library.shaderc_compile_into_spv.argtypes = [
        pointer,
        ctypes.c_char_p,
        ctypes.c_size_t,
        ctypes.c_int,
        ctypes.c_char_p,
        ctypes.c_char_p,
        pointer,
    ]
    library.shaderc_compile_into_spv.restype = pointer
    library.shaderc_result_get_compilation_status.argtypes = [pointer]
    library.shaderc_result_get_compilation_status.restype = ctypes.c_int
    library.shaderc_result_get_error_message.argtypes = [pointer]
    library.shaderc_result_get_error_message.restype = ctypes.c_char_p
    library.shaderc_result_get_length.argtypes = [pointer]
    library.shaderc_result_get_length.restype = ctypes.c_size_t
    library.shaderc_result_get_bytes.argtypes = [pointer]
    library.shaderc_result_get_bytes.restype = pointer
    library.shaderc_result_release.argtypes = [pointer]
    library.shaderc_result_release.restype = None
    return library, path


def compile_hlsl(library, source_path, stage, entrypoint):
    source = source_path.read_bytes()
    result = compile_hlsl_from_source(
        library, source, str(source_path), stage, entrypoint
    )
    if not result.succeeded:
        raise ValueError(result.diagnostics or "Shaderc compilation failed")
    return result.spirv


def compile_hlsl_from_source(
    library,
    source,
    source_name,
    stage,
    entrypoint,
    defines=None,
    include_resolver=None,
    *,
    debug_info=False,
    optimize=False,
    warnings_as_errors=False,
    preserve_reflection_names=False,
):
    """Compile HLSL bytes with shaderc preprocessing and caller-owned includes.

    The resolver is called as ``resolver(requesting_source, include_name)`` and
    returns ``(resolved_source_name, source_bytes)``. No files are read here.
    ``preserve_reflection_names`` requests internal debug metadata independently
    of the caller's ``debug_info`` choice.
    """
    if not isinstance(source, bytes):
        raise TypeError("source must be bytes")
    if not isinstance(source_name, str) or not isinstance(entrypoint, str):
        raise TypeError("source_name and entrypoint must be str")
    if b"\0" in source or "\0" in source_name or "\0" in entrypoint:
        raise ValueError("NUL byte in source, source name, or entrypoint")
    if stage not in STAGES:
        raise ValueError(f"unsupported stage {stage!r}; supported stages: vertex, pixel")
    source_name_bytes = source_name.encode("utf-8")
    entrypoint_bytes = entrypoint.encode("utf-8")
    if defines is None:
        defines = {}
    if not isinstance(defines, Mapping):
        raise TypeError("defines must be a mapping of macro names to string values")
    macro_definitions = []
    for name, value in defines.items():
        if isinstance(name, str) and "\0" in name:
            raise ValueError(f"NUL byte in shader define name {name!r}")
        if not isinstance(name, str) or not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", name):
            raise ValueError(f"invalid shader define name: {name!r}")
        if not isinstance(value, str):
            raise TypeError(f"shader define {name!r} value must be str")
        if "\0" in name or "\0" in value:
            raise ValueError(f"NUL byte in shader define {name!r}")
        macro_definitions.append((name.encode("utf-8"), value.encode("utf-8")))

    include_state = _IncludeCallbackState(include_resolver)
    resolver_callback_type = ctypes.CFUNCTYPE(
        ctypes.c_void_p, ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int,
        ctypes.c_char_p, ctypes.c_size_t,
    )
    releaser_callback_type = ctypes.CFUNCTYPE(
        None, ctypes.c_void_p, ctypes.c_void_p,
    )

    def resolve_callback(user_data, requested, include_type, requesting, depth):
        try:
            result_pointer = include_state.resolve(requested or b"", requesting or b"", depth)
            return ctypes.cast(result_pointer, ctypes.c_void_p).value
        except BaseException as error:
            include_state.errors.append(f"Include callback failed: {type(error).__name__}: {error}")
            result_pointer = include_state.allocate(
                b"shader-probe-include-error", str(error).encode("utf-8", errors="replace")
            )
            return ctypes.cast(result_pointer, ctypes.c_void_p).value

    def release_callback(user_data, result_pointer):
        try:
            include_state.release(result_pointer)
        except BaseException:
            pass

    resolver_callback = resolver_callback_type(resolve_callback)
    releaser_callback = releaser_callback_type(release_callback)
    compiler = library.shaderc_compiler_initialize()
    if not compiler:
        raise RuntimeError("Shaderc could not initialize a compiler")

    options = None
    result = None
    try:
        options = library.shaderc_compile_options_initialize()
        if not options:
            raise RuntimeError("Shaderc could not initialize compile options")
        # shaderc_source_language_hlsl = 1; shaderc_target_env_vulkan = 0.
        library.shaderc_compile_options_set_source_language(options, 1)
        library.shaderc_compile_options_set_target_env(
            options, 0, SHADERC_ENV_VERSION_VULKAN_1_0
        )
        for macro_name, macro_value in macro_definitions:
            library.shaderc_compile_options_add_macro_definition(
                options, macro_name, len(macro_name), macro_value, len(macro_value)
            )
        library.shaderc_compile_options_set_include_callbacks(
            options, resolver_callback, releaser_callback, None
        )
        if debug_info or preserve_reflection_names:
            library.shaderc_compile_options_set_generate_debug_info(options)
        if optimize:
            # shaderc_optimization_level_performance = 2.
            library.shaderc_compile_options_set_optimization_level(options, 2)
        if warnings_as_errors:
            library.shaderc_compile_options_set_warnings_as_errors(options)
        result = library.shaderc_compile_into_spv(
            compiler,
            source,
            len(source),
            STAGES[stage],
            source_name_bytes,
            entrypoint_bytes,
            options,
        )
        if not result:
            raise RuntimeError("Shaderc returned no compilation result")
        message = library.shaderc_result_get_error_message(result)
        diagnostics = message.decode("utf-8", errors="replace") if message else ""
        if library.shaderc_result_get_compilation_status(result) != 0:
            if not diagnostics:
                diagnostics = "Shaderc compilation failed"
            if include_state.errors:
                diagnostics += "\n" + "\n".join(include_state.errors)
            return ShaderCompileResult(None, diagnostics, tuple(dict.fromkeys(include_state.sources)))
        size = library.shaderc_result_get_length(result)
        data = ctypes.string_at(library.shaderc_result_get_bytes(result), size)
        if include_state.errors:
            diagnostics = "\n".join(filter(None, (diagnostics, *include_state.errors)))
        return ShaderCompileResult(data, diagnostics, tuple(dict.fromkeys(include_state.sources)))
    finally:
        if result:
            library.shaderc_result_release(result)
        if options:
            library.shaderc_compile_options_release(options)
        library.shaderc_compiler_release(compiler)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", nargs="?", help="authored HLSL source file")
    parser.add_argument("--stage", help="shader stage: vertex or pixel")
    parser.add_argument("--entry", help="HLSL entrypoint name")
    parser.add_argument("--output", help="destination SPIR-V file")
    parser.add_argument(
        "--shaderc",
        default=os.environ.get("SHADERC_SHARED_LIBRARY"),
        help="path to shaderc_shared.dll (or set SHADERC_SHARED_LIBRARY)",
    )
    parser.add_argument("--version", action="store_true", help="show binding version and native DLL hash")
    args = parser.parse_args(argv)

    if not args.shaderc:
        parser.error("--shaderc or SHADERC_SHARED_LIBRARY is required")

    if args.version:
        try:
            library, library_path = load_shaderc(args.shaderc)
            digest = hashlib.sha256(library_path.read_bytes()).hexdigest()
        except OSError as error:
            print(f"shader-probe: {error}", file=sys.stderr)
            return 1
        print(f"shader-probe {PROBE_VERSION}; shaderc native SHA-256 {digest}")
        return 0

    if not args.source or not args.stage or not args.entry or not args.output:
        parser.error("source, --stage, --entry, and --output are required for compilation")

    source_path = Path(args.source)
    output_path = Path(args.output)
    try:
        shaderc_path = Path(args.shaderc)
        resolved_output = output_path.resolve()
        if resolved_output in {source_path.resolve(), shaderc_path.resolve()}:
            print(
                "shader-probe: output path must differ from the source and Shaderc library",
                file=sys.stderr,
            )
            return 1

        # An explicit output denotes the result of this invocation. Remove any
        # previous artifact before attempting work so failures cannot leave it
        # looking like the current source compiled successfully. This happens
        # before loading or hashing the dependency so setup failures invalidate
        # stale output too.
        output_path.unlink(missing_ok=True)
        if args.stage not in STAGES:
            parser.error(f"unsupported stage {args.stage!r}; supported stages: vertex, pixel")

        library, library_path = load_shaderc(shaderc_path)
        digest = hashlib.sha256(library_path.read_bytes()).hexdigest()
        try:
            binary = compile_hlsl(library, source_path, args.stage, args.entry)
        except ValueError as error:
            print(str(error), file=sys.stderr, end="" if str(error).endswith("\n") else "\n")
            return 1

        output_path.parent.mkdir(parents=True, exist_ok=True)
        try:
            output_path.write_bytes(binary)
        except OSError:
            output_path.unlink(missing_ok=True)
            raise
        spirv_version = int.from_bytes(binary[4:8], "little")
        spirv_major = (spirv_version >> 16) & 0xFF
        spirv_minor = (spirv_version >> 8) & 0xFF
        print(
            f"shader-probe {PROBE_VERSION}; shaderc native SHA-256 {digest}; "
            f"target Vulkan 1.0 (env 0x{SHADERC_ENV_VERSION_VULKAN_1_0:08X}); "
            f"SPIR-V {spirv_major}.{spirv_minor}"
        )
        print(f"Wrote {output_path} ({len(binary)} bytes)")
        return 0
    except (OSError, RuntimeError) as error:
        print(f"shader-probe: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
