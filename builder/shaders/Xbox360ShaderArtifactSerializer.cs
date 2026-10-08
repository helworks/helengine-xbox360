using System.Security.Cryptography;
using System.Text;
using helengine;

namespace helengine.x360.builder;

/// <summary>Serializes validated VS/PS snapshots into the deterministic X3SP v1 format.</summary>
public static class Xbox360ShaderArtifactSerializer {
    static readonly UTF8Encoding StrictUtf8 = new(false, true);

    public static byte[] Serialize(Xbox360ShaderArtifact artifact) {
        ArgumentNullException.ThrowIfNull(artifact);
        ValidateText(artifact.AssetId, "asset ID", allowEmpty: false);
        Xbox360ShaderProgramPair pair = artifact.Programs
            ?? throw new ArgumentException("A validated shader program pair is required.", nameof(artifact));
        Xbox360ShaderProgram vertex = pair.Vertex;
        Xbox360ShaderProgram pixel = pair.Pixel;
        if (vertex == null || pixel == null
            || vertex.Stage != ShaderStage.Vertex || pixel.Stage != ShaderStage.Pixel
            || vertex.Target != ShaderCompileTarget.Xbox360 || pixel.Target != ShaderCompileTarget.Xbox360) {
            throw new InvalidDataException("Artifact programs must be an Xbox 360 vertex/pixel pair in that order.");
        }
        if (!string.Equals(vertex.Variant, pixel.Variant, StringComparison.Ordinal)) {
            throw new InvalidDataException("Artifact stages must have the same variant.");
        }
        byte[] toolDigest = DecodeDigest(vertex.ToolIdentity, "tool identity");
        if (!toolDigest.AsSpan().SequenceEqual(DecodeDigest(pixel.ToolIdentity, "pixel tool identity"))) {
            throw new InvalidDataException("Artifact stages must have the same tool identity digest.");
        }

        StringPool strings = new();
        StringToken assetToken = strings.Add(artifact.AssetId, "asset ID", allowEmpty: false);
        StringToken variantToken = strings.Add(vertex.Variant, "variant", allowEmpty: true);
        StageDraft[] stages = [new(vertex, strings, isVertex: true), new(pixel, strings, isVertex: false)];

        int interfaceCount = checked(stages[0].Inputs.Length + stages[0].Outputs.Length
            + stages[1].Inputs.Length + stages[1].Outputs.Length);
        int resourceCount = checked(stages[0].Resources.Length + stages[1].Resources.Length);
        int declarationCount = checked(stages[0].Declarations.Length + stages[1].Declarations.Length);
        int usedCount = checked(stages[0].Accesses.Length + stages[1].Accesses.Length);
        int wordCount = checked(stages[0].Program.WordCount + stages[1].Program.WordCount);
        ValidateCounts(stages, interfaceCount, resourceCount, declarationCount, usedCount, wordCount);

        int[] counts = [1, 2, wordCount, interfaceCount, resourceCount, declarationCount, usedCount, strings.Length];
        int[] offsets = new int[Xbox360ShaderArtifactWire.SectionCount];
        int[] lengths = new int[Xbox360ShaderArtifactWire.SectionCount];
        int cursor = Xbox360ShaderArtifactWire.FirstSectionOffset;
        for (int index = 0; index < counts.Length; index++) {
            offsets[index] = cursor;
            lengths[index] = Xbox360ShaderArtifactWire.CheckedLength(counts[index], Xbox360ShaderArtifactWire.RecordSizes[index]);
            cursor = Xbox360ShaderArtifactWire.Align4(checked(cursor + lengths[index]));
        }
        int totalSize = cursor;
        ValidateTotalFileSize(totalSize);
        strings.AssignOffsets(offsets[7]);

        int interfaceIndex = 0;
        int resourceIndex = 0;
        int declarationIndex = 0;
        int accessIndex = 0;
        int wordIndex = 0;
        foreach (StageDraft stage in stages) {
            stage.WordStart = wordIndex;
            wordIndex = checked(wordIndex + stage.Program.WordCount);
            stage.InputStart = interfaceIndex;
            interfaceIndex = checked(interfaceIndex + stage.Inputs.Length);
            stage.OutputStart = interfaceIndex;
            interfaceIndex = checked(interfaceIndex + stage.Outputs.Length);
            stage.ResourceStart = resourceIndex;
            resourceIndex = checked(resourceIndex + stage.Resources.Length);
            stage.DeclarationStart = declarationIndex;
            declarationIndex = checked(declarationIndex + stage.Declarations.Length);
            stage.AccessStart = accessIndex;
            accessIndex = checked(accessIndex + stage.Accesses.Length);
        }

        byte[] output = new byte[totalSize];
        WriteEnvelope(output, totalSize, counts, offsets, lengths);
        WritePair(output, offsets[0], assetToken, variantToken, toolDigest);
        WriteStages(output, offsets, stages);
        WriteMicrocode(output, offsets[2], stages);
        WriteInterfaces(output, offsets[3], stages);
        WriteResources(output, offsets[4], stages);
        WriteDeclarations(output, offsets[5], stages);
        WriteAccesses(output, offsets[6], offsets[5], stages);
        strings.WriteTo(output, offsets[7]);

        byte[] integrity = SHA256.HashData(output);
        integrity.CopyTo(output, 32);
        return output;
    }

