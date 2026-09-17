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

        if (args.Length >= 4 && string.Equals(args[0], "--write-catalog", StringComparison.OrdinalIgnoreCase)) {
            return WriteCatalog(args);
        }

        Console.WriteLine("helengine.x360.builder --describe");
        Console.WriteLine("helengine.x360.builder --write-catalog <output-directory> <startup-scene-id> <scene-id>=<cooked-path> ...");
        return 0;
    }

    /// <summary>Writes a scene catalog from explicit pairs, without an editor build.</summary>
    /// <remarks>
    /// The catalog is otherwise only produced inside a build, which needs a cooked project. This
    /// makes the generated half of the console's scene path reproducible on its own, so a change
    /// to the table can be compiled and booted without cooking anything first.
    /// </remarks>
    /// <param name="args">Output directory, startup scene id, then one <c>id=path</c> pair per scene.</param>
    /// <returns>Zero when the catalog was written.</returns>
    static int WriteCatalog(string[] args) {
        List<KeyValuePair<string, string>> entries = [];
        for (int index = 3; index < args.Length; index++) {
            string pair = args[index];
            int separator = pair.IndexOf('=');
            if (separator <= 0 || separator == pair.Length - 1) {
                Console.Error.WriteLine($"Expected <scene-id>=<cooked-path>, got '{pair}'.");
                return 2;
            }
            entries.Add(new KeyValuePair<string, string>(pair[..separator], pair[(separator + 1)..]));
        }
        if (entries.Count == 0) {
            Console.Error.WriteLine("At least one <scene-id>=<cooked-path> pair is required.");
            return 2;
        }
        try {
            string written = new Xbox360StartupCatalogWriter().Write(args[1], args[2], entries);
            Console.WriteLine(written);
            return 0;
        } catch (Exception exception) {
            Console.Error.WriteLine(exception.Message);
            return 1;
        }
    }
}
