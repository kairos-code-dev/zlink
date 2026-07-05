using Xunit;

namespace Zlink.Framework.SampleRegressionTests;

public sealed class RegressionTests
{
    [Fact]
    public void Samples_Do_Not_Use_Location_Stores_Or_Resolvers_As_Business_Dependencies()
    {
        // TODO(Q1): remove this exception when the actor client replaces the
        // resolve-then-send flow in DeliveryDispatch Tracking.
        var allowedActorAddressResolverFiles = new HashSet<string>(StringComparer.Ordinal)
        {
            NormalizeRelativePath(Path.Combine("DeliveryDispatch", "Server", "Tracking", "Handlers.cs"))
        };
        var forbidden = new[]
        {
            "ZLinkActorLocation",
            "IZLinkActorLocationStore",
            "IZLinkPeerLocationStore",
            "IZLinkSpotLocationStore",
            "IZLinkRouteLocationStore",
            "IZLinkLocationStore",
            "IZLinkOwnerLeaseStore",
            "IZLinkActorAddressResolver"
        };

        var offenders = EnumerateSourceFiles(ResolveSamplesRoot())
            .Where(static file => !IsAllowedSampleLocationStoreException(file))
            .SelectMany(file => forbidden
                .Where(token => File.ReadAllText(file).Contains(token, StringComparison.Ordinal))
                .Where(token => !IsAllowedActorAddressResolverException(
                    file,
                    token,
                    allowedActorAddressResolverFiles))
                .Select(token => $"{Path.GetRelativePath(ResolveSamplesRoot(), file)}:{token}"))
            .ToArray();

        Assert.True(
            offenders.Length == 0,
            "Samples must not use location stores or actor address resolvers directly: "
            + string.Join(", ", offenders));
    }

    [Fact]
    public void Sample_Session_Binding_Uses_BindOrGetAsync()
    {
        var allowedExplicitRebindFiles = new HashSet<string>(StringComparer.Ordinal)
        {
            NormalizeRelativePath(Path.Combine("e2e", "SpotService", "Server", "MultiNode", "Handlers", "MultiNodeSessionHandlers.cs")),
            NormalizeRelativePath(Path.Combine("e2e", "SpotService", "Server", "Play", "Handlers", "PlaySessionHandlers.cs")),
            NormalizeRelativePath(Path.Combine("e2e", "SpotService", "Server", "Session", "Handlers", "SessionSessionHandlers.cs")),
            NormalizeRelativePath(Path.Combine("e2e", "YieldDispatch", "Server", "Session", "Support", "YieldSession.cs"))
        };
        var sampleSessionFiles = new[] { "Bingo", "DeliveryDispatch", "SupportChat", "TicTacToe" }
            .Select(ResolveSampleRoot)
            .SelectMany(static root => EnumerateSessionRoots(root))
            .SelectMany(static root => EnumerateSourceFiles(root))
            .ToArray();
        var e2eSessionFiles = EnumerateSourceFiles(ResolveE2eRoot()).ToArray();
        var sessionFiles = sampleSessionFiles.Concat(e2eSessionFiles).ToArray();
        Assert.NotEmpty(sampleSessionFiles);
        Assert.NotEmpty(e2eSessionFiles);

        var sessionText = string.Join(Environment.NewLine, sessionFiles.Select(File.ReadAllText));
        var bindAsyncOffenders = sessionFiles
            .Where(file => File.ReadAllText(file).Contains(".BindAsync(", StringComparison.Ordinal))
            .Select(file => NormalizeRelativePath(Path.GetRelativePath(ResolveDotnetRoot(), file)))
            .Where(file => !allowedExplicitRebindFiles.Contains(file))
            .ToArray();

        Assert.Contains("BindOrGetAsync", sessionText, StringComparison.Ordinal);
        Assert.True(
            bindAsyncOffenders.Length == 0,
            "Session binding should use BindOrGetAsync unless the file intentionally exercises explicit rebinding: "
            + string.Join(", ", bindAsyncOffenders));
    }

    [Fact]
    public void Sample_Health_Checks_Use_Location_Readiness()
    {
        var hostFiles = EnumerateSourceFiles(ResolveSamplesRoot())
            .Where(static file => file.EndsWith("HostFactory.cs", StringComparison.Ordinal)
                                  || Path.GetFileName(file).Equals("Program.cs", StringComparison.Ordinal))
            .ToArray();
        Assert.NotEmpty(hostFiles);

        var allText = string.Join(Environment.NewLine, hostFiles.Select(File.ReadAllText));

        Assert.Contains("IZLinkLocationReadiness", allText, StringComparison.Ordinal);
        Assert.DoesNotContain("IZLinkLocationRuntimeQuery", allText, StringComparison.Ordinal);
    }

    [Fact]
    public void Bingo_Uses_Framework_Defaults_Without_Sample_Metadata_Store()
    {
        var sampleRoot = ResolveSampleRoot("Bingo");

        AssertNoSampleRouteStore(sampleRoot);
        AssertNoSampleMetadataStore(sampleRoot);
        AssertSessionServerUsesSessionRelay(sampleRoot, true);
        AssertSessionHandlersDoNotResolveActorRemoteAddresses(sampleRoot);
        AssertEnsureActorHandlersReturnSessionRelayRemoteAddresses(sampleRoot);
        AssertNoSampleSessionRelayJson(sampleRoot);
        AssertSessionPayloadPolicy(sampleRoot);
        AssertUsesFrameworkSessionPacketDispatcher(sampleRoot);
    }

