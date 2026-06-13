namespace Zlink.Framework.UnitTests.Samples;

public sealed class RegressionTests
{
    [Fact]
    public void Bingo_Uses_RegistryBacked_Defaults_Without_Sample_Metadata_Store()
    {
        var sampleRoot = ResolveSampleRoot("Bingo");

        AssertNoSampleRouteStore(sampleRoot);
        AssertNoSampleMetadataStore(sampleRoot);
        AssertSampleUsesRegistryDiscovery(sampleRoot);
        AssertSessionServerUsesActorGateway(sampleRoot, allowRouteMeshChannel: false);
        AssertSessionHandlersDoNotResolveActorRemoteAddresses(sampleRoot);
        AssertEnsureActorHandlersReturnActorGatewayRemoteAddresses(sampleRoot);
        AssertNoSampleSessionRelayJson(sampleRoot);
        AssertSessionPayloadPolicy(sampleRoot);
        AssertUsesFrameworkSessionPacketDispatcher(sampleRoot);
    }

    [Fact]
    public void Bingo_And_TicTacToe_Samples_Implement_Actor_Lifecycle_Spec()
    {
        AssertActorLifecycleSpec(
            ResolveSampleRoot("Bingo"),
            "Server/Play/Adapters/ZLink/Spots/BingoEntrySpot.cs",
            "Server/Play/Adapters/ZLink/Spots/BingoRoom.cs",
            "Server/Play/Adapters/ZLink/Actors/PlayerActor.cs",
            "Server/Session/Sessions/BingoSession.cs");
        AssertActorLifecycleSpec(
            ResolveSampleRoot("TicTacToe"),
            "Server/Play/Adapters/ZLink/Spots/PlayEntrySpot.cs",
            "Server/Play/Adapters/ZLink/Spots/TicTacToeGame.cs",
            "Server/Play/Adapters/ZLink/Actors/PlayActor.cs",
            "Server/Play/Adapters/ZLink/Sessions/PlaySession.cs");
    }

    [Fact]
    public void Aggregate_Sample_Runners_Keep_Actor_Lifecycle_Runtime_Gate()
    {
        var samplesRoot = ResolveSamplesRoot();
        var bashRunner = File.ReadAllText(Path.Combine(samplesRoot, "run_samples.sh"));
        var powerShellRunner = File.ReadAllText(Path.Combine(samplesRoot, "run_samples.ps1"));

        Assert.Contains(
            "ActorLifecycleTests.EntrySpot_DestroyActorAsync_Removes_EntryOwned_Actor_Without_Left_Callback",
            bashRunner,
            StringComparison.Ordinal);
        Assert.Contains("dotnet actor lifecycle sample gate completed", bashRunner, StringComparison.Ordinal);
        Assert.Contains(
            "ActorLifecycleTests.EntrySpot_DestroyActorAsync_Removes_EntryOwned_Actor_Without_Left_Callback",
            powerShellRunner,
            StringComparison.Ordinal);
        Assert.Contains("dotnet actor lifecycle sample gate completed", powerShellRunner, StringComparison.Ordinal);
    }

