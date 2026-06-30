using Systems.Zlink;
using YieldDispatch.Server.Play.Spots;
using YieldDispatch.Shared;
using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Spots;

namespace YieldDispatch.Server.Play.Handlers;

internal sealed class BindYieldActorsControlHandler(
    IZLinkActorManager actors,
    IZLinkSpotManager spots,
    EvidenceStore evidence,
    NodeOptions node)
    : IZLinkRouteRequestHandler<BindYieldActorsReq, BindYieldActorsRes>
{
    public async ValueTask<BindYieldActorsRes> HandleAsync(
        BindYieldActorsReq request,
        ZLinkRouteRequestContext context,
        CancellationToken cancellationToken)
    {
        _ = context;
        await spots.GetOrCreateAsync<YieldProbeSpot>(
            RoutingId.From(request.SpotRid),
            cancellationToken);
        var bindings = new List<YieldActorBinding>();
        foreach (var actorId in request.ActorIds)
        {
            var actor = await actors.GetOrCreateAsync(
                actorId,
                YieldDispatchNames.ActorType,
                cancellationToken);
            evidence.Add(
                $"bind-actor|rid={node.Rid}|spot={request.SpotRid}|actor={actor.ActorId}"
                + $"|generation={actor.Generation}");
            bindings.Add(new YieldActorBinding(
                actor.ActorId,
                actor.NodeRid.ToString(),
                actor.Generation));
        }

        return new BindYieldActorsRes(request.SpotRid, bindings.ToArray());
    }
}

internal sealed class EnsureSpotControlHandler(
    IZLinkSpotManager spots,
    NodeOptions node)
    : IZLinkRouteRequestHandler<EnsureSpotReq, EnsureSpotRes>
{
    public async ValueTask<EnsureSpotRes> HandleAsync(
        EnsureSpotReq request,
        ZLinkRouteRequestContext context,
        CancellationToken cancellationToken)
    {
        _ = context;
        await spots.GetOrCreateAsync<YieldProbeSpot>(
            RoutingId.From(request.SpotRid),
            cancellationToken);
        return new EnsureSpotRes(request.SpotRid, node.Rid);
    }
}

internal sealed class YieldEvidenceControlHandler(EvidenceStore evidence)
    : IZLinkRouteRequestHandler<YieldEvidenceReq, YieldEvidenceRes>
{
    public ValueTask<YieldEvidenceRes> HandleAsync(
        YieldEvidenceReq request,
        ZLinkRouteRequestContext context,
        CancellationToken cancellationToken)
    {
        _ = context;
        cancellationToken.ThrowIfCancellationRequested();
        return ValueTask.FromResult(new YieldEvidenceRes(request.RequestId, evidence.Snapshot()));
    }
}

internal sealed class YieldEvidenceWaitControlHandler(EvidenceStore evidence)
    : IZLinkRouteRequestHandler<YieldEvidenceWaitReq, YieldEvidenceRes>
{
    public async ValueTask<YieldEvidenceRes> HandleAsync(
        YieldEvidenceWaitReq request,
        ZLinkRouteRequestContext context,
        CancellationToken cancellationToken)
    {
        _ = context;
        var timeout = TimeSpan.FromMilliseconds(Math.Clamp(request.TimeoutMilliseconds, 1, 30000));
        var snapshot = await evidence.WaitUntilAsync(
            entries => entries.Any(line =>
                line.Contains($"request={request.RequestId}", StringComparison.Ordinal)
                && line.Contains(request.Marker, StringComparison.Ordinal)),
            timeout,
            cancellationToken);
        return new YieldEvidenceRes(request.RequestId, snapshot);
    }
}