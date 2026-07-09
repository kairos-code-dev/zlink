using SpotActorTransfer.Shared;
using Zlink.HttpClient;

var options = ClientOptions.Parse(args);
using var nodeA = ZLinkHttpClient.Create(options.NodeAUrl).Timeout(TimeSpan.FromSeconds(30)).Build();
using var nodeB = ZLinkHttpClient.Create(options.NodeBUrl).Timeout(TimeSpan.FromSeconds(30)).Build();

var scenarios = new Dictionary<string, Func<Task>>(StringComparer.OrdinalIgnoreCase)
{
    ["ST-A1"] = RunLocalAcceptAsync,
    ["ST-A2"] = RunLocalRejectAsync,
    ["ST-A3"] = RunLocalMovingDispatchBlockedAsync,
    ["ST-B1"] = RunRemoteStatefulTransferAsync,
    ["ST-B3"] = RunRemoteMissingAdapterAsync,
    ["ST-B4"] = RunRemoteStatelessTransferAsync,
    ["ST-D1"] = RunLocationCommitTimingAsync,
    ["ST-C3"] = RunCallbackFailureClassificationAsync,
    ["ST-C2"] = RunSourceDownAfterTargetCommitAsync
};

foreach (var name in SelectedScenarioNames(options.Scenario, scenarios.Keys))
{
    if (!scenarios.TryGetValue(name, out var scenario))
        throw new ArgumentException($"Unknown scenario '{name}'.");

    await scenario();
    Console.WriteLine($"operation SpotActorTransfer.{name} passed");
}

Console.WriteLine("spot-actor-transfer e2e partial result=passed");

async Task RunLocalAcceptAsync()
{
    var actorId = $"actor-local-ok-{Guid.NewGuid():N}";
    var spotRid = $"spot-local-ok-{Guid.NewGuid():N}";
    await CreateSpotAsync(nodeA, spotRid);
    await CreateActorAsync(nodeA, actorId, SpotActorTransferNames.ActorTypeStateful, 11);

    var join = await JoinAsync(nodeA, actorId, new JoinTargetReq("ST-A1", spotRid));
    Require(join.Accepted, "ST-A1 join was rejected.");

    var probe = await ProbeAsync(nodeA, actorId, new ProbeReq("ST-A1", "after-joined"));
    Require(probe.NodeRid == "actor-a", $"ST-A1 probe expected actor-a, got {probe.NodeRid}.");
    Require(probe.SpotRid == spotRid, "ST-A1 probe did not reach target spot.");

    var evidence = await WaitEvidenceAsync(nodeA, [
        $"ST-A1|{actorId}|admission|spot={spotRid}",
        $"transfer|{actorId}|leave|11",
        $"transfer|{actorId}|joined|{spotRid}:11",
        $"ST-A1|{actorId}|success_reply|{spotRid}",
        $"ST-A1|{actorId}|packet_handler|after-joined"
    ]);
    RequireContains(evidence, $"ST-A1|{actorId}|packet_handler|after-joined", "ST-A1 packet evidence missing.");
}

async Task RunLocalRejectAsync()
{
    var actorId = $"actor-local-reject-{Guid.NewGuid():N}";
    var spotRid = $"spot-local-reject-{Guid.NewGuid():N}";
    await CreateSpotAsync(nodeA, spotRid, "reject");
    await CreateActorAsync(nodeA, actorId, SpotActorTransferNames.ActorTypeStateful, 12);

    var join = await JoinAsync(nodeA, actorId, new JoinTargetReq("ST-A2", spotRid, "reject"));
    Require(!join.Accepted, "ST-A2 join should have been rejected.");

    var evidence = await WaitEvidenceAsync(nodeA, [
        $"ST-A2|{actorId}|admission|spot={spotRid}"
    ]);
    RequireNoContains(evidence, $"transfer|{actorId}|joined|{spotRid}", "ST-A2 joined side effect should not exist.");
}