    [Fact]
    public void DotNet_Docs_Keep_Actor_Destroy_Entry_Owned()
    {
        var dotnetRoot = ResolveDotnetRoot();
        var docs = EnumerateMarkdownFiles(Path.Combine(dotnetRoot, "doc", "guide"))
            .Concat(EnumerateMarkdownFiles(Path.Combine(dotnetRoot, "doc", "spec")))
            .Concat(EnumerateMarkdownFiles(Path.Combine(dotnetRoot, "doc", "internals")))
            .Concat(Directory.EnumerateFiles(Path.Combine(dotnetRoot, "samples"), "README.md", SearchOption.AllDirectories))
            .Concat(Directory.EnumerateFiles(Path.Combine(dotnetRoot, "samples"), "README.ko.md", SearchOption.AllDirectories))
            .ToArray();
        var offenders = new List<string>();
        (string Needle, string Reason)[] forbidden =
        [
            ("destroyActor(", "lower camel destroy API"),
            ("destroyActorAsync", "lower camel async destroy API"),
            ("destroy_actor", "snake case destroy API"),
            ("OnActorLeft", "legacy PascalCase left callback"),
            ("onActorLeft", "legacy lower camel left callback"),
            ("on_actor_left", "legacy snake case left callback"),
            ("OnCreateActor", "legacy PascalCase create callback"),
            ("on_actor_created", "legacy snake case create callback"),
            ("onPostActorJoined", "legacy post actor joined callback"),
            ("disconnect -> destroy", "disconnect-to-destroy arrow wording"),
            ("자동 삭제", "automatic deletion wording")
        ];

        foreach (var file in docs)
        {
            var text = File.ReadAllText(file);
            foreach (var (needle, reason) in forbidden)
            {
                if (text.Contains(needle, StringComparison.Ordinal))
                {
                    offenders.Add($"{Path.GetRelativePath(dotnetRoot, file)}: {reason}");
                }
            }
        }

        var actorSpec = File.ReadAllText(Path.Combine(dotnetRoot, "doc", "spec", "aspnet-core-actor.ko.md"));
        var actorGuide = File.ReadAllText(Path.Combine(dotnetRoot, "doc", "guide", "06-actor-session.ko.md"));
        Assert.Contains("DestroyActorAsync: Entry Spot", actorSpec, StringComparison.Ordinal);
        Assert.Contains("session 종료가 곧 actor leave 나 actor destroy 를 뜻하지 않는다", actorSpec, StringComparison.Ordinal);
        Assert.Contains("IZLinkEntrySpotContext.DestroyActorAsync(actor)", actorGuide, StringComparison.Ordinal);
        Assert.Contains("lifecycle callback 을", actorGuide, StringComparison.Ordinal);
        Assert.Contains("호출하지 않고 native actor ref", actorGuide, StringComparison.Ordinal);
        Assert.Empty(offenders.Order(StringComparer.Ordinal));
    }

    [Fact]
    public void TicTacToe_SessionGateway_Sample_Is_Removed()
    {
        var samplesRoot = ResolveSamplesRoot();
        var dotnetRoot = Directory.GetParent(samplesRoot)!.FullName;
        var sampleRoot = Path.Combine(samplesRoot, "TicTacToe.SessionGateway");
        var solution = Path.Combine(dotnetRoot, "Zlink.Framework.sln");
        var solutionText = File.ReadAllText(solution);

        Assert.False(
            Directory.Exists(sampleRoot),
            "TicTacToe keeps only the direct Api + Play sample. The SessionGateway variant must not be restored.");
        Assert.DoesNotContain("TicTacToe.SessionGateway", solutionText, StringComparison.Ordinal);
        Assert.DoesNotContain("samples\\TicTacToe.SessionGateway", solutionText, StringComparison.Ordinal);
    }

    [Fact]
    public void TicTacToe_Play_Session_Uses_FrameworkPayloadLifetimePolicy()
    {
        var sampleRoot = ResolveSampleRoot("TicTacToe");

        AssertSessionPayloadPolicy(sampleRoot);
    }

    [Fact]
    public void Bingo_Uses_Protobuf_And_TicTacToe_Uses_MessagePack_Sample_Payloads()
    {
        AssertSampleUsesProtobufPayloads(ResolveSampleRoot("Bingo"));
        AssertSampleUsesMessagePackPayloads(ResolveSampleRoot("TicTacToe"));
    }

    private static void AssertSampleUsesProtobufPayloads(string sampleRoot)
    {
        var sourceFiles = EnumerateSourceFiles(sampleRoot).ToArray();
        var projectFiles = Directory
            .EnumerateFiles(sampleRoot, "*.csproj", SearchOption.AllDirectories)
            .Where(static path => !path.Contains($"{Path.DirectorySeparatorChar}bin{Path.DirectorySeparatorChar}", StringComparison.Ordinal)
                && !path.Contains($"{Path.DirectorySeparatorChar}obj{Path.DirectorySeparatorChar}", StringComparison.Ordinal))
            .ToArray();
        var protoFiles = Directory
            .EnumerateFiles(sampleRoot, "*.proto", SearchOption.AllDirectories)
            .Where(static path => !path.Contains($"{Path.DirectorySeparatorChar}bin{Path.DirectorySeparatorChar}", StringComparison.Ordinal)
                && !path.Contains($"{Path.DirectorySeparatorChar}obj{Path.DirectorySeparatorChar}", StringComparison.Ordinal))
            .ToArray();
        var allText = string.Join(
            Environment.NewLine,
            sourceFiles.Concat(projectFiles).Concat(protoFiles).Select(File.ReadAllText));

        Assert.NotEmpty(protoFiles);
        Assert.Contains("AddProtobuf", allText, StringComparison.Ordinal);
        Assert.Contains("Stream.Connector.Protobuf", allText, StringComparison.Ordinal);
        Assert.Contains("Zlink.Codecs.Protobuf", allText, StringComparison.Ordinal);
        Assert.DoesNotContain("MessagePack", allText, StringComparison.Ordinal);
        Assert.DoesNotContain("MsgPack", allText, StringComparison.Ordinal);
    }

