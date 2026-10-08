using System.Buffers.Binary;
using System.Globalization;
using System.Security.Cryptography;
using System.Text.Json;
using helengine;

namespace helengine.x360.builder;

public static class Xbox360ShaderProgramFactory {
    const int MaximumMicrocodeWords = 1024;
    const int MaximumConstantBufferBytes = 4096;

    public static Xbox360ShaderProgram FromCompilation(Xbox360ShaderCompileResult compilation) {
        ArgumentNullException.ThrowIfNull(compilation);
        try {
            return new SnapshotBuilder(compilation).Build();
        } catch (OverflowException exception) {
            throw new InvalidDataException("Shader metadata arithmetic overflowed while creating the validated program snapshot.", exception);
        }
    }

    sealed class SnapshotBuilder {
        readonly Xbox360ShaderCompileResult Result;
        readonly ShaderCompileRequest Request;
        readonly ShaderProgramDefinition Definition;
        readonly ShaderCompiledBinary Binary;
        JsonElement Metadata;
        int RegisterCount;
        int RegisterField;

        public SnapshotBuilder(Xbox360ShaderCompileResult result) {
            Result = result;
            Request = result.Request;
            Definition = result.ProgramDefinition;
            Binary = result.Binary;
        }

        public Xbox360ShaderProgram Build() {
            ValidateCompilerResult();
            using JsonDocument document = Xbox360ShaderMetadataReader.Parse(Result.NativeMetadataJson);
            Metadata = document.RootElement;
            ValidateNativeHeader();
            uint[] microcodeWords = ReadMicrocodeWords();
            ValidateRegisterMetadata();

            List<InterfaceDraft> reflectedInputs = ReadInterfaces(Definition.Inputs, "program inputs");
            List<InterfaceDraft> reflectedOutputs = ReadInterfaces(Definition.Outputs, "program outputs");
            ValidateStageInterfaces(reflectedInputs, reflectedOutputs);

            Dictionary<int, NativeInput> nativeInputs = ReadAndValidateNativeInputs(reflectedInputs);
            Dictionary<int, int> varyingSlots = ReadAndValidateVaryingMap(reflectedInputs, reflectedOutputs);
            ValidateNativeOutputs(reflectedOutputs, varyingSlots);

            (List<Xbox360ShaderResource> resources,
                Xbox360ShaderConstantBufferLayout constantBuffer) = ReadResourcesAndConstants();

            List<Xbox360ShaderInterface> inputs = BuildInputSnapshots(reflectedInputs, nativeInputs);
            List<Xbox360ShaderInterface> outputs = BuildOutputSnapshots(reflectedOutputs, varyingSlots);

            return new Xbox360ShaderProgram(
                Request.Source.Path,
                Request.ProgramName,
                Request.EntryPoint,
                Request.Variant,
                Request.Target,
                Request.Stage,
                Result.VertexLayout,
                Result.ToolIdentity,
                Result.CompileIdentity,
                Convert.ToHexString(SHA256.HashData(Binary.Bytecode)),
                microcodeWords,
                RegisterCount,
                RegisterField,
                inputs,
                outputs,
                resources,
                constantBuffer);
        }

        void ValidateCompilerResult() {
            if (!Result.Success) {
                throw Invalid("compilation result", "the compiler result is not successful");
            }
            if (Request == null || Definition == null || Binary == null) {
                throw Invalid("compilation result", "request, program definition, and binary are required");
            }
            try {
                _ = Xbox360ShaderVertexLayouts.ToProtocolValue(Result.VertexLayout);
            } catch (ArgumentOutOfRangeException) {
                throw Invalid("compilation result", "the selected vertex layout is unsupported");
            }
            if (Request.Target != ShaderCompileTarget.Xbox360) {
                throw Invalid("request target", $"'{Request.Target}' is not Xbox360");
            }
            if (Request.Stage is not (ShaderStage.Vertex or ShaderStage.Pixel)) {
                throw Invalid("request stage", $"'{Request.Stage}' is not a supported Xenos graphics stage");
            }
            if (Request.ShaderModel == null || Request.ShaderModel.Major != 4 || Request.ShaderModel.Minor != 0) {
                throw Invalid("request shader model", "only Xbox360 model 4.0 is supported");
            }
            if (string.IsNullOrWhiteSpace(Request.Source?.Path)
                || string.IsNullOrWhiteSpace(Request.ProgramName)
                || string.IsNullOrWhiteSpace(Request.EntryPoint)
                || string.IsNullOrWhiteSpace(Request.Variant)) {
                throw Invalid("request", "source path, program name, entrypoint, and variant must be nonempty");
            }

            if (Definition.Stage != Request.Stage
                || !string.Equals(Definition.Name, Request.ProgramName, StringComparison.Ordinal)
                || !string.Equals(Definition.EntryPoint, Request.EntryPoint, StringComparison.Ordinal)) {
                throw Invalid("program definition", "stage, program name, or entrypoint disagrees with the request");
            }
            if (Definition.Variants == null || Definition.Variants.Length != 1
                || Definition.Variants[0] == null
                || !string.Equals(Definition.Variants[0].Name, Request.Variant, StringComparison.Ordinal)) {
                throw Invalid("program definition variants", "the single reflected variant must match the request variant");
            }

            if (Binary.Target != Request.Target
                || Binary.Stage != Request.Stage
                || !string.Equals(Binary.EntryPoint, Request.EntryPoint, StringComparison.Ordinal)
                || !string.Equals(Binary.Variant, Request.Variant, StringComparison.Ordinal)) {
                throw Invalid("compiled binary", "target, stage, entrypoint, or variant disagrees with the request");
            }
            ValidateIdentity(Result.ToolIdentity, "tool identity");
            ValidateIdentity(Result.CompileIdentity, "compile identity");

            if (Binary.Bytecode == null || Binary.Bytecode.Length == 0) {
                throw Invalid("microcode", "bytecode must be nonempty");
            }
            if ((Binary.Bytecode.Length & 3) != 0) {
                throw Invalid("microcode", $"bytecode length {Binary.Bytecode.Length} is not dword-aligned");
            }
            int wordCount = Binary.Bytecode.Length / sizeof(uint);
            if (wordCount > MaximumMicrocodeWords) {
                throw Invalid("microcode", $"word count {wordCount} exceeds {MaximumMicrocodeWords}");
            }
            if (wordCount % 3 != 0) {
                throw Invalid("microcode", $"dword count {wordCount} is not divisible by three Xenos instructions");
            }
        }

