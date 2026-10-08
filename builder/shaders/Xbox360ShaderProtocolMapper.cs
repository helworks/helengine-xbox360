using System.Globalization;
using System.Text.Json;
using helengine;

namespace helengine.x360.builder;

internal static class Xbox360ShaderProtocolMapper {
    const int MaximumBinaryBytes = 1024 * 4;
    const int MaximumIncludes = 4096;

    public static Xbox360ShaderCompileResult Map(
        ShaderCompileRequest request,
        JsonElement result,
        Xbox360ShaderVertexLayout expectedVertexLayout) {
        bool success = RequiredBoolean(result, "success", "result");
        string diagnostics = RequiredString(result, "diagnostics", "result", allowEmpty: true);
        if (!success) {
            throw new Xbox360ShaderCompilationException(
                request,
                string.IsNullOrWhiteSpace(diagnostics)
                    ? "The Python backend reported a compilation failure without diagnostics."
                    : diagnostics);
        }

        JsonElement metadata = RequiredObject(result, "metadata", "result");
        string layoutValue = RequiredString(metadata, "vertex_layout", "metadata");
        Xbox360ShaderVertexLayout actualVertexLayout = Xbox360ShaderVertexLayouts.FromProtocolValue(layoutValue);
        if (actualVertexLayout != expectedVertexLayout) {
            throw new InvalidDataException($"Compiler metadata vertex_layout '{layoutValue}' does not match requested profile '{Xbox360ShaderVertexLayouts.ToProtocolValue(expectedVertexLayout)}'.");
        }
        string expectedStage = GetStageName(request.Stage);
        string actualStage = RequiredString(metadata, "stage", "metadata");
        if (!string.Equals(actualStage, expectedStage, StringComparison.Ordinal)) {
            throw new InvalidDataException($"Compiler metadata stage '{actualStage}' does not match request stage '{expectedStage}'.");
        }

        string actualEntryPoint = RequiredString(metadata, "entrypoint", "metadata");
        if (!string.Equals(actualEntryPoint, request.EntryPoint, StringComparison.Ordinal)) {
            throw new InvalidDataException($"Compiler metadata entrypoint '{actualEntryPoint}' does not match request entrypoint '{request.EntryPoint}'.");
        }

        byte[] binary = ReadBinary(result);
        string toolIdentity = ReadIdentity(result, "tool_identity");
        string compileIdentity = ReadIdentity(result, "compile_identity");
        JsonElement reflection = RequiredObject(result, "reflection", "result");
        ShaderVertexElement[] inputs = ReadInterfaces(reflection, "inputs");
        ShaderVertexElement[] outputs = ReadInterfaces(reflection, "outputs");
        ShaderBinding[] bindings = ReadBindings(reflection, request.Options.BindingPolicy);
        Xbox360IncludedSource[] includes = ReadIncludes(result);
        ShaderVariant[] variants = [new ShaderVariant(request.Variant, BuildVariantDefines(request.Defines))];
        ShaderProgramDefinition definition = new(
            request.ProgramName,
            request.Stage,
            request.EntryPoint,
            bindings,
            inputs,
            outputs,
            variants);
        ShaderCompiledBinary compiledBinary = new(
            request.Target,
            request.Stage,
            request.EntryPoint,
            request.Variant,
            binary);
        ShaderCompileDiagnostic[] resultDiagnostics = string.IsNullOrEmpty(diagnostics)
            ? Array.Empty<ShaderCompileDiagnostic>()
            : [new ShaderCompileDiagnostic(ShaderDiagnosticSeverity.Warning, diagnostics, request.Source.Path, 0, 0)];

        return new Xbox360ShaderCompileResult(
            request,
            definition,
            compiledBinary,
            resultDiagnostics,
            metadata.GetRawText(),
            toolIdentity,
            compileIdentity,
            includes,
            expectedVertexLayout);
    }