    private static void AssertSampleUsesMessagePackPayloads(string sampleRoot)
    {
        var sourceFiles = EnumerateSourceFiles(sampleRoot).ToArray();
        var projectFiles = Directory
            .EnumerateFiles(sampleRoot, "*.csproj", SearchOption.AllDirectories)
            .Where(static path => !path.Contains($"{Path.DirectorySeparatorChar}bin{Path.DirectorySeparatorChar}", StringComparison.Ordinal)
                && !path.Contains($"{Path.DirectorySeparatorChar}obj{Path.DirectorySeparatorChar}", StringComparison.Ordinal))
            .ToArray();
        var protoFiles = Directory
            .EnumerateFiles(sampleRoot, "*.proto", SearchOption.AllDirectories)
            .Where(static path => !path.Contains($"{Path.DirectorySeparatorChar}bin{Path.DirectorySeparatorChar}", StringComparison.Ordinal)
                && !path.Contains($"{Path.DirectorySeparatorChar}obj{Path.DirectorySeparatorChar}", StringComparison.Ordinal))
            .ToArray();
        var allText = string.Join(
            Environment.NewLine,
            sourceFiles.Concat(projectFiles).Select(File.ReadAllText));

        Assert.Empty(protoFiles);
        Assert.Contains("MessagePackObject", allText, StringComparison.Ordinal);
        Assert.Contains("AddMessagePack", allText, StringComparison.Ordinal);
        Assert.Contains("Stream.Connector.MessagePack", allText, StringComparison.Ordinal);
        Assert.Contains("Zlink.Codecs.MessagePack", allText, StringComparison.Ordinal);
        Assert.DoesNotContain("Google.Protobuf", allText, StringComparison.Ordinal);
        Assert.DoesNotContain("Grpc.Tools", allText, StringComparison.Ordinal);
        Assert.DoesNotContain("AddProtobuf", allText, StringComparison.Ordinal);
        Assert.DoesNotContain("FromProto", allText, StringComparison.Ordinal);
        Assert.DoesNotContain("ToProto", allText, StringComparison.Ordinal);
    }

    private static void AssertNoSampleRouteStore(string sampleRoot)
    {
        var sourceFiles = EnumerateSourceFiles(sampleRoot).ToArray();
        var fileNames = sourceFiles.Select(Path.GetFileName).ToHashSet(StringComparer.Ordinal);

        Assert.DoesNotContain("RegistryRemoteAddressStore.cs", fileNames);
        Assert.DoesNotContain("RegistryRemoteAddressPublisher.cs", fileNames);
        Assert.DoesNotContain("SpotRouteContracts.cs", fileNames);

        foreach (var file in sourceFiles)
        {
            var text = File.ReadAllText(file);
            Assert.DoesNotContain("RegistryRemoteAddressStore", text, StringComparison.Ordinal);
            Assert.DoesNotContain("RegistryRemoteAddressPublisher", text, StringComparison.Ordinal);
            Assert.DoesNotContain("AddActorRemoteAddressResolver<RegistryRemoteAddressStore>", text, StringComparison.Ordinal);
            Assert.DoesNotContain("AddSpotRemoteAddressResolver<RegistryRemoteAddressStore>", text, StringComparison.Ordinal);
            Assert.DoesNotContain("BindInitialActorRemoteAddressesAsync", text, StringComparison.Ordinal);
        }
    }

