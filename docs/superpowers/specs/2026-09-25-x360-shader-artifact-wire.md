# X3SP v1 wire layout

Status: approved with the implementation plan; normative appendix to
`2026-09-25-x360-shader-artifact-design.md`. Offsets are bytes; scalar fields
are u32 little-endian unless stated otherwise. Native struct/enum layout is
never a file contract.

## Envelope and directory

Use the design's 64-byte header, section_count=8. Directory at64..191 contains
eight16-byte records `(kind,offset,length,count)` in the following order:

| Kind | Section | Record bytes | Count |
| --- | --- | --- | --- |
| 1 | Pair | 48 | 1 |
| 2 | Stages | 160 | 2 |
| 3 | Microcode | 4 | 6..2048; per-stage count divisible by3 and <=1024 |
| 4 | Interfaces | 36 | 2..24 |
| 5 | Resources | 40 | 0..4 |
| 6 | Declared constants | 24 | 0..512 |
| 7 | Used constants | 24 | 0..512 |
| 8 | UTF-8 bytes | 1 | 1..262144 |

Length=count*record size, checked without overflow. First section offset192;
each following offset=align4(previous end). Zero-length tables may share that
boundary. Padding must be zero. File size=align4(final end), at most1048576.
No unknown/duplicate/optional sections. Reserved fields must be zero.

Serialized references are absolute file byte offsets, not indexes/pointers.
Record ranges use `(offset,count)`. Empty range=(0,0); a nonempty range starts
on a record boundary in the correct section. Tables are fully partitioned in
VS/PS order; interfaces additionally inputs then outputs per stage. Reject
gaps, overlaps, aliased ownership and unused records. Public accessors may take
section-relative indexes; those indexes are not serialized references.

## Enum values and optional fields

- Stage:1=vertex,2=pixel; records must appear in that order.
- Profile:1=mesh3d-v1,2=batch2d-v1, independently mapped from C# enums.
- Interface kind:1=location,2=position. Width1..4 means float through float4.
- Fetch format:0=none,2=32_32_FLOAT,3=32_32_32_FLOAT,4=32_32_32_32_FLOAT.
- Resource:1=constant buffer,2=Texture2D,3=sampler.
- Member:1=float,2=float2,3=float3,4=float4,5=float4x4.
- Matrix major:0=none,1=row_major,2=column_major.
- Optional varying slot,fetch constant,resource fetch slot,bank:0xffffffff=absent.
  Zero is a real value, not an absent alias. Other values obey field domains.
- Logical sets/slots and physical sets/bindings:0..0x7fffffff. Texture/sampler
  physical sets equal their logical sets. Physical bindings retain raw unshifted
  values; no request shifts are serialized or reconstructed by guessing.

## Record fields

Pair48: asset string reference0,variant reference8,tool identity32-byte digest16.
String references are `(absolute byte offset,byte length)`,8 bytes.

Stage160:

| Offset | Field |
| --- | --- |
| 0,4,8 | stage,profile,register count |
| 12,16 | microcode offset,word count |
| 20,24 | input-record offset,count |
| 28,32 | output-record offset,count |
| 36,40 | resource-record offset,count |
| 44,48,52 | cbuffer byte size,vec4 capacity,physical bank |
| 56,60 | declaration-record offset,count |
| 64,68 | used-access-record offset,count |
| 72 | program-name string reference |
| 80 | entrypoint string reference |
| 88 | compile identity,32 digest bytes |
| 120 | compiler bytecode SHA-256,32 digest bytes |
| 152,156 | reserved zero |

Interface36: kind0,location4,width8,varying slot12,fetch constant16,
stride-in-dwords20,offset-in-dwords24,fetch format28,reserved32.
VS inputs: location,slot absent,fetch fields exactly selected profile.
VS location outputs/PS inputs: slot=location0..7,fetch absent,stride/offset/
format0. VS position: location0,width4,slot/fetch absent,other fetch fields0.
PS output: location0,width4,slot/fetch absent,other fetch fields0. Sort each
range by `(kind,location)`; reject duplicates. Width determines logical format.

Resource40: type0,name reference4,logical set12,logical slot16,physical set20,
physical binding24,fetch slot28,bank32,reserved36. Sort by `(type,set,slot)`.
VS <=1 cbuffer; PS <=1 cbuffer and either one texture plus one sampler or neither.
Names unique within each stage. Cbuffer: logical/physical set0,physical binding0,
fetch absent,bank0(VS)/256(PS). Texture/sampler: fetch1,bank absent.

