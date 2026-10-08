using System.Diagnostics;
using System.Text.Json;

if (args.Length < 4 || args[^2] != "--shaderc") {
    return 90;
}

string[] modeParts = File.ReadAllText(args[^1]).Split('|', 2);
string mode = modeParts[0];
if (mode == "launch-marker") {
    File.WriteAllText(modeParts[1], Process.GetCurrentProcess().Id.ToString());
    return 0;
}

using JsonDocument requestDocument = JsonDocument.Parse(Console.ReadLine() ?? string.Empty);
JsonElement request = requestDocument.RootElement;

if (mode == "timeout") {
    if (modeParts.Length > 1) {
        File.WriteAllText(modeParts[1], Process.GetCurrentProcess().Id.ToString());
    }
    Thread.Sleep(TimeSpan.FromSeconds(30));
    return 0;
}

if (mode == "malformed") {
    Console.WriteLine("{malformed-json}");
    return 0;
}

if (mode == "eof") {
    return 0;
}

if (mode == "oversized-line") {
    Console.WriteLine(new string('x', 8 * 1024 * 1024 + 32));
    return 0;
}

string includedSourceName = null;
if (mode == "include") {
    Console.WriteLine(JsonSerializer.Serialize(new {
        protocol = 1,
        type = "include",
        id = 1,
        requesting_source = request.GetProperty("source_name").GetString(),
        include_name = "common.hlsli"
    }));
    using JsonDocument includeDocument = JsonDocument.Parse(Console.ReadLine() ?? string.Empty);
    JsonElement includeResponse = includeDocument.RootElement;
    if (includeResponse.GetProperty("protocol").GetInt32() != 1
        || includeResponse.GetProperty("type").GetString() != "include_result"
        || includeResponse.GetProperty("id").GetInt32() != 1
        || includeResponse.GetProperty("source_name").GetString() != "resolved/common.hlsli"
        || includeResponse.GetProperty("source").GetString() != "float4 SharedTint;") {
        Console.WriteLine(JsonSerializer.Serialize(Failure("include response did not match the resolver result")));
        return 91;
    }
    includedSourceName = includeResponse.GetProperty("source_name").GetString();
}

if (mode == "nonzero") {
    Console.WriteLine(JsonSerializer.Serialize(Failure("fixture shader failure")));
    return 17;
}

string stage = request.GetProperty("stage").GetString();
string entrypoint = request.GetProperty("entrypoint").GetString();
string reflectedStage = mode == "wrong-stage" ? (stage == "vertex" ? "pixel" : "vertex") : stage;
string reflectedEntrypoint = mode == "wrong-entrypoint" ? "DifferentEntry" : entrypoint;
string binaryBase64 = mode switch {
    "bad-base64" => "%%%not-base64%%%",
    "bad-alignment" => Convert.ToBase64String([1, 2, 3]),
    _ => Convert.ToBase64String([0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef])
};

Dictionary<string, object> metadata = new(StringComparer.Ordinal) {
    ["stage"] = reflectedStage,
    ["entrypoint"] = reflectedEntrypoint,
    ["wire_request"] = JsonSerializer.Deserialize<JsonElement>(request.GetRawText()),
    ["physical_resources"] = new[] {
        new { kind = "constant_buffer", set = 0, binding = 0 },
        new { kind = "texture2d", set = 0, binding = 1 },
        new { kind = "sampler", set = 0, binding = 2 }
    }
};
if (mode != "missing-vertex-layout") {
    string requestedLayout = request.GetProperty("vertex_layout").GetString();
    metadata["vertex_layout"] = mode switch {
        "unknown-vertex-layout" => "experimental-v9",
        "wrong-vertex-layout" => requestedLayout == "batch2d-v1" ? "mesh3d-v1" : "batch2d-v1",
        _ => requestedLayout
    };
}

object[] bindings = stage == "pixel"
    ? [
        new { name = "Globals", kind = "constant_buffer", set = 0, binding = 0, size = 16, members = new object[] { new { name = "Tint", type = "float4", offset = 0, size = 16 } } },
        new { name = "Albedo", kind = "texture2d", set = 0, binding = 1, size = 0, members = Array.Empty<object>() },
        new { name = "LinearSampler", kind = "sampler", set = 0, binding = 2, size = 0, members = Array.Empty<object>() }
    ]
    : [
        new { name = "Globals", kind = "constant_buffer", set = 0, binding = 0, size = 16, members = new object[] { new { name = "Tint", type = "float4", offset = 0, size = 16 } } }
    ];

object[] inputs = stage == "pixel"
    ? [new { name = "uv", location = (int?)2, builtin = (int?)null, format = "float2" }]
    : [new { name = "position", location = (int?)0, builtin = (int?)null, format = "float3" }];
object[] outputs = stage == "vertex"
    ? [new { name = "position", location = (int?)null, builtin = (int?)0, format = "float4" }]
    : [new { name = "color", location = (int?)0, builtin = (int?)null, format = "float4" }];
object result = new {
    protocol = 1,
    type = "result",
    success = true,
    binary_base64 = binaryBase64,
    metadata,
    reflection = new { inputs, outputs, bindings },
    diagnostics = "warning: include path kept intact: common.hlsli",
    includes = includedSourceName == null
        ? Array.Empty<object>()
        : [new { source_name = includedSourceName, sha256 = new string('C', 64) }],
    tool_identity = new string('A', 64),
    compile_identity = new string('B', 64)
};
Console.WriteLine(JsonSerializer.Serialize(result));
return mode == "exit-mismatch" ? 19 : 0;

static object Failure(string diagnostics) => new {
    protocol = 1,
    type = "result",
    success = false,
    diagnostics
};