    private static void AssertNoSampleMetadataStore(string sampleRoot)
    {
        var sourceFiles = EnumerateSourceFiles(sampleRoot).ToArray();
        var fileNames = sourceFiles.Select(Path.GetFileName).ToHashSet(StringComparer.Ordinal);

        Assert.DoesNotContain("ActorSessionLocationStore.cs", fileNames);
        Assert.DoesNotContain("FileRegistryDiscoveryMetadata.cs", fileNames);
        Assert.DoesNotContain("InMemoryRegistryDiscoveryMetadata.cs", fileNames);
        Assert.DoesNotContain("IRegistryDiscoveryMetadata.cs", fileNames);
        Assert.DoesNotContain("RegistryMetadataEntry.cs", fileNames);
        Assert.DoesNotContain("SampleRuntime.cs", fileNames);

        foreach (var file in sourceFiles)
        {
            var text = File.ReadAllText(file);
            Assert.DoesNotContain("RegistryActorSessionLocationStore", text, StringComparison.Ordinal);
            Assert.DoesNotContain("IRegistryDiscoveryMetadata", text, StringComparison.Ordinal);
            Assert.DoesNotContain("FileRegistryDiscoveryMetadata", text, StringComparison.Ordinal);
            Assert.DoesNotContain("OpenRegistryMetadata", text, StringComparison.Ordinal);
            Assert.DoesNotContain("AddActorSessionBindingStore", text, StringComparison.Ordinal);
            Assert.DoesNotContain("IZLinkActorSessionClient", text, StringComparison.Ordinal);
            Assert.DoesNotContain("AddActorSessionBindingStore<RegistryActorSessionLocationStore>", text, StringComparison.Ordinal);
        }
    }

    private static void AssertNoSampleSessionRelayJson(string sampleRoot)
    {
        var sourceFiles = EnumerateSourceFiles(sampleRoot).ToArray();
        var fileNames = sourceFiles.Select(Path.GetFileName).ToHashSet(StringComparer.Ordinal);

        Assert.DoesNotContain("SessionRelayJson.cs", fileNames);
        foreach (var file in sourceFiles)
        {
            var text = File.ReadAllText(file);
            Assert.DoesNotContain("SessionRelayJson", text, StringComparison.Ordinal);
        }
    }

    private static void AssertSessionPayloadPolicy(string sampleRoot)
    {
        var sessionRoots = EnumerateSessionRoots(sampleRoot).ToArray();
        Assert.NotEmpty(sessionRoots);

        var sourceFiles = sessionRoots
            .SelectMany(static root => EnumerateSourceFiles(root))
            .ToArray();
        Assert.NotEmpty(sourceFiles);

        foreach (var file in sourceFiles)
        {
            var text = File.ReadAllText(file);
            Assert.DoesNotContain("payload.FromJson<", text, StringComparison.Ordinal);
            Assert.DoesNotContain("payload.Move()", text, StringComparison.Ordinal);
            Assert.DoesNotContain("using (payload)", text, StringComparison.Ordinal);
            Assert.DoesNotContain("await using (payload)", text, StringComparison.Ordinal);
            Assert.DoesNotContain(".Dispose()", text, StringComparison.Ordinal);
            Assert.DoesNotContain("IZLinkSessionActor? Actor", text, StringComparison.Ordinal);
            Assert.DoesNotContain("Actor { get; set; }", text, StringComparison.Ordinal);
        }
    }

    private static void AssertUsesFrameworkSessionPacketDispatcher(string sampleRoot)
    {
        var sourceFiles = EnumerateSourceFiles(Path.Combine(sampleRoot, "Server", "Session")).ToArray();
        var allText = string.Join(Environment.NewLine, sourceFiles.Select(File.ReadAllText));

        Assert.Contains("IZLinkSessionPacketDispatcher<", allText, StringComparison.Ordinal);
        Assert.Contains("IZLinkSessionPacketHandler<", allText, StringComparison.Ordinal);
        Assert.DoesNotContain("IBingoSessionHandler", allText, StringComparison.Ordinal);
        Assert.DoesNotContain("ISessionRelayPacketHandler", allText, StringComparison.Ordinal);
        Assert.DoesNotContain("ToDictionary(static handler => handler.PacketName", allText, StringComparison.Ordinal);
        Assert.DoesNotContain("BingoSessionContext", allText, StringComparison.Ordinal);
        Assert.DoesNotContain("SessionRelayPacketContext", allText, StringComparison.Ordinal);
        Assert.DoesNotContain("SessionRelayState", allText, StringComparison.Ordinal);
    }

