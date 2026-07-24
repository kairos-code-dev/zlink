using SpotService.Server.Session.Spots;
using SpotService.Shared;
using Systems.Zlink;
using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Errors;
using Zlink.Framework.Contracts.Handlers;
using Zlink.Framework.Contracts.Spots;

namespace SpotService.Server.Session.Handlers;

[ZLinkHandlerGroup("play")]
internal sealed class EnsureActorHandler(
    IZLinkActorManager actors,
    NodeOptions node,
    EvidenceStore evidence)
    : IZLinkRouteRequestHandler<EnsureActorReq, EnsureActorRes>
{
    public async ValueTask<EnsureActorRes> HandleAsync(
        EnsureActorReq request,
        ZLinkRouteRequestContext context,
        CancellationToken cancellationToken)
    {
        _ = context;
        var actor = await actors
            .GetOrCreate(request.ActorId, SpotServiceNames.ActorType)
            .Request(new ScenarioActorCreateReq(request.DisplayName))
            .Async(cancellationToken) switch
        {
            ZLinkActorCreateResult.Existing value => value.Actor,
            ZLinkActorCreateResult.Created value => value.Actor,
            _ => throw new InvalidOperationException("Actor creation was rejected.")
        };

        evidence.Add($"ensure-actor|rid={node.Rid}|actor={request.ActorId}");
        evidence.Add($"entry-joined|rid={node.Rid}|actor={request.ActorId}");
        return new EnsureActorRes(
            actor.ActorId,
            actor.NodeRid.ToString(),
            actor.Generation);
    }
}

[ZLinkHandlerGroup("play")]
internal sealed class ControlPingHandler(NodeOptions node, EvidenceStore evidence)
    : IZLinkRouteRequestHandler<ControlPingReq, ControlPingRes>
{
    public ValueTask<ControlPingRes> HandleAsync(
        ControlPingReq request,
        ZLinkRouteRequestContext context,
        CancellationToken cancellationToken)
    {
        _ = context;
        cancellationToken.ThrowIfCancellationRequested();
        evidence.Add($"control-ping|rid={node.Rid}|value={request.Value}");
        return ValueTask.FromResult(new ControlPingRes(request.Value, node.Rid));
    }
}

[ZLinkHandlerGroup("play")]
internal sealed class CreateSpotHandler(
    IZLinkSpotManager spots,
    NodeOptions node,
    EvidenceStore evidence)
    : IZLinkRouteRequestHandler<CreateSpotReq, CreateSpotRes>
{
    public async ValueTask<CreateSpotRes> HandleAsync(
        CreateSpotReq request,
        ZLinkRouteRequestContext context,
        CancellationToken cancellationToken)
    {
        _ = context;
        var result = await spots.GetOrCreateAsync<ScenarioUserSpot>(
            RoutingId.From(request.SpotRid),
            cancellationToken);
        evidence.Add($"create-spot|rid={node.Rid}|spot={result.SpotRid}|state={result.State}");
        return new CreateSpotRes(result.SpotRid.ToString(), node.Rid, result.State.ToString());
    }
}

[ZLinkHandlerGroup("play")]
internal sealed class CloseSpotHandler(
    IZLinkSpotManager spots,
    EvidenceStore evidence)
    : IZLinkRouteRequestHandler<CloseSpotReq, CloseSpotRes>
{
    public async ValueTask<CloseSpotRes> HandleAsync(
        CloseSpotReq request,
        ZLinkRouteRequestContext context,
        CancellationToken cancellationToken)
    {
        _ = context;
        var closed = await spots.CloseAsync(RoutingId.From(request.SpotRid), cancellationToken);
        evidence.Add($"close-spot|rid={evidence.Rid}|spot={request.SpotRid}|closed={closed}");
        return new CloseSpotRes(request.SpotRid, closed);
    }
}

[ZLinkHandlerGroup("play")]
internal sealed class SpotTypeMismatchHandler(
    IZLinkSpotManager spots,
    EvidenceStore evidence)
    : IZLinkRouteRequestHandler<SpotTypeMismatchReq, SpotTypeMismatchRes>
{
    public async ValueTask<SpotTypeMismatchRes> HandleAsync(
        SpotTypeMismatchReq request,
        ZLinkRouteRequestContext context,
        CancellationToken cancellationToken)
    {
        _ = context;
        var rid = RoutingId.From(request.SpotRid);
        var first = await spots.GetOrCreateAsync<ScenarioUserSpot>(rid, cancellationToken);
        try
        {
            await spots.GetOrCreateAsync<ScenarioAlternateSpot>(rid, cancellationToken);
        }
        catch (ZLinkFrameworkException ex) when (ex.Kind == ZLinkFrameworkErrorKind.SpotTypeMismatch)
        {
            evidence.Add($"spot-type-mismatch|rid={evidence.Rid}|spot={request.SpotRid}|kind={ex.Kind}");
            return new SpotTypeMismatchRes(request.SpotRid, true, ex.Kind.ToString(), first.State.ToString());
        }

        throw new InvalidOperationException("Expected SpotTypeMismatch for reused spot rid.");
    }
}