        void ValidateNativeHeader() {
            string stageName = Request.Stage == ShaderStage.Vertex ? "vertex" : "pixel";
            RequireString(Metadata, "stage", "metadata", stageName);
            RequireString(Metadata, "entrypoint", "metadata", Request.EntryPoint);
            RequireString(Metadata, "binary_dword_byte_order", "metadata", "big_endian");

            JsonElement unsupported = RequireArray(Metadata, "unsupported_features", "metadata");
            if (unsupported.GetArrayLength() != 0) {
                throw Invalid("metadata.unsupported_features", "must be empty for a production shader program");
            }
        }

        uint[] ReadMicrocodeWords() {
            byte[] bytecode = Binary.Bytecode;
            uint[] words = new uint[bytecode.Length / sizeof(uint)];
            for (int index = 0; index < words.Length; index++) {
                words[index] = BinaryPrimitives.ReadUInt32BigEndian(bytecode.AsSpan(index * sizeof(uint), sizeof(uint)));
            }
            return words;
        }

        void ValidateRegisterMetadata() {
            RegisterCount = RequireInt(Metadata, "register_count", "metadata", minimum: 1, maximum: 64);
            int highest = RequireInt(Metadata, "highest_register_index", "metadata", minimum: 0, maximum: 63);
            RegisterField = RequireInt(Metadata, "sq_program_cntl_register_field", "metadata", minimum: 0, maximum: 63);
            if (highest != RegisterCount - 1) {
                throw Invalid("metadata.highest_register_index", $"{highest} does not equal register_count - 1 ({RegisterCount - 1})");
            }
            if (RegisterField != RegisterCount - 1) {
                throw Invalid("metadata.sq_program_cntl_register_field", $"{RegisterField} does not equal register_count - 1 ({RegisterCount - 1})");
            }
        }

        List<InterfaceDraft> ReadInterfaces(ShaderVertexElement[] elements, string owner) {
            if (elements == null) {
                throw Invalid(owner, "interface collection is missing");
            }
            List<InterfaceDraft> interfaces = new(elements.Length);
            HashSet<(Xbox360ShaderInterfaceKind Kind, int Index)> seen = new();
            foreach (ShaderVertexElement element in elements) {
                if (element == null) {
                    throw Invalid(owner, "contains a null interface entry");
                }
                int width = GetWidth(element.Format, $"{owner} format");
                Xbox360ShaderInterfaceKind kind;
                if (string.Equals(element.Semantic, "LOCATION", StringComparison.Ordinal)) {
                    if (element.Index > 7) {
                        throw Invalid(owner, $"location {element.Index} is outside the 0..7 varying range");
                    }
                    kind = Xbox360ShaderInterfaceKind.Location;
                } else if (string.Equals(element.Semantic, "SV_POSITION", StringComparison.Ordinal)) {
                    if (element.Index != 0 || width != 4) {
                        throw Invalid(owner, "SV_POSITION must be float4 at index 0");
                    }
                    kind = Xbox360ShaderInterfaceKind.Position;
                } else {
                    throw Invalid(owner, $"semantic '{element.Semantic}' is not a LOCATION or SV_POSITION interface");
                }

                if (!seen.Add((kind, element.Index))) {
                    throw Invalid(owner, $"duplicate {kind} interface index {element.Index}");
                }
                interfaces.Add(new InterfaceDraft(kind, element.Index, width, element.Format));
            }
            return interfaces;
        }

