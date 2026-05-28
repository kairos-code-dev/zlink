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
    public void TicTacToe_Uses_RegistryBacked_Defaults_Without_Sample_Metadata_Store()
    {
        var sampleRoot = ResolveSampleRoot("TicTacToe.SessionGateway");

        AssertNoSampleRouteStore(sampleRoot);
        AssertNoSampleMetadataStore(sampleRoot);
        AssertSampleUsesRegistryDefaults(sampleRoot, "tictactoe");
        AssertSessionServerUsesActorGateway(sampleRoot, allowRouteMeshChannel: true);
        AssertSessionHandlersDoNotResolveActorRemoteAddresses(sampleRoot);
        AssertSessionAuthenticateJoinsPlayEntrySpotBeforeBinding(sampleRoot);
        AssertNoSampleSessionRelayJson(sampleRoot);
        AssertSessionPayloadPolicy(sampleRoot);
        AssertUsesFrameworkSessionPacketDispatcher(sampleRoot);
        AssertSpotRidJoinUsesRoutingId(sampleRoot);
    }

    [Fact]
    public void TicTacToe_Play_Session_Uses_FrameworkPayloadLifetimePolicy()
    {
        var sampleRoot = ResolveSampleRoot("TicTacToe");

        AssertSessionPayloadPolicy(sampleRoot);
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

    private static void AssertSpotRidJoinUsesRoutingId(string sampleRoot)
    {
        var joinHandler = Directory
            .EnumerateFiles(sampleRoot, "JoinMatchHandler.cs", SearchOption.AllDirectories)
            .Single();
        var text = File.ReadAllText(joinHandler);

        Assert.Contains("JoinSpot(RoutingId.FromHex(request.MatchId), request)", text, StringComparison.Ordinal);
        Assert.DoesNotContain("JoinSpot(request.MatchId, request)", text, StringComparison.Ordinal);
    }

    private static void AssertSampleUsesRegistryDefaults(
        string sampleRoot,
        string namespaceName)
    {
        var allText = string.Join(
            Environment.NewLine,
            EnumerateSourceFiles(sampleRoot).Select(File.ReadAllText));

        Assert.Contains($"UseRegistrySpotRemoteAddresses(\"{namespaceName}\")", allText, StringComparison.Ordinal);
        Assert.DoesNotContain("UseRegistryActorRemoteAddresses", allText, StringComparison.Ordinal);
        Assert.DoesNotContain("UseRegistryActorSessionBindings", allText, StringComparison.Ordinal);
        Assert.DoesNotContain("IZLinkActorSessionClient", allText, StringComparison.Ordinal);
    }

    private static void AssertSampleUsesRegistryDiscovery(string sampleRoot)
    {
        var allText = string.Join(
            Environment.NewLine,
            EnumerateSourceFiles(sampleRoot).Select(File.ReadAllText));

        Assert.Contains("UseDiscovery(discovery => discovery.Add(topology.RegistryRouterEndpoint))", allText, StringComparison.Ordinal);
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

    private static void AssertSessionAuthenticateJoinsPlayEntrySpotBeforeBinding(string sampleRoot)
    {
        var handler = Directory
            .EnumerateFiles(Path.Combine(sampleRoot, "Server", "Session"), "AuthenticateSessionPacketHandler.cs", SearchOption.AllDirectories)
            .Single();
        var text = File.ReadAllText(handler);
        var ensureIndex = text.IndexOf("EnsurePlayerActorReq", StringComparison.Ordinal);
        var bindIndex = text.IndexOf("Actors.BindAsync", StringComparison.Ordinal);

        Assert.True(ensureIndex >= 0, "Authenticate handler must request the Play server to ensure the actor.");
        Assert.True(bindIndex > ensureIndex, "Authenticate handler must bind the returned ActorRef after the Play actor is ensured.");
        Assert.DoesNotContain("ResolveActorRemoteAddressAsync", text, StringComparison.Ordinal);
    }

    private static IEnumerable<string> EnumerateSourceFiles(string root)
    {
        return Directory
            .EnumerateFiles(root, "*.cs", SearchOption.AllDirectories)
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
    }

    private static string ResolveSampleRoot(string sampleName)
    {
        var current = new DirectoryInfo(AppContext.BaseDirectory);

        while (current is not null)
        {
            var candidate = Path.Combine(
                current.FullName,
                "framework",
                "languages",
                "dotnet",
                "samples",
                sampleName);

            if (Directory.Exists(candidate))
            {
                return candidate;
            }

            current = current.Parent;
        }

        throw new DirectoryNotFoundException(
            $"Could not find framework/languages/dotnet/samples/{sampleName} from test runtime.");
    }
}
