using System.Text.Json.Nodes;
using helengine;
using helengine.x360.builder;

namespace helengine.x360.builder.tests;

public sealed class Xbox360ShaderProgramPairTests {
    [Fact]
    public void Link_computes_real_baseline_program_control_and_upload_cost() {
        Xbox360ShaderProgram vertex = Xbox360ShaderProgramFactory.FromCompilation(
            Xbox360ShaderProgramTestSupport.TransformVertex);
        Xbox360ShaderProgram pixel = Xbox360ShaderProgramFactory.FromCompilation(
            Xbox360ShaderProgramTestSupport.TexturedPixel);

        Xbox360ShaderProgramPair pair = Xbox360ShaderProgramPair.Link(vertex, pixel);

        Assert.Equal(0x00100408u, pair.ProgramControl);
        Assert.Equal(2, pair.InterpolatorCount);
        Assert.Equal(78, pair.ProgramUploadDwords);
        Assert.Equal(22, pair.ConstantUploadDwords);
        Assert.Equal(10, pair.StateUploadDwords);
        Assert.Equal(110, pair.PairUploadDwords);
        pair.ValidateUploadBudget(110);
        Action insufficientBudget = () => pair.ValidateUploadBudget(109);
        Assert.Throws<InvalidOperationException>(insufficientBudget);

        Action zeroBudget = () => pair.ValidateUploadBudget(0);
        Assert.Throws<ArgumentOutOfRangeException>(zeroBudget);
        Action oversizedBudget = () => pair.ValidateUploadBudget(8193);
        Assert.Throws<ArgumentOutOfRangeException>(oversizedBudget);
    }

    [Fact]
    public void Link_accepts_a_color_only_pixel_program_and_extra_vertex_outputs() {
        Xbox360ShaderProgram vertex = Xbox360ShaderProgramFactory.FromCompilation(
            Xbox360ShaderProgramTestSupport.TransformVertex);
        Xbox360ShaderProgram pixel = Xbox360ShaderProgramFactory.FromCompilation(
            Xbox360ShaderProgramTestSupport.ColorOnlyPixel);

        Xbox360ShaderProgramPair pair = Xbox360ShaderProgramPair.Link(vertex, pixel);

        Assert.Empty(pixel.Resources);
        Assert.Equal(2, pair.InterpolatorCount);
        Assert.Equal(new[] { 0, 1 }, vertex.Outputs
            .Where(output => output.Kind == Xbox360ShaderInterfaceKind.Location)
            .Select(output => output.Index));
        Assert.Single(pixel.Inputs);
    }

    [Fact]
    public void Link_does_not_require_program_names_or_entrypoints_to_match() {
        Xbox360ShaderCompileResult pixelCompilation = Xbox360ShaderProgramTestSupport.Reidentify(
            Xbox360ShaderProgramTestSupport.TexturedPixel,
            programName: "another-unrelated-program-label",
            entryPoint: "shadeFragment");
        Xbox360ShaderProgram vertex = Xbox360ShaderProgramFactory.FromCompilation(
            Xbox360ShaderProgramTestSupport.TransformVertex);
        Xbox360ShaderProgram pixel = Xbox360ShaderProgramFactory.FromCompilation(pixelCompilation);

        Xbox360ShaderProgramPair pair = Xbox360ShaderProgramPair.Link(vertex, pixel);

        Assert.Equal("arbitrary-vertex-program", pair.Vertex.ProgramName);
        Assert.Equal("another-unrelated-program-label", pair.Pixel.ProgramName);
        Assert.Equal("main", pair.Vertex.EntryPoint);
        Assert.Equal("shadeFragment", pair.Pixel.EntryPoint);
    }

    [Fact]
    public void Link_rejects_programs_in_pixel_then_vertex_order() {
        Xbox360ShaderProgram vertex = Xbox360ShaderProgramFactory.FromCompilation(
            Xbox360ShaderProgramTestSupport.TransformVertex);
        Xbox360ShaderProgram pixel = Xbox360ShaderProgramFactory.FromCompilation(
            Xbox360ShaderProgramTestSupport.TexturedPixel);

        Action link = () => Xbox360ShaderProgramPair.Link(pixel, vertex);

        Assert.Throws<InvalidDataException>(link);
    }

