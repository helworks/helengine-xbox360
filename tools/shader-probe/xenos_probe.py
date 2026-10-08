#!/usr/bin/env python3
"""Compile a narrow HLSL/SPIR-V subset to provisional Xenos microcode."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).parents[1]))

import shader_probe
from lower_xenos import LoweringError, lower_spirv


def _sha256(data):
    return hashlib.sha256(data).hexdigest()


def _output_paths(prefix):
    prefix = Path(prefix)
    return [prefix.with_suffix(suffix) for suffix in (".xasm", ".bin", ".json")]


def _validate_output_paths(input_path, layout_path, shaderc_path, prefix):
    output_paths = _output_paths(prefix)
    protected = [Path(input_path), Path(layout_path), Path(__file__),
                 Path(__file__).with_name("shader_probe.py"),
                 Path(__file__).with_name("spirv_parser.py"),
                 Path(__file__).with_name("lower_xenos.py"),
                 Path(__file__).parents[1] / "xenos_asm.py"]
    if shaderc_path:
        protected.append(Path(shaderc_path))
    resolved_outputs = [path.resolve() for path in output_paths]
    resolved_protected = {path.resolve() for path in protected}
    if len(set(resolved_outputs)) != len(resolved_outputs):
        raise ValueError("output .xasm, .bin, and .json paths must be distinct")
    collision = next((path for path in resolved_outputs if path in resolved_protected), None)
    if collision is not None:
        raise ValueError(f"output path aliases an input or tool: {collision}")
    return output_paths


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", required=True, help="authored .hlsl or precompiled .spv")
    parser.add_argument("--stage", required=True, choices=("vertex", "pixel"))
    parser.add_argument("--entry", required=True, help="SPIR-V/HLSL entry point name")
    parser.add_argument("--layout", required=True, help="explicit JSON input/uniform/varying map")
    parser.add_argument("--output-prefix", required=True, help="writes .xasm, .bin, and .json")
    parser.add_argument(
        "--shaderc", default=os.environ.get("SHADERC_SHARED_LIBRARY")
        or os.environ.get("HEL_X360_SHADERC"),
        help="shaderc_shared DLL for HLSL input (or SHADERC_SHARED_LIBRARY)",
    )
    args = parser.parse_args(argv)

    input_path = Path(args.input)
    layout_path = Path(args.layout)
    try:
        outputs = _validate_output_paths(input_path, layout_path,
                                         args.shaderc or "", args.output_prefix)
    except (OSError, ValueError) as error:
        print(f"xenos-probe: {error}", file=sys.stderr)
        return 1

    for output in outputs:
        output.unlink(missing_ok=True)
    try:
        layout_bytes = layout_path.read_bytes()
        layout = json.loads(layout_bytes.decode("utf-8"))
        if not isinstance(layout, dict):
            raise ValueError("layout JSON root must be an object")
        source_hash = _sha256(input_path.read_bytes())
        compiler_hash = None
        if input_path.suffix.lower() == ".spv":
            spirv = input_path.read_bytes()
            source_route = "spirv"
        elif input_path.suffix.lower() == ".hlsl":
            if not args.shaderc:
                raise ValueError("--shaderc or SHADERC_SHARED_LIBRARY is required for HLSL input")
            library, library_path = shader_probe.load_shaderc(args.shaderc)
            compiler_hash = _sha256(library_path.read_bytes())
            spirv = shader_probe.compile_hlsl(library, input_path, args.stage, args.entry)
            source_route = "hlsl"
        else:
            raise ValueError("--input must name an .hlsl or .spv file")

        lowered = lower_spirv(spirv, args.stage, args.entry, layout)
        metadata = dict(lowered.metadata)
        metadata.update({
            "probe": "xenos-probe",
            "route": source_route,
            "input_sha256": source_hash,
            "source_sha256": source_hash if source_route == "hlsl" else None,
            "spirv_sha256": _sha256(spirv),
            "shaderc_sha256": compiler_hash,
            "layout_sha256": _sha256(layout_bytes),
            "artifact_status": "provisional_probe_output_not_runtime_integrated",
        })

        outputs[0].parent.mkdir(parents=True, exist_ok=True)
        outputs[0].write_text(lowered.assembly, encoding="utf-8", newline="\n")
        outputs[1].write_bytes(lowered.binary)
        outputs[2].write_text(json.dumps(metadata, indent=2, sort_keys=True) + "\n",
                              encoding="utf-8", newline="\n")
    except (OSError, ValueError, RuntimeError, LoweringError) as error:
        for output in outputs:
            output.unlink(missing_ok=True)
        print(f"xenos-probe: {error}", file=sys.stderr)
        return 1

    print(f"xenos-probe: wrote {outputs[0]}, {outputs[1]}, {outputs[2]}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