    static byte[] ReadBinary(JsonElement result) {
        string encoded = RequiredString(result, "binary_base64", "result");
        if (encoded.Length == 0 || encoded.Length > ((MaximumBinaryBytes + 2) / 3) * 4 || encoded.Length % 4 != 0) {
            throw new InvalidDataException("Result binary_base64 is empty, truncated, or exceeds the 1024-dword binary limit.");
        }

        int padding = encoded.EndsWith("==", StringComparison.Ordinal) ? 2
            : encoded.EndsWith("=", StringComparison.Ordinal) ? 1
            : 0;
        int contentLength = encoded.Length - padding;
        for (int index = 0; index < contentLength; index++) {
            char value = encoded[index];
            bool valid = value is >= 'A' and <= 'Z'
                or >= 'a' and <= 'z'
                or >= '0' and <= '9'
                or '+' or '/';
            if (!valid) {
                throw new InvalidDataException("Result binary_base64 contains an invalid base64 character.");
            }
        }
        for (int index = contentLength; index < encoded.Length; index++) {
            if (encoded[index] != '=') {
                throw new InvalidDataException("Result binary_base64 has invalid padding.");
            }
        }

        byte[] binary;
        try {
            binary = Convert.FromBase64String(encoded);
        } catch (FormatException exception) {
            throw new InvalidDataException("Result binary_base64 is malformed or truncated.", exception);
        }

        if (binary.Length == 0) {
            throw new InvalidDataException("The compiler returned an empty binary.");
        }
        if ((binary.Length & 3) != 0) {
            throw new InvalidDataException($"The compiler returned {binary.Length} bytes; Xenos microcode must be dword-aligned.");
        }
        if (binary.Length > MaximumBinaryBytes) {
            throw new InvalidDataException("The compiler binary exceeds the 1024-dword limit.");
        }
        return binary;
    }

    static ShaderVertexElement[] ReadInterfaces(JsonElement reflection, string propertyName) {
        JsonElement values = RequiredArray(reflection, propertyName, "reflection");
        List<ShaderVertexElement> interfaces = new();
        HashSet<string> signatures = new(StringComparer.Ordinal);
        foreach (JsonElement value in values.EnumerateArray()) {
            RequireKind(value, JsonValueKind.Object, $"reflection.{propertyName} item");
            _ = RequiredString(value, "name", $"reflection.{propertyName} item");
            string format = RequiredString(value, "format", $"reflection.{propertyName} item");
            if (format is not ("float" or "float2" or "float3" or "float4")) {
                throw new InvalidDataException($"Unsupported reflected interface format '{format}'.");
            }

            JsonElement location = RequiredProperty(value, "location", $"reflection.{propertyName} item");
            JsonElement builtin = RequiredProperty(value, "builtin", $"reflection.{propertyName} item");
            bool hasLocation = location.ValueKind != JsonValueKind.Null;
            bool hasBuiltin = builtin.ValueKind != JsonValueKind.Null;
            if (hasLocation == hasBuiltin) {
                throw new InvalidDataException($"Reflected {propertyName} item must specify exactly one of location or builtin.");
            }

            string semantic;
            int index;
            if (hasBuiltin) {
                int builtinId = ReadNonnegativeInteger(builtin, $"reflection.{propertyName}.builtin");
                if (builtinId != 0 || format != "float4") {
                    throw new InvalidDataException($"Unsupported reflected builtin {builtinId} with format '{format}'.");
                }
                semantic = "SV_POSITION";
                index = 0;
            } else {
                index = ReadNonnegativeInteger(location, $"reflection.{propertyName}.location");
                if (index > 7) {
                    throw new InvalidDataException($"Reflected interface location {index} is outside the Xenos 0..7 varying range.");
                }
                semantic = "LOCATION";
            }

            string signature = string.Concat(semantic, ":", index.ToString(CultureInfo.InvariantCulture));
            if (!signatures.Add(signature)) {
                throw new InvalidDataException($"Duplicate reflected interface semantic '{signature}'.");
            }
            interfaces.Add(new ShaderVertexElement(semantic, index, format));
        }
        return interfaces.ToArray();
    }