        void ValidateStageInterfaces(List<InterfaceDraft> inputs, List<InterfaceDraft> outputs) {
            if (Request.Stage == ShaderStage.Vertex) {
                if (inputs.Any(input => input.Kind != Xbox360ShaderInterfaceKind.Location)) {
                    throw Invalid("vertex inputs", "builtin interfaces are not supported");
                }
                int highestInputLocation = Result.VertexLayout == Xbox360ShaderVertexLayout.Batch2D ? 5 : 3;
                if (inputs.Any(input => input.Index > highestInputLocation)) {
                    throw Invalid("vertex inputs", $"only LOCATION0 through LOCATION{highestInputLocation} have a supported {Result.VertexLayout} vf0 layout");
                }
                if (outputs.Count(output => output.Kind == Xbox360ShaderInterfaceKind.Position) != 1) {
                    throw Invalid("vertex outputs", "exactly one SV_POSITION float4 output is required");
                }
                if (outputs.Any(output => output.Kind is not (Xbox360ShaderInterfaceKind.Location or Xbox360ShaderInterfaceKind.Position))) {
                    throw Invalid("vertex outputs", "contains an unsupported interface kind");
                }
            } else {
                if (inputs.Any(input => input.Kind != Xbox360ShaderInterfaceKind.Location)) {
                    throw Invalid("pixel inputs", "builtin interfaces are not supported");
                }
                if (outputs.Count != 1
                    || outputs[0].Kind != Xbox360ShaderInterfaceKind.Location
                    || outputs[0].Index != 0
                    || outputs[0].Width != 4) {
                    throw Invalid("pixel outputs", "exactly one float4 LOCATION0 color output is required");
                }
            }
        }

        Dictionary<int, NativeInput> ReadAndValidateNativeInputs(List<InterfaceDraft> reflectedInputs) {
            Dictionary<int, NativeInput> inputs = ReadNativeInputMap("inputs");
            Dictionary<int, NativeInput> inputMap = ReadNativeInputMap("input_map");
            if (inputs.Count != inputMap.Count || inputs.Any(pair => !inputMap.TryGetValue(pair.Key, out NativeInput other) || pair.Value != other)) {
                throw Invalid("metadata.inputs/input_map", "the two native input maps disagree");
            }

            int[] expectedLocations = reflectedInputs.Select(input => input.Index).Order().ToArray();
            if (!inputs.Keys.Order().SequenceEqual(expectedLocations)) {
                throw Invalid("metadata.inputs", "native input locations do not exactly match reflected interfaces");
            }

            foreach (InterfaceDraft input in reflectedInputs) {
                NativeInput native = inputs[input.Index];
                if (native.Register < 0 || native.Register >= RegisterCount) {
                    throw Invalid($"metadata.inputs[{input.Index}].register", "register is outside the declared stage register range");
                }
                if (Request.Stage == ShaderStage.Vertex) {
                    (int width, int offset, int stride, string hardwareFormat) = GetVertexFetchLayout(input.Index);
                    if (input.Width != width
                        || native.FetchConstant != 0
                        || native.Stride != stride
                        || native.Offset != offset
                        || !string.Equals(native.HardwareFormat, hardwareFormat, StringComparison.Ordinal)) {
                        throw Invalid($"metadata.inputs[{input.Index}]", $"fetch constant, stride, offset, or hardware format disagrees with the {Result.VertexLayout} layout");
                    }
                } else if (!string.Equals(native.Kind, "interpolator", StringComparison.Ordinal)
                    || native.Register != input.Index) {
                    throw Invalid($"metadata.inputs[{input.Index}]", "pixel inputs must be interpolators whose register equals their location");
                }
            }
            return inputs;
        }

        (int Width, int Offset, int Stride, string HardwareFormat) GetVertexFetchLayout(int location) {
            if (Result.VertexLayout == Xbox360ShaderVertexLayout.Batch2D) {
                if (location is < 0 or > 5) {
                    throw Invalid("vertex inputs", $"LOCATION{location} is unsupported by the Batch2D layout");
                }
                return (4, checked(location * 4), 24, "32_32_32_32_FLOAT");
            }

            return location switch {
                0 => (3, 0, 12, "32_32_32_FLOAT"),
                1 => (3, 3, 12, "32_32_32_FLOAT"),
                2 => (2, 6, 12, "32_32_FLOAT"),
                3 => (4, 8, 12, "32_32_32_32_FLOAT"),
                _ => throw Invalid("vertex inputs", $"LOCATION{location} is unsupported by the Mesh3D layout")
            };
        }