Declaration24: name reference0,type8,offset-in-buffer12,size16,reserved20.
Names unique per stage; order by offset then UTF-8 bytewise name. Offsets must
be multiples of16; sizes4/8/12/16/64 for types1..5. Disjoint ranges inside the
buffer. Maximum256 declarations per stage.

Used access24: absolute declaration-record offset0,first vec4 register4,
register count8,major12,matrix stride16,reserved20. Declaration must belong to
same stage and cannot be referenced twice. Sort by declaration offset.
First register=member offset/16; count4 for matrix,1 otherwise; range inside
capacity. Matrix major1/2,stride16; nonmatrix major0,stride0. Declarations carry
no orientation, so unused matrices do not acquire invented orientation.

No cbuffer: size/capacity0,bank absent,no cbuffer resource/declarations/accesses.
Present: size16..4096 multiple16,capacity=size/16,bank matches stage/resource.
Empty declaration/access lists are allowed, matching the host factory.

## Strings, ordering and identities

Strict UTF-8, no NUL/overlong encoding/surrogates/out-of-range scalar. Nonempty
references <=4096 bytes and inside section8. Empty `(0,0)` only for variant.
No normalization or case folding. Asset ID is opaque nonempty caller text.
Intern identical complete byte strings by first occurrence, never substrings.

Traversal: pair asset,variant; then VS and PS, each program name,entrypoint,
sorted resource names,sorted declaration names. Used accesses reference
declarations instead of repeating names. Append new strings without internal
padding. Reader validates canonical first occurrences, exact pool coverage and
whole-span reuse. Maximum string references=522 (pair2 + stages4 + resources4
+ declarations512); bounded scans suffice. Digest fields are raw bytes, not
string references. Zero opaque identity digests are legal; content digests must
still match recomputation. Source path is not serialized.

## Semantic checks and derived values

VS exactly one position output plus0..8 varyings; PS exactly one color output
and0..8 inputs. All PS inputs match VS output location,width,slot. Extra VS
outputs allowed. Profiles may differ; both must be known. Tool/variant stored
once per pair. Mesh/batch fetch layouts and resource limits follow the design.

Register count1..64; encoded field=count-1. Highest slot=max VS user-output
location or0. Interpolator count=highest+1 if any user output,else0.
Control=`(vs_count-1)|((ps_count-1)<<8)|(highest<<20)`.
Program upload=VSwords+PSwords+6; constants=sum(capacity*4+2 for nonempty
buffers); state10; total=sum. These derived values are NOT serialized; expose
them in decoded views. They do not include fetch state/draws/rest of frame.

Stage digest hashes logical words re-encoded big-endian. Artifact digest hashes
all bytes with header32..63 virtually zero, without mutating input. Neither is
authenticity, sandboxing or a complete ISA verifier.

## Hand-derived minimal fixture recipe

Format-only synthetic fixture; NEVER execute its words on a GPU.
Directory `(kind,offset,length,count)`:
`(1,192,48,1) (2,240,320,2) (3,560,24,6) (4,584,72,2)`
`(5,656,0,0) (6,656,0,0) (7,656,0,0) (8,656,11,11)`.
Total668; padding at667=0. Asset(656,7),empty variant,tool digest32 zero bytes.
String bytes at656 are ASCII `fixtureVSPS`.

VS at240: stage1,profile1,registers1,words(560,3),empty inputs,outputs(584,1),
empty resources,cbsize/capacity0,bank0xffffffff,empty declarations/accesses,
program/entry(663,2),compile digest32 bytes0x11.
PS at400: stage2,profile1,registers1,words(572,3),empty inputs,outputs(620,1),
same absent fields,program/entry(665,2),compile digest32 bytes0x22.
Reserved0. VS interface at584: kind2,location0,width4; PS at620: kind1,
location0,width4. Both slot/fetch absent; stride/offset/format0.

VS logical hex words=`12345678 01020304 A0B0C0D0`;
PS=`87654321 04030201 0D0C0B0A`.
First word at560 is file bytes`78 56 34 12`. Total668 at12 is`9C 02 00 00`.
VS code offset560 at252 is`30 02 00 00`. Control0,interpolators0,program12,
constants0,state10,total22.

Task1 authors literal hex plus a text manifest from this recipe, resolves
digests using a standard SHA-256 primitive independent of the production writer,
and materializes the checked-in binary. Both C# and C consume that same fixture;
normal tests never regenerate expected bytes from the writer under test.
