using SpotActorTransfer.Shared;
using Zlink.HttpClient;

var options = ClientOptions.Parse(args);
using var nodeA = ZLinkHttpClient.Create(options.NodeAUrl).Timeout(TimeSpan.FromSeconds(30)).Build();
using var nodeB = ZLinkHttpClient.Create(options.NodeBUrl).Timeout(TimeSpan.FromSeconds(30)).Build();

var scenarios = new Dictionary<string, Func<Task>>(StringComparer.OrdinalIgnoreCase)
{
    ["ST-A1"] = RunLocalAcceptAsync,
    ["ST-A2"] = RunLocalRejectAsync,
    ["ST-B1"] = RunRemoteStatefulTransferAsync,
    ["ST-B3"] = RunRemoteMissingAdapterAsync,
    ["ST-B4"] = RunRemoteStatelessTransferAsync
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
    return (await client.Post("/evidence/wait").Body(new EvidenceWaitReq(containsAll))
               .SubmitAsync<IReadOnlyList<ActorEvidence>>()).Body
           ?? throw new InvalidOperationException("Evidence response was null.");
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
