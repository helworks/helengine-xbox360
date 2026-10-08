using helengine;

namespace helengine.x360.builder.tests;

public sealed class ShaderSharedContractTests {
    [Fact]
    public void Existing_target_ordinals_are_preserved() {
        Assert.Equal(0, (int)ShaderCompileTarget.DirectX9);
        Assert.Equal(1, (int)ShaderCompileTarget.DirectX11);
        Assert.Equal(2, (int)ShaderCompileTarget.DirectX12);
        Assert.Equal(3, (int)ShaderCompileTarget.Vulkan);
        Assert.Equal(4, (int)ShaderCompileTarget.Metal);
        Assert.Equal(5, (int)ShaderCompileTarget.PsVita);
        Assert.Equal(6, (int)ShaderCompileTarget.WiiU);
        Assert.Equal(7, (int)ShaderCompileTarget.Ps3);
    }

    [Fact]
    public void Xbox360_is_the_appended_target_with_a_round_tripping_descriptor() {
        ShaderCompileTarget xbox360 = Enum.Parse<ShaderCompileTarget>("Xbox360");
        Assert.Equal(8, (int)xbox360);

        ShaderTargetDescriptor descriptor = ShaderTargetDescriptors.Get(xbox360);
        Assert.Equal("x360", descriptor.Name);
        Assert.Equal("HEL_API_X360", descriptor.DefineName);
        Assert.True(ShaderTargetDescriptors.TryGetByName(" X360 ", out ShaderTargetDescriptor parsed));
        Assert.Same(descriptor, parsed);
        Assert.Equal(Enum.GetValues<ShaderCompileTarget>().Length, ShaderTargetDescriptors.All.Count);
        Assert.Equal(ShaderTargetDescriptors.All.Count, ShaderTargetDescriptors.All.Select(item => item.Name).Distinct(StringComparer.Ordinal).Count());
        Assert.Equal(ShaderTargetDescriptors.All.Count, ShaderTargetDescriptors.All.Select(item => item.DefineName).Distinct(StringComparer.Ordinal).Count());
        Assert.Equal("ps3", ShaderTargetDescriptors.Get(ShaderCompileTarget.Ps3).Name);
        Assert.Equal("HEL_API_PS3", ShaderTargetDescriptors.Get(ShaderCompileTarget.Ps3).DefineName);
    }

    [Fact]
    public void Compilation_assembly_exposes_the_optional_backend_cache_policy() {
        Assert.True(typeof(IShaderBackendCachePolicy).IsInterface);
        Assert.Equal(typeof(bool), typeof(IShaderBackendCachePolicy).GetProperty(nameof(IShaderBackendCachePolicy.UseSharedCache))?.PropertyType);
    }

    [Fact]
    public void Opt_out_backend_bypasses_cache_and_receives_each_fresh_compile_input() {
        var cache = new ThrowingCompileCache();
        var resolver = new MutableIncludeResolver { IncludeSource = "include-one" };
        var backend = new PolicyRecordingBackend(ShaderCompileTarget.Xbox360, useSharedCache: false) { ToolState = "compiler-one" };
        ShaderCompileService service = CreateService(cache, resolver, backend);

        ShaderCompileResult first = service.Compile(CreateRequest("source-one", "variant-one", optimize: false));
        resolver.IncludeSource = "include-two";
        backend.ToolState = "compiler-two";
        ShaderCompileResult second = service.Compile(CreateRequest("source-two", "variant-two", optimize: true));

        Assert.NotSame(first, second);
        Assert.Equal(2, backend.Invocations.Count);
        Assert.Equal(0, cache.LookupCount);
        Assert.Equal(0, cache.StoreCount);
        Assert.Equal(new BackendInvocation("source-one", "include-one", false, "variant-one", "compiler-one"), backend.Invocations[0]);
        Assert.Equal(new BackendInvocation("source-two", "include-two", true, "variant-two", "compiler-two"), backend.Invocations[1]);
    }

    [Fact]
    public void Failed_opt_out_compilation_is_not_reused_by_a_later_successful_call() {
        var cache = new ThrowingCompileCache();
        var backend = new PolicyRecordingBackend(ShaderCompileTarget.Xbox360, useSharedCache: false);
        backend.SuccessSequence.Enqueue(false);
        backend.SuccessSequence.Enqueue(true);
        ShaderCompileService service = CreateService(cache, new MutableIncludeResolver(), backend);
        ShaderCompileRequest request = CreateRequest("same-source", "same-variant", optimize: true);

        ShaderCompileResult failed = service.Compile(request);
        ShaderCompileResult succeeded = service.Compile(request);

        Assert.False(failed.Success);
        Assert.True(succeeded.Success);
        Assert.NotSame(failed, succeeded);
        Assert.Equal(2, backend.Invocations.Count);
        Assert.Equal(0, cache.LookupCount);
        Assert.Equal(0, cache.StoreCount);
    }