    [Fact]
    public void Link_rejects_a_variant_mismatch() {
        Xbox360ShaderCompileResult pixelCompilation = Xbox360ShaderProgramTestSupport.Reidentify(
            Xbox360ShaderProgramTestSupport.TexturedPixel, variant: "debug");
        Xbox360ShaderProgram vertex = Xbox360ShaderProgramFactory.FromCompilation(
            Xbox360ShaderProgramTestSupport.TransformVertex);
        Xbox360ShaderProgram pixel = Xbox360ShaderProgramFactory.FromCompilation(pixelCompilation);

        Action link = () => Xbox360ShaderProgramPair.Link(vertex, pixel);

        Assert.Throws<InvalidDataException>(link);
    }

    [Fact]
    public void Link_rejects_a_tool_identity_mismatch() {
        Xbox360ShaderCompileResult pixelCompilation = Xbox360ShaderProgramTestSupport.Reidentify(
            Xbox360ShaderProgramTestSupport.TexturedPixel,
            toolIdentity: new string('A', 64));
        Xbox360ShaderProgram vertex = Xbox360ShaderProgramFactory.FromCompilation(
            Xbox360ShaderProgramTestSupport.TransformVertex);
        Xbox360ShaderProgram pixel = Xbox360ShaderProgramFactory.FromCompilation(pixelCompilation);

        Action link = () => Xbox360ShaderProgramPair.Link(vertex, pixel);

        Assert.Throws<InvalidDataException>(link);
    }

    [Fact]
    public void Link_rejects_a_pixel_input_without_a_vertex_varying() {
        Xbox360ShaderCompileResult source = Xbox360ShaderProgramTestSupport.TransformVertex;
        ShaderVertexElement[] outputs = source.ProgramDefinition.Outputs
            .Where(output => output.Semantic != "LOCATION" || output.Index != 0)
            .Select(output => new ShaderVertexElement(output.Semantic, output.Index, output.Format))
            .ToArray();
        string metadata = Xbox360ShaderProgramTestSupport.EditMetadata(source, root => {
            ((JsonObject)root["varying_map"]).Remove("0");
            JsonArray nativeOutputs = (JsonArray)root["outputs"];
            for (int index = nativeOutputs.Count - 1; index >= 0; index--) {
                JsonNode item = nativeOutputs[index];
                if (item["kind"]?.GetValue<string>() == "varying"
                    && item["location"]?.GetValue<int>() == 0) {
                    nativeOutputs.RemoveAt(index);
                }
            }
        });
        Xbox360ShaderCompileResult missingOutput = Xbox360ShaderProgramTestSupport.CloneResult(
            source,
            programDefinition: Xbox360ShaderProgramTestSupport.CloneDefinition(source.ProgramDefinition, outputs: outputs),
            nativeMetadataJson: metadata);
        Xbox360ShaderProgram vertex = Xbox360ShaderProgramFactory.FromCompilation(missingOutput);
        Xbox360ShaderProgram pixel = Xbox360ShaderProgramFactory.FromCompilation(
            Xbox360ShaderProgramTestSupport.TexturedPixel);

        Action link = () => Xbox360ShaderProgramPair.Link(vertex, pixel);

        Assert.Throws<InvalidDataException>(link);
    }

    [Fact]
    public void Link_rejects_a_varying_width_mismatch() {
        Xbox360ShaderCompileResult source = Xbox360ShaderProgramTestSupport.TransformVertex;
        ShaderVertexElement[] outputs = source.ProgramDefinition.Outputs.Select(output =>
            output.Semantic == "LOCATION" && output.Index == 0
                ? new ShaderVertexElement(output.Semantic, output.Index, "float2")
                : new ShaderVertexElement(output.Semantic, output.Index, output.Format)).ToArray();
        Xbox360ShaderProgram vertex = Xbox360ShaderProgramFactory.FromCompilation(
            Xbox360ShaderProgramTestSupport.CloneResult(source,
                programDefinition: Xbox360ShaderProgramTestSupport.CloneDefinition(source.ProgramDefinition, outputs: outputs)));
        Xbox360ShaderProgram pixel = Xbox360ShaderProgramFactory.FromCompilation(
            Xbox360ShaderProgramTestSupport.TexturedPixel);

        Action link = () => Xbox360ShaderProgramPair.Link(vertex, pixel);

        Assert.Throws<InvalidDataException>(link);
    }
}
