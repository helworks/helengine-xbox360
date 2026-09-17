namespace helengine.x360.builder;

/// <summary>Console entry point used only for a manual smoke check of the builder assembly.</summary>
/// <remarks>
/// The editor never runs this as a program; it loads the assembly and reflects for the builder
/// type. This exists so a person can confirm the assembly loads and self-describes without
/// starting the editor.
/// </remarks>
public static class Program {
    /// <summary>Prints the builder identity when invoked with <c>--describe</c>; otherwise prints usage.</summary>
    /// <param name="args">Command-line arguments.</param>
    /// <returns>Zero on success.</returns>
    public static int Main(string[] args) {
        if (args.Length == 1 && string.Equals(args[0], "--describe", StringComparison.OrdinalIgnoreCase)) {
            Xbox360PlatformAssetBuilder builder = new();
            Console.WriteLine(builder.Descriptor.BuilderId);
            Console.WriteLine(builder.Descriptor.TargetPlatformId);
            Console.WriteLine(builder.Definition.DisplayName);
            Console.WriteLine(builder.Definition.CodegenProfiles[0].Endianness);
            return 0;
        }

        Console.WriteLine("helengine.x360.builder --describe");
        return 0;
    }
}
