using SpotActorTransfer.Shared;
using Systems.Zlink.Stream.Connector.Contracts;
using Zlink.HttpClient;

namespace SpotActorTransfer.Client.Support;

internal sealed class SpotActorTransferScenarioContext : IDisposable
{
    public SpotActorTransferScenarioContext(ClientOptions options)
    {
        Options = options;
        NodeA = CreateClient(options.NodeAUrl);
        NodeB = CreateClient(options.NodeBUrl);
        NodeC = CreateClient(options.NodeCUrl);
    }

    public ClientOptions Options { get; }
    public ZLinkHttpClient NodeA { get; }
    public ZLinkHttpClient NodeB { get; }
    public ZLinkHttpClient NodeC { get; }

    public void Dispose()
    {
        NodeA.Dispose();
        NodeB.Dispose();
        NodeC.Dispose();
    }

    public async Task<CreateSpotRes> CreateSpotAsync(
        ZLinkHttpClient client,
        string spotRid,
        string mode = "accept")
    {
        return (await client.Post("/spots").Body(new CreateSpotReq(spotRid, mode))
                   .SubmitAsync<CreateSpotRes>()).Body
               ?? throw new InvalidOperationException("Create spot response was null.");
    }

    public async Task<ActorCreateRes> CreateActorAsync(
        ZLinkHttpClient client,
        string actorId,
        string actorType,
        int stateVersion)
    {
        return (await client.Post("/actors").Body(new ActorCreateReq(actorId, actorType, stateVersion))
                   .SubmitAsync<ActorCreateRes>()).Body
               ?? throw new InvalidOperationException("Create actor response was null.");
    }

    public async Task<GateReleaseRes> ReleaseJoinedGateAsync(ZLinkHttpClient client, string spotRid)
    {
        return (await client.Post($"/joined-gates/{spotRid}/release").SubmitAsync<GateReleaseRes>()).Body
               ?? throw new InvalidOperationException("Gate release response was null.");
    }

    public async Task<ActorRefSnapshotRes> GetActorRefAsync(ZLinkHttpClient client, string actorId)
    {
        return (await client.Get($"/actors/{actorId}/ref").SubmitAsync<ActorRefSnapshotRes>()).Body
               ?? throw new InvalidOperationException("Actor ref response was null.");
    }

    public async Task<IReadOnlyList<ActorEvidence>> GetEvidenceAsync(ZLinkHttpClient client)
    {
        return (await client.Get("/evidence").SubmitAsync<IReadOnlyList<ActorEvidence>>()).Body
               ?? throw new InvalidOperationException("Evidence response was null.");
    }

    public async Task ShutdownAsync(ZLinkHttpClient client)
    {
        await client.Post("/shutdown").SubmitRawAsync();
    }

    public async Task<JoinTargetRes> JoinAsync(
        ZLinkHttpClient client,
        string actorId,
        JoinTargetReq request)
    {
        return (await JoinRawAsync(client, actorId, request)).ToJoinTargetRes();
    }

    public async Task<JoinResponse> JoinRawAsync(
        ZLinkHttpClient client,
        string actorId,
        JoinTargetReq request)
    {
        return (await client.Post($"/actors/{actorId}/join").Body(request)
                   .SubmitAsync<JoinResponse>()).Body
               ?? throw new InvalidOperationException("Join response was null.");
    }

    public async Task<ProbeRes> ProbeAsync(ZLinkHttpClient client, string actorId, ProbeReq request)
    {
        return (await client.Post($"/actors/{actorId}/probe").Body(request).SubmitAsync<ProbeRes>()).Body
               ?? throw new InvalidOperationException("Probe response was null.");
    }

    public async Task<ActorRefProbeRes> ProbeRefAsync(
        ZLinkHttpClient client,
        string actorId,
        ActorRefSnapshotRes actor,
        ProbeReq request,
        TimeSpan? timeout = null)
    {
        return (await client.Post($"/actors/{actorId}/probe-ref")
                   .Body(new ActorRefProbeReq(
                       request.Scenario,
                       request.Marker,
                       actor.NodeRid,
                       actor.Generation,
                       checked((int)(timeout ?? TimeSpan.FromSeconds(5)).TotalMilliseconds)))
                   .SubmitAsync<ActorRefProbeRes>()).Body
               ?? throw new InvalidOperationException("Actor ref probe response was null.");
    }

    public async Task SendRefAsync(
        ZLinkHttpClient client,
        string actorId,
        ActorRefSnapshotRes actor,
        HandoffPacket packet)
    {
        await client.Post($"/actors/{actorId}/send-ref")
            .Body(new ActorRefProbeReq(packet.Scenario, packet.Marker, actor.NodeRid, actor.Generation))
            .SubmitRawAsync();
    }

    public async Task<BoundPushRes> BoundPushAsync(
        ZLinkHttpClient client,
        string actorId,
        BoundPushReq request)
    {
        return (await client.Post($"/actors/{actorId}/bound-push").Body(request)
                   .SubmitAsync<BoundPushRes>()).Body
               ?? throw new InvalidOperationException("Bound push response was null.");
    }