async Task RunLocalMovingDispatchBlockedAsync()
{
    var actorId = $"actor-local-moving-{Guid.NewGuid():N}";
    var spotRid = $"spot-local-moving-{Guid.NewGuid():N}";
    await CreateSpotAsync(nodeA, spotRid, "delay-joined");
    await CreateActorAsync(nodeA, actorId, SpotActorTransferNames.ActorTypeStateful, 13);

    var joinTask = JoinAsync(nodeA, actorId, new JoinTargetReq("ST-A3", spotRid));
    var waitingEvidence = await WaitEvidenceAsync(nodeA, [
        $"ST-A3|{actorId}|admission|spot={spotRid}",
        $"transfer|{actorId}|leave|13",
        $"ST-A3|{actorId}|joined_wait|{spotRid}"
    ]);
    RequireNoContains(
        waitingEvidence,
        $"ST-A3|{actorId}|packet_handler|during-joined-wait",
        "ST-A3 packet should not run before OnJoinedActorAsync is released.");

    var blockedProbe = ProbeAsync(nodeA, actorId, new ProbeReq("ST-A3", "during-joined-wait"));
    await Task.Delay(500);
    Require(!blockedProbe.IsCompleted, "ST-A3 actor packet completed while OnJoinedActorAsync was still blocked.");

    var release = await ReleaseJoinedGateAsync(nodeA, spotRid);
    Require(release.Released, "ST-A3 joined gate was already released before the scenario released it.");

    var join = await joinTask;
    Require(join.Accepted, "ST-A3 join was rejected.");
    var probe = await blockedProbe.WaitAsync(TimeSpan.FromSeconds(10));
    Require(probe.SpotRid == spotRid, "ST-A3 blocked packet did not resume on the target spot.");

    await WaitEvidenceAsync(nodeA, [
        $"ST-A3|{actorId}|joined_released|{spotRid}",
        $"transfer|{actorId}|joined|{spotRid}:13",
        $"ST-A3|{actorId}|packet_handler|during-joined-wait",
        $"ST-A3|{actorId}|success_reply|{spotRid}"
    ]);
}

async Task RunRemoteStatefulTransferAsync()
{
    var actorId = $"actor-remote-ok-{Guid.NewGuid():N}";
    var spotRid = $"spot-remote-ok-{Guid.NewGuid():N}";
    await CreateSpotAsync(nodeB, spotRid);
    await CreateActorAsync(nodeA, actorId, SpotActorTransferNames.ActorTypeStateful, 21);

    var join = await JoinAsync(nodeA, actorId, new JoinTargetReq("ST-B1", spotRid));
    Require(join.Accepted, "ST-B1 join was rejected.");

    var probe = await ProbeAsync(nodeB, actorId, new ProbeReq("ST-B1", "after-transfer"));
    Require(probe.NodeRid == "actor-b", $"ST-B1 probe expected actor-b, got {probe.NodeRid}.");
    Require(probe.StateVersion == 21, $"ST-B1 state version expected 21, got {probe.StateVersion}.");

    await WaitEvidenceAsync(nodeA, [
        $"transfer|{actorId}|transfer_out|21",
        $"transfer|{actorId}|leave|21",
        $"ST-B1|{actorId}|success_reply|{spotRid}"
    ]);
    await WaitEvidenceAsync(nodeB, [
        $"transfer|{actorId}|transfer_in|21",
        $"transfer|{actorId}|joined|{spotRid}:21",
        $"ST-B1|{actorId}|packet_handler|after-transfer"
    ]);
}

async Task RunRemoteMissingAdapterAsync()
{
    var actorId = $"actor-no-adapter-{Guid.NewGuid():N}";
    var spotRid = $"spot-no-adapter-{Guid.NewGuid():N}";
    await CreateSpotAsync(nodeB, spotRid);
    await CreateActorAsync(nodeA, actorId, SpotActorTransferNames.ActorTypeNoAdapter, 31);

    var response = await JoinRawAsync(nodeA, actorId, new JoinTargetReq("ST-B3", spotRid));
    Require(!response.Accepted, "ST-B3 join should have failed.");
    Require(response.ErrorKind is not null, "ST-B3 expected explicit error kind.");

    var evidence = await WaitEvidenceAsync(nodeA, [
        $"ST-B3|{actorId}|join_failed|{response.ErrorKind}"
    ]);
    RequireNoContains(evidence, $"transfer|{actorId}|leave|31", "ST-B3 source leave should not run.");
}