        Dictionary<int, NativeInput> ReadNativeInputMap(string propertyName) {
            JsonElement inputObject = RequireObject(Metadata, propertyName, "metadata");
            Dictionary<int, NativeInput> result = new();
            foreach (JsonProperty property in inputObject.EnumerateObject()) {
                int location = ParseLocationKey(property.Name, $"metadata.{propertyName}", maximum: 7);
                JsonElement item = property.Value;
                RequireKind(item, JsonValueKind.Object, $"metadata.{propertyName}[{location}]");
                int register = RequireInt(item, "register", $"metadata.{propertyName}[{location}]", 0, 63);
                NativeInput native;
                if (Request.Stage == ShaderStage.Vertex) {
                    native = new NativeInput(
                        register,
                        RequireInt(item, "fetch_constant", $"metadata.{propertyName}[{location}]", 0, 95),
                        RequireInt(item, "stride", $"metadata.{propertyName}[{location}]", 1, int.MaxValue),
                        RequireInt(item, "offset", $"metadata.{propertyName}[{location}]", 0, int.MaxValue),
                        RequireString(item, "format", $"metadata.{propertyName}[{location}]"),
                        null);
                } else {
                    native = new NativeInput(
                        register, null, null, null, null,
                        RequireString(item, "kind", $"metadata.{propertyName}[{location}]"));
                }
                result.Add(location, native);
            }
            return result;
        }

        Dictionary<int, int> ReadAndValidateVaryingMap(
            List<InterfaceDraft> reflectedInputs,
            List<InterfaceDraft> reflectedOutputs) {
            JsonElement varyingObject = RequireObject(Metadata, "varying_map", "metadata");
            Dictionary<int, int> map = new();
            foreach (JsonProperty property in varyingObject.EnumerateObject()) {
                int location = ParseLocationKey(property.Name, "metadata.varying_map", maximum: 7);
                int slot = ReadInt(property.Value, $"metadata.varying_map[{location}]", 0, 7);
                map.Add(location, slot);
            }

            IEnumerable<int> requiredLocations = Request.Stage == ShaderStage.Vertex
                ? reflectedOutputs.Where(item => item.Kind == Xbox360ShaderInterfaceKind.Location).Select(item => item.Index)
                : reflectedInputs.Where(item => item.Kind == Xbox360ShaderInterfaceKind.Location).Select(item => item.Index);
            int[] expected = requiredLocations.Order().ToArray();
            if (!map.Keys.Order().SequenceEqual(expected)) {
                throw Invalid("metadata.varying_map", "locations do not exactly match vertex outputs or pixel inputs");
            }
            foreach ((int location, int slot) in map) {
                if (slot != location) {
                    throw Invalid("metadata.varying_map", $"LOCATION{location} must use physical varying slot {location}, not {slot}");
                }
            }
            return map;
        }

        void ValidateNativeOutputs(List<InterfaceDraft> reflectedOutputs, Dictionary<int, int> varyingSlots) {
            JsonElement nativeOutputs = RequireArray(Metadata, "outputs", "metadata");
            HashSet<int> nativeVaryings = new();
            HashSet<int> nativeColors = new();
            int positions = 0;
            foreach (JsonElement item in nativeOutputs.EnumerateArray()) {
                RequireKind(item, JsonValueKind.Object, "metadata.outputs item");
                string kind = RequireString(item, "kind", "metadata.outputs item");
                switch (kind) {
                    case "position" when Request.Stage == ShaderStage.Vertex:
                        positions++;
                        break;
                    case "varying" when Request.Stage == ShaderStage.Vertex:
                        int varyingLocation = RequireInt(item, "location", "metadata.outputs varying", 0, 7);
                        if (!nativeVaryings.Add(varyingLocation)) {
                            throw Invalid("metadata.outputs", $"duplicate vertex varying export at physical slot {varyingLocation}");
                        }
                        break;
                    case "color" when Request.Stage == ShaderStage.Pixel:
                        int colorLocation = RequireInt(item, "location", "metadata.outputs color", 0, 7);
                        if (!nativeColors.Add(colorLocation)) {
                            throw Invalid("metadata.outputs", $"duplicate pixel color export at location {colorLocation}");
                        }
                        break;
                    default:
                        throw Invalid("metadata.outputs", $"native output kind '{kind}' is unsupported for {Request.Stage}");
                }
            }

            if (Request.Stage == ShaderStage.Vertex) {
                int[] expectedVaryings = reflectedOutputs
                    .Where(output => output.Kind == Xbox360ShaderInterfaceKind.Location)
                    .Select(output => varyingSlots[output.Index])
                    .Order().ToArray();
                if (positions != 1 || !nativeVaryings.Order().SequenceEqual(expectedVaryings)) {
                    throw Invalid("metadata.outputs", "position or varying exports do not match reflected vertex outputs");
                }
            } else if (nativeColors.Count != 1 || !nativeColors.Contains(0)) {
                throw Invalid("metadata.outputs", "pixel color exports must contain exactly LOCATION0");
            }
        }