    static ShaderBinding[] ReadBindings(JsonElement reflection, ShaderBindingPolicy policy) {
        JsonElement values = RequiredArray(reflection, "bindings", "reflection");
        List<ShaderBinding> bindings = new();
        HashSet<string> physicalSlots = new(StringComparer.Ordinal);
        foreach (JsonElement value in values.EnumerateArray()) {
            RequireKind(value, JsonValueKind.Object, "reflection.bindings item");
            string name = RequiredString(value, "name", "reflection.bindings item");
            string kind = RequiredString(value, "kind", $"binding '{name}'");
            int set = ReadNonnegativeInteger(RequiredProperty(value, "set", $"binding '{name}'"), $"binding '{name}'.set");
            int physicalBinding = ReadNonnegativeInteger(RequiredProperty(value, "binding", $"binding '{name}'"), $"binding '{name}'.binding");
            if (set != 0) {
                throw new InvalidDataException($"Binding '{name}' uses set {set}; the Xenos host layout supports set 0 only.");
            }

            ShaderResourceType type;
            int shift;
            switch (kind) {
                case "constant_buffer":
                    type = ShaderResourceType.ConstantBuffer;
                    shift = policy.ConstantBufferShift;
                    break;
                case "texture2d":
                    type = ShaderResourceType.Texture2D;
                    shift = policy.TextureShift;
                    break;
                case "sampler":
                    type = ShaderResourceType.Sampler;
                    shift = policy.SamplerShift;
                    break;
                default:
                    throw new InvalidDataException($"Unsupported reflected resource kind '{kind}' for binding '{name}'.");
            }

            string physicalKey = string.Concat(kind, ":", set.ToString(CultureInfo.InvariantCulture), ":", physicalBinding.ToString(CultureInfo.InvariantCulture));
            if (!physicalSlots.Add(physicalKey)) {
                throw new InvalidDataException($"Duplicate reflected physical binding '{physicalKey}'.");
            }

            int logicalSlot;
            try {
                logicalSlot = checked(physicalBinding + shift);
            } catch (OverflowException exception) {
                throw new InvalidDataException($"Logical binding slot overflowed for resource '{name}'.", exception);
            }

            int size = ReadNonnegativeInteger(RequiredProperty(value, "size", $"binding '{name}'"), $"binding '{name}'.size");
            JsonElement memberValues = RequiredArray(value, "members", $"binding '{name}'");
            List<ShaderConstantMember> members = new();
            foreach (JsonElement member in memberValues.EnumerateArray()) {
                RequireKind(member, JsonValueKind.Object, $"binding '{name}' member");
                members.Add(new ShaderConstantMember(
                    RequiredString(member, "name", $"binding '{name}' member"),
                    RequiredString(member, "type", $"binding '{name}' member"),
                    ReadNonnegativeInteger(RequiredProperty(member, "offset", $"binding '{name}' member"), $"binding '{name}' member offset"),
                    ReadNonnegativeInteger(RequiredProperty(member, "size", $"binding '{name}' member"), $"binding '{name}' member size")));
            }

            if (type == ShaderResourceType.ConstantBuffer) {
                if (size <= 0) {
                    throw new InvalidDataException($"Reflected constant buffer '{name}' has no positive declared size.");
                }
            } else if (size != 0 || members.Count != 0) {
                throw new InvalidDataException($"Non-constant resource '{name}' must not carry constant-buffer size or members.");
            }

            bindings.Add(new ShaderBinding(name, type, set, logicalSlot, size, members.ToArray()));
        }
        return bindings.ToArray();
    }