    static void ValidateTotalFileSize(int totalSize) {
        if (totalSize < Xbox360ShaderArtifactWire.FirstSectionOffset
            || totalSize > Xbox360ShaderArtifactWire.MaximumFileBytes) {
            throw new InvalidDataException($"Serialized artifact size {totalSize} is outside the X3SP v1 file bounds.");
        }
    }

    static void ValidateCounts(StageDraft[] stages, int interfaces, int resources, int declarations, int used, int words) {
        foreach (StageDraft stage in stages) {
            if (stage.Program.RegisterCount is < 1 or > 64) {
                throw new InvalidDataException("Shader register count must be between 1 and 64.");
            }
            if (stage.Program.WordCount is < 1 or > 1024 || stage.Program.WordCount % 3 != 0) {
                throw new InvalidDataException("Stage microcode must contain 1..1024 words in complete three-word instructions.");
            }
            if (stage.Program.RegisterField != stage.Program.RegisterCount - 1) {
                throw new InvalidDataException("Shader register field must equal register count minus one.");
            }
            if (stage.Resources.Length > 3 || stage.Declarations.Length > 256 || stage.Accesses.Length > 256) {
                throw new InvalidDataException("Stage resource or constant record count exceeds the X3SP v1 limit.");
            }
            if (stage.Program.ConstantBuffer is { } buffer) {
                if (buffer.SizeBytes is < 16 or > 4096 || buffer.SizeBytes % 16 != 0
                    || buffer.RegisterCapacity != buffer.SizeBytes / 16) {
                    throw new InvalidDataException("Constant buffer size and register capacity are inconsistent with X3SP v1.");
                }
                int requiredBank = stage.IsVertex ? 0 : 256;
                if (buffer.PhysicalBankBaseRegister != requiredBank) {
                    throw new InvalidDataException("Vertex and pixel constant banks must be 0 and 256 respectively.");
                }
            } else if (stage.Declarations.Length != 0 || stage.Accesses.Length != 0) {
                throw new InvalidDataException("Constant declarations/accesses require a constant buffer.");
            }
        }
        if (interfaces is < 2 or > 24 || resources > 4 || declarations > 512 || used > 512 || words is < 6 or > 2048) {
            throw new InvalidDataException("Artifact table count is outside the X3SP v1 limits.");
        }
    }

    static void WriteEnvelope(byte[] output, int totalSize, int[] counts, int[] offsets, int[] lengths) {
        output[0] = (byte)'X'; output[1] = (byte)'3'; output[2] = (byte)'S'; output[3] = (byte)'P';
        Xbox360ShaderArtifactWire.WriteUInt32(output, 4, 1);
        Xbox360ShaderArtifactWire.WriteUInt32(output, 8, Xbox360ShaderArtifactWire.HeaderSize);
        Xbox360ShaderArtifactWire.WriteUInt32(output, 12, totalSize);
        Xbox360ShaderArtifactWire.WriteUInt32(output, 16, 8);
        Xbox360ShaderArtifactWire.WriteUInt32(output, 20, Xbox360ShaderArtifactWire.SectionCount);
        Xbox360ShaderArtifactWire.WriteUInt32(output, 24, Xbox360ShaderArtifactWire.DirectoryOffset);
        Xbox360ShaderArtifactWire.WriteUInt32(output, 28, 0);
        for (int section = 0; section < Xbox360ShaderArtifactWire.SectionCount; section++) {
            int entry = Xbox360ShaderArtifactWire.DirectoryOffset + section * Xbox360ShaderArtifactWire.DirectoryEntrySize;
            Xbox360ShaderArtifactWire.WriteUInt32(output, entry, section + 1);
            Xbox360ShaderArtifactWire.WriteUInt32(output, entry + 4, offsets[section]);
            Xbox360ShaderArtifactWire.WriteUInt32(output, entry + 8, lengths[section]);
            Xbox360ShaderArtifactWire.WriteUInt32(output, entry + 12, counts[section]);
        }
    }