        List<Xbox360ShaderInterface> BuildInputSnapshots(
            List<InterfaceDraft> reflectedInputs,
            Dictionary<int, NativeInput> nativeInputs) {
            List<Xbox360ShaderInterface> inputs = new(reflectedInputs.Count);
            foreach (InterfaceDraft input in reflectedInputs) {
                NativeInput native = nativeInputs[input.Index];
                if (Request.Stage == ShaderStage.Vertex) {
                    Xbox360ShaderVertexFetchLayout fetch = new(
                        native.FetchConstant.Value,
                        native.Stride.Value,
                        native.Offset.Value,
                        native.HardwareFormat);
                    inputs.Add(new Xbox360ShaderInterface(input.Kind, input.Index, input.Width,
                        input.Format, null, fetch));
                } else {
                    inputs.Add(new Xbox360ShaderInterface(input.Kind, input.Index, input.Width,
                        input.Format, native.Register, null));
                }
            }
            return inputs;
        }

        List<Xbox360ShaderInterface> BuildOutputSnapshots(
            List<InterfaceDraft> reflectedOutputs,
            Dictionary<int, int> varyingSlots) {
            List<Xbox360ShaderInterface> outputs = new(reflectedOutputs.Count);
            foreach (InterfaceDraft output in reflectedOutputs) {
                int? physicalSlot = output.Kind == Xbox360ShaderInterfaceKind.Location
                    ? Request.Stage == ShaderStage.Vertex ? varyingSlots[output.Index] : output.Index
                    : null;
                outputs.Add(new Xbox360ShaderInterface(output.Kind, output.Index, output.Width,
                    output.Format, physicalSlot, null));
            }
            return outputs;
        }

        (List<Xbox360ShaderResource> Resources, Xbox360ShaderConstantBufferLayout ConstantBuffer) ReadResourcesAndConstants() {
            if (Definition.Bindings == null) {
                throw Invalid("program definition bindings", "binding collection is missing");
            }
            List<ShaderBinding> constantBindings = new();
            List<ShaderBinding> textureBindings = new();
            List<ShaderBinding> samplerBindings = new();
            foreach (ShaderBinding binding in Definition.Bindings) {
                if (binding == null || string.IsNullOrWhiteSpace(binding.Name)) {
                    throw Invalid("program definition bindings", "contains a missing binding or name");
                }
                if (binding.Set != 0) {
                    throw Invalid($"binding '{binding.Name}'", "only descriptor set 0 is supported");
                }
                switch (binding.Type) {
                    case ShaderResourceType.ConstantBuffer:
                        constantBindings.Add(binding);
                        break;
                    case ShaderResourceType.Texture2D:
                        textureBindings.Add(binding);
                        break;
                    case ShaderResourceType.Sampler:
                        samplerBindings.Add(binding);
                        break;
                    default:
                        throw Invalid($"binding '{binding.Name}'", $"resource type '{binding.Type}' is unsupported");
                }
            }
            if (constantBindings.Count > 1) {
                throw Invalid("constant buffers", "only one b0/set0 buffer is supported per stage");
            }
            if (textureBindings.Count > 1 || samplerBindings.Count > 1) {
                throw Invalid("texture resources", "only one Texture2D and one Sampler are supported per stage");
            }
            if (textureBindings.Count != samplerBindings.Count) {
                throw Invalid("texture resources", "Texture2D and Sampler must be declared together");
            }
            if (textureBindings.Count != 0 && Request.Stage != ShaderStage.Pixel) {
                throw Invalid("texture resources", "Texture2D and Sampler are pixel-stage only");
            }

            ShaderBinding constant = constantBindings.SingleOrDefault();
            Xbox360ShaderConstantBufferLayout constantLayout = null;
            List<Xbox360ShaderDeclaredConstantMember> declared = new();
            if (constant != null) {
                if (constant.Slot != Request.Options.BindingPolicy.ConstantBufferShift) {
                    throw Invalid($"binding '{constant.Name}'", "logical b0 slot does not match the requested constant-buffer shift");
                }
                declared = ValidateDeclaredConstantMembers(constant);
                int bankBase = Request.Stage == ShaderStage.Vertex ? 0 : 256;
                constantLayout = ReadConstantBufferMetadata(constant, declared, bankBase);
            } else {
                JsonElement uniforms = RequireObject(Metadata, "uniforms", "metadata");
                if (uniforms.EnumerateObject().Any()) {
                    throw Invalid("metadata.uniforms", "contains a native uniform mapping without a reflected constant buffer");
                }
            }

            List<Xbox360ShaderResource> resources = new();
            if (constant != null) {
                resources.Add(new Xbox360ShaderResource(constant.Name, constant.Type, constant.Set,
                    constant.Slot, 0, 0, null, constantLayout.PhysicalBankBaseRegister));
            }
            if (textureBindings.Count == 0) {
                JsonElement texturesWithoutBindings = RequireArray(Metadata, "textures", "metadata");
                if (texturesWithoutBindings.GetArrayLength() != 0) {
                    throw Invalid("metadata.textures", "contains a native texture mapping without a reflected texture/sampler pair");
                }
                return (resources, constantLayout);
            }

            ShaderBinding texture = textureBindings[0];
            ShaderBinding sampler = samplerBindings[0];
            ValidateNonConstantBinding(texture);
            ValidateNonConstantBinding(sampler);
            int rawTextureBinding = SubtractShift(texture.Slot, Request.Options.BindingPolicy.TextureShift, texture.Name);
            int rawSamplerBinding = SubtractShift(sampler.Slot, Request.Options.BindingPolicy.SamplerShift, sampler.Name);
            JsonElement nativeTextures = RequireArray(Metadata, "textures", "metadata");
            if (nativeTextures.GetArrayLength() != 1) {
                throw Invalid("metadata.textures", "must contain exactly one native Texture2D/Sampler access");
            }
            JsonElement nativeTexture = nativeTextures[0];
            RequireKind(nativeTexture, JsonValueKind.Object, "metadata.textures[0]");
            int textureSet = RequireInt(nativeTexture, "texture_set", "metadata.textures[0]", 0, int.MaxValue);
            int textureBinding = RequireInt(nativeTexture, "texture_binding", "metadata.textures[0]", 0, int.MaxValue);
            int samplerSet = RequireInt(nativeTexture, "sampler_set", "metadata.textures[0]", 0, int.MaxValue);
            int samplerBinding = RequireInt(nativeTexture, "sampler_binding", "metadata.textures[0]", 0, int.MaxValue);
            int fetchConstant = RequireInt(nativeTexture, "fetch_constant", "metadata.textures[0]", 0, 31);
            RequireString(nativeTexture, "stage", "metadata.textures[0]", "pixel");
            RequireString(nativeTexture, "dimension", "metadata.textures[0]", "2D");
            if (textureSet != texture.Set || samplerSet != sampler.Set
                || textureBinding != rawTextureBinding || samplerBinding != rawSamplerBinding
                || fetchConstant != 1) {
                throw Invalid("metadata.textures[0]", "native set, binding, or tf1 slot disagrees with the reflected pair and request shifts");
            }

            resources.Add(new Xbox360ShaderResource(texture.Name, texture.Type, texture.Set,
                texture.Slot, textureSet, textureBinding, fetchConstant, null));
            resources.Add(new Xbox360ShaderResource(sampler.Name, sampler.Type, sampler.Set,
                sampler.Slot, samplerSet, samplerBinding, fetchConstant, null));
            return (resources, constantLayout);
        }