async Task RunRemoteStatelessTransferAsync()
{
    var actorId = $"actor-empty-state-{Guid.NewGuid():N}";
    var spotRid = $"spot-empty-state-{Guid.NewGuid():N}";
    await CreateSpotAsync(nodeB, spotRid);
    await CreateActorAsync(nodeA, actorId, SpotActorTransferNames.ActorTypeStateless, 41);

    var join = await JoinAsync(nodeA, actorId, new JoinTargetReq("ST-B4", spotRid));
    Require(join.Accepted, "ST-B4 join was rejected.");

    var probe = await ProbeAsync(nodeB, actorId, new ProbeReq("ST-B4", "after-stateless-transfer"));
    Require(probe.NodeRid == "actor-b", $"ST-B4 probe expected actor-b, got {probe.NodeRid}.");
    Require(probe.StateVersion == 0, $"ST-B4 stateless target state expected 0, got {probe.StateVersion}.");

    await WaitEvidenceAsync(nodeB, [
        $"transfer|{actorId}|joined|{spotRid}:0",
        $"transfer|{actorId}|domain_state_loaded|{actorId}",
        $"ST-B4|{actorId}|packet_handler|after-stateless-transfer"
    ]);
}

async Task RunLocationCommitTimingAsync()
{
    await RunLocalLocationCommitTimingAsync();
    await RunRemoteLocationCommitTimingAsync();
}

async Task RunSourceDownAfterTargetCommitAsync()
{
    var actorId = $"actor-source-down-after-commit-{Guid.NewGuid():N}";
    var spotRid = $"spot-source-down-after-commit-{Guid.NewGuid():N}";
    await CreateSpotAsync(nodeB, spotRid);
    await CreateActorAsync(nodeA, actorId, SpotActorTransferNames.ActorTypeStateful, 61);

    var join = await JoinAsync(nodeA, actorId, new JoinTargetReq("ST-C2", spotRid));
    Require(join.Accepted, "ST-C2 join was rejected.");
    await WaitEvidenceAsync(nodeB, [
        $"transfer|{actorId}|transfer_in|61",
        $"transfer|{actorId}|joined|{spotRid}:61"
    ]);
    var beforeShutdown = await GetActorRefAsync(nodeB, actorId);
    Require(beforeShutdown.NodeRid == "actor-b", $"ST-C2 target ref expected actor-b, got {beforeShutdown.NodeRid}.");

    await ShutdownAsync(nodeA);
    await Task.Delay(TimeSpan.FromSeconds(2));

    var afterShutdown = await GetActorRefAsync(nodeB, actorId);
    Require(afterShutdown.NodeRid == "actor-b", $"ST-C2 target ref changed after source shutdown: {afterShutdown.NodeRid}.");
    Require(
        afterShutdown.Generation == beforeShutdown.Generation,
        $"ST-C2 target generation changed after source shutdown. before={beforeShutdown.Generation}, after={afterShutdown.Generation}");

    var probe = await ProbeAsync(nodeB, actorId, new ProbeReq("ST-C2", "after-source-down"));
    Require(probe.NodeRid == "actor-b", $"ST-C2 probe expected actor-b, got {probe.NodeRid}.");
    Require(probe.SpotRid == spotRid, "ST-C2 probe did not reach target spot after source shutdown.");
    await WaitEvidenceAsync(nodeB, [
        $"ST-C2|{actorId}|packet_handler|after-source-down"
    ]);
}

async Task RunCallbackFailureClassificationAsync()
{
    await RunTransferOutFailureAsync();
    await RunSourceLeaveFailureAsync();
    await RunTransferInFailureAsync();
    await RunJoinedFailureAsync();
}

async Task RunTransferOutFailureAsync()
{
    var actorId = $"actor-fail-transfer-out-{Guid.NewGuid():N}";
    var spotRid = $"spot-fail-transfer-out-{Guid.NewGuid():N}";
    await CreateSpotAsync(nodeB, spotRid);
    await CreateActorAsync(nodeA, actorId, SpotActorTransferNames.ActorTypeFailTransferOut, 71);

    var response = await JoinRawAsync(nodeA, actorId, new JoinTargetReq("ST-C3", spotRid));
    Require(!response.Accepted, "ST-C3 transfer-out failure should not return accepted.");
    var sourceEvidence = await WaitEvidenceAsync(nodeA, [
        $"ST-C3|{actorId}|transfer_out_failed|71",
        $"ST-C3|{actorId}|join_failed|"
    ]);
    RequireNoContains(sourceEvidence, $"transfer|{actorId}|leave|71", "ST-C3 transfer-out failure should not leave source.");
    var targetEvidence = await GetEvidenceAsync(nodeB);
    RequireNoContains(targetEvidence, $"transfer|{actorId}|joined|{spotRid}", "ST-C3 transfer-out failure should not join target.");
}

