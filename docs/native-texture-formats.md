# Native Xbox 360 textures

Per-platform texture settings use `Xbox360.Linear.*` identifiers. Textures and
font atlases share this cook contract. New selections default to
`Xbox360.Linear.8_8_8_8`, eight-bit alpha and an 8192-pixel resolution cap. A cap
of zero skips import resizing; each resulting axis still must fit within 8192.
Existing generic engine format settings remain valid.

`2_10_10_10` and its expanded alias expose `A2` alpha precision, retaining all
four native alpha levels (0, 85, 170 and 255). This policy belongs to these native
formats; generic engine textures retain their existing supported policies.

`8_A` stores the source alpha and samples white RGB with that coverage, matching
the documented use of this code for text. `8_B` retains scalar luminance sampling
in this contract; it is not an implicit blue-channel extraction.

The shared catalog records all 64 Xenos format codes. Cooking availability and
compatibility with the pinned Xenia backend are separate properties. The native
codec implements 44 codes:

| Codes | Storage families |
| --- | --- |
| 2–12, 14–17 | Eight-bit channels, packed color, shared-component pairs and aliases |
| 18–20 | DXT1, DXT2/3 and DXT4/5 |
| 22–32 | Depth, 16-bit channels, expand and half-float variants |
| 36–38 | IEEE 32-bit floating-point channels |
| 49–56 | DXN and variants interpreted as 16-bit components |
| 58–61 | DXT3A, DXT5A, CTX1 and DXT3A interpreted as four one-bit components |

The remaining entries retain explicit reasons for unavailable cooking:
0/1 are one-bit formats without verified sampling semantics; 13/21/62/63 are
exclusive EDRAM formats; 33–35 are unverified integer formats; 39–48 are
unverified packed/video/interlaced formats; 57 is an unverified three-component
32-bit float format. Unsupported choices fail rather than cook a different
format under the requested name. Known aliases 9 and 14 have native encoders,
but the pinned Xenia D3D12 backend has no loader for these exact codes.

## Payload and upload

`TextureAssetColorFormat.Xbox360Native` has serialized value 14. `Colors`
contains a 56-byte `X3T1` version-one header followed by GPU texels. Header words
and texels use little-endian storage even when their enclosing asset container
uses the target's big-endian serialization. There is no palette payload.

The header contains fourteen 32-bit words, in order: magic `0x31543358`, version,
hardware format code, flags, real width, real height, storage width in texels,
storage height, row pitch in bytes, texel byte length, channel swizzle, component
signs, number format and exponent adjustment. Version one has zero flags and
uses the catalog's canonical fetch controls. Parsers check dimensions, controls,
pitch and exact payload length before allocating GPU storage.

Storage is linear 2D with one level. Rows contain whole blocks, are aligned to
32 blocks and occupy a multiple of 256 bytes. Final block rows repeat edge
texels; authored dimensions remain unchanged. Fetch sizes use the real bounds,
so sprite and mesh UVs remain normalized to the authored image. Native upload
copies GPU texels directly into physical memory and retains existing retirement
after the GPU fence completes.

DXT2/3 and DXT4/5 each share one hardware code. Version one cooks straight-alpha
BC2/BC3 data; it does not expose separate premultiplied DXT2/4 choices. Tiled
storage, mip chains and cube textures are outside this payload version.

## Hardware reference and validation scope

The reference is the repository's pinned
[Xenia Canary revision](https://github.com/xenia-canary/xenia-canary/tree/f21ebd49e979e44f081f474df78c3fbfee9cb3f2),
specifically `src/xenia/gpu/xenos.h`, `texture_info_formats.inl`,
`texture_util.cc`, texture load shaders and the D3D12 texture format table.
Tests of byte layouts, managed/native interoperability and cross compilation
do not establish visual support for every format in an emulator or on hardware.