        List<Xbox360ShaderDeclaredConstantMember> ValidateDeclaredConstantMembers(ShaderBinding binding) {
            if (binding.Size <= 0 || binding.Size > MaximumConstantBufferBytes || binding.Size % 16 != 0) {
                throw Invalid($"constant buffer '{binding.Name}'", "size must be positive, 16-byte padded, and no more than 4096 bytes");
            }
            if (binding.Members == null) {
                throw Invalid($"constant buffer '{binding.Name}'", "declared members are missing");
            }
            List<Xbox360ShaderDeclaredConstantMember> members = new(binding.Members.Length);
            HashSet<string> names = new(StringComparer.Ordinal);
            List<(int Start, int End, string Name)> ranges = new();
            foreach (ShaderConstantMember member in binding.Members) {
                if (member == null || string.IsNullOrWhiteSpace(member.Name)) {
                    throw Invalid($"constant buffer '{binding.Name}'", "contains a missing declared member or name");
                }
                if (!names.Add(member.Name)) {
                    throw Invalid($"constant buffer '{binding.Name}'", $"declared member '{member.Name}' appears more than once");
                }
                int expectedSize = member.Type switch {
                    "float" => 4,
                    "float2" => 8,
                    "float3" => 12,
                    "float4" => 16,
                    "float4x4" => 64,
                    _ => throw Invalid($"constant buffer member '{member.Name}'", $"type '{member.Type}' is unsupported")
                };
                if (member.Size != expectedSize) {
                    throw Invalid($"constant buffer member '{member.Name}'", $"size {member.Size} does not match {member.Type} ({expectedSize} bytes)");
                }
                if (member.Offset < 0 || member.Offset % 16 != 0) {
                    throw Invalid($"constant buffer member '{member.Name}'", "offset must be nonnegative and float4-aligned");
                }
                int end = checked(member.Offset + member.Size);
                if (end > binding.Size) {
                    throw Invalid($"constant buffer member '{member.Name}'", "extends beyond the declared buffer size");
                }
                ranges.Add((member.Offset, end, member.Name));
                members.Add(new Xbox360ShaderDeclaredConstantMember(member.Name, member.Type, member.Offset, member.Size));
            }
            ranges.Sort((left, right) => left.Start.CompareTo(right.Start));
            for (int index = 1; index < ranges.Count; index++) {
                if (ranges[index].Start < ranges[index - 1].End) {
                    throw Invalid($"constant buffer '{binding.Name}'", $"members '{ranges[index - 1].Name}' and '{ranges[index].Name}' overlap");
                }
            }
            return members;
        }