    static void WritePair(byte[] output, int offset, StringToken asset, StringToken variant, byte[] toolDigest) {
        WriteStringRef(output, offset, asset);
        WriteStringRef(output, offset + 8, variant);
        toolDigest.CopyTo(output, offset + 16);
    }

    static void WriteStages(byte[] output, int[] sectionOffsets, StageDraft[] stages) {
        for (int index = 0; index < stages.Length; index++) {
            StageDraft stage = stages[index];
            int record = sectionOffsets[1] + index * 160;
            Xbox360ShaderArtifactWire.WriteUInt32(output, record, stage.IsVertex ? 1 : 2);
            Xbox360ShaderArtifactWire.WriteUInt32(output, record + 4, ProfileCode(stage.Program.VertexLayout));
            Xbox360ShaderArtifactWire.WriteUInt32(output, record + 8, stage.Program.RegisterCount);
            Xbox360ShaderArtifactWire.WriteRange(output, record + 12, sectionOffsets[2], stage.WordStart,
                stage.Program.WordCount, 4);
            Xbox360ShaderArtifactWire.WriteRange(output, record + 20, sectionOffsets[3], stage.InputStart,
                stage.Inputs.Length, 36);
            Xbox360ShaderArtifactWire.WriteRange(output, record + 28, sectionOffsets[3], stage.OutputStart,
                stage.Outputs.Length, 36);
            Xbox360ShaderArtifactWire.WriteRange(output, record + 36, sectionOffsets[4], stage.ResourceStart,
                stage.Resources.Length, 40);

            Xbox360ShaderConstantBufferLayout buffer = stage.Program.ConstantBuffer;
            Xbox360ShaderArtifactWire.WriteUInt32(output, record + 44, buffer?.SizeBytes ?? 0);
            Xbox360ShaderArtifactWire.WriteUInt32(output, record + 48, buffer?.RegisterCapacity ?? 0);
            Xbox360ShaderArtifactWire.WriteUInt32(output, record + 52,
                buffer == null ? Xbox360ShaderArtifactWire.Absent : checked((uint)buffer.PhysicalBankBaseRegister));
            Xbox360ShaderArtifactWire.WriteRange(output, record + 56, sectionOffsets[5], stage.DeclarationStart,
                stage.Declarations.Length, 24);
            Xbox360ShaderArtifactWire.WriteRange(output, record + 64, sectionOffsets[6], stage.AccessStart,
                stage.Accesses.Length, 24);
            WriteStringRef(output, record + 72, stage.ProgramName);
            WriteStringRef(output, record + 80, stage.EntryPoint);
            Xbox360ShaderArtifactWire.WriteDigest(output, record + 88, stage.Program.CompileIdentity, "compile identity");
            Xbox360ShaderArtifactWire.WriteDigest(output, record + 120, stage.Program.BytecodeSha256, "bytecode SHA-256");
        }
    }

    static void WriteMicrocode(byte[] output, int sectionOffset, StageDraft[] stages) {
        int index = 0;
        foreach (StageDraft stage in stages) {
            foreach (uint word in stage.Program.MicrocodeWords) {
                Xbox360ShaderArtifactWire.WriteUInt32(output, sectionOffset + checked(index * 4), word);
                index++;
            }
        }
    }