    [Theory]
    [InlineData(false)]
    [InlineData(true)]
    public void Default_and_explicit_true_backends_continue_to_use_shared_cache(bool explicitTruePolicy) {
        var cache = new RecordingCompileCache();
        var resolver = new MutableIncludeResolver();
        IShaderBackend backend = explicitTruePolicy
            ? new PolicyRecordingBackend(ShaderCompileTarget.Xbox360, useSharedCache: true)
            : new RecordingBackend(ShaderCompileTarget.Xbox360);
        ShaderCompileService service = CreateService(cache, resolver, backend);
        ShaderCompileRequest request = CreateRequest("same-source", "same-variant", optimize: true);

        ShaderCompileResult first = service.Compile(request);
        ShaderCompileResult second = service.Compile(request);

        Assert.Same(first, second);
        Assert.Single(((RecordingBackend)backend).Invocations);
        Assert.Equal(2, cache.LookupCount);
        Assert.Equal(1, cache.StoreCount);
    }

    [Fact]
    public void Null_request_and_unregistered_target_fail_before_cache_access() {
        var cache = new ThrowingCompileCache();
        ShaderCompileService service = new ShaderCompileService(new MutableIncludeResolver(), cache, new ShaderSourceHasher());

        Assert.Throws<ArgumentNullException>(() => service.Compile(null));
        Assert.Throws<InvalidOperationException>(() => service.Compile(CreateRequest("source", "variant", true, ShaderCompileTarget.DirectX9)));
        Assert.Equal(0, cache.LookupCount);
        Assert.Equal(0, cache.StoreCount);
    }

    static ShaderCompileService CreateService(IShaderCompileCache cache, IShaderIncludeResolver resolver, IShaderBackend backend) {
        var service = new ShaderCompileService(resolver, cache, new ShaderSourceHasher());
        service.RegisterBackend(backend);
        return service;
    }

    static ShaderCompileRequest CreateRequest(string source, string variant, bool optimize, ShaderCompileTarget target = ShaderCompileTarget.Xbox360) {
        return new ShaderCompileRequest(
            new ShaderSourceInfo("shader.hlsl", source),
            "program",
            "VSMain",
            ShaderStage.Vertex,
            target,
            new ShaderModel(4, 0),
            variant,
            Array.Empty<ShaderDefine>(),
            new ShaderCompileOptions(new ShaderBindingPolicy(0, 0, 0, 0, 0), false, optimize, false));
    }

    sealed record BackendInvocation(string Source, string IncludeSource, bool Optimize, string Variant, string ToolState);

    sealed class MutableIncludeResolver : IShaderIncludeResolver {
        public string IncludeSource { get; set; } = "include-default";

        public ShaderIncludeResult Resolve(string requestingFile, string includePath) {
            return new ShaderIncludeResult(includePath, IncludeSource);
        }
    }

    class RecordingBackend : IShaderBackend {
        public RecordingBackend(ShaderCompileTarget target) {
            Target = target;
            Capabilities = new ShaderBackendCapabilities(new ShaderModel(4, 0), new ShaderModel(4, 0), [ShaderStage.Vertex], false);
        }

        public ShaderCompileTarget Target { get; }
        public ShaderBackendCapabilities Capabilities { get; }
        public string ToolState { get; set; } = "compiler-default";
        public List<BackendInvocation> Invocations { get; } = new List<BackendInvocation>();
        public Queue<bool> SuccessSequence { get; } = new Queue<bool>();

        public ShaderCompileResult Compile(ShaderCompileRequest request, IShaderIncludeResolver includeResolver) {
            ShaderIncludeResult include = includeResolver.Resolve(request.Source.Path, "shared.inc");
            Invocations.Add(new BackendInvocation(request.Source.Source, include.Source, request.Options.Optimize, request.Variant, ToolState));
            bool success = SuccessSequence.Count == 0 || SuccessSequence.Dequeue();
            var definition = new ShaderProgramDefinition(request.ProgramName, request.Stage, request.EntryPoint,
                Array.Empty<ShaderBinding>(), Array.Empty<ShaderVertexElement>(), Array.Empty<ShaderVertexElement>(), Array.Empty<ShaderVariant>());
            var binary = new ShaderCompiledBinary(request.Target, request.Stage, request.EntryPoint, request.Variant, [1]);
            return new ShaderCompileResult(request, definition, binary, Array.Empty<ShaderCompileDiagnostic>(), success);
        }
    }

    sealed class PolicyRecordingBackend : RecordingBackend, IShaderBackendCachePolicy {
        public PolicyRecordingBackend(ShaderCompileTarget target, bool useSharedCache) : base(target) {
            UseSharedCache = useSharedCache;
        }

        public bool UseSharedCache { get; }
    }

    sealed class ThrowingCompileCache : IShaderCompileCache {
        public int LookupCount { get; private set; }
        public int StoreCount { get; private set; }

        public bool TryGet(ShaderCompileCacheKey key, out ShaderCompileResult result) {
            LookupCount++;
            throw new InvalidOperationException("Opt-out backend must not query the shared cache.");
        }

        public void Store(ShaderCompileCacheKey key, ShaderCompileResult result) {
            StoreCount++;
            throw new InvalidOperationException("Opt-out backend must not store in the shared cache.");
        }
    }

    sealed class RecordingCompileCache : IShaderCompileCache {
        ShaderCompileResult stored;

        public int LookupCount { get; private set; }
        public int StoreCount { get; private set; }

        public bool TryGet(ShaderCompileCacheKey key, out ShaderCompileResult result) {
            LookupCount++;
            result = stored;
            return stored != null;
        }

        public void Store(ShaderCompileCacheKey key, ShaderCompileResult result) {
            StoreCount++;
            stored = result;
        }
    }
}