async Task RunSourceLeaveFailureAsync()
{
    var actorId = $"actor-fail-leave-{Guid.NewGuid():N}";
    var spotRid = $"spot-fail-leave-{Guid.NewGuid():N}";
    await CreateSpotAsync(nodeB, spotRid);
    await CreateActorAsync(nodeA, actorId, SpotActorTransferNames.ActorTypeFailLeave, 72);

    var response = await JoinRawAsync(nodeA, actorId, new JoinTargetReq("ST-C3", spotRid));
    Require(!response.Accepted, "ST-C3 source leave failure should not return accepted.");
    await WaitEvidenceAsync(nodeA, [
        $"transfer|{actorId}|transfer_out|72",
        $"ST-C3|{actorId}|leave_failed|72",
        $"ST-C3|{actorId}|join_failed|"
    ]);
    var targetEvidence = await GetEvidenceAsync(nodeB);
    RequireNoContains(targetEvidence, $"transfer|{actorId}|transfer_in|72", "ST-C3 source leave failure should not transfer in target.");
    RequireNoContains(targetEvidence, $"transfer|{actorId}|joined|{spotRid}", "ST-C3 source leave failure should not join target.");
}

async Task RunTransferInFailureAsync()
{
    var actorId = $"actor-fail-transfer-in-{Guid.NewGuid():N}";
    var spotRid = $"spot-fail-transfer-in-{Guid.NewGuid():N}";
    await CreateSpotAsync(nodeB, spotRid);
    await CreateActorAsync(nodeA, actorId, SpotActorTransferNames.ActorTypeFailTransferIn, 73);

    var response = await JoinRawAsync(nodeA, actorId, new JoinTargetReq("ST-C3", spotRid));
    Require(!response.Accepted, "ST-C3 transfer-in failure should not return accepted.");
    await WaitEvidenceAsync(nodeB, [
        $"ST-C3|{actorId}|transfer_in_failed|73"
    ]);
    await WaitEvidenceAsync(nodeA, [
        $"transfer|{actorId}|transfer_out|73",
        $"transfer|{actorId}|leave|73",
        $"ST-C3|{actorId}|join_failed|"
    ]);
    var targetEvidence = await GetEvidenceAsync(nodeB);
    RequireNoContains(targetEvidence, $"transfer|{actorId}|joined|{spotRid}", "ST-C3 transfer-in failure should not join target.");
}

async Task RunJoinedFailureAsync()
{
    var actorId = $"actor-fail-joined-{Guid.NewGuid():N}";
    var spotRid = $"spot-fail-joined-{Guid.NewGuid():N}";
    await CreateSpotAsync(nodeB, spotRid, "fail-joined");
    await CreateActorAsync(nodeA, actorId, SpotActorTransferNames.ActorTypeStateful, 74);

    var response = await JoinRawAsync(nodeA, actorId, new JoinTargetReq("ST-C3", spotRid));
    Require(!response.Accepted, "ST-C3 joined failure should not return accepted.");
    await WaitEvidenceAsync(nodeB, [
        $"ST-C3|{actorId}|joined_failed|{spotRid}"
    ]);
    await WaitEvidenceAsync(nodeA, [
        $"transfer|{actorId}|transfer_out|74",
        $"transfer|{actorId}|leave|74",
        $"ST-C3|{actorId}|join_failed|"
    ]);
    var targetEvidence = await GetEvidenceAsync(nodeB);
    RequireNoContains(targetEvidence, $"ST-C3|{actorId}|packet_handler|after-joined-failure", "ST-C3 joined failure should not dispatch as joined.");
}