    public Task<ZlinkStreamMessage<BoundPushNotify>> WaitBoundPushAsync(
        IZlinkStreamConnector stream,
        string marker)
    {
        return stream.WaitFor<BoundPushNotify>()
            .Where(message => message.Payload.Marker == marker)
            .Timeout(TimeSpan.FromSeconds(10))
            .Async().AsTask();
    }

    public async Task<IZlinkStreamConnector> ConnectAndBindAsync(
        string endpoint,
        string scenario,
        ActorRefSnapshotRes actor)
    {
        var stream = ZlinkStreamConnectorFactory.Create(new ZlinkStreamConnectorOptions
        {
            Endpoint = new Uri(endpoint),
            ConnectTimeout = TimeSpan.FromSeconds(5),
            RequestTimeout = TimeSpan.FromSeconds(10),
            Heartbeat = new ZlinkStreamHeartbeatOptions { Enabled = false },
            DispatchMode = ZlinkStreamDispatchMode.Immediate,
            MaxReceivedMessages = 1024
        });
        await stream.Connect.Async();
        var bound = await stream.Request(new BindActorSessionReq(
                scenario, actor.ActorId, actor.NodeRid, actor.Generation))
            .PacketName(nameof(BindActorSessionReq))
            .Async<BindActorSessionRes>();
        Require(bound.ActorId == actor.ActorId, $"{scenario} session bind actor mismatch.");
        return stream;
    }

    public async Task<IReadOnlyList<ActorEvidence>> WaitEvidenceAsync(
        ZLinkHttpClient client,
        string[] containsAll)
    {
        var evidence = (await client.Post("/evidence/wait").Body(new EvidenceWaitReq(containsAll))
                .SubmitAsync<IReadOnlyList<ActorEvidence>>()).Body
            ?? throw new InvalidOperationException("Evidence response was null.");
        foreach (var expected in containsAll)
            RequireContains(evidence, expected, $"Expected evidence marker was not observed: {expected}");
        return evidence;
    }

    public async Task AssertEvidenceOrderAsync(
        ZLinkHttpClient client,
        string actorId,
        string kind,
        string[] values)
    {
        await WaitEvidenceAsync(client, values.Select(value => $"{actorId}|{kind}|{value}").ToArray());
        var evidence = (await GetEvidenceAsync(client))
            .Where(item => item.ActorId == actorId && item.Kind == kind)
            .Select(item => item.Value)
            .ToArray();
        Require(evidence.SequenceEqual(values),
            $"Actor '{actorId}' {kind} order mismatch: {string.Join(",", evidence)}.");
    }

    public async Task<(string ActorId, ActorRefSnapshotRes OldRef)> TransferForStragglerAsync(
        string scenario,
        int stateVersion)
    {
        var actorId = $"actor-straggler-{scenario}-{Guid.NewGuid():N}";
        var spotRid = $"spot-straggler-{scenario}-{Guid.NewGuid():N}";
        await CreateSpotAsync(NodeB, spotRid);
        await CreateActorAsync(NodeA, actorId, SpotActorTransferNames.ActorTypeStateful, stateVersion);
        var oldRef = await GetActorRefAsync(NodeA, actorId);
        Require((await JoinAsync(NodeA, actorId, new JoinTargetReq(scenario, spotRid))).Accepted,
            $"{scenario} transfer was rejected.");
        return (actorId, oldRef);
    }

    public static void Require(bool condition, string message)
    {
        if (!condition) throw new InvalidOperationException(message);
    }

    public static void RequireContains(IEnumerable<ActorEvidence> evidence, string expected, string message)
    {
        Require(evidence.Any(item => EvidenceText(item).Contains(expected, StringComparison.Ordinal)), message);
    }

    public static void RequireNoContains(IEnumerable<ActorEvidence> evidence, string expected, string message)
    {
        Require(!evidence.Any(item => EvidenceText(item).Contains(expected, StringComparison.Ordinal)), message);
    }

    private static string EvidenceText(ActorEvidence evidence) =>
        $"{evidence.Scenario}|{evidence.ActorId}|{evidence.Kind}|{evidence.Value}|{evidence.NodeRid}";

    private static ZLinkHttpClient CreateClient(string url) =>
        ZLinkHttpClient.Create(url).Timeout(TimeSpan.FromSeconds(30)).Build();
}

internal sealed record ClientOptions(
    string NodeAUrl,
    string NodeBUrl,
    string NodeCUrl,
    string NodeAStreamEndpoint,
    string NodeBStreamEndpoint,
    string Scenario)
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
            values["node-a-url"], values["node-b-url"], values["node-c-url"],
            values["node-a-stream-endpoint"], values["node-b-stream-endpoint"],
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
    public JoinTargetRes ToJoinTargetRes() => new(
        Scenario, ActorId, Accepted, SourceNodeRid ?? "", TargetSpotRid ?? "", StateVersion ?? 0);
}