    static void WriteInterfaces(byte[] output, int sectionOffset, StageDraft[] stages) {
        int index = 0;
        foreach (StageDraft stage in stages) {
            foreach (InterfaceDraft item in stage.Inputs) {
                int record = checked(sectionOffset + index * 36);
                Xbox360ShaderArtifactWire.WriteUInt32(output, record, item.Kind);
                Xbox360ShaderArtifactWire.WriteUInt32(output, record + 4, item.Index);
                Xbox360ShaderArtifactWire.WriteUInt32(output, record + 8, item.Width);
                Xbox360ShaderArtifactWire.WriteUInt32(output, record + 12, item.VaryingSlot);
                Xbox360ShaderArtifactWire.WriteUInt32(output, record + 16, item.FetchConstant);
                Xbox360ShaderArtifactWire.WriteUInt32(output, record + 20, item.Stride);
                Xbox360ShaderArtifactWire.WriteUInt32(output, record + 24, item.Offset);
                Xbox360ShaderArtifactWire.WriteUInt32(output, record + 28, item.FetchFormat);
                index++;
            }
            foreach (InterfaceDraft item in stage.Outputs) {
                int record = checked(sectionOffset + index * 36);
                Xbox360ShaderArtifactWire.WriteUInt32(output, record, item.Kind);
                Xbox360ShaderArtifactWire.WriteUInt32(output, record + 4, item.Index);
                Xbox360ShaderArtifactWire.WriteUInt32(output, record + 8, item.Width);
                uint varyingSlot = stage.IsVertex ? item.VaryingSlot : Xbox360ShaderArtifactWire.Absent;
                Xbox360ShaderArtifactWire.WriteUInt32(output, record + 12, varyingSlot);
                Xbox360ShaderArtifactWire.WriteUInt32(output, record + 16, item.FetchConstant);
                Xbox360ShaderArtifactWire.WriteUInt32(output, record + 20, item.Stride);
                Xbox360ShaderArtifactWire.WriteUInt32(output, record + 24, item.Offset);
                Xbox360ShaderArtifactWire.WriteUInt32(output, record + 28, item.FetchFormat);
                index++;
            }
        }
    }

    static void WriteResources(byte[] output, int sectionOffset, StageDraft[] stages) {
        int index = 0;
        foreach (StageDraft stage in stages) {
            foreach (ResourceDraft item in stage.Resources) {
                int record = checked(sectionOffset + index * 40);
                Xbox360ShaderArtifactWire.WriteUInt32(output, record, item.Type);
                WriteStringRef(output, record + 4, item.Name);
                Xbox360ShaderArtifactWire.WriteUInt32(output, record + 12, item.Resource.LogicalSet);
                Xbox360ShaderArtifactWire.WriteUInt32(output, record + 16, item.Resource.LogicalSlot);
                Xbox360ShaderArtifactWire.WriteUInt32(output, record + 20, item.Resource.PhysicalSet);
                Xbox360ShaderArtifactWire.WriteUInt32(output, record + 24, item.Resource.PhysicalBinding);
                Xbox360ShaderArtifactWire.WriteUInt32(output, record + 28, Optional(item.Resource.PhysicalSlot));
            Xbox360ShaderArtifactWire.WriteUInt32(output, record + 32,
                    Optional(item.Resource.PhysicalBankBaseRegister));
                index++;
            }
        }
    }

    static void WriteDeclarations(byte[] output, int sectionOffset, StageDraft[] stages) {
        int index = 0;
        foreach (StageDraft stage in stages) {
            foreach (DeclarationDraft item in stage.Declarations) {
                int record = checked(sectionOffset + index * 24);
                WriteStringRef(output, record, item.Name);
                Xbox360ShaderArtifactWire.WriteUInt32(output, record + 8, item.Type);
                Xbox360ShaderArtifactWire.WriteUInt32(output, record + 12, item.Member.OffsetBytes);
                Xbox360ShaderArtifactWire.WriteUInt32(output, record + 16, item.Member.SizeBytes);
                index++;
            }
        }
    }

    static void WriteAccesses(byte[] output, int sectionOffset, int declarationSectionOffset, StageDraft[] stages) {
        int index = 0;
        foreach (StageDraft stage in stages) {
            foreach (AccessDraft item in stage.Accesses) {
                int record = checked(sectionOffset + index * 24);
                int declarationRecord = checked(declarationSectionOffset
                    + checked(stage.DeclarationStart + item.DeclarationIndex) * 24);
                Xbox360ShaderArtifactWire.WriteUInt32(output, record, declarationRecord);
                Xbox360ShaderArtifactWire.WriteUInt32(output, record + 4, item.FirstRegister);
                Xbox360ShaderArtifactWire.WriteUInt32(output, record + 8, item.RegisterCount);
                Xbox360ShaderArtifactWire.WriteUInt32(output, record + 12, item.MatrixMajor);
                Xbox360ShaderArtifactWire.WriteUInt32(output, record + 16, item.MatrixStride);
                index++;
            }
        }
    }