async Task RunLocalLocationCommitTimingAsync()
{
    var actorId = $"actor-location-local-{Guid.NewGuid():N}";
    var spotRid = $"spot-location-local-{Guid.NewGuid():N}";
    await CreateSpotAsync(nodeA, spotRid, "delay-joined");
    await CreateActorAsync(nodeA, actorId, SpotActorTransferNames.ActorTypeStateful, 51);
    var before = await GetActorRefAsync(nodeA, actorId);

    var joinTask = JoinAsync(nodeA, actorId, new JoinTargetReq("ST-D1", spotRid));
    var waitingEvidence = await WaitEvidenceAsync(nodeA, [
        $"ST-D1|{actorId}|admission|spot={spotRid}",
        $"ST-D1|{actorId}|joined_wait|{spotRid}"
    ]);
    RequireNoContains(
        waitingEvidence,
        $"ST-D1|{actorId}|success_reply|{spotRid}",
        "ST-D1 local join returned success before OnJoinedActorAsync completed.");
    var during = await GetActorRefAsync(nodeA, actorId);
    Require(
        during.Generation == before.Generation,
        $"ST-D1 local actor generation changed before joined completed. before={before.Generation}, during={during.Generation}");

    await ReleaseJoinedGateAsync(nodeA, spotRid);
    var join = await joinTask;
    Require(join.Accepted, "ST-D1 local join was rejected.");
    var after = await GetActorRefAsync(nodeA, actorId);
    Require(after.Generation >= before.Generation, "ST-D1 local actor generation regressed after commit.");

    await WaitEvidenceAsync(nodeA, [
        $"ST-D1|{actorId}|joined_released|{spotRid}",
        $"transfer|{actorId}|joined|{spotRid}:51",
        $"ST-D1|{actorId}|success_reply|{spotRid}"
    ]);
}

async Task RunRemoteLocationCommitTimingAsync()
{
    var actorId = $"actor-location-remote-{Guid.NewGuid():N}";
    var spotRid = $"spot-location-remote-{Guid.NewGuid():N}";
    await CreateSpotAsync(nodeB, spotRid, "delay-joined");
    await CreateActorAsync(nodeA, actorId, SpotActorTransferNames.ActorTypeStateful, 52);

    var joinTask = JoinAsync(nodeA, actorId, new JoinTargetReq("ST-D1", spotRid));
    await WaitEvidenceAsync(nodeB, [
        $"ST-D1|{actorId}|admission|spot={spotRid}",
        $"ST-D1|{actorId}|joined_wait|{spotRid}"
    ]);
    var sourceDuring = await GetActorRefAsync(nodeA, actorId);
    Require(
        sourceDuring.NodeRid == "actor-a",
        $"ST-D1 remote source ref moved before target joined completed. got={sourceDuring.NodeRid}");

    await ReleaseJoinedGateAsync(nodeB, spotRid);
    var join = await joinTask;
    Require(join.Accepted, "ST-D1 remote join was rejected.");
    var targetAfter = await GetActorRefAsync(nodeB, actorId);
    Require(
        targetAfter.NodeRid == "actor-b",
        $"ST-D1 remote target ref was not committed after joined completed. got={targetAfter.NodeRid}");

    await WaitEvidenceAsync(nodeA, [
        $"transfer|{actorId}|leave|52",
        $"ST-D1|{actorId}|success_reply|{spotRid}"
    ]);
    await WaitEvidenceAsync(nodeB, [
        $"ST-D1|{actorId}|joined_released|{spotRid}",
        $"transfer|{actorId}|joined|{spotRid}:52"
    ]);
}

async Task<CreateSpotRes> CreateSpotAsync(ZLinkHttpClient client, string spotRid, string mode = "accept")
{
    return (await client.Post("/spots").Body(new CreateSpotReq(spotRid, mode)).SubmitAsync<CreateSpotRes>()).Body
           ?? throw new InvalidOperationException("Create spot response was null.");
}

async Task<ActorCreateRes> CreateActorAsync(ZLinkHttpClient client, string actorId, string actorType, int stateVersion)
{
    return (await client.Post("/actors").Body(new ActorCreateReq(actorId, actorType, stateVersion))
               .SubmitAsync<ActorCreateRes>()).Body
           ?? throw new InvalidOperationException("Create actor response was null.");
}

async Task<GateReleaseRes> ReleaseJoinedGateAsync(ZLinkHttpClient client, string spotRid)
{
    return (await client.Post($"/joined-gates/{spotRid}/release").SubmitAsync<GateReleaseRes>()).Body
           ?? throw new InvalidOperationException("Gate release response was null.");
}

