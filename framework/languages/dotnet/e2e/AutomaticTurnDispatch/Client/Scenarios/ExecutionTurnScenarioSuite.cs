using AutomaticTurnDispatch.Client.Support;
using AutomaticTurnDispatch.Shared;
using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Workers;
using Zlink.HttpClient;

namespace AutomaticTurnDispatch.Client.Scenarios;

internal sealed class ExecutionTurnScenarioSuite(
    IZlinkStreamConnector client,
    string sessionBStreamEndpoint)
{
    private string? _spotRid;
    private AwaitActorScenarioContext? _actors;

    public Task TdA1Async()
    {
        AssertTerminators(typeof(IZLinkRequestCall));
        AssertTerminators(typeof(IZLinkActorJoinCall));
        AssertTerminators(typeof(IZLinkWorkerCall<>));
        AssertTerminators(typeof(ZLinkHttpRequestBuilder));
        return Task.CompletedTask;
    }

    public async Task TdA2Async() => await VerifySpotInterleaveAsync("TD-A2", "async", false);

    public async Task TdA3Async() => await VerifyCounterAsync("TD-A3", "async", 8);

    public async Task TdA4Async()
    {
        var spot = await SpotAsync();
        var requestId = NewId("TD-A4");
        var reply = await client.Request(new AwaitReq(requestId, 1000, "completion-axis", "async"))
            .PacketName("AwaitReq")
            .Metadata(AutomaticTurnDispatchNames.SpotRidMetadata, spot)
            .Timeout(TimeSpan.FromSeconds(5))
            .Async<AutomaticTurnDispatchRes>();
        ScenarioAssert.That(reply.Marker == "async-completed", "TD-A4 async completion did not resume.");
    }

    public async Task TdA5Async() => await VerifyTimerInterleaveAsync("TD-A5", "async", false);

    public async Task TdB1Async() => await VerifySpotInterleaveAsync("TD-B1", "yield", true);

    public async Task TdB2Async()
    {
        var spot = await SpotAsync();
        var requestId = NewId("TD-B2");
        SendSpot(new AwaitMsg(requestId, 300, "queue-order", "yield"), "AwaitMsg", spot);
        await EvidenceAsync(requestId, "yield-released");
        for (var index = 1; index <= 3; index++)
            SendSpot(new ProbeMsg(requestId, $"probe-{index}"), "ProbeMsg", spot);
        var evidence = await EvidenceAsync(requestId, "yield-completed");
        ScenarioAssert.ContainsExactRequestInOrder(evidence, requestId,
        [
            "yield-released", "marker=probe-1", "marker=probe-2", "marker=probe-3", "yield-resumed",
            "yield-completed"
        ]);
    }

    public async Task TdB3Async() => await VerifyCounterAsync("TD-B3", "yield", 1);

    public async Task TdB4Async() => await VerifyTimerInterleaveAsync("TD-B4", "yield", true);

    public async Task TdC1Async() => await VerifyHttpInterleaveAsync("TD-C1", "yield", true);

    public async Task TdC2Async() => await VerifyHttpInterleaveAsync("TD-C2", "async", false);

    public async Task TdC3Async()
    {
        var spot = await SpotAsync();
        var requestId = NewId("TD-C3");
        for (var index = 0; index < 32; index++)
            SendSpot(new IoWorkerAwaitMsg(requestId, $"io-{index:D2}", 150), "IoWorkerAwaitMsg", spot);
        await EvidenceAsync(requestId, "operation=io-31|marker=io-31");
        var evidence = Array.Empty<string>();
        var deadline = DateTimeOffset.UtcNow + TimeSpan.FromSeconds(5);
        while (DateTimeOffset.UtcNow < deadline)
        {
            evidence = (await client.Request(new AwaitEvidenceReq(requestId))
                .PacketName("AwaitEvidenceReq")
                .Metadata(AutomaticTurnDispatchNames.TargetNodeRidMetadata, "play-a")
                .Timeout(TimeSpan.FromSeconds(5)).Async<AwaitEvidenceRes>()).Evidence;
            if (evidence.Count(line => line.Contains($"request={requestId}", StringComparison.Ordinal)
                                       && line.Contains("io-worker-completed", StringComparison.Ordinal)) == 32)
                break;
            await Task.Delay(25);
        }
        ScenarioAssert.That(
            evidence.Count(line => line.Contains($"request={requestId}", StringComparison.Ordinal)
                                   && line.Contains("io-worker-completed", StringComparison.Ordinal)) == 32,
            "TD-C3 did not complete all I/O workers.");
        ScenarioAssert.That(
            evidence.All(line => !line.Contains("WorkerQueueFull", StringComparison.Ordinal)),
            "TD-C3 exhausted the CPU worker queue.");
    }

    public async Task TdC4Async()
    {
        await VerifyCpuWorkerAsync("TD-C4-async", "async", false);
        await VerifyCpuWorkerAsync("TD-C4-yield", "yield", true);
    }

    public Task TdC5Async()
    {
        var root = Path.Combine(FindAutomaticTurnDispatchRoot(), "Server", "Play");
        var files = Directory.EnumerateFiles(root, "*.cs", SearchOption.AllDirectories);
        foreach (var file in files)
        {
            var source = File.ReadAllText(file);
            if (!source.Contains("RunCpuWorker", StringComparison.Ordinal)) continue;
            ScenarioAssert.That(
                !source.Contains("GetAwaiter().GetResult()", StringComparison.Ordinal),
                $"TD-C5 found blocking I/O in CPU worker source: {file}");
        }
        return Task.CompletedTask;
    }

    public async Task TdD1Async() => await VerifyActorInterleaveAsync("TD-D1", false);

    public async Task TdD2Async() => await VerifyActorInterleaveAsync("TD-D2", true);

    public async Task TdD3Async()
    {
        var spot = await SpotAsync();
        var requestId = NewId("TD-D3");
        SendSpot(new TimerStartMsg(requestId, requestId, "yield-on-first", 40, 250), "TimerStartMsg", spot);
        var evidence = await EvidenceAsync(requestId, "timer-next-completed");
        ScenarioAssert.ContainsExactRequestInOrder(evidence, requestId,
            ["timer-yield-released", "timer-yield-resumed", "timer-yield-completed", "timer-next-started"]);
        SendSpot(new TimerStopMsg(requestId), "TimerStopMsg", spot);
    }

    public async Task TdE1Async()
    {
        var actors = await ActorsAsync();
        var requestId = NewId("TD-E1");
        var reply = await ActorRequestAsync<ActorAwaitRes>(actors.ActorA,
            new ActorJoinAwaitReq(requestId, actors.SpotRid), "ActorJoinAwaitReq");
        ScenarioAssert.That(reply.Marker == "actor-join-await-completed", "TD-E1 entry join failed.");
    }

    public async Task TdE2Async()
    {
        var actors = await ActorsAsync();
        await EnsureActorInSpotAsync(actors.ActorA, actors.SpotRid, "TD-E2-prepare");
        var target = $"td-e2-target-{Guid.NewGuid():N}";
        await EnsureSpotAsync(target, "play-a");
        var reply = await ActorRequestAsync<ActorAwaitRes>(actors.ActorA,
            new ActorJoinAwaitReq(NewId("TD-E2"), target), "ActorJoinAwaitReq");
        ScenarioAssert.That(reply.Marker == "actor-join-completed", "TD-E2 user Spot join failed.");
    }

    public async Task TdE3Async()
    {
        var actors = await ActorsAsync();
        var spotA = $"td-e3-a-{Guid.NewGuid():N}";
        var spotB = $"td-e3-b-{Guid.NewGuid():N}";
        await EnsureSpotAsync(spotA, "play-a");
        await EnsureSpotAsync(spotB, "play-a");
        await EnsureActorInSpotAsync(actors.ActorA, spotA, "TD-E3-prepare-a");
        await EnsureActorInSpotAsync(actors.ActorB, spotB, "TD-E3-prepare-b");
        var moveA = ActorRequestAsync<ActorAwaitRes>(actors.ActorA,
            new ActorJoinAwaitReq(NewId("TD-E3-A"), spotB), "ActorJoinAwaitReq");
        var moveB = ActorRequestAsync<ActorAwaitRes>(actors.ActorB,
            new ActorJoinAwaitReq(NewId("TD-E3-B"), spotA), "ActorJoinAwaitReq");
        var replies = await Task.WhenAll(moveA, moveB);
        ScenarioAssert.That(replies.All(reply => reply.Marker == "actor-join-completed"),
            "TD-E3 opposite joins did not both complete.");
    }

    public async Task TdF1Async() => await VerifyRemoteTopologyAsync("TD-F1");

    public async Task TdF2Async() => await VerifySpotInterleaveAsync("TD-F2", "yield", true, "play-b");

    public async Task TdF3Async() => await VerifyActorInterleaveAsync("TD-F3", false);

    public async Task TdF4Async() => await RequestTimeoutProbe.RunAsync(client);

    public async Task TdF5Async()
    {
        await CancellationProbe.RunAsync(client);
        _ = sessionBStreamEndpoint;
    }

    public async Task TdF6Async()
    {
        var spot = await SpotAsync();
        var requestId = NewId("TD-F6");
        SendSpot(new SelfCycleMsg(requestId, 150), "SelfCycleMsg", spot);
        await EvidenceAsync(requestId, "self-cycle-timed-out");
        var reply = await SpotRequestAsync<AutomaticTurnDispatchRes>(spot,
            new ProbeReq(requestId, "post-cycle"), "ProbeReq");
        ScenarioAssert.That(reply.Marker == "post-cycle", "TD-F6 Spot did not recover after timeout.");
    }

    public async Task TdG1Async()
    {
        await TdA1Async();
        await VerifySpotInterleaveAsync("TD-G1", "async", false);
        await VerifySpotInterleaveAsync("TD-G1", "yield", true);
    }

    private async Task VerifySpotInterleaveAsync(
        string scenarioId,
        string terminator,
        bool probeDuringWait,
        string targetNode = "play-a")
    {
        var spot = targetNode == "play-a" ? await SpotAsync() : $"{scenarioId.ToLowerInvariant()}-{Guid.NewGuid():N}";
        if (targetNode != "play-a") await EnsureSpotAsync(spot, targetNode);
        var requestId = NewId(scenarioId);
        SendSpot(new AwaitMsg(requestId, 300, scenarioId, terminator), "AwaitMsg", spot);
        await EvidenceAsync(requestId, terminator == "yield" ? "yield-released" : "await-held", targetNode);
        SendSpot(new ProbeMsg(requestId, "interleave-probe"), "ProbeMsg", spot);
        await EvidenceAsync(requestId, terminator == "yield" ? "yield-completed" : "async-completed", targetNode);
        var evidence = await EvidenceAsync(requestId, "probe-completed", targetNode);
        if (probeDuringWait)
            ScenarioAssert.ContainsExactRequestInOrder(evidence, requestId,
                ["yield-released", "probe-started", "probe-completed", "yield-resumed"]);
        else
            ScenarioAssert.ContainsExactRequestInOrder(evidence, requestId,
                ["await-held", "async-resumed", "async-completed", "probe-started", "probe-completed"]);
    }

    private async Task VerifyCounterAsync(string scenarioId, string terminator, int expected)
    {
        var spot = await SpotAsync();
        var requestId = NewId(scenarioId);
        SendSpot(new CounterResetMsg(requestId), "CounterResetMsg", spot);
        await EvidenceAsync(requestId, "counter-reset");
        for (var index = 0; index < 8; index++)
            SendSpot(new CounterAwaitMsg(requestId, $"op-{index}", 200, terminator), "CounterAwaitMsg", spot);
        await Task.Delay(600);
        var counter = await SpotRequestAsync<CounterReadRes>(spot, new CounterReadReq(requestId), "CounterReadReq");
        ScenarioAssert.That(counter.Value == expected,
            $"{scenarioId} expected counter {expected}, actual {counter.Value}.");
    }

    private async Task VerifyTimerInterleaveAsync(string scenarioId, string terminator, bool tickDuringWait)
    {
        var spot = await SpotAsync();
        var requestId = NewId(scenarioId);
        SendSpot(new TimerStartMsg(requestId, requestId, "fast", 40, 0), "TimerStartMsg", spot);
        await EvidenceAsync(requestId, "timer-started");
        SendSpot(new AwaitMsg(requestId, 300, scenarioId, terminator), "AwaitMsg", spot);
        await EvidenceAsync(requestId, terminator == "yield" ? "yield-released" : "await-held");
        await EvidenceAsync(requestId, terminator == "yield" ? "yield-completed" : "async-completed");
        var evidence = await EvidenceAsync(requestId, "timer-fast-completed");
        if (tickDuringWait)
            ScenarioAssert.ContainsExactRequestInOrder(evidence, requestId,
                ["yield-released", "timer-fast-started", "timer-fast-completed", "yield-resumed"]);
        else
            ScenarioAssert.ContainsExactRequestInOrder(evidence, requestId,
                ["await-held", "async-completed", "timer-fast-started", "timer-fast-completed"]);
        SendSpot(new TimerStopMsg(requestId), "TimerStopMsg", spot);
    }

    private async Task VerifyHttpInterleaveAsync(string scenarioId, string terminator, bool probeDuringWait)
    {
        var spot = await SpotAsync();
        var requestId = NewId(scenarioId);
        SendSpot(new HttpAwaitMsg(requestId, 300, terminator), "HttpAwaitMsg", spot);
        await EvidenceAsync(requestId, $"http-{terminator}-{(probeDuringWait ? "released" : "held")}");
        SendSpot(new ProbeMsg(requestId, "http-probe"), "ProbeMsg", spot);
        await EvidenceAsync(requestId, $"http-{terminator}-completed");
        var evidence = await EvidenceAsync(requestId, "probe-completed");
        if (probeDuringWait)
            ScenarioAssert.ContainsExactRequestInOrder(evidence, requestId,
                ["http-yield-released", "probe-started", "probe-completed", "http-yield-resumed"]);
        else
            ScenarioAssert.ContainsExactRequestInOrder(evidence, requestId,
                ["http-async-held", "http-async-completed", "probe-started", "probe-completed"]);
    }

    private async Task VerifyCpuWorkerAsync(string scenarioId, string terminator, bool probeDuringWait)
    {
        var spot = await SpotAsync();
        var requestId = NewId(scenarioId);
        SendSpot(new CpuWorkerAwaitMsg(requestId, 250, terminator), "CpuWorkerAwaitMsg", spot);
        await EvidenceAsync(requestId, $"cpu-worker-{terminator}-{(probeDuringWait ? "released" : "held")}");
        SendSpot(new ProbeMsg(requestId, "cpu-probe"), "ProbeMsg", spot);
        await EvidenceAsync(requestId, $"cpu-worker-{terminator}-completed");
        var evidence = await EvidenceAsync(requestId, "probe-completed");
        if (probeDuringWait)
            ScenarioAssert.ContainsExactRequestInOrder(evidence, requestId,
                ["cpu-worker-yield-released", "probe-started", "probe-completed", "cpu-worker-yield-completed"]);
        else
            ScenarioAssert.ContainsExactRequestInOrder(evidence, requestId,
                ["cpu-worker-async-held", "cpu-worker-async-completed", "probe-started", "probe-completed"]);
        var completion = evidence.First(line => line.Contains($"request={requestId}", StringComparison.Ordinal)
                                                && line.Contains("cpu-worker", StringComparison.Ordinal)
                                                && line.Contains("completed", StringComparison.Ordinal));
        ScenarioAssert.That(!completion.Contains("caller-thread=0|worker-thread=0", StringComparison.Ordinal),
            $"{scenarioId} did not record CPU worker thread evidence.");
    }

    private async Task VerifyActorInterleaveAsync(string scenarioId, bool sameActor)
    {
        var actors = await ActorsAsync();
        var requestId = NewId(scenarioId);
        var pending = ActorRequestAsync<ActorAwaitRes>(actors.ActorA,
            new ActorAwaitReq(requestId, 300, "yield"), "ActorAwaitReq");
        await EvidenceAsync(requestId, "actor-await-released");
        var fastActor = sameActor ? actors.ActorA : actors.ActorB;
        var fast = ActorRequestAsync<ActorAwaitRes>(fastActor,
            new ActorFastReq(requestId, "actor-fast"), "ActorFastReq");
        await Task.WhenAll(pending, fast);
        var evidence = await EvidenceAsync(requestId, "actor-fast-completed");
        var markers = sameActor
            ? new[] { "actor-await-released", "actor-await-resumed", "actor-await-completed", "actor-fast-started" }
            : ["actor-await-released", "actor-fast-started", "actor-fast-completed", "actor-await-resumed"];
        ScenarioAssert.ContainsExactRequestInOrder(evidence, requestId, markers);
    }

    private async Task VerifyRemoteTopologyAsync(string scenarioId)
    {
        var owner = await SpotAsync();
        var target = $"td-f1-target-{Guid.NewGuid():N}";
        await EnsureSpotAsync(target, "play-b");
        var reply = await SpotRequestAsync<AutomaticTurnDispatchRes>(owner,
            new RemoteSpotAwaitReq(NewId(scenarioId), target, 100), "RemoteSpotAwaitReq");
        ScenarioAssert.That(reply.NodeRid == "play-a", "TD-F1 continuation did not return to the caller node.");
    }

    private async Task<string> SpotAsync()
    {
        if (_spotRid is not null) return _spotRid;
        _spotRid = $"execution-turn-{Guid.NewGuid():N}";
        await EnsureSpotAsync(_spotRid, "play-a");
        return _spotRid;
    }

    private async Task<AwaitActorScenarioContext> ActorsAsync()
    {
        if (_actors is not null) return _actors;
        var spot = await SpotAsync();
        var actorA = $"actor-a-{Guid.NewGuid():N}";
        var actorB = $"actor-b-{Guid.NewGuid():N}";
        var result = await client.Request(new BindAwaitActorsReq(spot, [actorA, actorB]))
            .PacketName("BindAwaitActorsReq")
            .Timeout(TimeSpan.FromSeconds(30))
            .Async<BindAwaitActorsRes>();
        ScenarioAssert.That(result.Actors.Length == 2, "Execution turn actor binding failed.");
        _actors = new AwaitActorScenarioContext(spot, actorA, actorB);
        return _actors;
    }

    private async Task EnsureActorInSpotAsync(string actorId, string spotRid, string scenarioId)
    {
        var reply = await ActorRequestAsync<ActorAwaitRes>(actorId,
            new ActorJoinAwaitReq(NewId(scenarioId), spotRid), "ActorJoinAwaitReq");
        ScenarioAssert.That(reply.Marker.Contains("actor-join", StringComparison.Ordinal),
            $"{scenarioId} actor placement failed.");
    }

    private async Task EnsureSpotAsync(string spotRid, string targetNode)
    {
        var builder = client.Request(new EnsureSpotReq(spotRid)).PacketName("EnsureSpotReq");
        if (targetNode != "play-a")
            builder.Metadata(AutomaticTurnDispatchNames.TargetNodeRidMetadata, targetNode);
        var result = await builder.Timeout(TimeSpan.FromSeconds(30)).Async<EnsureSpotRes>();
        ScenarioAssert.That(result.SpotRid == spotRid, $"Spot creation failed for {spotRid}.");
    }

    private void SendSpot<T>(T message, string packetName, string spotRid)
    {
        client.Send(message).PacketName(packetName)
            .Metadata(AutomaticTurnDispatchNames.SpotRidMetadata, spotRid).Submit();
    }

    private async Task<T> SpotRequestAsync<T>(string spotRid, object request, string packetName)
    {
        return await client.Request(request).PacketName(packetName)
            .Metadata(AutomaticTurnDispatchNames.SpotRidMetadata, spotRid)
            .Timeout(TimeSpan.FromSeconds(30)).Async<T>();
    }

    private async Task<T> ActorRequestAsync<T>(string actorId, object request, string packetName)
    {
        return await client.Request(request).PacketName(packetName)
            .Metadata(AutomaticTurnDispatchNames.ActorIdMetadata, actorId)
            .Timeout(TimeSpan.FromSeconds(30)).Async<T>();
    }

    private async Task<string[]> EvidenceAsync(string requestId, string marker, string targetNode = "play-a")
    {
        return (await client.Request(new AwaitEvidenceWaitReq(requestId, marker))
            .PacketName("AwaitEvidenceWaitReq")
            .Metadata(AutomaticTurnDispatchNames.TargetNodeRidMetadata, targetNode)
            .Timeout(TimeSpan.FromSeconds(30)).Async<AwaitEvidenceRes>()).Evidence;
    }

    private static string NewId(string scenarioId) => $"{scenarioId}-{Guid.NewGuid():N}";

    private static void AssertTerminators(Type type)
    {
        var methods = type.GetMethods().Select(method => method.Name).ToHashSet(StringComparer.Ordinal);
        foreach (var name in new[] { "Submit", "Async", "Yield" })
            ScenarioAssert.That(methods.Contains(name), $"TD-A1 {type.Name} is missing {name}.");
        ScenarioAssert.That(!methods.Contains("Fetch"), $"TD-A1 {type.Name} exposes blocking Fetch.");
    }

    private static string FindAutomaticTurnDispatchRoot()
    {
        var current = new DirectoryInfo(AppContext.BaseDirectory);
        while (current is not null)
        {
            var candidate = Path.Combine(current.FullName, "e2e", "AutomaticTurnDispatch");
            if (Directory.Exists(candidate)) return candidate;
            current = current.Parent;
        }
        throw new DirectoryNotFoundException("AutomaticTurnDispatch source root was not found.");
    }
}