    static void WriteStringRef(byte[] output, int offset, StringToken token) {
        if (token.Bytes.Length == 0) {
            Xbox360ShaderArtifactWire.WriteUInt32(output, offset, 0);
            Xbox360ShaderArtifactWire.WriteUInt32(output, offset + 4, 0);
            return;
        }
        Xbox360ShaderArtifactWire.WriteUInt32(output, offset, token.AbsoluteOffset);
        Xbox360ShaderArtifactWire.WriteUInt32(output, offset + 4, token.Bytes.Length);
    }

    static uint Optional(int? value) => value.HasValue
        ? checked((uint)value.Value)
        : Xbox360ShaderArtifactWire.Absent;

    static int ProfileCode(Xbox360ShaderVertexLayout profile) => profile switch {
        Xbox360ShaderVertexLayout.Mesh3D => 1,
        Xbox360ShaderVertexLayout.Batch2D => 2,
        _ => throw new InvalidDataException($"Unsupported Xbox 360 shader vertex profile '{profile}'.")
    };

    static uint ResourceCode(ShaderResourceType type) => type switch {
        ShaderResourceType.ConstantBuffer => 1,
        ShaderResourceType.Texture2D => 2,
        ShaderResourceType.Sampler => 3,
        _ => throw new InvalidDataException($"Unsupported X3SP resource type '{type}'.")
    };

    static uint MemberCode(string type) => type switch {
        "float" => 1,
        "float2" => 2,
        "float3" => 3,
        "float4" => 4,
        "float4x4" => 5,
        _ => throw new InvalidDataException($"Unsupported X3SP constant member type '{type}'.")
    };

    static uint MatrixMajorCode(string major) => major switch {
        null => 0,
        "row_major" => 1,
        "column_major" => 2,
        _ => throw new InvalidDataException($"Unsupported matrix major '{major}'.")
    };

    static uint FetchFormatCode(string format) => format switch {
        "32_32_FLOAT" => 2,
        "32_32_32_FLOAT" => 3,
        "32_32_32_32_FLOAT" => 4,
        _ => throw new InvalidDataException($"Unsupported X3SP vertex fetch format '{format}'.")
    };

    static byte[] DecodeDigest(string value, string field) {
        if (value == null || value.Length != 64) {
            throw new InvalidDataException($"{field} must contain exactly 64 hexadecimal characters.");
        }
        try {
            return Convert.FromHexString(value);
        } catch (FormatException exception) {
            throw new InvalidDataException($"{field} is not a valid hexadecimal SHA-256 digest.", exception);
        }
    }

    static byte[] ValidateText(string value, string field, bool allowEmpty) {
        if (value == null) throw new ArgumentException($"{field} cannot be null.", field);
        if (!allowEmpty && value.Length == 0) throw new ArgumentException($"{field} cannot be empty.", field);
        if (value.IndexOf('\0') >= 0) throw new ArgumentException($"{field} cannot contain NUL.", field);
        try {
            byte[] encoded = StrictUtf8.GetBytes(value);
            if (encoded.Length > Xbox360ShaderArtifactWire.MaximumStringBytes) {
                throw new ArgumentException($"{field} exceeds {Xbox360ShaderArtifactWire.MaximumStringBytes} UTF-8 bytes.", field);
            }
            return encoded;
        } catch (EncoderFallbackException exception) {
            throw new ArgumentException($"{field} is not valid Unicode for strict UTF-8 encoding.", field, exception);
        }
    }

