# Minimal X3SP v1 fixture

Format-only synthetic fixture, never executable GPU microcode. Size: 668 bytes.

| Region | Offset | Length | Notes |
| --- | ---: | ---: | --- |
| Header | 0 | 64 | `X3SP`, version 1, target 8, directory at 64 |
| Directory | 64 | 128 | Eight required sections, kinds 1 through 8 |
| Pair | 192 | 48 | Asset string `(656,7)`, empty variant `(0,0)`, zero tool digest |
| Stages | 240 | 320 | VS record at 240, PS record at 400 |
| Microcode | 560 | 24 | 6 LE dwords representing compiler BE words |
| Interfaces | 584 | 72 | VS position output and PS color output |
| Resources/constants | 656 | 0 | Empty sections, all at common boundary |
| Strings | 656 | 11 | `fixtureVSPS`; byte 667 is zero alignment padding |

Stage word bytes are written LE. Their digest preimages re-encode the logical
words into compiler BE order:

| Stage | Logical words | Digest preimage hex | SHA-256 |
| --- | --- | --- | --- |
| VS | `12345678 01020304 A0B0C0D0` | `1234567801020304a0b0c0d0` | `1951763138c86ef2059da0f8cfdd380310c4f0ae4d89a88e6bb163a013a749bb` |
| PS | `87654321 04030201 0D0C0B0A` | `87654321040302010d0c0b0a` | `87fba55c4bf7c7ef9b8e681b2a2a6c3946967469f31997ed644db73c0b3e34b9` |

The artifact digest is SHA-256 over all 668 bytes with bytes 32..63 treated as
zero, yielding
`955313d63bf7a11b633805695ad5fcd19c5ca21b4f7d139fa6b4548be25d0cdd`.
Digests were calculated independently with Python `hashlib`; the fixture
materializer validates the literal and defaults to verification.

The fixture is a wire-format example only. Hashes establish integrity, not
authenticity, shader safety, or complete ISA validation.
