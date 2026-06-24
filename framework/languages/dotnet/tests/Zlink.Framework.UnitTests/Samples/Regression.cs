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
        AssertSessionServerUsesActorGateway(sampleRoot, allowRouteMeshChannel: true);
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
            "Server/Play/Infrastructure/ZLink/Spots/BingoEntrySpot.cs",
            "Server/Play/Infrastructure/ZLink/Spots/BingoRoom.cs",
            "Server/Play/Infrastructure/ZLink/Actors/PlayerActor.cs",
            "Server/Session/Sessions/BingoSession.cs");
        AssertActorLifecycleSpec(
            ResolveSampleRoot("TicTacToe"),
            "Server/Play/Infrastructure/ZLink/Spots/PlayEntrySpot.cs",
            "Server/Play/Infrastructure/ZLink/Spots/TicTacToeGame.cs",
            "Server/Play/Infrastructure/ZLink/Actors/PlayActor.cs",
            "Server/Play/Infrastructure/ZLink/Sessions/PlaySession.cs");
    }

    [Fact]
    public void DotNet_Docs_Keep_Actor_Destroy_Entry_Owned()
    {
        var dotnetRoot = ResolveDotnetRoot();
        var docs = EnumerateMarkdownFiles(Path.Combine(dotnetRoot, "..", "..", "doc", "framework", "dotnet", "guide"))
            .Concat(EnumerateMarkdownFiles(Path.Combine(dotnetRoot, "..", "..", "doc", "framework", "dotnet", "spec")))
            .Concat(EnumerateMarkdownFiles(Path.Combine(dotnetRoot, "..", "..", "doc", "framework", "dotnet", "internals")))
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
            ("OnCreateActor(", "legacy PascalCase create callback"),
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

        var actorSpec = File.ReadAllText(Path.Combine(dotnetRoot, "..", "..", "doc", "framework", "dotnet", "spec", "aspnet-core-actor.ko.md"));
        var actorGuide = File.ReadAllText(Path.Combine(dotnetRoot, "..", "..", "doc", "framework", "dotnet", "guide", "06-actor-session.ko.md"));
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
    public void Bingo_Uses_Protobuf_And_TicTacToe_Uses_Json_Sample_Payloads()
    {
        var bingoRoot = ResolveSampleRoot("Bingo");
        var ticTacToeRoot = ResolveSampleRoot("TicTacToe");

        AssertSampleUsesProtobufPayloads(bingoRoot);
        AssertSampleUsesJsonPayloads(ticTacToeRoot);
        AssertCodecHelpersStayConfinedToRawLifecycleBoundaries(bingoRoot);
        AssertCodecHelpersStayConfinedToRawLifecycleBoundaries(ticTacToeRoot);
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
        var sharedProject = Path.Combine(sampleRoot, "Shared", "Bingo.Shared.csproj");
        var sharedProjectText = File.ReadAllText(sharedProject);
        var sharedContractSourceText = string.Join(
            Environment.NewLine,
            Directory
                .EnumerateFiles(Path.Combine(sampleRoot, "Shared", "Contracts"), "*.cs", SearchOption.AllDirectories)
                .Where(static path => !path.Contains($"{Path.DirectorySeparatorChar}bin{Path.DirectorySeparatorChar}", StringComparison.Ordinal)
                    && !path.Contains($"{Path.DirectorySeparatorChar}obj{Path.DirectorySeparatorChar}", StringComparison.Ordinal))
                .Select(File.ReadAllText));

        Assert.NotEmpty(protoFiles);
        Assert.Contains("Google.Protobuf", sharedProjectText, StringComparison.Ordinal);
        Assert.Contains("Grpc.Tools", sharedProjectText, StringComparison.Ordinal);
        Assert.Contains("<Protobuf Include=\"Contracts\\bingo_messages.proto\" GrpcServices=\"None\" />", sharedProjectText, StringComparison.Ordinal);
        Assert.DoesNotContain("record ", sharedContractSourceText, StringComparison.Ordinal);
        Assert.DoesNotContain("class AuthenticateReq", sharedContractSourceText, StringComparison.Ordinal);
        Assert.DoesNotContain("class BingoRoomJoinReq", sharedContractSourceText, StringComparison.Ordinal);
        Assert.Contains("ZLinkProtobufCodec.Default", allText, StringComparison.Ordinal);
        Assert.Contains("Zlink.Framework.Codecs.Protobuf", allText, StringComparison.Ordinal);
        Assert.DoesNotContain("AddProtobuf", allText, StringComparison.Ordinal);
        Assert.DoesNotContain("Stream.Connector.Protobuf", allText, StringComparison.Ordinal);
        Assert.DoesNotContain("Zlink.Codecs.Protobuf", allText, StringComparison.Ordinal);
        Assert.DoesNotContain("MessagePack", allText, StringComparison.Ordinal);
        Assert.DoesNotContain("MsgPack", allText, StringComparison.Ordinal);
    }

    private static void AssertSampleUsesJsonPayloads(string sampleRoot)
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
        Assert.DoesNotContain("Stream.Connector.Json", allText, StringComparison.Ordinal);
        Assert.DoesNotContain("Zlink.Codecs.Json", allText, StringComparison.Ordinal);
        Assert.DoesNotContain("Google.Protobuf", allText, StringComparison.Ordinal);
        Assert.DoesNotContain("Grpc.Tools", allText, StringComparison.Ordinal);
        Assert.DoesNotContain("AddProtobuf", allText, StringComparison.Ordinal);
        Assert.DoesNotContain("FromProto", allText, StringComparison.Ordinal);
        Assert.DoesNotContain("ToProto", allText, StringComparison.Ordinal);
        Assert.DoesNotContain("MessagePackObject", allText, StringComparison.Ordinal);
        Assert.DoesNotContain("AddMessagePack", allText, StringComparison.Ordinal);
    }

    private static void AssertCodecHelpersStayConfinedToRawLifecycleBoundaries(string sampleRoot)
    {
        var sampleName = Path.GetFileName(sampleRoot);
        var violations = new List<string>();
        foreach (var file in EnumerateSourceFiles(sampleRoot))
        {
            var text = File.ReadAllText(file);
            if (!ContainsRawCodecHelper(text))
            {
                continue;
            }

            var relative = Path.GetRelativePath(sampleRoot, file).Replace('\\', '/');
            if (!IsAllowedRawCodecLifecycleFile(sampleName, relative))
            {
                violations.Add($"{sampleName}/{relative}");
            }
        }

        Assert.Empty(violations.Order(StringComparer.Ordinal));
    }

    private static bool ContainsRawCodecHelper(string text)
    {
        return text.Contains(".ToJson()", StringComparison.Ordinal)
            || text.Contains(".FromJson<", StringComparison.Ordinal)
            || text.Contains(".ToProto()", StringComparison.Ordinal)
            || text.Contains(".FromProto<", StringComparison.Ordinal);
    }

    private static bool IsAllowedRawCodecLifecycleFile(string sampleName, string relative)
    {
        return (sampleName, relative) switch
        {
            ("Bingo", "Server/Session/Sessions/Handlers/AuthenticateSessionHandler.cs") => true,
            ("Bingo", "Server/Play/Application/RoomAllocation/BingoRoomAllocator.cs") => true,
            ("Bingo", "Server/Play/Infrastructure/ZLink/Handlers/AllocateBingoRoomHandler.cs") => true,
            ("Bingo", "Server/Play/Adapters/ZLink/Spots/BingoRoom.cs") => true,
            ("Bingo", "Server/Play/Adapters/ZLink/Spots/Handlers/MatchBingoActorHandler.cs") => true,
            ("Bingo", "Server/Play/Infrastructure/ZLink/Spots/BingoRoom.cs") => true,
            ("Bingo", "Server/Play/Infrastructure/ZLink/Spots/BingoRoomSettingsPayloadMapper.cs") => true,
            ("Bingo", "Server/Play/Infrastructure/ZLink/Spots/Handlers/MatchBingoActorHandler.cs") => true,
            ("Bingo", "Server/Play/Infrastructure/ZLink/Spots/Handlers/ObserveBingoEventsHandler.cs") => true,
            ("TicTacToe", "Server/Play/Adapters/ZLink/Spots/TicTacToeGame.cs") => true,
            ("TicTacToe", "Server/Play/Adapters/ZLink/Spots/Handlers/PlayActorJoinGameHandler.cs") => true,
            ("TicTacToe", "Server/Play/Infrastructure/ZLink/Spots/TicTacToeGame.cs") => true,
            ("TicTacToe", "Server/Play/Infrastructure/ZLink/Spots/Handlers/PlayActorJoinGameHandler.cs") => true,
            _ => false
        };
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
            "UseDiscovery().AddRegistryEndpoint(topology.RegistryRouterEndpoint)",
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
        Assert.True(
            text.Contains("EnableRouter", StringComparison.Ordinal)
                || text.Contains("ConfigureRouter", StringComparison.Ordinal));
        Assert.Contains("AttachActorGateway", text, StringComparison.Ordinal);
        if (!allowRouteMeshChannel)
        {
            Assert.DoesNotContain("AddRouteMesh", text, StringComparison.Ordinal);
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

        Assert.Contains("OnCreateActorAsync", entrySpot, StringComparison.Ordinal);
        Assert.Contains("OnJoinedActorAsync", entrySpot, StringComparison.Ordinal);
        Assert.Contains("OnLeaveActorAsync", entrySpot, StringComparison.Ordinal);
        Assert.Contains("OnDisconnectActorAsync", entrySpot, StringComparison.Ordinal);
        Assert.Contains("DestroyActorAsync", entrySpot, StringComparison.Ordinal);
        Assert.Contains("DestroyAfterEntrySpotJoin", entrySpot, StringComparison.Ordinal);
        Assert.Contains("MarkDisconnected", entrySpot, StringComparison.Ordinal);

        Assert.Contains("OnLeaveActorAsync", userSpot, StringComparison.Ordinal);
        Assert.Contains("OnDisconnectActorAsync", userSpot, StringComparison.Ordinal);
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

        var infrastructureSessionsRoot = Path.Combine(
            sampleRoot,
            "Server",
            "Play",
            "Infrastructure",
            "ZLink",
            "Sessions");
        if (Directory.Exists(infrastructureSessionsRoot))
        {
            yield return infrastructureSessionsRoot;
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