    sealed class StageDraft {
        public StageDraft(Xbox360ShaderProgram program, StringPool strings, bool isVertex) {
            Program = program;
            IsVertex = isVertex;
            ProgramName = strings.Add(program.ProgramName, "program name", allowEmpty: false);
            EntryPoint = strings.Add(program.EntryPoint, "entrypoint", allowEmpty: false);
            Inputs = (program.Inputs ?? throw new InvalidDataException("Program inputs are missing."))
                .Select(InterfaceDraft.Create).OrderBy(item => item.Kind).ThenBy(item => item.Index).ToArray();
            Outputs = (program.Outputs ?? throw new InvalidDataException("Program outputs are missing."))
                .Select(InterfaceDraft.Create).OrderBy(item => item.Kind).ThenBy(item => item.Index).ToArray();
            Xbox360ShaderResource[] orderedResources = (program.Resources ?? throw new InvalidDataException("Program resources are missing."))
                .OrderBy(resource => ResourceCode(resource.Type))
                .ThenBy(resource => resource.LogicalSet)
                .ThenBy(resource => resource.LogicalSlot)
                .ToArray();
            Resources = orderedResources
                .Select(resource => new ResourceDraft(resource, strings.Add(resource.Name, "resource name", allowEmpty: false)))
                .ToArray();

            Xbox360ShaderConstantBufferLayout buffer = program.ConstantBuffer;
            Xbox360ShaderDeclaredConstantMember[] orderedDeclarations =
                (buffer?.DeclaredMembers ?? Array.Empty<Xbox360ShaderDeclaredConstantMember>())
                .OrderBy(member => member.OffsetBytes)
                .ThenBy(member => StrictUtf8.GetBytes(member.Name), Utf8ByteComparer.Instance)
                .ToArray();
            Declarations = orderedDeclarations
                .Select(member => new DeclarationDraft(member,
                    strings.Add(member.Name, "constant member name", allowEmpty: false)))
                .ToArray();
            Dictionary<(string Name, int Offset), int> declarationIndices = Declarations
                .Select((item, index) => (item, index)).ToDictionary(pair => (pair.item.Member.Name, pair.item.Member.OffsetBytes), pair => pair.index);
            Accesses = (buffer?.UsedMembers ?? Array.Empty<Xbox360ShaderUsedConstantMember>())
                .Select(used => MakeAccess(used, declarationIndices, Declarations))
                .OrderBy(item => item.DeclarationIndex).ToArray();
        }

        public Xbox360ShaderProgram Program { get; }
        public bool IsVertex { get; }
        public StringToken ProgramName { get; }
        public StringToken EntryPoint { get; }
        public InterfaceDraft[] Inputs { get; }
        public InterfaceDraft[] Outputs { get; }
        public ResourceDraft[] Resources { get; }
        public DeclarationDraft[] Declarations { get; }
        public AccessDraft[] Accesses { get; }
        public int WordStart { get; set; }
        public int InputStart { get; set; }
        public int OutputStart { get; set; }
        public int ResourceStart { get; set; }
        public int DeclarationStart { get; set; }
        public int AccessStart { get; set; }

        static AccessDraft MakeAccess(
            Xbox360ShaderUsedConstantMember used,
            Dictionary<(string Name, int Offset), int> declarations,
            DeclarationDraft[] declarationRows) {
            if (used == null || !declarations.TryGetValue((used.Name, used.OffsetBytes), out int index)) {
                throw new InvalidDataException("Used constant access does not identify one declared member in its stage.");
            }
            DeclarationDraft declaration = declarationRows[index];
            bool matrix = declaration.Member.Type == "float4x4";
            int first = declaration.Member.OffsetBytes / 16;
            int expectedCount = matrix ? 4 : 1;
            if (used.RegisterIndices == null || used.RegisterIndices.Count != expectedCount
                || used.RegisterIndices.Where((register, position) => register != first + position).Any()) {
                throw new InvalidDataException("Used constant registers disagree with the member's declared range.");
            }
            if (matrix && (used.MatrixStrideBytes != 16 || used.MatrixMajor is not ("row_major" or "column_major"))) {
                throw new InvalidDataException("A used matrix must have a 16-byte matrix stride.");
            }
            if (!matrix && (used.MatrixMajor != null || used.MatrixStrideBytes.HasValue)) {
                throw new InvalidDataException("A non-matrix constant access cannot carry matrix decorations.");
            }
            return new AccessDraft(index, first, expectedCount, MatrixMajorCode(matrix ? used.MatrixMajor : null), matrix ? 16 : 0);
        }
    }

    sealed class InterfaceDraft {
        InterfaceDraft(uint kind, int index, int width, uint varyingSlot, uint fetchConstant,
            int stride, int offset, uint fetchFormat) {
            Kind = kind; Index = index; Width = width; VaryingSlot = varyingSlot;
            FetchConstant = fetchConstant; Stride = stride; Offset = offset; FetchFormat = fetchFormat;
        }
        public uint Kind { get; }
        public int Index { get; }
        public int Width { get; }
        public uint VaryingSlot { get; }
        public uint FetchConstant { get; }
        public int Stride { get; }
        public int Offset { get; }
        public uint FetchFormat { get; }

