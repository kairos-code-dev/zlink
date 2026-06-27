using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Http;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using Systems.Zlink;
using YieldDispatch.Shared;
using Zlink.Framework;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Configuration;
using Zlink.Framework.Contracts.Dispatch;
using Zlink.Framework.Contracts.Errors;
using Zlink.Framework.Contracts.Handlers;
using Zlink.Framework.Contracts.Messaging;
using Zlink.Framework.Contracts.Spots;
using Zlink.Framework.Contracts.Timers;
using YieldDispatch.Server.Play.Handlers;
using YieldDispatch.Server.Play.Spots;

namespace YieldDispatch.Server.Play.Handlers;

internal sealed class BindYieldActorsControlHandler(
    IZLinkActorManager actors,
    IZLinkSpotManager spots,
    EvidenceStore evidence,
    NodeOptions node)
    : IZLinkRouteRequestHandler<BindYieldActorsReq, BindYieldActorsReply>
{
    public async ValueTask<BindYieldActorsReply> HandleAsync(
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

        return new BindYieldActorsReply(request.SpotRid, bindings.ToArray());
    }
}

internal sealed class EnsureSpotControlHandler(
    IZLinkSpotManager spots,
    NodeOptions node)
    : IZLinkRouteRequestHandler<EnsureSpotReq, EnsureSpotReply>
{
    public async ValueTask<EnsureSpotReply> HandleAsync(
        EnsureSpotReq request,
        ZLinkRouteRequestContext context,
        CancellationToken cancellationToken)
    {
        _ = context;
        await spots.GetOrCreateAsync<YieldProbeSpot>(
            RoutingId.From(request.SpotRid),
            cancellationToken);
        return new EnsureSpotReply(request.SpotRid, node.Rid);
    }
}

internal sealed class YieldEvidenceControlHandler(EvidenceStore evidence)
    : IZLinkRouteRequestHandler<YieldEvidenceReq, YieldEvidenceReply>
{
    public ValueTask<YieldEvidenceReply> HandleAsync(
        YieldEvidenceReq request,
        ZLinkRouteRequestContext context,
        CancellationToken cancellationToken)
    {
        _ = context;
        cancellationToken.ThrowIfCancellationRequested();
        return ValueTask.FromResult(new YieldEvidenceReply(request.RequestId, evidence.Snapshot()));
    }
}

internal sealed class YieldEvidenceWaitControlHandler(EvidenceStore evidence)
    : IZLinkRouteRequestHandler<YieldEvidenceWaitReq, YieldEvidenceReply>
{
    public async ValueTask<YieldEvidenceReply> HandleAsync(
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
        return new YieldEvidenceReply(request.RequestId, snapshot);
    }
}
