using Zlink.Framework.E2E.Configuration;
using System.Diagnostics;
using SpotActorTransfer.Shared;
using Systems.Zlink.Stream.Connector.Contracts;
using Zlink.Framework.Contracts.Errors;
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
                   .Async<CreateSpotRes>()).Body
               ?? throw new InvalidOperationException("Create spot response was null.");
    }

    public async Task<ActorCreateRes> CreateActorAsync(
        ZLinkHttpClient client,
        string actorId,
        string actorType,
        int stateVersion)
    {
        return (await client.Post("/actors").Body(new ActorCreateReq(actorId, actorType, stateVersion))
                   .Async<ActorCreateRes>()).Body
               ?? throw new InvalidOperationException("Create actor response was null.");
    }

    public async Task<GateReleaseRes> ReleaseJoinedGateAsync(ZLinkHttpClient client, string spotRid)
    {
        return (await client.Post($"/joined-gates/{spotRid}/release").Async<GateReleaseRes>()).Body
               ?? throw new InvalidOperationException("Gate release response was null.");
    }

    public async Task ArmCleanupGateAsync(
        ZLinkHttpClient client,
        string actorId,
        string scenario)
    {
        var result = (await client.Post($"/cleanup-gates/{actorId}/arm")
                .Body(new CleanupGateArmReq(scenario))
                .Async<CleanupGateRes>()).Body
            ?? throw new InvalidOperationException("Cleanup gate arm response was null.");
        ZlinkStreamAssert.Ensure(result.Changed,
            $"{scenario} cleanup gate for actor '{actorId}' was already armed.");
    }

    public async Task ReleaseCleanupGateAsync(
        ZLinkHttpClient client,
        string actorId,
        string scenario)
    {
        var result = (await client.Post($"/cleanup-gates/{actorId}/release")
                .Async<CleanupGateRes>()).Body
            ?? throw new InvalidOperationException("Cleanup gate release response was null.");
        ZlinkStreamAssert.Ensure(result.Changed,
            $"{scenario} cleanup gate for actor '{actorId}' was not waiting.");
    }

    public async Task AllowCleanupAttemptAsync(
        ZLinkHttpClient client,
        string actorId,
        string scenario)
    {
        var result = (await client.Post($"/cleanup-gates/{actorId}/allow-attempt")
                .Async<CleanupGateRes>()).Body
            ?? throw new InvalidOperationException("Cleanup gate allow response was null.");
        ZlinkStreamAssert.Ensure(result.Changed,
            $"{scenario} cleanup attempt for actor '{actorId}' was already allowed.");
    }

    public async Task<ActorRefSnapshotRes> GetActorRefAsync(ZLinkHttpClient client, string actorId)
    {
        return (await client.Get($"/actors/{actorId}/ref").Async<ActorRefSnapshotRes>()).Body
               ?? throw new InvalidOperationException("Actor ref response was null.");
    }

    public async Task<ActorRefSnapshotRes> GetActorRefWithEvidenceAsync(
        ZLinkHttpClient client,
        string actorId,
        string scenario,
        string marker)
    {
        return (await client.Get($"/actors/{actorId}/ref-evidence/{scenario}/{marker}")
                   .Async<ActorRefSnapshotRes>()).Body
               ?? throw new InvalidOperationException("Actor ref evidence response was null.");
    }

    public async Task<IReadOnlyList<ActorEvidence>> GetEvidenceAsync(ZLinkHttpClient client)
    {
        return (await client.Get("/evidence").Async<IReadOnlyList<ActorEvidence>>()).Body
               ?? throw new InvalidOperationException("Evidence response was null.");
    }

    public async Task ShutdownAsync(ZLinkHttpClient client)
    {
        await client.Post("/shutdown").AsyncRaw();
    }

    public async Task ShutdownAndWaitUnavailableAsync(ZLinkHttpClient client, string url)
    {
        await ShutdownAsync(client);
        await WaitUnavailableAsync(url, "shutdown");
    }

    public async Task CrashNodeAAndWaitUnavailableAsync()
    {
        using var process = Process.GetProcessById(Options.NodeAPid);
        process.Kill(entireProcessTree: true);
        await process.WaitForExitAsync().WaitAsync(TimeSpan.FromSeconds(10));
        await WaitUnavailableAsync(Options.NodeAUrl, "SIGKILL");
    }

    private static async Task WaitUnavailableAsync(string url, string operation)
    {
        using var probe = ZLinkHttpClient.Create(url)
            .Timeout(TimeSpan.FromMilliseconds(250))
            .Build();
        var deadline = DateTimeOffset.UtcNow + TimeSpan.FromSeconds(10);
        while (DateTimeOffset.UtcNow < deadline)
        {
            try
            {
                await probe.Get("/health").AsyncRaw();
            }
            catch (Exception error) when (error is HttpRequestException or IOException)
            {
                return;
            }
            catch (ZLinkFrameworkException error) when (
                error.Kind == ZLinkFrameworkErrorKind.RequestFailed
                && HasConnectionFailure(error))
            {
                return;
            }
            catch (Exception error) when (
                error is TaskCanceledException or TimeoutException
                || error is ZLinkFrameworkException { Kind: ZLinkFrameworkErrorKind.RequestFailed })
            {
                // A slow probe does not prove process exit; keep observing.
            }
            await Task.Delay(50);
        }
        throw new TimeoutException($"Source node at '{url}' remained reachable after {operation}.");
    }

    private static bool HasConnectionFailure(Exception error)
    {
        for (var current = error; current is not null; current = current.InnerException)
            if (current is HttpRequestException or IOException)
                return true;
        return false;
    }

    public async Task DrainAsync(ZLinkHttpClient client)
    {
        await client.Post("/drain").AsyncRaw();
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
                   .Async<JoinResponse>()).Body
               ?? throw new InvalidOperationException("Join response was null.");
    }

    public async Task<ProbeRes> ProbeAsync(ZLinkHttpClient client, string actorId, ProbeReq request)
    {
        return (await client.Post($"/actors/{actorId}/probe").Body(request).Async<ProbeRes>()).Body
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
                   .Async<ActorRefProbeRes>()).Body
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
            .AsyncRaw();
    }

    public async Task<BoundPushRes> BoundPushAsync(
        ZLinkHttpClient client,
        string actorId,
        BoundPushReq request)
    {
        return (await client.Post($"/actors/{actorId}/bound-push").Body(request)
                   .Async<BoundPushRes>()).Body
               ?? throw new InvalidOperationException("Bound push response was null.");
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
            .Async<BindActorSessionRes>();
        ZlinkStreamAssert.Ensure(bound.ActorId == actor.ActorId, $"{scenario} session bind actor mismatch.");
        return stream;
    }

    public async Task<IReadOnlyList<ActorEvidence>> WaitEvidenceAsync(
        ZLinkHttpClient client,
        string[] containsAll)
    {
        var evidence = (await client.Post("/evidence/wait").Body(new EvidenceWaitReq(containsAll))
                .Async<IReadOnlyList<ActorEvidence>>()).Body
            ?? throw new InvalidOperationException("Evidence response was null.");
        foreach (var expected in containsAll)
            ZlinkStreamAssert.Ensure(
                evidence.Any(item => EvidenceText(item).Contains(expected, StringComparison.Ordinal)),
                $"Expected evidence marker was not observed: {expected}");
        return evidence;
    }

    public async Task WaitRuntimeEvidenceAsync(ZLinkHttpClient client, params string[] containsAll)
    {
        var evidence = (await client.Post("/runtime-evidence/wait")
                .Body(new EvidenceWaitReq(containsAll))
                .Async<string[]>()).Body
            ?? throw new InvalidOperationException("Runtime evidence response was null.");
        foreach (var expected in containsAll)
            ZlinkStreamAssert.Ensure(evidence.Any(item => item.Contains(expected, StringComparison.Ordinal)),
                $"Expected runtime evidence marker was not observed: {expected}");
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
        ZlinkStreamAssert.Ensure(evidence.SequenceEqual(values),
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
        ZlinkStreamAssert.Ensure((await JoinAsync(NodeA, actorId, new JoinTargetReq(scenario, spotRid))).Accepted,
            $"{scenario} transfer was rejected.");
        return (actorId, oldRef);
    }

    public static string EvidenceText(ActorEvidence evidence) =>
        $"{evidence.Scenario}|{evidence.ActorId}|{evidence.Kind}|{evidence.Value}|{evidence.NodeRid}";

    private static ZLinkHttpClient CreateClient(string url) =>
        ZLinkHttpClient.Create(url).Timeout(TimeSpan.FromSeconds(30)).Build();
}

internal sealed record ClientOptions(
    string NodeAUrl,
    int NodeAPid,
    string NodeBUrl,
    string NodeCUrl,
    string NodeAStreamEndpoint,
    string NodeBStreamEndpoint,
    string Scenario)
{
    public static ClientOptions Parse(string[] args)
        => E2eConfiguration.Load<ClientOptions>(args);
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
