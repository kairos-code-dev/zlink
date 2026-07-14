using Xunit;

namespace Zlink.Framework.SampleRegressionTests;

public sealed partial class RegressionTests
{
    [Fact]
    public void Bingo_Registers_Stateful_Actor_Transfer_Adapter()
    {
        var sampleRoot = ResolveSampleRoot("Bingo");
        var host = File.ReadAllText(Path.Combine(sampleRoot, "Server", "Play", "PlayServerHostFactory.cs"));
        var adapter = File.ReadAllText(Path.Combine(sampleRoot, "Server", "Play", "Infrastructure", "ZLink",
            "Actors", "PlayerActorTransferAdapter.cs"));

        Assert.Contains("AddActorTransferAdapter<PlayerActor, PlayerActorTransferAdapter>", host,
            StringComparison.Ordinal);
        Assert.Contains("IZLinkActorTransferAdapter<PlayerActor>", adapter, StringComparison.Ordinal);
        Assert.DoesNotContain("AddStatelessActorTransfer", host, StringComparison.Ordinal);
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
        AssertUsesAutoRegisteredSessionHandlers(sampleRoot);
    }

    [Fact]
    public void Bingo_Client_Gate_Verifies_Submitted_Cards_And_Matching_Draw_State()
    {
        var sampleRoot = ResolveSampleRoot("Bingo");
        var scenario = File.ReadAllText(Path.Combine(sampleRoot, "Client", "BingoClientScenario.cs"));

        Assert.Contains("client1Card.State.Players.Count == 2", scenario, StringComparison.Ordinal);
        Assert.Contains("client1Card.State.Players.All(static player => player.Card.Count == 9)", scenario,
            StringComparison.Ordinal);
        Assert.Contains("client2Drawn.Payload.State.Equals(client1Drawn.Payload.State)", scenario,
            StringComparison.Ordinal);
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
        AssertShellRunnerUsesRedisDockerHelper(shellRunner, "zlink-bingo-dotnet-redis", "BINGO_REDIS_ENDPOINT");
        Assert.DoesNotContain("if [[ -z \"${BINGO_REDIS_ENDPOINT:-}\" ]]", shellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("when BINGO_REDIS_ENDPOINT is not set", shellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("BINGO_STARTUP_DELAY_SECONDS", shellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("BINGO_STARTUP_SETTLE_SECONDS", shellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("auto-connect reconcile loops", shellRunner, StringComparison.Ordinal);
        Assert.DoesNotContain("intentionally derived here, not read", shellRunner, StringComparison.Ordinal);

        Assert.Contains("$RunId = \"$PID-$([Guid]::NewGuid().ToString('N'))\"", powershellRunner, StringComparison.Ordinal);
        Assert.Contains("[Environment]::SetEnvironmentVariable(\"BINGO_REDIS_KEY_PREFIX\", \"bingo:dotnet:${RunId}:\", \"Process\")",
            powershellRunner, StringComparison.Ordinal);
        AssertPowerShellRunnerUsesRedisDockerHelper(powershellRunner, "zlink-bingo-dotnet-redis");
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
}