async Task<ActorRefSnapshotRes> GetActorRefAsync(ZLinkHttpClient client, string actorId)
{
    return (await client.Get($"/actors/{actorId}/ref").SubmitAsync<ActorRefSnapshotRes>()).Body
           ?? throw new InvalidOperationException("Actor ref response was null.");
}

async Task<IReadOnlyList<ActorEvidence>> GetEvidenceAsync(ZLinkHttpClient client)
{
    return (await client.Get("/evidence").SubmitAsync<IReadOnlyList<ActorEvidence>>()).Body
           ?? throw new InvalidOperationException("Evidence response was null.");
}

async Task ShutdownAsync(ZLinkHttpClient client)
{
    await client.Post("/shutdown").SubmitRawAsync();
}

async Task<JoinTargetRes> JoinAsync(ZLinkHttpClient client, string actorId, JoinTargetReq request)
{
    var response = await JoinRawAsync(client, actorId, request);
    return response.ToJoinTargetRes();
}

async Task<JoinResponse> JoinRawAsync(ZLinkHttpClient client, string actorId, JoinTargetReq request)
{
    return (await client.Post($"/actors/{actorId}/join").Body(request).SubmitAsync<JoinResponse>()).Body
           ?? throw new InvalidOperationException("Join response was null.");
}

async Task<ProbeRes> ProbeAsync(ZLinkHttpClient client, string actorId, ProbeReq request)
{
    return (await client.Post($"/actors/{actorId}/probe").Body(request).SubmitAsync<ProbeRes>()).Body
           ?? throw new InvalidOperationException("Probe response was null.");
}

async Task<IReadOnlyList<ActorEvidence>> WaitEvidenceAsync(ZLinkHttpClient client, string[] containsAll)
{
    var evidence = (await client.Post("/evidence/wait").Body(new EvidenceWaitReq(containsAll))
            .SubmitAsync<IReadOnlyList<ActorEvidence>>()).Body
        ?? throw new InvalidOperationException("Evidence response was null.");

    foreach (var expected in containsAll)
        RequireContains(evidence, expected, $"Expected evidence marker was not observed: {expected}");

    return evidence;
}

static void Require(bool condition, string message)
{
    if (!condition) throw new InvalidOperationException(message);
}

static void RequireContains(IEnumerable<ActorEvidence> evidence, string expected, string message)
{
    Require(evidence.Any(item => EvidenceText(item).Contains(expected, StringComparison.Ordinal)), message);
}

static void RequireNoContains(IEnumerable<ActorEvidence> evidence, string expected, string message)
{
    Require(!evidence.Any(item => EvidenceText(item).Contains(expected, StringComparison.Ordinal)), message);
}

static string EvidenceText(ActorEvidence evidence) =>
    $"{evidence.Scenario}|{evidence.ActorId}|{evidence.Kind}|{evidence.Value}|{evidence.NodeRid}";

static IEnumerable<string> SelectedScenarioNames(string selector, IEnumerable<string> allNames)
{
    if (string.Equals(selector, "all", StringComparison.OrdinalIgnoreCase))
        return allNames;

    return selector
        .Split(',', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries);
}

internal sealed record ClientOptions(string NodeAUrl, string NodeBUrl, string Scenario)
{
    public static ClientOptions Parse(string[] args)
    {
        var values = new Dictionary<string, string>(StringComparer.Ordinal);
        for (var i = 0; i < args.Length; i += 2)
        {
            var key = args[i].TrimStart('-');
            if (i + 1 >= args.Length) throw new ArgumentException($"Missing value for '{args[i]}'.");
            values[key] = args[i + 1];
        }

        return new ClientOptions(
            values["node-a-url"],
            values["node-b-url"],
            values.TryGetValue("scenario", out var scenario) ? scenario : "all");
    }
}

internal sealed record JoinResponse(
    string Scenario,
    string ActorId,
    bool Accepted,
    string? SourceNodeRid,
    string? TargetSpotRid,
    int? StateVersion,
    string? ErrorKind)
{
    public JoinTargetRes ToJoinTargetRes()
    {
        return new JoinTargetRes(
            Scenario,
            ActorId,
            Accepted,
            SourceNodeRid ?? "",
            TargetSpotRid ?? "",
            StateVersion ?? 0);
    }
}