        public static InterfaceDraft Create(Xbox360ShaderInterface value) {
            if (value == null || value.Width is < 1 or > 4 || value.Index < 0) {
                throw new InvalidDataException("Shader interface contains an invalid width or location.");
            }
            uint kind = value.Kind switch {
                Xbox360ShaderInterfaceKind.Location => 1,
                Xbox360ShaderInterfaceKind.Position => 2,
                _ => throw new InvalidDataException($"Unsupported shader interface kind '{value.Kind}'.")
            };
            uint slot = value.PhysicalSlot.HasValue ? checked((uint)value.PhysicalSlot.Value) : Xbox360ShaderArtifactWire.Absent;
            uint fetch = Xbox360ShaderArtifactWire.Absent;
            int stride = 0, offset = 0;
            uint format = 0;
            if (value.VertexFetch != null) {
                fetch = checked((uint)value.VertexFetch.FetchConstant);
                stride = value.VertexFetch.Stride;
                offset = value.VertexFetch.Offset;
                format = FetchFormatCode(value.VertexFetch.HardwareFormat);
            }
            return new InterfaceDraft(kind, value.Index, value.Width, slot, fetch, stride, offset, format);
        }
    }

    sealed class ResourceDraft {
        public ResourceDraft(Xbox360ShaderResource resource, StringToken name) {
            Resource = resource ?? throw new InvalidDataException("Program contains a null resource.");
            Name = name;
            Type = ResourceCode(resource.Type);
            if (resource.LogicalSet < 0 || resource.LogicalSlot < 0 || resource.PhysicalSet < 0 || resource.PhysicalBinding < 0) {
                throw new InvalidDataException("Resource set and binding fields must be nonnegative.");
            }
        }
        public Xbox360ShaderResource Resource { get; }
        public StringToken Name { get; }
        public uint Type { get; }
    }

    sealed class DeclarationDraft {
        public DeclarationDraft(Xbox360ShaderDeclaredConstantMember member, StringToken name) {
            Member = member ?? throw new InvalidDataException("Constant buffer contains a null member.");
            Name = name;
            Type = MemberCode(member.Type);
            if (member.OffsetBytes < 0 || member.SizeBytes <= 0 || member.OffsetBytes % 16 != 0) {
                throw new InvalidDataException("Constant member offset and size are invalid for X3SP v1.");
            }
        }
        public Xbox360ShaderDeclaredConstantMember Member { get; }
        public StringToken Name { get; }
        public uint Type { get; }
    }

    sealed record AccessDraft(int DeclarationIndex, int FirstRegister, int RegisterCount, uint MatrixMajor, int MatrixStride);

    sealed class StringToken(string value, byte[] bytes) {
        public string Value { get; } = value;
        public byte[] Bytes { get; } = bytes;
        public int RelativeOffset { get; set; }
        public int AbsoluteOffset { get; set; }
    }

    sealed class StringPool {
        readonly Dictionary<string, StringToken> byValue = new(StringComparer.Ordinal);
        readonly List<StringToken> ordered = [];
        int byteCount;
        public int Length => byteCount;

        public StringToken Add(string value, string field, bool allowEmpty) {
            byte[] bytes = ValidateText(value, field, allowEmpty);
            if (byValue.TryGetValue(value, out StringToken existing)) return existing;
            int newLength = checked(byteCount + bytes.Length);
            if (newLength > Xbox360ShaderArtifactWire.MaximumStringPoolBytes) {
                throw new InvalidDataException($"UTF-8 string pool exceeds {Xbox360ShaderArtifactWire.MaximumStringPoolBytes} bytes.");
            }
            StringToken token = new(value, bytes) { RelativeOffset = byteCount };
            byteCount = newLength;
            byValue.Add(value, token);
            ordered.Add(token);
            return token;
        }

        public void AssignOffsets(int absoluteStart) {
            foreach (StringToken token in ordered) token.AbsoluteOffset = checked(absoluteStart + token.RelativeOffset);
        }

        public void WriteTo(byte[] output, int absoluteStart) {
            foreach (StringToken token in ordered) token.Bytes.CopyTo(output, checked(absoluteStart + token.RelativeOffset));
        }
    }

    sealed class Utf8ByteComparer : IComparer<byte[]> {
        public static readonly Utf8ByteComparer Instance = new();
        public int Compare(byte[] left, byte[] right) => left.AsSpan().SequenceCompareTo(right);
    }
}