    private static void AssertSampleUsesRegistryDiscovery(string sampleRoot)
    {
        var allText = string.Join(
            Environment.NewLine,
            EnumerateSourceFiles(sampleRoot).Select(File.ReadAllText));

        Assert.Contains(
            "UseDiscovery(discovery => discovery.AddRegistryEndpoint(topology.RegistryRouterEndpoint))",
            allText,
            StringComparison.Ordinal);
        Assert.DoesNotContain("UseRegistryActorRemoteAddresses", allText, StringComparison.Ordinal);
        Assert.DoesNotContain("UseRegistryActorSessionBindings", allText, StringComparison.Ordinal);
        Assert.DoesNotContain("IZLinkActorSessionClient", allText, StringComparison.Ordinal);
    }

    private static void AssertSessionServerUsesActorGateway(
        string sampleRoot,
        bool allowRouteMeshChannel)
    {
        var sessionHostFactory = Directory
            .EnumerateFiles(Path.Combine(sampleRoot, "Server", "Session"), "*HostFactory.cs", SearchOption.AllDirectories)
            .Single();
        var text = File.ReadAllText(sessionHostFactory);

        Assert.Contains("AddSpotMesh", text, StringComparison.Ordinal);
        Assert.Contains("EnableRouter", text, StringComparison.Ordinal);
        Assert.Contains("AttachActorGateway", text, StringComparison.Ordinal);
        if (!allowRouteMeshChannel)
        {
            Assert.DoesNotContain("AddRouteMeshChannel", text, StringComparison.Ordinal);
        }
        Assert.DoesNotContain("AddScoped<IBingoSessionHandler", text, StringComparison.Ordinal);
        Assert.DoesNotContain("AddScoped<ISessionRelayPacketHandler", text, StringComparison.Ordinal);
    }

    private static void AssertSessionHandlersDoNotResolveActorRemoteAddresses(string sampleRoot)
    {
        var sessionRoot = Path.Combine(sampleRoot, "Server", "Session");
        var sourceFiles = EnumerateSourceFiles(sessionRoot)
            .Where(static file => file.EndsWith("Handler.cs", StringComparison.Ordinal))
            .ToArray();

        Assert.NotEmpty(sourceFiles);

        foreach (var file in sourceFiles)
        {
            var text = File.ReadAllText(file);
            Assert.DoesNotContain("IZLinkActorRemoteAddressResolver", text, StringComparison.Ordinal);
            Assert.DoesNotContain("ResolveActorRemoteAddressAsync", text, StringComparison.Ordinal);
        }
    }

    private static void AssertEnsureActorHandlersReturnActorGatewayRemoteAddresses(string sampleRoot)
    {
        var playRoot = Path.Combine(sampleRoot, "Server", "Play");
        var sourceFiles = EnumerateSourceFiles(playRoot)
            .Where(static file => Path.GetFileName(file).Equals(
                "EnsurePlayerActorHandler.cs",
                StringComparison.Ordinal))
            .ToArray();

        Assert.NotEmpty(sourceFiles);

        foreach (var file in sourceFiles)
        {
            var text = File.ReadAllText(file);
            Assert.DoesNotContain("IZLinkActorRemoteAddressResolver", text, StringComparison.Ordinal);
            Assert.DoesNotContain("ResolveActorRemoteAddressAsync", text, StringComparison.Ordinal);
            Assert.DoesNotContain("GetRemoteAddressAsync", text, StringComparison.Ordinal);
            Assert.Contains("JoinEntrySpot", text, StringComparison.Ordinal);
            Assert.Contains("ActorRefSnapshot", text, StringComparison.Ordinal);
        }
    }

