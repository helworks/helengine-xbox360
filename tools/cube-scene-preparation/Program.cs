try {
    if (args.Length != 2) throw new ArgumentException("Expected staged project root and mesh HLSL path.");
    CubeTestShaderPreparation.Prepare(args[0], args[1]);
    return 0;
} catch (Exception exception) {
    Console.Error.WriteLine(exception.Message);
    return 1;
}
