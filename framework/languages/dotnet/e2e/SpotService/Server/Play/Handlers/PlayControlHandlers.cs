using SpotService.Server.Play.Spots;
using SpotService.Shared;
using Systems.Zlink;
using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Errors;
using Zlink.Framework.Contracts.Handlers;
using Zlink.Framework.Contracts.Spots;

namespace SpotService.Server.Play.Handlers;

[ZLinkHandlerGroup("play")]
internal sealed class EnsureActorHandler(
    IZLinkActorManager actors,
    NodeOptions node,
    EvidenceStore evidence)
    : IZLinkRouteRequestHandler<EnsureActorReq, EnsureActorReply>
{
    public async ValueTask<EnsureActorReply> HandleAsync(
        EnsureActorReq request,
        ZLinkRouteRequestContext context,
        CancellationToken cancellationToken)
    {
        _ = context;
        var actor = await actors.GetOrCreateAsync(
            request.ActorId,
            SpotServiceNames.ActorType,
            new ScenarioActorCreateRequest(request.DisplayName),
            cancellationToken);

        evidence.Add($"ensure-actor|rid={node.Rid}|actor={request.ActorId}");
        evidence.Add($"entry-joined|rid={node.Rid}|actor={request.ActorId}");
        return new EnsureActorReply(
            actor.ActorId,
            actor.NodeRid.ToString(),
            actor.Generation);
    }
}

[ZLinkHandlerGroup("play")]
internal sealed class ControlPingHandler(NodeOptions node, EvidenceStore evidence)
    : IZLinkRouteRequestHandler<ControlPingReq, ControlPingReply>
{
    public ValueTask<ControlPingReply> HandleAsync(
        ControlPingReq request,
        ZLinkRouteRequestContext context,
        CancellationToken cancellationToken)
    {
        _ = context;
        cancellationToken.ThrowIfCancellationRequested();
        evidence.Add($"control-ping|rid={node.Rid}|value={request.Value}");
        return ValueTask.FromResult(new ControlPingReply(request.Value, node.Rid));
    }
}

[ZLinkHandlerGroup("play")]
internal sealed class CreateSpotHandler(
    IZLinkSpotManager spots,
    NodeOptions node,
    EvidenceStore evidence)
    : IZLinkRouteRequestHandler<CreateSpotReq, CreateSpotReply>
{
    public async ValueTask<CreateSpotReply> HandleAsync(
        CreateSpotReq request,
        ZLinkRouteRequestContext context,
        CancellationToken cancellationToken)
    {
        _ = context;
        var result = await spots.GetOrCreateAsync<ScenarioUserSpot>(
            RoutingId.From(request.SpotRid),
            cancellationToken);
        evidence.Add($"create-spot|rid={node.Rid}|spot={result.SpotRid}|state={result.State}");
        return new CreateSpotReply(result.SpotRid.ToString(), node.Rid, result.State.ToString());
    }
}

[ZLinkHandlerGroup("play")]
internal sealed class CloseSpotHandler(
    IZLinkSpotManager spots,
    EvidenceStore evidence)
    : IZLinkRouteRequestHandler<CloseSpotReq, CloseSpotReply>
{
    public async ValueTask<CloseSpotReply> HandleAsync(
        CloseSpotReq request,
        ZLinkRouteRequestContext context,
        CancellationToken cancellationToken)
    {
        _ = context;
        var closed = await spots.CloseAsync(RoutingId.From(request.SpotRid), cancellationToken);
        evidence.Add($"close-spot|rid={evidence.Rid}|spot={request.SpotRid}|closed={closed}");
        return new CloseSpotReply(request.SpotRid, closed);
    }
}

[ZLinkHandlerGroup("play")]
internal sealed class SpotTypeMismatchHandler(
    IZLinkSpotManager spots,
    EvidenceStore evidence)
    : IZLinkRouteRequestHandler<SpotTypeMismatchReq, SpotTypeMismatchReply>
{
    public async ValueTask<SpotTypeMismatchReply> HandleAsync(
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
            return new SpotTypeMismatchReply(request.SpotRid, true, ex.Kind.ToString(), first.State.ToString());
        }

        throw new InvalidOperationException("Expected SpotTypeMismatch for reused spot rid.");
    }
}

[ZLinkHandlerGroup("play")]
internal sealed class JoinUserSpotActorHandler(
    IZLinkActorManager actors,
    EvidenceStore evidence)
    : IZLinkRouteRequestHandler<JoinUserSpotActorReq, JoinUserSpotActorReply>
{
    public async ValueTask<JoinUserSpotActorReply> HandleAsync(
        JoinUserSpotActorReq request,
        ZLinkRouteRequestContext context,
        CancellationToken cancellationToken)
    {
        _ = context;
        var actor = await actors.GetOrCreateAsync(
            request.ActorId,
            SpotServiceNames.ActorType,
            new ScenarioActorCreateRequest(request.SpotRid),
            cancellationToken);
        evidence.Add(
            $"join-user-spot-actor|rid={evidence.Rid}|spot={request.SpotRid}"
            + $"|actor={request.ActorId}|accepted=True");
        evidence.Add($"spot-actor-joined|rid={evidence.Rid}|spot={request.SpotRid}|actor={request.ActorId}");
        return new JoinUserSpotActorReply(
            request.SpotRid,
            actor.ActorId,
            true,
            actor.Generation);
    }
}