    private static void AssertActorLifecycleSpec(
        string sampleRoot,
        string entrySpotRelativePath,
        string userSpotRelativePath,
        string actorRelativePath,
        string sessionRelativePath)
    {
        var entrySpot = File.ReadAllText(Path.Combine(sampleRoot, entrySpotRelativePath));
        var userSpot = File.ReadAllText(Path.Combine(sampleRoot, userSpotRelativePath));
        var actor = File.ReadAllText(Path.Combine(sampleRoot, actorRelativePath));
        var session = File.ReadAllText(Path.Combine(sampleRoot, sessionRelativePath));

        Assert.Contains("onCreateActor", entrySpot, StringComparison.Ordinal);
        Assert.Contains("onJoinActor", entrySpot, StringComparison.Ordinal);
        Assert.Contains("onLeaveActor", entrySpot, StringComparison.Ordinal);
        Assert.Contains("onDisconnectActor", entrySpot, StringComparison.Ordinal);
        Assert.Contains("DestroyActorAsync", entrySpot, StringComparison.Ordinal);
        Assert.Contains("DestroyAfterEntrySpotJoin", entrySpot, StringComparison.Ordinal);
        Assert.Contains("MarkDisconnected", entrySpot, StringComparison.Ordinal);

        Assert.Contains("onLeaveActor", userSpot, StringComparison.Ordinal);
        Assert.Contains("onDisconnectActor", userSpot, StringComparison.Ordinal);
        Assert.Contains("leaveActor", userSpot, StringComparison.Ordinal);
        Assert.Contains("MarkForDestroyAfterRoomLeave", userSpot, StringComparison.Ordinal);
        Assert.Contains("MarkDisconnected", userSpot, StringComparison.Ordinal);
        Assert.DoesNotContain("DestroyActorAsync", userSpot, StringComparison.Ordinal);

        Assert.Contains("DestroyAfterEntrySpotJoin", actor, StringComparison.Ordinal);
        Assert.Contains("MarkForDestroyAfterRoomLeave", actor, StringComparison.Ordinal);
        Assert.Contains("MarkDisconnected", actor, StringComparison.Ordinal);
        Assert.Contains("Disconnected", actor, StringComparison.Ordinal);

        Assert.Contains("OnDisconnectedAsync", session, StringComparison.Ordinal);
        Assert.Contains("NotifyDisconnectedAsync", session, StringComparison.Ordinal);
    }

    private static IEnumerable<string> EnumerateSourceFiles(string root)
    {
        return Directory
            .EnumerateFiles(root, "*.cs", SearchOption.AllDirectories)
            .Where(static path => !path.Contains($"{Path.DirectorySeparatorChar}bin{Path.DirectorySeparatorChar}", StringComparison.Ordinal)
                && !path.Contains($"{Path.DirectorySeparatorChar}obj{Path.DirectorySeparatorChar}", StringComparison.Ordinal));
    }

    private static IEnumerable<string> EnumerateMarkdownFiles(string root)
    {
        return Directory
            .EnumerateFiles(root, "*.md", SearchOption.AllDirectories)
            .Where(static path => !path.Contains($"{Path.DirectorySeparatorChar}bin{Path.DirectorySeparatorChar}", StringComparison.Ordinal)
                && !path.Contains($"{Path.DirectorySeparatorChar}obj{Path.DirectorySeparatorChar}", StringComparison.Ordinal));
    }

    private static IEnumerable<string> EnumerateSessionRoots(string sampleRoot)
    {
        var sessionRoot = Path.Combine(sampleRoot, "Server", "Session");
        if (Directory.Exists(sessionRoot))
        {
            yield return sessionRoot;
        }

        var playSessionsRoot = Path.Combine(sampleRoot, "Server", "Play", "Sessions");
        if (Directory.Exists(playSessionsRoot))
        {
            yield return playSessionsRoot;
        }

        var adapterSessionsRoot = Path.Combine(
            sampleRoot,
            "Server",
            "Play",
            "Adapters",
            "ZLink",
            "Sessions");
        if (Directory.Exists(adapterSessionsRoot))
        {
            yield return adapterSessionsRoot;
        }
    }

    private static string ResolveSampleRoot(string sampleName)
    {
        return Path.Combine(ResolveSamplesRoot(), sampleName);
    }

    private static string ResolveSamplesRoot()
    {
        return Path.Combine(ResolveDotnetRoot(), "samples");
    }

    private static string ResolveDotnetRoot()
    {
        var current = new DirectoryInfo(AppContext.BaseDirectory);

        while (current is not null)
        {
            var candidate = Path.Combine(
                current.FullName,
                "framework",
                "languages",
                "dotnet",
                "samples");

            if (Directory.Exists(candidate))
            {
                return Directory.GetParent(candidate)!.FullName;
            }

            current = current.Parent;
        }

        throw new DirectoryNotFoundException(
            "Could not find framework/languages/dotnet/samples from test runtime.");
    }
}