    static Xbox360IncludedSource[] ReadIncludes(JsonElement result) {
        JsonElement values = RequiredArray(result, "includes", "result");
        if (values.GetArrayLength() > MaximumIncludes) {
            throw new InvalidDataException($"Compiler returned more than {MaximumIncludes} include identities.");
        }

        List<Xbox360IncludedSource> includes = new();
        foreach (JsonElement value in values.EnumerateArray()) {
            RequireKind(value, JsonValueKind.Object, "result.includes item");
            string sourceName = RequiredString(value, "source_name", "result.includes item");
            string sha256 = ReadIdentity(value, "sha256");
            includes.Add(new Xbox360IncludedSource(sourceName, sha256));
        }
        return includes.ToArray();
    }

    static string[] BuildVariantDefines(IReadOnlyList<ShaderDefine> defines) {
        string[] values = new string[defines.Count];
        for (int index = 0; index < defines.Count; index++) {
            ShaderDefine define = defines[index];
            values[index] = string.IsNullOrEmpty(define.Value)
                ? define.Name
                : string.Concat(define.Name, "=", define.Value);
        }
        return values;
    }

    static string ReadIdentity(JsonElement value, string propertyName) {
        string identity = RequiredString(value, propertyName, "result");
        if (identity.Length != 64 || identity.Any(character => !Uri.IsHexDigit(character))) {
            throw new InvalidDataException($"Result {propertyName} must be a 64-character SHA256 identity.");
        }
        return identity;
    }

    static string GetStageName(ShaderStage stage) => stage switch {
        ShaderStage.Vertex => "vertex",
        ShaderStage.Pixel => "pixel",
        _ => throw new InvalidDataException($"Unsupported stage '{stage}'.")
    };

    static string RequiredString(JsonElement value, string propertyName, string owner, bool allowEmpty = false) {
        JsonElement property = RequiredProperty(value, propertyName, owner);
        if (property.ValueKind != JsonValueKind.String) {
            throw new InvalidDataException($"{owner}.{propertyName} must be a string.");
        }
        string result = property.GetString();
        if (!allowEmpty && string.IsNullOrWhiteSpace(result)) {
            throw new InvalidDataException($"{owner}.{propertyName} must be nonempty.");
        }
        return result;
    }

    static bool RequiredBoolean(JsonElement value, string propertyName, string owner) {
        JsonElement property = RequiredProperty(value, propertyName, owner);
        if (property.ValueKind is not (JsonValueKind.True or JsonValueKind.False)) {
            throw new InvalidDataException($"{owner}.{propertyName} must be a boolean.");
        }
        return property.GetBoolean();
    }

    static JsonElement RequiredObject(JsonElement value, string propertyName, string owner) {
        JsonElement property = RequiredProperty(value, propertyName, owner);
        RequireKind(property, JsonValueKind.Object, $"{owner}.{propertyName}");
        return property;
    }

    static JsonElement RequiredArray(JsonElement value, string propertyName, string owner) {
        JsonElement property = RequiredProperty(value, propertyName, owner);
        RequireKind(property, JsonValueKind.Array, $"{owner}.{propertyName}");
        return property;
    }

    static JsonElement RequiredProperty(JsonElement value, string propertyName, string owner) {
        if (value.ValueKind != JsonValueKind.Object || !value.TryGetProperty(propertyName, out JsonElement property)) {
            throw new InvalidDataException($"{owner} is missing required field '{propertyName}'.");
        }
        return property;
    }

    static int ReadNonnegativeInteger(JsonElement value, string label) {
        if (value.ValueKind != JsonValueKind.Number || !value.TryGetInt32(out int result) || result < 0) {
            throw new InvalidDataException($"{label} must be a nonnegative 32-bit integer.");
        }
        return result;
    }

    static void RequireKind(JsonElement value, JsonValueKind expected, string label) {
        if (value.ValueKind != expected) {
            throw new InvalidDataException($"{label} must be a JSON {expected.ToString().ToLowerInvariant()}.");
        }
    }
}
