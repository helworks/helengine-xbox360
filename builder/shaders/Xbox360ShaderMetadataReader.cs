using System.Text;
using System.Text.Json;

namespace helengine.x360.builder;

internal static class Xbox360ShaderMetadataReader {
    const int MaximumMetadataBytes = 8 * 1024 * 1024;

    public static JsonDocument Parse(string json) {
        if (json == null) {
            throw new InvalidDataException("Compiler metadata is missing.");
        }

        if (Encoding.UTF8.GetByteCount(json) > MaximumMetadataBytes) {
            throw new InvalidDataException($"Compiler metadata exceeds the {MaximumMetadataBytes}-byte limit.");
        }

        JsonDocument document;
        try {
            document = JsonDocument.Parse(json, new JsonDocumentOptions { MaxDepth = 64 });
        } catch (JsonException exception) {
            throw new InvalidDataException("Compiler metadata is not valid JSON.", exception);
        }

        try {
            if (document.RootElement.ValueKind != JsonValueKind.Object) {
                throw new InvalidDataException("Compiler metadata root must be a JSON object.");
            }
            ValidateUniqueProperties(document.RootElement, "metadata");
            return document;
        } catch {
            document.Dispose();
            throw;
        }
    }

    static void ValidateUniqueProperties(JsonElement value, string path) {
        if (value.ValueKind == JsonValueKind.Object) {
            HashSet<string> names = new(StringComparer.Ordinal);
            foreach (JsonProperty property in value.EnumerateObject()) {
                if (!names.Add(property.Name)) {
                    throw new InvalidDataException($"Compiler metadata contains duplicate property '{path}.{property.Name}'.");
                }
                ValidateUniqueProperties(property.Value, $"{path}.{property.Name}");
            }
        } else if (value.ValueKind == JsonValueKind.Array) {
            int index = 0;
            foreach (JsonElement item in value.EnumerateArray()) {
                ValidateUniqueProperties(item, $"{path}[{index}]");
                index++;
            }
        }
    }
}
