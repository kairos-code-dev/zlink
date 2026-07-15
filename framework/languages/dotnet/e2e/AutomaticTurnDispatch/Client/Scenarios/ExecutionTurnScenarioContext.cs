// Hides deployment addressing and connector mechanics shared by execution-turn scenarios.
using AutomaticTurnDispatch.Shared;

namespace AutomaticTurnDispatch.Client.Scenarios;

internal sealed class ExecutionTurnScenarioContext(IZlinkStreamConnector client)
{
    private string? _spotRid;
    private AwaitActorScenarioContext? _actors;

    internal async Task<string> SpotAsync()
    {
        if (_spotRid is not null) return _spotRid;
        _spotRid = $"execution-turn-{Guid.NewGuid():N}";
        await EnsureSpotAsync(_spotRid, "play-a");
        return _spotRid;
    }

    internal async Task<AwaitActorScenarioContext> ActorsAsync()
    {
        if (_actors is not null) return _actors;
        var spot = await SpotAsync();
        var actorA = $"actor-a-{Guid.NewGuid():N}";
        var actorB = $"actor-b-{Guid.NewGuid():N}";
        var result = await client.Request(new BindAwaitActorsReq(spot, [actorA, actorB]))
            .Timeout(TimeSpan.FromSeconds(30))
            .Async<BindAwaitActorsRes>();
        ZlinkStreamAssert.Ensure(result.Actors.Length == 2, "Execution turn actor binding failed.");
        _actors = new AwaitActorScenarioContext(spot, actorA, actorB);
        return _actors;
    }

    internal async Task EnsureActorInSpotAsync(string actorId, string spotRid, string scenarioId)
    {
        var reply = await ActorRequest(actorId,
                new ActorJoinAwaitReq(NewId(scenarioId), spotRid))
            .Async<ActorAwaitRes>();
        ZlinkStreamAssert.Ensure(reply.Marker.Contains("actor-join", StringComparison.Ordinal),
            $"{scenarioId} actor placement failed.");
    }

    internal async Task EnsureSpotAsync(string spotRid, string targetNode)
    {
        var builder = client.Request(new EnsureSpotReq(spotRid));
        if (targetNode != "play-a")
            builder.Metadata(AutomaticTurnDispatchNames.TargetNodeRidMetadata, targetNode);
        var result = await builder.Timeout(TimeSpan.FromSeconds(30)).Async<EnsureSpotRes>();
        ZlinkStreamAssert.Ensure(result.SpotRid == spotRid, $"Spot creation failed for {spotRid}.");
    }

    internal void SendSpot<T>(T message, string spotRid)
    {
        client.Send(message)
            .Metadata(AutomaticTurnDispatchNames.SpotRidMetadata, spotRid)
            .Submit();
    }

    internal ZlinkStreamTypedRequestBuilder SpotRequest<TRequest>(
        string spotRid,
        TRequest request,
        TimeSpan? timeout = null)
    {
        return client.Request(request)
            .Metadata(AutomaticTurnDispatchNames.SpotRidMetadata, spotRid)
            .Timeout(timeout ?? TimeSpan.FromSeconds(30));
    }

    internal ZlinkStreamTypedRequestBuilder ActorRequest<TRequest>(string actorId, TRequest request)
    {
        return client.Request(request)
            .Metadata(AutomaticTurnDispatchNames.ActorIdMetadata, actorId)
            .Timeout(TimeSpan.FromSeconds(30));
    }

    internal async Task<string[]> EvidenceAsync(
        string requestId,
        string marker,
        string targetNode = "play-a",
        int minimumCount = 1)
    {
        return (await client.Request(new AwaitEvidenceWaitReq(
                requestId,
                marker,
                MinimumCount: minimumCount))
            .Metadata(AutomaticTurnDispatchNames.TargetNodeRidMetadata, targetNode)
            .Timeout(TimeSpan.FromSeconds(30)).Async<AwaitEvidenceRes>()).Evidence;
    }

    internal static string NewId(string scenarioId) => $"{scenarioId}-{Guid.NewGuid():N}";

}