    [Fact]
    public void Bingo_Runner_Uses_Isolated_Docker_Redis()
    {
        var sampleRoot = ResolveSampleRoot("Bingo");
        var shellRunner = File.ReadAllText(Path.Combine(sampleRoot, "run_sample.sh"));
        var powershellRunner = File.ReadAllText(Path.Combine(sampleRoot, "run_sample.ps1"));
        var readme = File.ReadAllText(Path.Combine(sampleRoot, "README.md"));

        Assert.Contains("RUN_ID=\"$(basename \"${RUN_DIR}\")-$$-${RANDOM}\"", shellRunner, StringComparison.Ordinal);
        Assert.Contains("export BINGO_REDIS_KEY_PREFIX=\"bingo:dotnet:${RUN_ID}:\"", shellRunner, StringComparison.Ordinal);
        Assert.Contains("REDIS_CONTAINER=\"zlink-bingo-dotnet-redis-${RUN_ID}\"", shellRunner, StringComparison.Ordinal);
        Assert.Contains("docker run -d --rm --name \"${REDIS_CONTAINER}\"", shellRunner, StringComparison.Ordinal);
        Assert.Contains("-p \"127.0.0.1::6379\"", shellRunner, StringComparison.Ordinal);
        Assert.Contains("if [[ -n \"${REDIS_CONTAINER}\" ]]; then", shellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("if [[ -z \"${BINGO_REDIS_ENDPOINT:-}\" ]]", shellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("when BINGO_REDIS_ENDPOINT is not set", shellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("BINGO_STARTUP_DELAY_SECONDS", shellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("BINGO_STARTUP_SETTLE_SECONDS", shellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("auto-connect reconcile loops", shellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("intentionally derived here, not read", shellRunner, StringComparison.Ordinal);

        Assert.Contains("$RunId = \"$PID-$([Guid]::NewGuid().ToString('N'))\"", powershellRunner, StringComparison.Ordinal);
        Assert.Contains("[Environment]::SetEnvironmentVariable(\"BINGO_REDIS_KEY_PREFIX\", \"bingo:dotnet:${RunId}:\", \"Process\")",
            powershellRunner, StringComparison.Ordinal);
        Assert.Contains("$RedisContainer = \"zlink-bingo-dotnet-redis-$RunId\"", powershellRunner, StringComparison.Ordinal);
        Assert.Contains("docker run -d --rm --name $RedisContainer", powershellRunner, StringComparison.Ordinal);
        Assert.Contains("-p \"127.0.0.1::6379\"", powershellRunner, StringComparison.Ordinal);
        Assert.Contains("if ($RedisContainer)", powershellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("if (-not $env:BINGO_REDIS_ENDPOINT)", powershellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("when BINGO_REDIS_ENDPOINT is not set", powershellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("BINGO_STARTUP_DELAY_SECONDS", powershellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("BINGO_STARTUP_SETTLE_SECONDS", powershellRunner, StringComparison.Ordinal);
        Assert.Contains("$env:BINGO_LOG_DIR = $SampleLogDir", powershellRunner, StringComparison.Ordinal);
        Assert.Contains("function Wait-LogContains", powershellRunner, StringComparison.Ordinal);
        Assert.Contains("Select-String -Pattern $Pattern -List", powershellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("Select-String -Pattern $Pattern -Quiet", powershellRunner, StringComparison.Ordinal);
        Assert.Contains("Wait-SampleLogContains \"message flow\" \"Bingo message-flow evidence\"", powershellRunner,
            StringComparison.Ordinal);
        Assert.DoesNotContain("intentionally derived here, not read", powershellRunner, StringComparison.Ordinal);

        Assert.Contains("always provisions a dedicated Redis Docker", readme, StringComparison.Ordinal);
        Assert.Contains("does not", readme, StringComparison.Ordinal);
        Assert.Contains("reuse an externally supplied Redis", readme, StringComparison.Ordinal);
        Assert.Contains("endpoint", readme, StringComparison.Ordinal);
        Assert.Contains("sample name and execution id", readme, StringComparison.Ordinal);
        Assert.Contains("public endpoints", readme, StringComparison.Ordinal);
        Assert.DoesNotContain("waits briefly", readme, StringComparison.Ordinal);
        Assert.Contains("parallel sample runs do not share location store", readme, StringComparison.Ordinal);
        Assert.Contains("or match queue keys", readme, StringComparison.Ordinal);
    }

    [Fact]
    public void TicTacToe_Runner_Uses_Isolated_Docker_Redis()
    {
        var sampleRoot = ResolveSampleRoot("TicTacToe");
        var shellRunner = File.ReadAllText(Path.Combine(sampleRoot, "run_sample.sh"));
        var powershellRunner = File.ReadAllText(Path.Combine(sampleRoot, "run_sample.ps1"));
        var settings = File.ReadAllText(Path.Combine(sampleRoot, "Server", "Configuration", "SampleSettings.cs"));
        var readme = File.ReadAllText(Path.Combine(sampleRoot, "README.md"));

        Assert.Contains("RUN_ID=\"$(basename \"${RUN_DIR}\")-$$-${RANDOM}\"", shellRunner, StringComparison.Ordinal);
        Assert.Contains("export TICTACTOE_REDIS_KEY_PREFIX=\"tictactoe:dotnet:${RUN_ID}:\"", shellRunner,
            StringComparison.Ordinal);
        Assert.Contains("--name \"zlink-tictactoe-dotnet-redis-${RUN_ID}\"", shellRunner, StringComparison.Ordinal);
        Assert.Contains("-p \"127.0.0.1::6379\"", shellRunner, StringComparison.Ordinal);
        Assert.Contains("SAMPLE_LOG_DIR=\"${RUN_DIR}/sample-logs\"", shellRunner, StringComparison.Ordinal);
        Assert.Contains("export TICTACTOE_LOG_DIR=\"${SAMPLE_LOG_DIR}\"", shellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("if [[ -z \"${TICTACTOE_REDIS_ENDPOINT:-}\" ]]", shellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("when TICTACTOE_REDIS_ENDPOINT is not set", shellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("TICTACTOE_BASE_PORT", shellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("${TICTACTOE_API_A_BIND_URL", shellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("${TICTACTOE_API_B_BIND_URL", shellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("${TICTACTOE_API_A_PUBLIC_URL", shellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("${TICTACTOE_API_B_PUBLIC_URL", shellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("${TICTACTOE_API_A_CHANNEL_ENDPOINT", shellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("${TICTACTOE_API_B_CHANNEL_ENDPOINT", shellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("${TICTACTOE_PLAY_A_CHANNEL_ENDPOINT", shellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("${TICTACTOE_PLAY_B_CHANNEL_ENDPOINT", shellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("${TICTACTOE_PLAY_A_ENDPOINT", shellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("${TICTACTOE_PLAY_B_ENDPOINT", shellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("${TICTACTOE_SPOT_A_ENDPOINT", shellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("${TICTACTOE_SPOT_B_ENDPOINT", shellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("${TICTACTOE_SPOT_A_PUBSUB_ENDPOINT", shellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("${TICTACTOE_SPOT_B_PUBSUB_ENDPOINT", shellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("${TICTACTOE_LOG_DIR:-", shellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("rm -f \"${TICTACTOE_LOG_DIR}\"/*.log", shellRunner, StringComparison.Ordinal);
        Assert.Contains("TICTACTOE_REDIS_KEY_PREFIX", shellRunner, StringComparison.Ordinal);
        Assert.Contains("\"RedisKeyPrefix\": \"${TICTACTOE_REDIS_KEY_PREFIX}\"", shellRunner,
            StringComparison.Ordinal);
        Assert.DoesNotContain("intentionally derived here, not read", shellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("sleep 2", shellRunner, StringComparison.Ordinal);

        Assert.Contains("$RunId = \"$PID-$([Guid]::NewGuid().ToString('N'))\"", powershellRunner,
            StringComparison.Ordinal);
        Assert.Contains("$env:TICTACTOE_REDIS_KEY_PREFIX = \"tictactoe:dotnet:${RunId}:\"", powershellRunner,
            StringComparison.Ordinal);
        Assert.Contains("--name \"zlink-tictactoe-dotnet-redis-$RunId\"", powershellRunner,
            StringComparison.Ordinal);
        Assert.Contains("-p \"127.0.0.1::6379\"", powershellRunner, StringComparison.Ordinal);
        Assert.Contains("$SampleLogDir = Join-Path $RunDir \"sample-logs\"", powershellRunner,
            StringComparison.Ordinal);
        Assert.Contains("$ports = New-SamplePorts -Count 13 -BasePort 0", powershellRunner,
            StringComparison.Ordinal);
        Assert.DoesNotContain("if (-not $env:TICTACTOE_REDIS_ENDPOINT)", powershellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("when TICTACTOE_REDIS_ENDPOINT is not set", powershellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("$env:TICTACTOE_BASE_PORT", powershellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("$env:TICTACTOE_API_A_BIND_URL", powershellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("$env:TICTACTOE_API_B_BIND_URL", powershellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("$env:TICTACTOE_API_A_PUBLIC_URL", powershellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("$env:TICTACTOE_API_B_PUBLIC_URL", powershellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("$env:TICTACTOE_API_A_CHANNEL_ENDPOINT", powershellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("$env:TICTACTOE_API_B_CHANNEL_ENDPOINT", powershellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("$env:TICTACTOE_PLAY_A_CHANNEL_ENDPOINT", powershellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("$env:TICTACTOE_PLAY_B_CHANNEL_ENDPOINT", powershellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("$env:TICTACTOE_PLAY_A_ENDPOINT", powershellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("$env:TICTACTOE_PLAY_B_ENDPOINT", powershellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("$env:TICTACTOE_SPOT_A_ENDPOINT", powershellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("$env:TICTACTOE_SPOT_B_ENDPOINT", powershellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("$env:TICTACTOE_SPOT_A_PUBSUB_ENDPOINT", powershellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("$env:TICTACTOE_SPOT_B_PUBSUB_ENDPOINT", powershellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("if ($env:TICTACTOE_LOG_DIR)", powershellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("Remove-Item -Path (Join-Path $SampleLogDir \"*.log\")", powershellRunner,
            StringComparison.Ordinal);
        Assert.Contains("TICTACTOE_REDIS_KEY_PREFIX", powershellRunner, StringComparison.Ordinal);
        Assert.Contains("RedisKeyPrefix = $env:TICTACTOE_REDIS_KEY_PREFIX", powershellRunner,
            StringComparison.Ordinal);
        Assert.DoesNotContain("intentionally derived here, not read", powershellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("Start-Sleep -Seconds 2", powershellRunner, StringComparison.Ordinal);

        Assert.DoesNotContain("AddEnvironmentVariables(\"TICTACTOE_\")", settings, StringComparison.Ordinal);
        Assert.Contains("section[nameof(RedisEndpoint)]", settings, StringComparison.Ordinal);
        Assert.Contains("section[nameof(RedisKeyPrefix)]", settings, StringComparison.Ordinal);

        Assert.Contains("Redis is required for the room route store", readme, StringComparison.Ordinal);
        Assert.Contains("always provisions a", readme, StringComparison.Ordinal);
        Assert.Contains("does not", readme, StringComparison.Ordinal);
        Assert.Contains("reuse an externally supplied Redis endpoint", readme, StringComparison.Ordinal);
        Assert.Contains("sample name and execution id", readme, StringComparison.Ordinal);
        Assert.Contains("`TICTACTOE_REDIS_KEY_PREFIX`", readme, StringComparison.Ordinal);
    }

    [Fact]
    public void TicTacToe_Runner_Verifies_Client_And_Server_Evidence()
    {
        var sampleRoot = ResolveSampleRoot("TicTacToe");
        var shellRunner = File.ReadAllText(Path.Combine(sampleRoot, "run_sample.sh"));
        var powershellRunner = File.ReadAllText(Path.Combine(sampleRoot, "run_sample.ps1"));

        Assert.Contains("stream-inbound sample=TicTacToe", shellRunner, StringComparison.Ordinal);
        Assert.Contains("stream-inbound sample=TicTacToe .* seq=[0-9]", shellRunner, StringComparison.Ordinal);
        Assert.Contains("stream-inbound sample=TicTacToe .* name=.*Notify", shellRunner, StringComparison.Ordinal);
        Assert.Contains("observer-win-milestone=verified", shellRunner, StringComparison.Ordinal);
        Assert.Contains("actor: LeaveGameReq completed. actor=player-x", shellRunner, StringComparison.Ordinal);
        Assert.Contains("actor: LeaveGameReq completed. actor=player-o", shellRunner, StringComparison.Ordinal);
        Assert.Contains("entry spot: actor destroy completed. actor=player-x", shellRunner, StringComparison.Ordinal);
        Assert.Contains("entry spot: actor destroy completed. actor=player-o", shellRunner, StringComparison.Ordinal);
        Assert.Contains("grep -R -q \"dispatch-error\"", shellRunner, StringComparison.Ordinal);
        Assert.Contains("grep -R -q \"message flow outcome=error\"", shellRunner, StringComparison.Ordinal);
        Assert.Contains("grep -Rq \"message flow\"", shellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("LeaveGameMsg", shellRunner, StringComparison.Ordinal);

        Assert.Contains("stream-inbound sample=TicTacToe", powershellRunner, StringComparison.Ordinal);
        Assert.Contains("stream-inbound sample=TicTacToe .* seq=[0-9]", powershellRunner,
            StringComparison.Ordinal);
        Assert.Contains("stream-inbound sample=TicTacToe .* name=.*Notify", powershellRunner,
            StringComparison.Ordinal);
        Assert.Contains("observer-win-milestone=verified", powershellRunner, StringComparison.Ordinal);
        Assert.Contains("actor: LeaveGameReq completed. actor=player-x", powershellRunner,
            StringComparison.Ordinal);
        Assert.Contains("actor: LeaveGameReq completed. actor=player-o", powershellRunner,
            StringComparison.Ordinal);
        Assert.Contains("entry spot: actor destroy completed. actor=player-x", powershellRunner,
            StringComparison.Ordinal);
        Assert.Contains("entry spot: actor destroy completed. actor=player-o", powershellRunner,
            StringComparison.Ordinal);
        Assert.Contains("Wait-SampleLogContains \"message flow\" \"TicTacToe message-flow evidence\"",
            powershellRunner, StringComparison.Ordinal);
        Assert.Contains("Select-String -Pattern $Pattern -List", powershellRunner, StringComparison.Ordinal);
        Assert.Contains("Select-String -Pattern \"dispatch-error\" -List", powershellRunner, StringComparison.Ordinal);
        Assert.Contains("Select-String -Pattern \"message flow outcome=error\" -List", powershellRunner,
            StringComparison.Ordinal);
        Assert.DoesNotContain("Select-String -Pattern $Pattern -Quiet", powershellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("Select-String -Pattern \"dispatch-error\" -Quiet", powershellRunner,
            StringComparison.Ordinal);
        Assert.DoesNotContain("LeaveGameMsg", powershellRunner, StringComparison.Ordinal);
    }

    [Fact]
    public void TicTacToe_RoomRouteStore_Uses_Prefixed_Redis_Routes()
    {
        var sampleRoot = ResolveSampleRoot("TicTacToe");
        var settings = File.ReadAllText(Path.Combine(sampleRoot, "Server", "Configuration", "SampleSettings.cs"));
        var routeStore = File.ReadAllText(Path.Combine(sampleRoot, "Server", "Configuration", "RedisRoomRouteStore.cs"));

        Assert.Contains("string RedisKeyPrefix", settings, StringComparison.Ordinal);
        Assert.Contains("case \"--redis-key-prefix\"", settings, StringComparison.Ordinal);
        Assert.Contains("settings.RedisKeyPrefix", routeStore, StringComparison.Ordinal);
        Assert.Contains("return $\"{_keyPrefix}rooms:{roomId}\"", routeStore, StringComparison.Ordinal);
    }

    [Fact]
    public void TicTacToe_ClientScenario_Matches_Common_Flow()
    {
        var sampleRoot = ResolveSampleRoot("TicTacToe");
        var messages = File.ReadAllText(Path.Combine(sampleRoot, "Shared", "Contracts", "Messages.cs"));
        var clientScenario = File.ReadAllText(Path.Combine(sampleRoot, "Client", "TicTacToeClientScenario.cs"));
        var authenticateHandler = File.ReadAllText(Path.Combine(sampleRoot, "Server", "Play", "Infrastructure",
            "ZLink", "Sessions", "Handlers", "AuthenticatePlaySessionHandler.cs"));

        Assert.Contains("record LeaveGameReq", messages, StringComparison.Ordinal);
        Assert.DoesNotContain("LeaveGameMsg", messages, StringComparison.Ordinal);

        Assert.Contains("client1SawClient2Join.Payload.RoomId == room.RoomId", clientScenario,
            StringComparison.Ordinal);
        Assert.Contains("client1Move1.State.LastMoveActorId == options.XActorId", clientScenario,
            StringComparison.Ordinal);
        Assert.Contains("client1Move1.State.LastMoveCell == 0", clientScenario, StringComparison.Ordinal);
        Assert.Contains("client2Move1.State.LastMoveActorId == options.OActorId", clientScenario,
            StringComparison.Ordinal);
        Assert.Contains("client2Move1.State.LastMoveCell == 3", clientScenario, StringComparison.Ordinal);
        Assert.Contains("client1Move2.State.LastMoveActorId == options.XActorId", clientScenario,
            StringComparison.Ordinal);
        Assert.Contains("client1Move2.State.LastMoveCell == 1", clientScenario, StringComparison.Ordinal);
        Assert.Contains("client2Move2.State.LastMoveActorId == options.OActorId", clientScenario,
            StringComparison.Ordinal);
        Assert.Contains("client2Move2.State.LastMoveCell == 4", clientScenario, StringComparison.Ordinal);
        Assert.Contains("client1FinalMove.State.LastMoveActorId == options.XActorId", clientScenario,
            StringComparison.Ordinal);
        Assert.Contains("client1FinalMove.State.LastMoveCell == 2", clientScenario, StringComparison.Ordinal);
        Assert.DoesNotContain("SampleSettings settings", authenticateHandler, StringComparison.Ordinal);
    }

    [Fact]
    public void TicTacToe_Docs_Match_ScaleOut_RoomRoute_Flow()
    {
        var sampleRoot = ResolveSampleRoot("TicTacToe");
        var readme = File.ReadAllText(Path.Combine(sampleRoot, "README.md"));
        var clientReadme = File.ReadAllText(Path.Combine(sampleRoot, "Client", "README.md"));
        var samplesReadme = File.ReadAllText(Path.Combine(ResolveSamplesRoot(), "README.md"));

        Assert.Contains("two API roles", readme, StringComparison.Ordinal);
        Assert.Contains("two Play roles", readme, StringComparison.Ordinal);
        Assert.Contains("`PlayEndpoints` and `PlayNodes`", readme, StringComparison.Ordinal);
        Assert.Contains("host, guest, and observer", readme, StringComparison.Ordinal);
        Assert.Contains("observer milestone verification", readme, StringComparison.Ordinal);
        Assert.Contains("`LeaveGameReq` completion for both players", readme, StringComparison.Ordinal);
        Assert.Contains("entry-spot actor destroy evidence", readme, StringComparison.Ordinal);
        Assert.DoesNotContain("the play server to create a game", readme, StringComparison.Ordinal);
        Assert.DoesNotContain("creates the two stream connectors", readme, StringComparison.Ordinal);

        Assert.Contains("--observer-actor-id", clientReadme, StringComparison.Ordinal);
        Assert.Contains("three STREAM connections", clientReadme, StringComparison.Ordinal);
        Assert.Contains("observer", clientReadme, StringComparison.Ordinal);
        Assert.Contains("ObserveMilestoneReq", clientReadme, StringComparison.Ordinal);
        Assert.Contains("WinMilestoneNotify", clientReadme, StringComparison.Ordinal);
        Assert.Contains("LeaveGameReq", clientReadme, StringComparison.Ordinal);
        Assert.DoesNotContain("opens two STREAM connections", clientReadme, StringComparison.Ordinal);

        Assert.Contains("two API roles", samplesReadme, StringComparison.Ordinal);
        Assert.Contains("two Play roles", samplesReadme, StringComparison.Ordinal);
        Assert.Contains("manual endpoint", samplesReadme, StringComparison.Ordinal);
        Assert.Contains("Redis room routes", samplesReadme, StringComparison.Ordinal);
        Assert.Contains("play-a --config ./appsettings.play-a.json", samplesReadme, StringComparison.Ordinal);
        Assert.Contains("play-b --config ./appsettings.play-b.json", samplesReadme, StringComparison.Ordinal);
        Assert.Contains("api-a --config ./appsettings.api-a.json", samplesReadme, StringComparison.Ordinal);
        Assert.Contains("api-b --config ./appsettings.api-b.json", samplesReadme, StringComparison.Ordinal);
        Assert.DoesNotContain("temporary appsettings.json", samplesReadme, StringComparison.Ordinal);
        Assert.DoesNotContain("-- play --config ./appsettings.json", samplesReadme, StringComparison.Ordinal);
        Assert.DoesNotContain("-- api --config ./appsettings.json", samplesReadme, StringComparison.Ordinal);
    }

    [Fact]
    public void SupportChat_Runner_Uses_Isolated_Docker_Redis_And_Location_Store()
    {
        var sampleRoot = ResolveSampleRoot("SupportChat");
        var shellRunner = File.ReadAllText(Path.Combine(sampleRoot, "run_sample.sh"));
        var powershellRunner = File.ReadAllText(Path.Combine(sampleRoot, "run_sample.ps1"));
        var readme = File.ReadAllText(Path.Combine(sampleRoot, "README.ko.md"));
        var apiHost = File.ReadAllText(Path.Combine(sampleRoot, "Server", "Api", "ApiServerHostFactory.cs"));
        var sessionHost = File.ReadAllText(Path.Combine(sampleRoot, "Server", "Session", "SessionServerHostFactory.cs"));
        var supportHost = File.ReadAllText(Path.Combine(sampleRoot, "Server", "Support", "SupportServerHostFactory.cs"));
        var topology = File.ReadAllText(Path.Combine(sampleRoot, "Server", "Configuration", "SampleTopology.cs"));
        var sharedMessages = File.ReadAllText(Path.Combine(sampleRoot, "Shared", "Contracts", "Messages.cs"));
        var serverContracts = File.ReadAllText(Path.Combine(sampleRoot, "Server", "Configuration", "SupportServerContracts.cs"));
        var assignment = File.ReadAllText(Path.Combine(sampleRoot, "Server", "Support", "Application",
            "ConversationAssignment", "AgentAssignmentService.cs"));
        var availability = File.ReadAllText(Path.Combine(sampleRoot, "Server", "Support", "Application",
            "ConversationAssignment", "AgentAvailabilityDirectory.cs"));
        var availableHandler = File.ReadAllText(Path.Combine(sampleRoot, "Server", "Support", "Infrastructure",
            "ZLink", "Spots", "EntrySpot", "Handlers", "SetAgentAvailableHandler.cs"));
        var ensureUserHandler = File.ReadAllText(Path.Combine(sampleRoot, "Server", "Support", "Infrastructure",
            "ZLink", "Handlers", "EnsureSupportUserActorHandler.cs"));
        var ensureConversationHandler = File.ReadAllText(Path.Combine(sampleRoot, "Server", "Support", "Infrastructure",
            "ZLink", "Handlers", "EnsureAgentConversationHandler.cs"));
        var entrySpot = File.ReadAllText(Path.Combine(sampleRoot, "Server", "Support", "Infrastructure", "ZLink",
            "Spots", "EntrySpot", "SupportEntrySpot.cs"));
        var conversationSpot = File.ReadAllText(Path.Combine(sampleRoot, "Server", "Support", "Infrastructure",
            "ZLink", "Spots", "ConversationSpot", "ConversationSpot.cs"));
        var clientScenario = File.ReadAllText(Path.Combine(sampleRoot, "Client", "SupportChatClientScenario.cs"));

        Assert.Contains("RUN_ID=\"$(basename \"${RUN_DIR}\")-$$-${RANDOM}\"", shellRunner, StringComparison.Ordinal);
        Assert.Contains("SAMPLE_LOG_DIR=\"${RUN_DIR}/sample-logs\"", shellRunner, StringComparison.Ordinal);
        Assert.Contains("export SUPPORTCHAT_LOG_DIR=\"${SAMPLE_LOG_DIR}\"", shellRunner, StringComparison.Ordinal);
        Assert.Contains("export SUPPORTCHAT_REDIS_KEY_PREFIX=\"supportchat:dotnet:${RUN_ID}:\"", shellRunner,
            StringComparison.Ordinal);
        Assert.Contains("REDIS_CONTAINER=\"zlink-supportchat-dotnet-redis-${RUN_ID}\"", shellRunner,
            StringComparison.Ordinal);
        Assert.Contains("docker run -d --rm --name \"${REDIS_CONTAINER}\"", shellRunner, StringComparison.Ordinal);
        Assert.Contains("if [[ -n \"${REDIS_CONTAINER}\" ]]; then", shellRunner, StringComparison.Ordinal);
        Assert.Contains("-p \"127.0.0.1::6379\"", shellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("if [[ -z \"${SUPPORTCHAT_REDIS_ENDPOINT:-}\" ]]", shellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("when SUPPORTCHAT_REDIS_ENDPOINT is not set", shellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("SUPPORTCHAT_BASE_PORT", shellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("${SUPPORTCHAT_API_CHANNEL_ENDPOINT:-", shellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("${SUPPORTCHAT_SUPPORT_CHANNEL_ENDPOINT:-", shellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("${SUPPORTCHAT_SESSION_SPOT_ENDPOINT:-", shellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("${SUPPORTCHAT_SESSION_ROUTER_ENDPOINT:-", shellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("${SUPPORTCHAT_SUPPORT_ROUTER_ENDPOINT:-", shellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("export SUPPORTCHAT_SUPPORT_ROUTER_ENDPOINT=", shellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("${SUPPORTCHAT_ENTRY_SPOT_ENDPOINT:-", shellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("${SUPPORTCHAT_ENTRY_SPOT_ROUTER_ENDPOINT:-", shellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("${SUPPORTCHAT_CONVERSATION_SPOT_ENDPOINT:-", shellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("${SUPPORTCHAT_CONVERSATION_SPOT_ROUTER_ENDPOINT:-", shellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("export SUPPORTCHAT_CONVERSATION_SPOT_ENDPOINT=", shellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("export SUPPORTCHAT_CONVERSATION_SPOT_ROUTER_ENDPOINT=", shellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("${SUPPORTCHAT_STREAM_ENDPOINT:-", shellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("${SUPPORTCHAT_RECONNECT_STREAM_ENDPOINT:-", shellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("export SUPPORTCHAT_RECONNECT_STREAM_ENDPOINT=", shellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("${SUPPORTCHAT_REDIS_KEY_PREFIX:-", shellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("${SUPPORTCHAT_LOG_DIR:-", shellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("rm -f \"${SUPPORTCHAT_LOG_DIR}\"/*.log", shellRunner, StringComparison.Ordinal);
        Assert.Contains("supportchat=completed", shellRunner, StringComparison.Ordinal);
        Assert.Contains("supportchat-closed-typing-ignore=verified", shellRunner, StringComparison.Ordinal);
        Assert.Contains("supportchat-server-evidence=completed", shellRunner, StringComparison.Ordinal);
        Assert.Contains("status=WaitingForAgent", shellRunner, StringComparison.Ordinal);
        Assert.Contains("status=Active", shellRunner, StringComparison.Ordinal);
        Assert.Contains("status=WaitingForClose", shellRunner, StringComparison.Ordinal);
        Assert.Contains("status=Closed", shellRunner, StringComparison.Ordinal);
        Assert.Contains("grep -Rq \"message flow\" \"${SUPPORTCHAT_LOG_DIR}\"", shellRunner,
            StringComparison.Ordinal);
        Assert.DoesNotContain("SUPPORTCHAT_STARTUP_DELAY_SECONDS", shellRunner, StringComparison.Ordinal);

        Assert.Contains("$RunId = \"$PID-$([Guid]::NewGuid().ToString('N'))\"", powershellRunner,
            StringComparison.Ordinal);
        Assert.Contains("$SampleLogDir = Join-Path $RunDir \"sample-logs\"", powershellRunner,
            StringComparison.Ordinal);
        Assert.Contains("$ports = New-SamplePorts -Count 7 -BasePort 0", powershellRunner,
            StringComparison.Ordinal);
        Assert.Contains("$env:SUPPORTCHAT_REDIS_KEY_PREFIX = \"supportchat:dotnet:${RunId}:\"",
            powershellRunner, StringComparison.Ordinal);
        Assert.Contains("$RedisContainer = \"zlink-supportchat-dotnet-redis-$RunId\"", powershellRunner,
            StringComparison.Ordinal);
        Assert.Contains("docker run -d --rm --name $RedisContainer", powershellRunner, StringComparison.Ordinal);
        Assert.Contains("if ($RedisContainer)", powershellRunner, StringComparison.Ordinal);
        Assert.Contains("$env:SUPPORTCHAT_LOG_DIR = $SampleLogDir", powershellRunner, StringComparison.Ordinal);
        Assert.Contains("-p \"127.0.0.1::6379\"", powershellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("if (-not $env:SUPPORTCHAT_REDIS_ENDPOINT)", powershellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("when SUPPORTCHAT_REDIS_ENDPOINT is not set", powershellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("$env:SUPPORTCHAT_BASE_PORT", powershellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("$env:SUPPORTCHAT_API_CHANNEL_ENDPOINT) {", powershellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("$env:SUPPORTCHAT_SUPPORT_CHANNEL_ENDPOINT) {", powershellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("$env:SUPPORTCHAT_SESSION_SPOT_ENDPOINT) {", powershellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("$env:SUPPORTCHAT_SESSION_ROUTER_ENDPOINT) {", powershellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("$env:SUPPORTCHAT_SUPPORT_ROUTER_ENDPOINT) {", powershellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("$env:SUPPORTCHAT_SUPPORT_ROUTER_ENDPOINT =", powershellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("$env:SUPPORTCHAT_ENTRY_SPOT_ENDPOINT) {", powershellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("$env:SUPPORTCHAT_ENTRY_SPOT_ROUTER_ENDPOINT) {", powershellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("$env:SUPPORTCHAT_CONVERSATION_SPOT_ENDPOINT) {", powershellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("$env:SUPPORTCHAT_CONVERSATION_SPOT_ROUTER_ENDPOINT) {", powershellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("$env:SUPPORTCHAT_CONVERSATION_SPOT_ENDPOINT =", powershellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("$env:SUPPORTCHAT_CONVERSATION_SPOT_ROUTER_ENDPOINT =", powershellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("$env:SUPPORTCHAT_STREAM_ENDPOINT) {", powershellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("$env:SUPPORTCHAT_RECONNECT_STREAM_ENDPOINT) {", powershellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("$env:SUPPORTCHAT_RECONNECT_STREAM_ENDPOINT =", powershellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("if ($env:SUPPORTCHAT_LOG_DIR)", powershellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("Set-DefaultEnv", powershellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("Remove-Item -Path (Join-Path $SampleLogDir \"*.log\")", powershellRunner,
            StringComparison.Ordinal);
        Assert.Contains("supportchat=completed", powershellRunner, StringComparison.Ordinal);
        Assert.Contains("supportchat-closed-typing-ignore=verified", powershellRunner, StringComparison.Ordinal);
        Assert.Contains("supportchat-server-evidence=completed", powershellRunner, StringComparison.Ordinal);
        Assert.Contains("status=WaitingForAgent", powershellRunner, StringComparison.Ordinal);
        Assert.Contains("status=Active", powershellRunner, StringComparison.Ordinal);
        Assert.Contains("status=WaitingForClose", powershellRunner, StringComparison.Ordinal);
        Assert.Contains("status=Closed", powershellRunner, StringComparison.Ordinal);
        Assert.Contains("Wait-SampleLogContains \"message flow\" \"SupportChat message-flow evidence\"",
            powershellRunner, StringComparison.Ordinal);
        Assert.Contains("Select-String -Pattern $Pattern -List", powershellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("SUPPORTCHAT_STARTUP_DELAY_SECONDS", powershellRunner, StringComparison.Ordinal);

        Assert.Contains("SUPPORTCHAT_REDIS_ENDPOINT", topology, StringComparison.Ordinal);
        Assert.Contains("SUPPORTCHAT_REDIS_KEY_PREFIX", topology, StringComparison.Ordinal);
        Assert.DoesNotContain("public sealed record ActorRefSnapshot", sharedMessages, StringComparison.Ordinal);
        Assert.DoesNotContain("EnsureSupportUserActorReq", sharedMessages, StringComparison.Ordinal);
        Assert.DoesNotContain("public sealed record ActorRefSnapshot", serverContracts, StringComparison.Ordinal);
        Assert.Contains("ZLinkActorRefSnapshot Actor", serverContracts, StringComparison.Ordinal);
        Assert.Contains("ZLinkActorRefSnapshot.From(actor)", ensureUserHandler, StringComparison.Ordinal);
        Assert.Contains("ZLinkActorRefSnapshot.From(actorRef)", ensureConversationHandler, StringComparison.Ordinal);
        Assert.Contains("public sealed record EnsureSupportUserActorReq", serverContracts, StringComparison.Ordinal);
        AssertLocationStoreHost(apiHost);
        AssertLocationStoreHost(sessionHost);
        AssertLocationStoreHost(supportHost);
        Assert.Contains("_reservations.Values.Count", assignment, StringComparison.Ordinal);
        Assert.Contains("availability.SetAvailable(rosterActorId, displayName, isAvailable, activeConversations)",
            assignment, StringComparison.Ordinal);
        Assert.Contains("int activeConversations", availability, StringComparison.Ordinal);
        Assert.Contains("assignment.SetAvailable(actor.ActorId, actor.DisplayName, message.IsAvailable)",
            availableHandler, StringComparison.Ordinal);
        Assert.Contains("assignment.SetAvailable(actor.ActorId, actor.DisplayName, false)", entrySpot,
            StringComparison.Ordinal);
        Assert.Contains("support conversation: state changed", conversationSpot, StringComparison.Ordinal);
        Assert.Contains("supportchat-closed-typing-ignore=verified", clientScenario, StringComparison.Ordinal);
        Assert.Contains("ExpectTimeoutAsync", clientScenario, StringComparison.Ordinal);

        Assert.Contains("외부 Redis endpoint 재사용 mode는 제공하지 않는다", readme, StringComparison.Ordinal);
        Assert.Contains("실행별 `SUPPORTCHAT_REDIS_KEY_PREFIX`", readme, StringComparison.Ordinal);
        Assert.Contains("동시에 도는 다른 테스트와 섞이지 않는다", readme, StringComparison.Ordinal);
        Assert.Contains("message-flow evidence", readme, StringComparison.Ordinal);
    }

    [Fact]
    public void ShoppingMall_Runner_Uses_Isolated_Docker_Redis_And_Redis_Stores()
    {
        var sampleRoot = ResolveSampleRoot("ShoppingMall");
        var shellRunner = File.ReadAllText(Path.Combine(sampleRoot, "run_sample.sh"));
        var powershellRunner = File.ReadAllText(Path.Combine(sampleRoot, "run_sample.ps1"));
        var topology = File.ReadAllText(Path.Combine(sampleRoot, "Server", "Configuration", "SampleNames.cs"));
        var commerceApi = File.ReadAllText(Path.Combine(sampleRoot, "Server", "CommerceApi", "Program.cs"));
        var commerceWorkflowPorts = File.ReadAllText(Path.Combine(sampleRoot, "Server", "CommerceApi", "Ports",
            "Outbound", "WorkflowPorts.cs"));
        var commerceSelfCheckClient = File.ReadAllText(Path.Combine(sampleRoot, "Server", "CommerceApi",
            "Infrastructure", "Http", "HttpOrderWorkflowSelfCheckClient.cs"));
        var workflow = File.ReadAllText(Path.Combine(sampleRoot, "Server", "OrderWorkflow", "Program.cs"));
        var workflowService = File.ReadAllText(Path.Combine(sampleRoot, "Server", "OrderWorkflow", "Application",
            "OrderWorkflow", "OrderWorkflowService.cs"));
        var workflowSelfCheck = File.ReadAllText(Path.Combine(sampleRoot, "Server", "OrderWorkflow", "Application",
            "SelfCheck", "OrderWorkflowSelfCheckService.cs"));
        var workflowSpot = File.ReadAllText(Path.Combine(sampleRoot, "Server", "OrderWorkflow", "Infrastructure",
            "ZLink", "Spots", "OrderWorkflowSpot", "OrderWorkflowSpot.cs"));
        var startUseCase = File.ReadAllText(Path.Combine(sampleRoot, "Server", "CommerceApi", "Application",
            "OrderWorkflow", "StartOrderUseCase.cs"));
        var routeHandlers = File.ReadAllText(Path.Combine(sampleRoot, "Server", "OrderWorkflow", "Infrastructure",
            "ZLink", "Handlers", "OrderWorkflowRouteHandlers.cs"));
        var messages = File.ReadAllText(Path.Combine(sampleRoot, "Shared", "Contracts", "Messages.cs"));
        var clientScenario = File.ReadAllText(Path.Combine(sampleRoot, "Client", "ShoppingMallClientScenario.cs"));
        var stores = File.ReadAllText(Path.Combine(sampleRoot, "Server", "Shared", "Store", "RedisCommerceStores.cs"));
        var readme = File.ReadAllText(Path.Combine(sampleRoot, "README.ko.md"));

        Assert.Contains("RUN_ID=\"$(basename \"${RUN_DIR}\")-$$-${RANDOM}\"", shellRunner, StringComparison.Ordinal);
        Assert.Contains("SAMPLE_LOG_DIR=\"${RUN_DIR}/sample-logs\"", shellRunner, StringComparison.Ordinal);
        Assert.Contains("export SHOPPINGMALL_LOG_DIR=\"${SAMPLE_LOG_DIR}\"", shellRunner, StringComparison.Ordinal);
        Assert.Contains("export SHOPPINGMALL_REDIS_KEY_PREFIX=\"shoppingmall:dotnet:${RUN_ID}:\"", shellRunner,
            StringComparison.Ordinal);
        Assert.Contains("REDIS_CONTAINER=\"zlink-shoppingmall-dotnet-redis-${RUN_ID}\"", shellRunner,
            StringComparison.Ordinal);
        Assert.Contains("docker run -d --rm --name \"${REDIS_CONTAINER}\"", shellRunner, StringComparison.Ordinal);
        Assert.Contains("-p \"127.0.0.1::6379\"", shellRunner, StringComparison.Ordinal);
        Assert.Contains("if [[ -n \"${REDIS_CONTAINER}\" ]]; then", shellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("SHOPPINGMALL_BASE_PORT", shellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("${SHOPPINGMALL_API_A_HTTP_URL:-", shellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("${SHOPPINGMALL_API_B_HTTP_URL:-", shellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("${SHOPPINGMALL_API_A_ROUTE_ENDPOINT:-", shellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("${SHOPPINGMALL_API_B_ROUTE_ENDPOINT:-", shellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("${SHOPPINGMALL_WORKFLOW_A_HTTP_URL:-", shellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("${SHOPPINGMALL_WORKFLOW_B_HTTP_URL:-", shellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("${SHOPPINGMALL_WORKFLOW_A_ROUTE_ENDPOINT:-", shellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("${SHOPPINGMALL_WORKFLOW_B_ROUTE_ENDPOINT:-", shellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("${SHOPPINGMALL_WORKFLOW_A_SPOT_ENDPOINT:-", shellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("${SHOPPINGMALL_WORKFLOW_A_SPOT_ROUTER_ENDPOINT:-", shellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("${SHOPPINGMALL_WORKFLOW_B_SPOT_ENDPOINT:-", shellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("${SHOPPINGMALL_WORKFLOW_B_SPOT_ROUTER_ENDPOINT:-", shellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("${SHOPPINGMALL_REDIS_KEY_PREFIX:-", shellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("${SHOPPINGMALL_LOG_DIR:-", shellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("SHOPPINGMALL_STORE_DIR", shellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("rm -f \"${SHOPPINGMALL_LOG_DIR}\"/*.log", shellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("SHOPPINGMALL_STARTUP_DELAY_SECONDS", shellRunner, StringComparison.Ordinal);
        Assert.Contains("shoppingmall=completed", shellRunner, StringComparison.Ordinal);
        Assert.Contains("shoppingmall-server-evidence=completed", shellRunner, StringComparison.Ordinal);
        Assert.Contains("grep -Rq \"message flow\" \"${SHOPPINGMALL_LOG_DIR}\"", shellRunner,
            StringComparison.Ordinal);

        Assert.Contains("$RunId = \"$PID-$([Guid]::NewGuid().ToString('N'))\"", powershellRunner,
            StringComparison.Ordinal);
        Assert.Contains("$SampleLogDir = Join-Path $RunDir \"sample-logs\"", powershellRunner,
            StringComparison.Ordinal);
        Assert.Contains("$ports = New-SamplePorts -Count 12 -BasePort 0", powershellRunner,
            StringComparison.Ordinal);
        Assert.Contains("$env:SHOPPINGMALL_LOG_DIR = $SampleLogDir", powershellRunner, StringComparison.Ordinal);
        Assert.Contains("$env:SHOPPINGMALL_REDIS_KEY_PREFIX = \"shoppingmall:dotnet:${RunId}:\"",
            powershellRunner, StringComparison.Ordinal);
        Assert.Contains("$RedisContainer = \"zlink-shoppingmall-dotnet-redis-$RunId\"", powershellRunner,
            StringComparison.Ordinal);
        Assert.Contains("docker run -d --rm --name $RedisContainer", powershellRunner, StringComparison.Ordinal);
        Assert.Contains("-p \"127.0.0.1::6379\"", powershellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("Set-DefaultEnv", powershellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("$env:SHOPPINGMALL_BASE_PORT", powershellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("$env:SHOPPINGMALL_STORE_DIR", powershellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("SHOPPINGMALL_STARTUP_DELAY_SECONDS", powershellRunner, StringComparison.Ordinal);
        Assert.Contains("Assert-SampleLogContains -LogDirectory $SampleLogDir -Pattern \"shoppingmall=completed\"",
            powershellRunner, StringComparison.Ordinal);
        Assert.Contains("Join-Path $LogDir \"workflow-a.out.log\"", powershellRunner, StringComparison.Ordinal);
        Assert.Contains("Join-Path $LogDir \"workflow-b.out.log\"", powershellRunner, StringComparison.Ordinal);
        Assert.Contains("workflow-a did not record a shoppingmall order start", powershellRunner,
            StringComparison.Ordinal);
        Assert.Contains("workflow-b did not record a shoppingmall order start", powershellRunner,
            StringComparison.Ordinal);
        Assert.Contains("Assert-SampleLogContains -LogDirectory $SampleLogDir -Pattern \"message flow\"",
            powershellRunner, StringComparison.Ordinal);

        Assert.Contains("SHOPPINGMALL_REDIS_ENDPOINT", topology, StringComparison.Ordinal);
        Assert.Contains("SHOPPINGMALL_REDIS_KEY_PREFIX", topology, StringComparison.Ordinal);
        Assert.DoesNotContain("SHOPPINGMALL_STORE_DIR", topology, StringComparison.Ordinal);
        Assert.Contains("new RedisCommerceStores(topology)", commerceApi, StringComparison.Ordinal);
        Assert.Contains("new RedisCommerceStores(topology)", workflow, StringComparison.Ordinal);
        Assert.Contains("/self-check/workflow/inventory-reserved", commerceApi, StringComparison.Ordinal);
        Assert.Contains("/self-check/workflow/inventory-reserved", workflow, StringComparison.Ordinal);
        Assert.Contains("/self-check/workflow/{orderId}/continue", commerceApi, StringComparison.Ordinal);
        Assert.Contains("PrepareInventoryReservedOrderUseCase", commerceApi, StringComparison.Ordinal);
        Assert.Contains("IOrderWorkflowSelfCheckClient", commerceWorkflowPorts, StringComparison.Ordinal);
        Assert.Contains("HttpOrderWorkflowSelfCheckClient", commerceSelfCheckClient, StringComparison.Ordinal);
        Assert.DoesNotContain("var cart = await commerce.GetCartAsync", commerceApi, StringComparison.Ordinal);
        Assert.DoesNotContain("ReserveIdempotencyAsync", commerceApi, StringComparison.Ordinal);
        Assert.Contains("evidence.StartedIdempotencyCount == 7", commerceApi, StringComparison.Ordinal);
        Assert.Contains("owners={topology.ForOrderId(request.SuccessfulOrderId).InstanceId},{topology.ForOrderId(request.ScaleOutOrderId).InstanceId}",
            commerceApi, StringComparison.Ordinal);
        Assert.Contains("ownersDiffer", commerceApi, StringComparison.Ordinal);
        Assert.DoesNotContain("ServerAssertionReq", messages, StringComparison.Ordinal);
        Assert.DoesNotContain("ServerAssertionRes", messages, StringComparison.Ordinal);
        Assert.Contains("internal sealed record ServerAssertionReq", commerceApi, StringComparison.Ordinal);
        Assert.Contains("internal sealed record ServerAssertionReq", clientScenario, StringComparison.Ordinal);
        Assert.DoesNotContain("StartOrderWorkflowToInventoryReq", messages, StringComparison.Ordinal);
        Assert.DoesNotContain("StartOrderWorkflowToInventoryRes", messages, StringComparison.Ordinal);
        Assert.DoesNotContain("StartOrderWorkflowToInventoryRouteHandler", routeHandlers, StringComparison.Ordinal);
        Assert.DoesNotContain("StartToInventoryAsync", workflowService, StringComparison.Ordinal);
        Assert.Contains("OrderWorkflowSelfCheckService", workflow, StringComparison.Ordinal);
        Assert.Contains("PrepareInventoryReservedCheckpointReq", workflowSpot, StringComparison.Ordinal);
        Assert.Contains("ContinueUntilInventoryReservedAsync(command.OrderId, cancellationToken)", workflowSelfCheck,
            StringComparison.Ordinal);
        Assert.Contains("RequestToSpot(SampleNames.OrderWorkflowRouteChannel", routeHandlers, StringComparison.Ordinal);
        Assert.DoesNotContain("workflow.StartAsync(request", routeHandlers, StringComparison.Ordinal);
        Assert.DoesNotContain("workflow.ContinueAsync(request", routeHandlers, StringComparison.Ordinal);
        Assert.DoesNotContain("workflow.RebuildProjectionAsync", routeHandlers, StringComparison.Ordinal);
        Assert.Contains("StartAndContinueAsync", workflowService, StringComparison.Ordinal);
        Assert.DoesNotContain("ContinueAfterStartAsync", workflowSpot, StringComparison.Ordinal);
        Assert.Contains("!string.Equals(mapping.OwnerInstanceId, options.InstanceId, StringComparison.Ordinal)",
            startUseCase, StringComparison.Ordinal);
        Assert.Contains("peers.ForwardStartAsync(mapping.OwnerInstanceId, request, cancellationToken)",
            startUseCase, StringComparison.Ordinal);
        Assert.Contains("Task.WhenAll(concurrentA, concurrentB)", clientScenario, StringComparison.Ordinal);
        Assert.Contains("concurrentA.Result.OrderId == concurrentB.Result.OrderId", clientScenario,
            StringComparison.Ordinal);
        Assert.Contains("/self-check/workflow/inventory-reserved", clientScenario, StringComparison.Ordinal);
        Assert.Contains($"/self-check/workflow/{{inventoryReserved.OrderId}}/continue", clientScenario,
            StringComparison.Ordinal);
        Assert.Contains("resumed.State.ReservationId == $\"reservation-{inventoryReserved.OrderId}\"",
            clientScenario, StringComparison.Ordinal);
        Assert.Contains("resumed.State.PaymentId == $\"payment-{inventoryReserved.OrderId}\"",
            clientScenario, StringComparison.Ordinal);
        Assert.Contains($"/self-check/workflow/{{success.OrderId}}/continue", clientScenario,
            StringComparison.Ordinal);
        Assert.Contains("healedByContinue.State.Status == OrderStatuses.Confirmed", clientScenario,
            StringComparison.Ordinal);
        Assert.Contains("OrderStatuses.InventoryReserved", clientScenario, StringComparison.Ordinal);
        Assert.Contains("OrderStatuses.PaymentAuthorized", clientScenario, StringComparison.Ordinal);
        Assert.Contains("SaveProjectionFromEventsAsync(stored, cancellationToken)", workflowService,
            StringComparison.Ordinal);
        Assert.Contains("static status => status is OrderStatuses.Confirmed or OrderStatuses.Failed",
            workflowService, StringComparison.Ordinal);
        Assert.Contains("CancellationToken.None", workflowService, StringComparison.Ordinal);
        Assert.Contains("ContinueWorkflowInBackgroundAsync", workflowService, StringComparison.Ordinal);
        Assert.Contains("catch (OrderStreamVersionConflictException)", workflowService, StringComparison.Ordinal);
        Assert.Contains("throw new OrderStreamVersionConflictException", stores, StringComparison.Ordinal);
        Assert.Contains("if (aggregate.HasProcessedMsg(command.IdempotencyKey))", workflowService,
            StringComparison.Ordinal);
        Assert.Contains("await commerce.MarkIdempotencyStartedAsync(command.IdempotencyKey, cancellationToken);",
            workflowService, StringComparison.Ordinal);
        Assert.Contains("return await SaveProjectionFromEventsAsync(stored, cancellationToken);", workflowService,
            StringComparison.Ordinal);
        Assert.Contains("OrderStatuses.PaymentFailed => await ReleaseInventoryAsync", workflowService,
            StringComparison.Ordinal);
        Assert.Contains("OrderStatuses.InventoryReleased => aggregate.FailAfterInventoryRelease", workflowService,
            StringComparison.Ordinal);
        Assert.Contains("public sealed class RedisCommerceStores", stores, StringComparison.Ordinal);
        Assert.Contains("ConnectionMultiplexer.Connect(topology.RedisEndpoint)", stores, StringComparison.Ordinal);
        Assert.Contains("topology.RedisKeyPrefix", stores, StringComparison.Ordinal);
        Assert.Contains("_database.LockTakeAsync", stores, StringComparison.Ordinal);
        Assert.Contains("string reservationId", stores, StringComparison.Ordinal);
        Assert.Contains("string paymentId", stores, StringComparison.Ordinal);
        Assert.Contains("state.Reservations.TryGetValue(reservationId", stores, StringComparison.Ordinal);
        Assert.Contains("InventoryReservationLine", stores, StringComparison.Ordinal);
        Assert.Contains("state.Inventory[line.Sku] = state.Inventory.GetValueOrDefault(line.Sku) + line.Quantity",
            stores, StringComparison.Ordinal);
        Assert.Contains("state.Payments.ContainsKey(paymentId)", stores, StringComparison.Ordinal);
        Assert.Contains("AcquireLockAsync(cancellationToken)", stores, StringComparison.Ordinal);
        Assert.Contains("Task.Delay(10, cancellationToken)", stores, StringComparison.Ordinal);
        Assert.DoesNotContain("File.ReadAllText", stores, StringComparison.Ordinal);
        Assert.DoesNotContain("File.WriteAllText", stores, StringComparison.Ordinal);
        Assert.DoesNotContain("FileStream", stores, StringComparison.Ordinal);

        Assert.Contains("주문 이벤트 스트림, 조회 모델, 장바구니·재고·결제·멱등 상태도 같은 Redis", readme,
            StringComparison.Ordinal);
        Assert.Contains("외부 Redis endpoint 재사용 mode는 제공하지 않는다", readme, StringComparison.Ordinal);
        Assert.Contains("동시에 도는 다른 테스트와 섞이지 않는다", readme, StringComparison.Ordinal);
        Assert.Contains("같은 멱등 키의 동시 시작 경쟁", readme, StringComparison.Ordinal);
        Assert.Contains("`InventoryReserved` 이후 명시 재개", readme, StringComparison.Ordinal);
    }

    [Fact]
    public void GameQuest_Runner_Uses_Isolated_Docker_Redis_And_Stream_Actions()
    {
        var sampleRoot = ResolveSampleRoot("GameQuest");
        var shellRunner = File.ReadAllText(Path.Combine(sampleRoot, "run_sample.sh"));
        var powershellRunner = File.ReadAllText(Path.Combine(sampleRoot, "run_sample.ps1"));
        var messages = File.ReadAllText(Path.Combine(sampleRoot, "Shared", "Messages.cs"));
        var clientScenario = File.ReadAllText(Path.Combine(sampleRoot, "Client", "GameQuestClientScenario.cs"));
        var sessionHandlers = File.ReadAllText(Path.Combine(sampleRoot, "Server", "GameApi", "Session",
            "GameQuestSessionHandlers.cs"));
        var topology = File.ReadAllText(Path.Combine(sampleRoot, "Server", "Configuration", "SampleConfiguration.cs"));
        var gameApiStore = File.ReadAllText(Path.Combine(sampleRoot, "Server", "GameApi", "Infrastructure", "Store",
            "GameQuestStores.cs"));
        var questStore = File.ReadAllText(Path.Combine(sampleRoot, "Server", "QuestMission", "Infrastructure", "Store",
            "QuestStores.cs"));
        var questProcessor = File.ReadAllText(Path.Combine(sampleRoot, "Server", "QuestMission", "Application",
            "QuestEventProcessor.cs"));
        var redisJsonStore = File.ReadAllText(Path.Combine(sampleRoot, "Server", "Configuration", "RedisJsonStore.cs"));
        var gameApiProgram = File.ReadAllText(Path.Combine(sampleRoot, "Server", "GameApi", "Program.cs"));
        var missionProgram = File.ReadAllText(Path.Combine(sampleRoot, "Server", "QuestMission", "Program.cs"));
        var playerQuestProvisioner = File.ReadAllText(Path.Combine(sampleRoot, "Server", "QuestMission",
            "Infrastructure", "ZLink", "PlayerQuestSpotProvisioner.cs"));
        var gameplayIngress = missionProgram;
        var playerQuestSpot = File.ReadAllText(Path.Combine(sampleRoot, "Server", "QuestMission", "Infrastructure",
            "ZLink", "Spots", "PlayerQuestSpot", "PlayerQuestSpot.cs"));
        var questDomain = File.ReadAllText(Path.Combine(sampleRoot, "Server", "QuestMission", "Domain",
            "QuestDomain.cs"));
        var actionService = File.ReadAllText(Path.Combine(sampleRoot, "Server", "GameApi", "Application",
            "GameplayActionService.cs"));
        var eventDispatcher = File.ReadAllText(Path.Combine(sampleRoot, "Server", "GameApi", "Infrastructure",
            "ZLink", "GameplayEventOwnerDispatcher.cs"));
        var readme = File.ReadAllText(Path.Combine(sampleRoot, "README.ko.md"));

        Assert.Contains("RUN_ID=\"$(basename \"${RUN_DIR}\")-$$-${RANDOM}\"", shellRunner, StringComparison.Ordinal);
        Assert.Contains("SAMPLE_LOG_DIR=\"${RUN_DIR}/sample-logs\"", shellRunner, StringComparison.Ordinal);
        Assert.Contains("export GAMEQUEST_LOG_DIR=\"${SAMPLE_LOG_DIR}\"", shellRunner, StringComparison.Ordinal);
        Assert.Contains("export GAMEQUEST_REDIS_KEY_PREFIX=\"gamequest:dotnet:${RUN_ID}:\"", shellRunner,
            StringComparison.Ordinal);
        Assert.Contains("REDIS_CONTAINER=\"zlink-gamequest-dotnet-redis-${RUN_ID}\"", shellRunner,
            StringComparison.Ordinal);
        Assert.Contains("-p \"127.0.0.1::6379\"", shellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("GAMEQUEST_BASE_PORT", shellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("${GAMEQUEST_GAMEAPI_A_HTTP_BASE_URL:-", shellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("${GAMEQUEST_REDIS_KEY_PREFIX:-", shellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("GAMEQUEST_STORE_DIR", shellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("GAMEQUEST_STARTUP_DELAY_SECONDS", shellRunner, StringComparison.Ordinal);
        Assert.Contains("grep -Rq \"message flow\" \"${SAMPLE_LOG_DIR}\"", shellRunner, StringComparison.Ordinal);

        Assert.Contains("$RunId = \"$PID-$([Guid]::NewGuid().ToString('N'))\"", powershellRunner,
            StringComparison.Ordinal);
        Assert.Contains("$SampleLogDir = Join-Path $RunDir \"sample-logs\"", powershellRunner,
            StringComparison.Ordinal);
        Assert.Contains("$ports = New-SamplePorts -Count 16 -BasePort 0", powershellRunner,
            StringComparison.Ordinal);
        Assert.Contains("$env:GAMEQUEST_REDIS_KEY_PREFIX = \"gamequest:dotnet:${RunId}:\"", powershellRunner,
            StringComparison.Ordinal);
        Assert.Contains("$RedisContainer = \"zlink-gamequest-dotnet-redis-$RunId\"", powershellRunner,
            StringComparison.Ordinal);
        Assert.DoesNotContain("Set-DefaultEnv", powershellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("GAMEQUEST_BASE_PORT", powershellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("GAMEQUEST_STORE_DIR", powershellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("GAMEQUEST_STARTUP_DELAY_SECONDS", powershellRunner, StringComparison.Ordinal);
        Assert.Contains("Assert-SampleLogContains -LogDirectory $SampleLogDir -Pattern \"message flow\"",
            powershellRunner, StringComparison.Ordinal);

        Assert.Contains("record JoinSessionReq", messages, StringComparison.Ordinal);
        Assert.Contains("record JoinSessionRes", messages, StringComparison.Ordinal);
        Assert.DoesNotContain("SubscribeQuestReq", messages, StringComparison.Ordinal);
        Assert.DoesNotContain("SubscribeQuestRes", messages, StringComparison.Ordinal);
        Assert.DoesNotContain("ApplyGameplayEventReq", messages, StringComparison.Ordinal);
        Assert.DoesNotContain("ApplyGameplayEventRes", messages, StringComparison.Ordinal);
        Assert.Contains("record ApplyGameplayEventReq", topology, StringComparison.Ordinal);
        Assert.Contains("record ApplyGameplayEventRes", topology, StringComparison.Ordinal);
        Assert.Contains("apiAStream.Request(new JoinSessionReq", clientScenario, StringComparison.Ordinal);
        Assert.Contains("apiAStream.Request(new KillMonsterReq", clientScenario, StringComparison.Ordinal);
        Assert.Contains("apiAStream.Request(new UnlockFeatureReq", clientScenario, StringComparison.Ordinal);
        Assert.Contains("apiAStream.Request(new CompleteMissionReq", clientScenario, StringComparison.Ordinal);
        Assert.Contains("apiAStream.Request(new EnterAreaReq", clientScenario, StringComparison.Ordinal);
        Assert.Contains("apiBStream.Request(new CollectItemReq", clientScenario, StringComparison.Ordinal);
        Assert.Contains("/self-check/owner/player-alice/close", clientScenario, StringComparison.Ordinal);
        Assert.DoesNotContain("Post(\"/combat/kill\")", clientScenario, StringComparison.Ordinal);
        Assert.DoesNotContain("Post(\"/inventory/collect\")", clientScenario, StringComparison.Ordinal);
        Assert.Contains("class JoinSessionHandler", sessionHandlers, StringComparison.Ordinal);
        Assert.Contains("JoinQuestSessionUseCase", sessionHandlers, StringComparison.Ordinal);
        Assert.DoesNotContain("SubscribeQuestHandler", sessionHandlers, StringComparison.Ordinal);
        Assert.Contains("class KillMonsterHandler", sessionHandlers, StringComparison.Ordinal);
        Assert.Contains("class CollectItemHandler", sessionHandlers, StringComparison.Ordinal);
        Assert.Contains("class CompleteMissionHandler", sessionHandlers, StringComparison.Ordinal);
        Assert.Contains("class EnterAreaHandler", sessionHandlers, StringComparison.Ordinal);
        Assert.Contains("class UnlockFeatureHandler", sessionHandlers, StringComparison.Ordinal);

        Assert.DoesNotContain("MapPost(\"/combat/kill\"", gameApiProgram, StringComparison.Ordinal);
        Assert.DoesNotContain("MapPost(\"/inventory/collect\"", gameApiProgram, StringComparison.Ordinal);
        Assert.DoesNotContain("MapPost(\"/mission/complete\"", gameApiProgram, StringComparison.Ordinal);
        Assert.DoesNotContain("MapPost(\"/world/enter\"", gameApiProgram, StringComparison.Ordinal);
        Assert.DoesNotContain("MapPost(\"/feature/unlock\"", gameApiProgram, StringComparison.Ordinal);
        Assert.DoesNotContain("AddFanoutChannel", gameApiProgram, StringComparison.Ordinal);
        Assert.DoesNotContain("AddFanoutChannel", missionProgram, StringComparison.Ordinal);
        Assert.DoesNotContain("AddRouteMeshChannel", gameApiProgram, StringComparison.Ordinal);
        Assert.DoesNotContain("AddRouteMeshChannel", missionProgram, StringComparison.Ordinal);

        Assert.DoesNotContain("StoreDirectory", topology, StringComparison.Ordinal);
        Assert.DoesNotContain("GAMEQUEST_STORE_DIR", topology, StringComparison.Ordinal);
        Assert.Contains("new RedisJsonStore(topology.RedisEndpoint)", gameApiStore, StringComparison.Ordinal);
        Assert.Contains("new RedisJsonStore(topology.RedisEndpoint)", questStore, StringComparison.Ordinal);
        Assert.Contains("topology.RedisKeyPrefix", gameApiStore, StringComparison.Ordinal);
        Assert.Contains("topology.RedisKeyPrefix", questStore, StringComparison.Ordinal);
        Assert.Contains("_database.LockTakeAsync", redisJsonStore, StringComparison.Ordinal);
        Assert.DoesNotContain("_database.LockTakeAsync", gameApiStore, StringComparison.Ordinal);
        Assert.DoesNotContain("_database.LockTakeAsync", questStore, StringComparison.Ordinal);
        Assert.DoesNotContain("File.ReadAllText", gameApiStore, StringComparison.Ordinal);
        Assert.DoesNotContain("File.WriteAllText", gameApiStore, StringComparison.Ordinal);
        Assert.DoesNotContain("File.ReadAllText", questStore, StringComparison.Ordinal);
        Assert.DoesNotContain("File.WriteAllText", questStore, StringComparison.Ordinal);
        Assert.Contains("MapPost(\"/internal/apply\"", missionProgram, StringComparison.Ordinal);
        Assert.Contains("playerQuestOwners.ApplyGameplayEventAsync", gameplayIngress, StringComparison.Ordinal);
        Assert.Contains("ownerRouter.IsLocalOwner", gameplayIngress, StringComparison.Ordinal);
        Assert.Contains("IGameplayEventOwnerDispatcher", actionService, StringComparison.Ordinal);
        Assert.Contains("Post(\"/internal/apply\")", eventDispatcher, StringComparison.Ordinal);
        Assert.Contains("topology.OwnerRouteRid(gameplayEvent.PlayerId)", eventDispatcher, StringComparison.Ordinal);
        Assert.DoesNotContain("interface IPlayerQuestOwnerProvisioner", questProcessor, StringComparison.Ordinal);
        Assert.DoesNotContain("IPlayerQuestOwnerProvisioner", gameplayIngress, StringComparison.Ordinal);
        Assert.DoesNotContain("ValueTask EnsureAsync", questProcessor, StringComparison.Ordinal);
        Assert.DoesNotContain("public async ValueTask EnsureAsync", playerQuestProvisioner,
            StringComparison.Ordinal);
        Assert.Contains("RequestToSpot(SampleNames.QuestSpotDiscovery", playerQuestProvisioner,
            StringComparison.Ordinal);
        Assert.Contains("IZLinkSpotRequestHandler<PlayerQuestSpot, ApplyGameplayEventReq, ApplyGameplayEventRes>",
            playerQuestSpot, StringComparison.Ordinal);
        Assert.Contains("processor.ProcessAsync(request.Event, cancellationToken)", playerQuestSpot,
            StringComparison.Ordinal);
        Assert.Contains("ReadQuestStreamAsync(gameplayEvent.PlayerId, definition.QuestId", questProcessor,
            StringComparison.Ordinal);
        Assert.Contains("Replay(QuestDefinition definition, IReadOnlyList<StoredQuestEvent> stream)", questDomain,
            StringComparison.Ordinal);
        Assert.Contains("RecordOwnerRehydratedAsync", playerQuestSpot, StringComparison.Ordinal);
        Assert.Contains("spots.CloseAsync(spotRid, cancellationToken)", missionProgram, StringComparison.Ordinal);
        Assert.Contains("ReadOwnerRehydrateEvidenceAsync", gameApiProgram, StringComparison.Ordinal);
        Assert.Contains("rehydrates.GetValueOrDefault(\"player-alice\") >= 2", gameApiProgram, StringComparison.Ordinal);
        Assert.Contains("rehydrated:{pair.Key}:{pair.Value}", gameApiProgram, StringComparison.Ordinal);

        Assert.Contains("외부 Redis endpoint", readme, StringComparison.Ordinal);
        Assert.Contains("재사용 mode는 제공하지 않는다", readme, StringComparison.Ordinal);
        Assert.Contains("같은 stream으로", readme, StringComparison.Ordinal);
    }

    [Fact]
    public void DeliveryDispatch_Runner_Reuses_External_Redis_And_Uses_Location_Store()
    {
        var sampleRoot = ResolveSampleRoot("DeliveryDispatch");
        var shellRunner = File.ReadAllText(Path.Combine(sampleRoot, "run_sample.sh"));
        var powershellRunner = File.ReadAllText(Path.Combine(sampleRoot, "run_sample.ps1"));
        var readme = File.ReadAllText(Path.Combine(sampleRoot, "README.ko.md"));
        var topology = File.ReadAllText(Path.Combine(sampleRoot, "Server", "Configuration", "SampleTopology.cs"));

        Assert.Contains("if [[ -z \"${DELIVERYDISPATCH_REDIS_ENDPOINT:-}\" ]]", shellRunner,
            StringComparison.Ordinal);
        Assert.Contains("docker run -d --rm --name \"${REDIS_CONTAINER}\"", shellRunner, StringComparison.Ordinal);
        Assert.Contains("if [[ -n \"${REDIS_CONTAINER}\" ]]; then", shellRunner, StringComparison.Ordinal);
        Assert.Contains("DELIVERYDISPATCH_REDIS_KEY_PREFIX", shellRunner, StringComparison.Ordinal);
        Assert.Contains("deliverydispatch=completed", shellRunner, StringComparison.Ordinal);
        Assert.Contains("topology=ready", shellRunner, StringComparison.Ordinal);
        Assert.Contains("deliverydispatch-reassignment=completed", shellRunner, StringComparison.Ordinal);
        Assert.Contains("deliverydispatch-server-evidence=completed", shellRunner, StringComparison.Ordinal);
        Assert.Contains("deliverydispatch-runner-evidence=completed", shellRunner, StringComparison.Ordinal);
        Assert.Contains("grep -Rq \"message flow\" \"${DELIVERYDISPATCH_LOG_DIR}\"", shellRunner,
            StringComparison.Ordinal);
        Assert.DoesNotContain("DELIVERYDISPATCH_STARTUP_DELAY_SECONDS", shellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("DELIVERYDISPATCH_STARTUP_SETTLE_SECONDS", shellRunner, StringComparison.Ordinal);

        Assert.Contains("if (-not $env:DELIVERYDISPATCH_REDIS_ENDPOINT)", powershellRunner,
            StringComparison.Ordinal);
        Assert.Contains("docker run -d --rm --name $RedisContainer", powershellRunner, StringComparison.Ordinal);
        Assert.Contains("if ($RedisContainer)", powershellRunner, StringComparison.Ordinal);
        Assert.Contains("$env:DELIVERYDISPATCH_LOG_DIR = $SampleLogDir", powershellRunner,
            StringComparison.Ordinal);
        Assert.Contains("DELIVERYDISPATCH_REDIS_KEY_PREFIX", powershellRunner, StringComparison.Ordinal);
        Assert.Contains("deliverydispatch=completed", powershellRunner, StringComparison.Ordinal);
        Assert.Contains("topology=ready", powershellRunner, StringComparison.Ordinal);
        Assert.Contains("deliverydispatch-reassignment=completed", powershellRunner, StringComparison.Ordinal);
        Assert.Contains("deliverydispatch-server-evidence=completed", powershellRunner, StringComparison.Ordinal);
        Assert.Contains("deliverydispatch-runner-evidence=completed", powershellRunner, StringComparison.Ordinal);
        Assert.Contains("Wait-SampleLogContains \"message flow\" \"DeliveryDispatch message-flow evidence\"",
            powershellRunner, StringComparison.Ordinal);
        Assert.Contains("Select-String -Pattern $Pattern -List", powershellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("DELIVERYDISPATCH_STARTUP_DELAY_SECONDS", powershellRunner,
            StringComparison.Ordinal);
        Assert.DoesNotContain("DELIVERYDISPATCH_STARTUP_SETTLE_SECONDS", powershellRunner,
            StringComparison.Ordinal);

        Assert.Contains("DELIVERYDISPATCH_REDIS_ENDPOINT", topology, StringComparison.Ordinal);
        Assert.Contains("DELIVERYDISPATCH_REDIS_KEY_PREFIX", topology, StringComparison.Ordinal);
        foreach (var hostFactory in Directory.EnumerateFiles(Path.Combine(sampleRoot, "Server"), "*HostFactory.cs",
                     SearchOption.AllDirectories))
        {
            AssertLocationStoreHost(File.ReadAllText(hostFactory));
        }

        Assert.Contains("`DELIVERYDISPATCH_REDIS_ENDPOINT`가 이미 있으면", readme, StringComparison.Ordinal);
        Assert.Contains("그 Redis를 재사용하고 정리하지 않는다", readme, StringComparison.Ordinal);
        Assert.Contains("`run_sample.sh`와 `run_sample.ps1`", readme, StringComparison.Ordinal);
        Assert.Contains("AssignDeliveryMsg", readme, StringComparison.Ordinal);
        Assert.DoesNotContain("`AssignDelivery`", readme, StringComparison.Ordinal);
    }

    [Fact]
    public void DeliveryDispatch_Contracts_Match_Common_Role_Model()
    {
        var sampleRoot = ResolveSampleRoot("DeliveryDispatch");
        var readme = File.ReadAllText(Path.Combine(sampleRoot, "README.ko.md"));
        var shellRunner = File.ReadAllText(Path.Combine(sampleRoot, "run_sample.sh"));
        var powershellRunner = File.ReadAllText(Path.Combine(sampleRoot, "run_sample.ps1"));
        var topology = File.ReadAllText(Path.Combine(sampleRoot, "Server", "Configuration", "SampleTopology.cs"));
        var messages = File.ReadAllText(Path.Combine(sampleRoot, "Shared", "Contracts", "Messages.cs"));
        var courierRoutes = File.ReadAllText(Path.Combine(sampleRoot, "Server", "CourierActorNode", "RouteHandlers.cs"));
        var customerSession = File.ReadAllText(Path.Combine(sampleRoot, "Server", "CustomerGateway",
            "SubscribeDeliverySessionHandler.cs"));
        var customerStatusHandler = File.ReadAllText(Path.Combine(sampleRoot, "Server", "CustomerGateway",
            "Spots", "EntrySpot", "Handlers", "DeliveryStatusUpdatedHandler.cs"));
        var customerAccess = File.ReadAllText(Path.Combine(sampleRoot, "Server", "CustomerGateway",
            "CustomerActorAccess.cs"));
        var clientScenario = File.ReadAllText(Path.Combine(sampleRoot, "Client", "DeliveryDispatchClientScenario.cs"));

        Assert.Contains("record AssignDeliveryMsg", messages, StringComparison.Ordinal);
        Assert.Contains("record FindCourierActorReq", messages, StringComparison.Ordinal);
        Assert.Contains("record FindCustomerActorReq", messages, StringComparison.Ordinal);
        Assert.Contains("record DeliveryStatusUpdatedMsg", messages, StringComparison.Ordinal);
        Assert.Contains("record BindCourierSessionReq", messages, StringComparison.Ordinal);
        Assert.Contains("record BindCourierSessionRes", messages, StringComparison.Ordinal);
        Assert.Contains("ActorRefSnapshot? Actor = null", messages, StringComparison.Ordinal);
        Assert.Contains("string? SessionRoute = null", messages, StringComparison.Ordinal);
        Assert.DoesNotContain("record DeliveryStatusUpdatedRes", messages, StringComparison.Ordinal);
        Assert.DoesNotContain("record BindCourierReq", messages, StringComparison.Ordinal);
        Assert.DoesNotContain("record ReassignDelivery", messages, StringComparison.Ordinal);
        Assert.Contains("FindAsync(request.CourierId", courierRoutes, StringComparison.Ordinal);
        Assert.Contains("FindAsync(request.CustomerId", customerAccess, StringComparison.Ordinal);
        Assert.Contains("GetOrCreateAsync(", customerAccess, StringComparison.Ordinal);
        Assert.Contains("new FindCustomerActorReq(CustomerId)", customerSession, StringComparison.Ordinal);
        Assert.Contains("new EnsureCustomerActorReq(CustomerId)", customerSession, StringComparison.Ordinal);
        Assert.Contains("IZLinkSpotPacketHandler<CustomerEntrySpot, DeliveryStatusUpdatedMsg>",
            customerStatusHandler, StringComparison.Ordinal);
        Assert.Contains(".Actor.NodeRid", clientScenario, StringComparison.Ordinal);
        Assert.Contains("WaitForStatusAsync(customer, deliveryId, DeliveryStatus.Assigned", clientScenario,
            StringComparison.Ordinal);
        Assert.Contains("WaitForStatusAsync(customer, deliveryId, DeliveryStatus.Reassigned", clientScenario,
            StringComparison.Ordinal);
        Assert.Contains("deliverydispatch courier-session: bound courier=courier-a", shellRunner,
            StringComparison.Ordinal);
        Assert.Contains("deliverydispatch courier-session: bound courier=courier-b", shellRunner,
            StringComparison.Ordinal);
        Assert.DoesNotContain("CourierGateway", readme, StringComparison.Ordinal);
        Assert.DoesNotContain("deliverydispatch.courier", readme, StringComparison.Ordinal);
        Assert.DoesNotContain("courier-gateway", shellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("courier-gateway", powershellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("DELIVERYDISPATCH_COURIER_ROUTE", topology, StringComparison.Ordinal);
        Assert.DoesNotContain("DELIVERYDISPATCH_CUSTOMER_ROUTE", topology, StringComparison.Ordinal);
        Assert.DoesNotContain("DELIVERYDISPATCH_COURIER_ROUTE", shellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("DELIVERYDISPATCH_CUSTOMER_ROUTE", shellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("DELIVERYDISPATCH_COURIER_ROUTE", powershellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("DELIVERYDISPATCH_CUSTOMER_ROUTE", powershellRunner, StringComparison.Ordinal);
        Assert.False(Directory.Exists(Path.Combine(sampleRoot, "Server", "CourierGateway")));
    }

    [Fact]
    public void DeliveryDispatch_CourierSession_Bind_Registers_Binder_And_Replies_To_Client()
    {
        var sampleRoot = ResolveSampleRoot("DeliveryDispatch");
        var courierSessionBinder = File.ReadAllText(Path.Combine(sampleRoot, "Server", "CourierSession",
            "CourierSessionBinder.cs"));
        var courierSessionHandler = File.ReadAllText(Path.Combine(sampleRoot, "Server", "CourierSession",
            "BindCourierSessionHandler.cs"));
        var courierSessionHost = File.ReadAllText(Path.Combine(sampleRoot, "Server", "CourierSession",
            "CourierSessionHostFactory.cs"));

        Assert.Contains("new FindCourierActorReq(courierId)", courierSessionBinder, StringComparison.Ordinal);
        Assert.Contains("new EnsureCourierActorReq(courierId)", courierSessionBinder, StringComparison.Ordinal);
        Assert.Contains("context.Actors.BindOrGetAsync", courierSessionBinder, StringComparison.Ordinal);
        Assert.Contains("builder.Services.AddSingleton<CourierSessionBinder>()", courierSessionHost,
            StringComparison.Ordinal);
        Assert.Contains("await binder.BindAsync(request.CourierId", courierSessionHandler, StringComparison.Ordinal);
        Assert.Contains("context.Client.Reply(bound).Submit()", courierSessionHandler, StringComparison.Ordinal);
    }

    [Fact]
    public void DeliveryDispatch_Dispatch_Uses_Readiness_Health_Without_Business_Request_Retry()
    {
        var sampleRoot = ResolveSampleRoot("DeliveryDispatch");
        var dispatchHost = File.ReadAllText(Path.Combine(sampleRoot, "Server", "Dispatch",
            "DispatchServerHostFactory.cs"));
        var dispatchAdapters = File.ReadAllText(Path.Combine(sampleRoot, "Server", "Dispatch",
            "DispatchZLinkAdapters.cs"));
        var dispatchWorker = File.ReadAllText(Path.Combine(sampleRoot, "Server", "Dispatch",
            "DispatchWorker.cs"));

        Assert.Contains("IZLinkLocationReadiness", dispatchHost, StringComparison.Ordinal);
        Assert.Contains("readiness.IsPeerReadyAsync", dispatchHost, StringComparison.Ordinal);
        Assert.DoesNotContain("HasReadyCourierRouteAsync", dispatchHost, StringComparison.Ordinal);
        Assert.Contains("StatusCodes.Status503ServiceUnavailable", dispatchHost, StringComparison.Ordinal);
        Assert.DoesNotContain("new FindCourierActorReq(\"__health__\")", dispatchHost, StringComparison.Ordinal);
        Assert.DoesNotContain("RequestWithRetryAsync", dispatchAdapters, StringComparison.Ordinal);
        Assert.DoesNotContain("Task.Delay(100", dispatchAdapters, StringComparison.Ordinal);
        Assert.DoesNotContain("catch (Exception error)", dispatchAdapters, StringComparison.Ordinal);
        Assert.DoesNotContain("catch (Exception error)", dispatchWorker, StringComparison.Ordinal);
        Assert.DoesNotContain("courier timeout", dispatchWorker, StringComparison.Ordinal);
        Assert.Contains("RequestAsync<FindCourierActorReq, FindCourierActorRes>", dispatchAdapters,
            StringComparison.Ordinal);
        Assert.Contains("RequestAsync<OfferDeliveryReq, OfferDeliveryRes>", dispatchAdapters,
            StringComparison.Ordinal);
        Assert.Contains("RequestAsync<DeliveryStatusChangedReq, DeliveryStatusChangedRes>", dispatchAdapters,
            StringComparison.Ordinal);
    }

    [Fact]
    public void DotNet_Samples_Do_Not_Use_Legacy_Registry_Discovery()
    {
        var samplesRoot = ResolveSamplesRoot();
        var offenders = Directory
            .EnumerateDirectories(samplesRoot)
            .Where(static path => !string.Equals(Path.GetFileName(path), "TicTacToe", StringComparison.Ordinal))
            .SelectMany(EnumerateSourceFiles)
            .Select(path => (Path: path, Text: File.ReadAllText(path)))
            .Where(static source =>
                source.Text.Contains("UseDiscovery(", StringComparison.Ordinal)
                || source.Text.Contains("UseRegistrySpotResolver", StringComparison.Ordinal)
                || source.Text.Contains("AddZLinkRegistry", StringComparison.Ordinal)
                || source.Text.Contains("AddRegistryEvents", StringComparison.Ordinal))
            .Select(source => Path.GetRelativePath(samplesRoot, source.Path))
            .Order(StringComparer.Ordinal)
            .ToArray();

        Assert.Empty(offenders);
    }

    [Fact]
    public void Bingo_And_TicTacToe_Samples_Implement_Actor_Lifecycle_Spec()
    {
        AssertActorLifecycleSpec(
            ResolveSampleRoot("Bingo"),
            "Server/Play/Infrastructure/ZLink/Spots/EntrySpot/BingoEntrySpot.cs",
            "Server/Play/Infrastructure/ZLink/Spots/BingoRoomSpot/BingoRoom.cs",
            "Server/Play/Infrastructure/ZLink/Actors/PlayerActor.cs",
            "Server/Session/Sessions/BingoSession.cs");
        AssertActorLifecycleSpec(
            ResolveSampleRoot("TicTacToe"),
            "Server/Play/Infrastructure/ZLink/Spots/EntrySpot/PlayEntrySpot.cs",
            "Server/Play/Infrastructure/ZLink/Spots/TicTacToeGameSpot/TicTacToeGame.cs",
            "Server/Play/Infrastructure/ZLink/Actors/PlayActor.cs",
            "Server/Play/Infrastructure/ZLink/Sessions/PlaySession.cs");
    }

    [Fact]
    public void DotNet_Docs_Keep_Actor_Destroy_Entry_Owned()
    {
        var dotnetRoot = ResolveDotnetRoot();
        var docs = EnumerateMarkdownFiles(Path.Combine(dotnetRoot, "..", "..", "doc", "framework", "dotnet", "guide"))
            .Concat(EnumerateMarkdownFiles(Path.Combine(dotnetRoot, "..", "..", "doc", "framework", "dotnet", "spec")))
            .Concat(EnumerateMarkdownFiles(Path.Combine(dotnetRoot, "..", "..", "doc", "framework", "dotnet",
                "internals")))
            .Concat(Directory.EnumerateFiles(Path.Combine(dotnetRoot, "samples"), "README.md",
                SearchOption.AllDirectories))
            .Concat(Directory.EnumerateFiles(Path.Combine(dotnetRoot, "samples"), "README.ko.md",
                SearchOption.AllDirectories))
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
                if (text.Contains(needle, StringComparison.Ordinal))
                    offenders.Add($"{Path.GetRelativePath(dotnetRoot, file)}: {reason}");
        }

        var actorSpec = File.ReadAllText(Path.Combine(dotnetRoot, "..", "..", "doc", "framework", "dotnet", "spec",
            "aspnet-core-actor.ko.md"));
        var actorGuide = File.ReadAllText(Path.Combine(dotnetRoot, "..", "..", "doc", "framework", "dotnet", "guide",
            "06-actor-spot.ko.md"));
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

    private static void AssertLocationStoreHost(string hostFactory)
    {
        Assert.Contains("AddLocationStore(new ZLinkRedisLocationStore", hostFactory, StringComparison.Ordinal);
        Assert.Contains(".SetConnectionString(topology.RedisEndpoint)", hostFactory, StringComparison.Ordinal);
        Assert.Contains(".SetKeyPrefix(topology.RedisKeyPrefix)", hostFactory, StringComparison.Ordinal);
    }

    private static void AssertSampleUsesProtobufPayloads(string sampleRoot)
    {
        var sourceFiles = EnumerateSourceFiles(sampleRoot).ToArray();
        var projectFiles = Directory
            .EnumerateFiles(sampleRoot, "*.csproj", SearchOption.AllDirectories)
            .Where(static path => !path.Contains($"{Path.DirectorySeparatorChar}bin{Path.DirectorySeparatorChar}",
                                      StringComparison.Ordinal)
                                  && !path.Contains($"{Path.DirectorySeparatorChar}obj{Path.DirectorySeparatorChar}",
                                      StringComparison.Ordinal))
            .ToArray();
        var protoFiles = Directory
            .EnumerateFiles(sampleRoot, "*.proto", SearchOption.AllDirectories)
            .Where(static path => !path.Contains($"{Path.DirectorySeparatorChar}bin{Path.DirectorySeparatorChar}",
                                      StringComparison.Ordinal)
                                  && !path.Contains($"{Path.DirectorySeparatorChar}obj{Path.DirectorySeparatorChar}",
                                      StringComparison.Ordinal))
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
                .Where(static path => !path.Contains($"{Path.DirectorySeparatorChar}bin{Path.DirectorySeparatorChar}",
                                          StringComparison.Ordinal)
                                      && !path.Contains(
                                          $"{Path.DirectorySeparatorChar}obj{Path.DirectorySeparatorChar}",
                                          StringComparison.Ordinal))
                .Select(File.ReadAllText));

        Assert.NotEmpty(protoFiles);
        Assert.Contains("Google.Protobuf", sharedProjectText, StringComparison.Ordinal);
        Assert.Contains("Grpc.Tools", sharedProjectText, StringComparison.Ordinal);
        Assert.Contains("Protobuf Include=\"Contracts\\bingo_messages.proto\"", sharedProjectText, StringComparison.Ordinal);
        Assert.Contains("GrpcServices=\"None\"", sharedProjectText, StringComparison.Ordinal);
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
            .Where(static path => !path.Contains($"{Path.DirectorySeparatorChar}bin{Path.DirectorySeparatorChar}",
                                      StringComparison.Ordinal)
                                  && !path.Contains($"{Path.DirectorySeparatorChar}obj{Path.DirectorySeparatorChar}",
                                      StringComparison.Ordinal))
            .ToArray();
        var protoFiles = Directory
            .EnumerateFiles(sampleRoot, "*.proto", SearchOption.AllDirectories)
            .Where(static path => !path.Contains($"{Path.DirectorySeparatorChar}bin{Path.DirectorySeparatorChar}",
                                      StringComparison.Ordinal)
                                  && !path.Contains($"{Path.DirectorySeparatorChar}obj{Path.DirectorySeparatorChar}",
                                      StringComparison.Ordinal))
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
            if (!ContainsRawCodecHelper(text)) continue;

            var relative = Path.GetRelativePath(sampleRoot, file).Replace('\\', '/');
            if (!IsAllowedRawCodecLifecycleFile(sampleName, relative)) violations.Add($"{sampleName}/{relative}");
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
            Assert.DoesNotContain("AddActorRemoteAddressResolver<RegistryRemoteAddressStore>", text,
                StringComparison.Ordinal);
            Assert.DoesNotContain("AddSpotRemoteAddressResolver<RegistryRemoteAddressStore>", text,
                StringComparison.Ordinal);
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
            Assert.DoesNotContain("AddActorSessionBindingStore<RegistryActorSessionLocationStore>", text,
                StringComparison.Ordinal);
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

    private static void AssertSessionServerUsesSessionRelay(
        string sampleRoot,
        bool allowRouteMeshChannel)
    {
        var sessionHostFactory = Directory
            .EnumerateFiles(Path.Combine(sampleRoot, "Server", "Session"), "*HostFactory.cs",
                SearchOption.AllDirectories)
            .Single();
        var text = File.ReadAllText(sessionHostFactory);

        Assert.Contains("AddSpotMesh", text, StringComparison.Ordinal);
        Assert.True(
            text.Contains("EnableRouter", StringComparison.Ordinal)
            || text.Contains("ConfigureRouter", StringComparison.Ordinal));
        if (!allowRouteMeshChannel) Assert.DoesNotContain("AddRouteMesh", text, StringComparison.Ordinal);
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

    private static void AssertEnsureActorHandlersReturnSessionRelayRemoteAddresses(string sampleRoot)
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
            Assert.Contains("ActorRefWire", text, StringComparison.Ordinal);
            Assert.DoesNotContain("ActorRefSnapshot", text, StringComparison.Ordinal);
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
        Assert.Contains("LeaveActorAsync", userSpot, StringComparison.Ordinal);
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
            .Where(static path => !path.Contains($"{Path.DirectorySeparatorChar}bin{Path.DirectorySeparatorChar}",
                                      StringComparison.Ordinal)
                                  && !path.Contains($"{Path.DirectorySeparatorChar}obj{Path.DirectorySeparatorChar}",
                                      StringComparison.Ordinal));
    }

    private static IEnumerable<string> EnumerateMarkdownFiles(string root)
    {
        return Directory
            .EnumerateFiles(root, "*.md", SearchOption.AllDirectories)
            .Where(static path => !path.Contains($"{Path.DirectorySeparatorChar}bin{Path.DirectorySeparatorChar}",
                                      StringComparison.Ordinal)
                                  && !path.Contains($"{Path.DirectorySeparatorChar}obj{Path.DirectorySeparatorChar}",
                                      StringComparison.Ordinal));
    }

    private static IEnumerable<string> EnumerateSessionRoots(string sampleRoot)
    {
        var sessionRoot = Path.Combine(sampleRoot, "Server", "Session");
        if (Directory.Exists(sessionRoot)) yield return sessionRoot;

        var playSessionsRoot = Path.Combine(sampleRoot, "Server", "Play", "Sessions");
        if (Directory.Exists(playSessionsRoot)) yield return playSessionsRoot;

        var adapterSessionsRoot = Path.Combine(
            sampleRoot,
            "Server",
            "Play",
            "Adapters",
            "ZLink",
            "Sessions");
        if (Directory.Exists(adapterSessionsRoot)) yield return adapterSessionsRoot;

        var infrastructureSessionsRoot = Path.Combine(
            sampleRoot,
            "Server",
            "Play",
            "Infrastructure",
            "ZLink",
            "Sessions");
        if (Directory.Exists(infrastructureSessionsRoot)) yield return infrastructureSessionsRoot;
    }

    private static bool IsAllowedSampleLocationStoreException(string file)
    {
        return Path.GetFileName(file).Equals("RedisRoomRouteStore.cs", StringComparison.Ordinal)
               && file.Contains(
                   $"{Path.DirectorySeparatorChar}TicTacToe{Path.DirectorySeparatorChar}",
                   StringComparison.Ordinal);
    }

    private static bool IsAllowedActorAddressResolverException(
        string file,
        string token,
        ISet<string> allowedRelativeFiles)
    {
        return string.Equals(token, "IZLinkActorAddressResolver", StringComparison.Ordinal)
               && allowedRelativeFiles.Contains(
                   NormalizeRelativePath(Path.GetRelativePath(ResolveSamplesRoot(), file)));
    }

    private static string ResolveSampleRoot(string sampleName)
    {
        return Path.Combine(ResolveSamplesRoot(), sampleName);
    }

    private static string ResolveSamplesRoot()
    {
        return Path.Combine(ResolveDotnetRoot(), "samples");
    }

    private static string ResolveE2eRoot()
    {
        return Path.Combine(ResolveDotnetRoot(), "e2e");
    }

    [Fact]
    public void Samples_And_E2E_Use_ZLinkHttpClient_Not_Raw_HttpClient()
    {
        // 규약: 샘플·e2e의 HTTP 클라이언트는 Zlink.HttpClient(ZLinkHttpClient)만 쓴다.
        // raw System.Net.Http.HttpClient 인스턴스화는 규약 위반이다.
        var root = ResolveDotnetRoot();
        var offenders = new List<string>();
        foreach (var dir in new[] { "samples", "e2e" })
        {
            var basePath = Path.Combine(root, dir);
            if (!Directory.Exists(basePath)) continue;
            foreach (var file in Directory.EnumerateFiles(basePath, "*.cs", SearchOption.AllDirectories))
            {
                if (file.Contains($"{Path.DirectorySeparatorChar}obj{Path.DirectorySeparatorChar}")
                    || file.Contains($"{Path.DirectorySeparatorChar}bin{Path.DirectorySeparatorChar}"))
                    continue;
                if (File.ReadAllText(file).Contains("new HttpClient"))
                    offenders.Add(NormalizeRelativePath(Path.GetRelativePath(root, file)));
            }
        }

        Assert.Empty(offenders);
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

            if (Directory.Exists(candidate)) return Directory.GetParent(candidate)!.FullName;

            current = current.Parent;
        }

        throw new DirectoryNotFoundException(
            "Could not find framework/languages/dotnet/samples from test runtime.");
    }

    private static string NormalizeRelativePath(string path)
    {
        return path.Replace(Path.DirectorySeparatorChar, '/');
    }

}