        Xbox360ShaderConstantBufferLayout ReadConstantBufferMetadata(
            ShaderBinding binding,
            List<Xbox360ShaderDeclaredConstantMember> declared,
            int bankBase) {
            JsonElement uniforms = RequireObject(Metadata, "uniforms", "metadata");
            JsonProperty[] entries = uniforms.EnumerateObject().ToArray();
            if (entries.Length != 1 || !string.Equals(entries[0].Name, "0", StringComparison.Ordinal)) {
                throw Invalid("metadata.uniforms", "a reflected constant buffer requires exactly native uniform key '0'");
            }
            JsonElement uniform = entries[0].Value;
            RequireKind(uniform, JsonValueKind.Object, "metadata.uniforms.0");
            if (RequireInt(uniform, "binding", "metadata.uniforms.0", 0, int.MaxValue) != 0
                || RequireInt(uniform, "descriptor_set", "metadata.uniforms.0", 0, int.MaxValue) != 0) {
                throw Invalid("metadata.uniforms.0", "only b0/set0 is supported");
            }
            RequireString(uniform, "raw_memory_order", "metadata.uniforms.0", "consecutive_uploaded_vec4_registers");
            int capacity = RequireInt(uniform, "register_count", "metadata.uniforms.0", 1, 256);
            if (capacity != binding.Size / 16) {
                throw Invalid("metadata.uniforms.0.register_count", $"capacity {capacity} does not match reflected buffer size {binding.Size}");
            }
            if (uniform.TryGetProperty("register_values", out _)) {
                throw Invalid("metadata.uniforms.0.register_values", "value-based constant uploads are not supported on this production route");
            }

            JsonElement usedArray = RequireArray(uniform, "members", "metadata.uniforms.0");
            List<Xbox360ShaderUsedConstantMember> used = new(usedArray.GetArrayLength());
            HashSet<int> usedOffsets = new();
            Dictionary<int, Xbox360ShaderDeclaredConstantMember> declaredByOffset = declared.ToDictionary(member => member.OffsetBytes);
            foreach (JsonElement item in usedArray.EnumerateArray()) {
                RequireKind(item, JsonValueKind.Object, "metadata.uniforms.0.members item");
                int offset = RequireInt(item, "offset_bytes", "metadata.uniforms.0.members item", 0, binding.Size - 1);
                if (!usedOffsets.Add(offset)) {
                    throw Invalid("metadata.uniforms.0.members", $"duplicate access at offset {offset}");
                }
                if (!declaredByOffset.TryGetValue(offset, out Xbox360ShaderDeclaredConstantMember member)) {
                    throw Invalid("metadata.uniforms.0.members", $"offset {offset} does not exactly match a declared member");
                }
                JsonElement registersElement = RequireArray(item, "registers", "metadata.uniforms.0.members item");
                int registerCount = member.Type == "float4x4" ? 4 : 1;
                if (registersElement.GetArrayLength() != registerCount) {
                    throw Invalid($"metadata.uniforms.0.members offset {offset}", $"expected {registerCount} consecutive register(s)");
                }
                int firstRegister = offset / 16;
                int[] registers = new int[registerCount];
                for (int index = 0; index < registerCount; index++) {
                    registers[index] = ReadInt(registersElement[index], $"metadata.uniforms.0.members[{offset}].registers[{index}]", 0, capacity - 1);
                    if (registers[index] != firstRegister + index) {
                        throw Invalid($"metadata.uniforms.0.members offset {offset}", "registers must be the exact contiguous range beginning at offset_bytes / 16");
                    }
                }

                string major = null;
                int? stride = null;
                bool isMatrix = member.Type == "float4x4";
                bool hasMajor = item.TryGetProperty("major", out JsonElement majorElement);
                bool hasStride = item.TryGetProperty("matrix_stride_bytes", out JsonElement strideElement);
                if (isMatrix) {
                    if (!hasMajor || !hasStride) {
                        throw Invalid($"metadata.uniforms.0.members offset {offset}", "matrix access requires major orientation and matrix stride");
                    }
                    major = ReadString(majorElement, $"metadata.uniforms.0.members offset {offset}.major");
                    if (major is not ("row_major" or "column_major")) {
                        throw Invalid($"metadata.uniforms.0.members offset {offset}.major", "must be row_major or column_major");
                    }
                    stride = ReadInt(strideElement, $"metadata.uniforms.0.members offset {offset}.matrix_stride_bytes", 1, int.MaxValue);
                    if (stride != 16) {
                        throw Invalid($"metadata.uniforms.0.members offset {offset}", "matrix stride must be 16 bytes");
                    }
                } else if (hasMajor || hasStride) {
                    throw Invalid($"metadata.uniforms.0.members offset {offset}", "scalar/vector access must not carry matrix decorations");
                }
                used.Add(new Xbox360ShaderUsedConstantMember(member.Name, offset, registers, major, stride));
            }
            return new Xbox360ShaderConstantBufferLayout(binding.Size, capacity, bankBase, declared, used);
        }

