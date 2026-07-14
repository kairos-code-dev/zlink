using Xunit;

namespace Zlink.Framework.SampleRegressionTests;

public sealed partial class RegressionTests
{
    [Fact]
    public void SupportChat_Client_Gate_Exercises_All_Required_Rejections()
    {
        var sampleRoot = ResolveSampleRoot("SupportChat");
        var scenario = File.ReadAllText(Path.Combine(sampleRoot, "Client", "SupportChatClientScenario.cs"));

        Assert.Contains("Unauthenticated client must not open a conversation.", scenario, StringComparison.Ordinal);
        Assert.Contains("Unauthenticated client must not send chat messages.", scenario, StringComparison.Ordinal);
        Assert.Contains("Agent must not open a customer conversation.", scenario, StringComparison.Ordinal);
        Assert.Contains("Non-participant must not send chat messages.", scenario, StringComparison.Ordinal);
    }

    [Fact]
    public void SupportChat_Registers_Stateful_Actor_Transfer_Adapter()
    {
        var sampleRoot = ResolveSampleRoot("SupportChat");
        var host = File.ReadAllText(Path.Combine(sampleRoot, "Server", "Support", "SupportServerHostFactory.cs"));
        var adapter = File.ReadAllText(Path.Combine(sampleRoot, "Server", "Support", "Infrastructure", "ZLink",
            "Actors", "SupportUserActorTransferAdapter.cs"));

        Assert.Contains("AddActorTransferAdapter<SupportUserActor, SupportUserActorTransferAdapter>", host,
            StringComparison.Ordinal);
        Assert.Contains("IZLinkActorTransferAdapter<SupportUserActor>", adapter, StringComparison.Ordinal);
        Assert.DoesNotContain("AddStatelessActorTransfer", host, StringComparison.Ordinal);
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
        AssertShellRunnerUsesRedisDockerHelper(
            shellRunner,
            "zlink-supportchat-dotnet-redis",
            "SUPPORTCHAT_REDIS_ENDPOINT");
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
        AssertPowerShellRunnerUsesRedisDockerHelper(powershellRunner, "zlink-supportchat-dotnet-redis");
        Assert.Contains("if ($RedisContainer)", powershellRunner, StringComparison.Ordinal);
        Assert.Contains("$env:SUPPORTCHAT_LOG_DIR = $SampleLogDir", powershellRunner, StringComparison.Ordinal);
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
        Assert.Contains("ActorRefSnapshot Actor", serverContracts, StringComparison.Ordinal);
        Assert.Contains("ActorRefSnapshot.From(actor)", ensureUserHandler, StringComparison.Ordinal);
        Assert.Contains("ActorRefSnapshot.From(actorRef)", ensureConversationHandler, StringComparison.Ordinal);
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
}
