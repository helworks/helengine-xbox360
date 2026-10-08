"""Explicit, versioned physical vertex-fetch layouts supported by the host."""

from dataclasses import dataclass
from types import MappingProxyType


DEFAULT_VERTEX_LAYOUT = "mesh3d-v1"


@dataclass(frozen=True)
class VertexAttribute:
    format: str
    offset: int
    hardware_format: str


@dataclass(frozen=True)
class VertexLayoutProfile:
    name: str
    stride: int
    inputs: object


PROFILES = MappingProxyType({
    "mesh3d-v1": VertexLayoutProfile(
        "mesh3d-v1",
        12,
        MappingProxyType({
            0: VertexAttribute("float3", 0, "32_32_32_FLOAT"),
            1: VertexAttribute("float3", 3, "32_32_32_FLOAT"),
            2: VertexAttribute("float2", 6, "32_32_FLOAT"),
            3: VertexAttribute("float4", 8, "32_32_32_32_FLOAT"),
        }),
    ),
    "batch2d-v1": VertexLayoutProfile(
        "batch2d-v1",
        24,
        MappingProxyType({
            location: VertexAttribute("float4", location * 4, "32_32_32_32_FLOAT")
            for location in range(6)
        }),
    ),
})


def normalize_vertex_layout(value=DEFAULT_VERTEX_LAYOUT):
    """Return a supported profile ID, rejecting implicit type coercion."""
    if type(value) is not str:
        raise ValueError("vertex_layout must be a string")
    if value not in PROFILES:
        raise ValueError(
            f"unsupported vertex_layout {value!r}; expected one of {sorted(PROFILES)}"
        )
    return value


def profile_for(value=DEFAULT_VERTEX_LAYOUT):
    return PROFILES[normalize_vertex_layout(value)]


def profiles_abi():
    """Return canonical JSON-compatible layout schema for tool identity."""
    return {
        name: {
            "stride": profile.stride,
            "inputs": {
                str(location): [item.format, item.offset, item.hardware_format]
                for location, item in profile.inputs.items()
            },
        }
        for name, profile in PROFILES.items()
    }