        void ValidateNonConstantBinding(ShaderBinding binding) {
            if (binding.Size != 0 || binding.Members == null || binding.Members.Length != 0) {
                throw Invalid($"binding '{binding.Name}'", "texture and sampler resources must not carry constant-buffer size or members");
            }
        }

        static int SubtractShift(int logicalSlot, int shift, string name) {
            int raw;
            try {
                raw = checked(logicalSlot - shift);
            } catch (OverflowException exception) {
                throw new InvalidDataException($"Binding '{name}' physical-slot subtraction overflowed.", exception);
            }
            if (raw < 0) {
                throw Invalid($"binding '{name}'", "logical slot is below the requested binding shift");
            }
            return raw;
        }

        static int ParseLocationKey(string key, string owner, int maximum) {
            if (!int.TryParse(key, NumberStyles.None, CultureInfo.InvariantCulture, out int location)
                || location < 0 || location > maximum
                || !string.Equals(key, location.ToString(CultureInfo.InvariantCulture), StringComparison.Ordinal)) {
                throw Invalid(owner, $"key '{key}' is not a canonical location in 0..{maximum}");
            }
            return location;
        }

        static int GetWidth(string format, string owner) => format switch {
            "float" => 1,
            "float2" => 2,
            "float3" => 3,
            "float4" => 4,
            _ => throw Invalid(owner, $"format '{format}' is unsupported")
        };

        static void ValidateIdentity(string identity, string owner) {
            if (identity == null || identity.Length != 64 || identity.Any(character =>
                !(character is >= '0' and <= '9' or >= 'a' and <= 'f' or >= 'A' and <= 'F'))) {
                throw Invalid(owner, "must be exactly 64 hexadecimal characters");
            }
        }

        static string RequireString(JsonElement parent, string propertyName, string owner, string expected = null) {
            JsonElement value = RequireProperty(parent, propertyName, owner);
            string actual = ReadString(value, $"{owner}.{propertyName}");
            if (expected != null && !string.Equals(actual, expected, StringComparison.Ordinal)) {
                throw Invalid($"{owner}.{propertyName}", $"'{actual}' does not match expected '{expected}'");
            }
            return actual;
        }

        static string ReadString(JsonElement value, string owner) {
            if (value.ValueKind != JsonValueKind.String) {
                throw Invalid(owner, "must be a JSON string");
            }
            string result = value.GetString();
            if (string.IsNullOrWhiteSpace(result)) {
                throw Invalid(owner, "must be nonempty");
            }
            return result;
        }

        static int RequireInt(JsonElement parent, string propertyName, string owner, int minimum, int maximum) =>
            ReadInt(RequireProperty(parent, propertyName, owner), $"{owner}.{propertyName}", minimum, maximum);

        static int ReadInt(JsonElement value, string owner, int minimum, int maximum) {
            if (value.ValueKind != JsonValueKind.Number || !value.TryGetInt32(out int result)
                || result < minimum || result > maximum) {
                throw Invalid(owner, $"value {value.GetRawText()} must be an integer in {minimum}..{maximum}");
            }
            return result;
        }

        static JsonElement RequireObject(JsonElement parent, string propertyName, string owner) {
            JsonElement value = RequireProperty(parent, propertyName, owner);
            RequireKind(value, JsonValueKind.Object, $"{owner}.{propertyName}");
            return value;
        }

        static JsonElement RequireArray(JsonElement parent, string propertyName, string owner) {
            JsonElement value = RequireProperty(parent, propertyName, owner);
            RequireKind(value, JsonValueKind.Array, $"{owner}.{propertyName}");
            return value;
        }

        static JsonElement RequireProperty(JsonElement parent, string propertyName, string owner) {
            if (parent.ValueKind != JsonValueKind.Object || !parent.TryGetProperty(propertyName, out JsonElement value)) {
                throw Invalid(owner, $"is missing required field '{propertyName}'");
            }
            return value;
        }

        static void RequireKind(JsonElement value, JsonValueKind expected, string owner) {
            if (value.ValueKind != expected) {
                throw Invalid(owner, $"must be a JSON {expected.ToString().ToLowerInvariant()}");
            }
        }

        static InvalidDataException Invalid(string owner, string detail) =>
            new($"Xbox360 shader {owner}: {detail}.");

        readonly record struct InterfaceDraft(Xbox360ShaderInterfaceKind Kind, int Index, int Width, string Format);

        readonly record struct NativeInput(
            int Register,
            int? FetchConstant,
            int? Stride,
            int? Offset,
            string HardwareFormat,
            string Kind);
    }
}
