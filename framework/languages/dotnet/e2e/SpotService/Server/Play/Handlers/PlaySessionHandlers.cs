using SpotService.Server.Play.Spots;
using SpotService.Shared;
using Systems.Zlink;
using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Errors;
using Zlink.Framework.Contracts.Messaging;
using Zlink.Framework.Contracts.Streams;

namespace SpotService.Server.Play.Handlers;

internal sealed class ScenarioSession(
    IZLinkSessionContext context,
    EvidenceStore evidence) : IZLinkSession
{
    public IZLinkSessionContext Context { get; } = context;

    public void Configure()
    {
        Context.Handlers.AddHandler<AuthSessionHandler>();
        Context.Handlers.AddHandler<MultiBindSessionHandler>();
        Context.Handlers.AddHandler<UserSpotAuthSessionHandler>();
    }

    public ValueTask OnConnectedAsync(CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        evidence.Add($"session-connected|rid={evidence.Rid}|session={Context.SessionId}");
        return ValueTask.CompletedTask;
    }

    public async ValueTask OnDisconnectedAsync(CancellationToken cancellationToken)
    {
        foreach (var actor in Context.Actors.Bound.Take(1))
            try
            {
                await actor.NotifyDisconnectedAsync(cancellationToken);
            }
            catch (ZLinkFrameworkException error)
                when (error.Kind == ZLinkFrameworkErrorKind.ActorRouteNotFound)
            {
                evidence.Add(
                    $"session-disconnect-skip|rid={evidence.Rid}|session={Context.SessionId}"
                    + $"|actor={actor.ActorId}|reason=actor-route-not-found");
            }

        evidence.Add($"session-disconnected|rid={evidence.Rid}|session={Context.SessionId}");
    }

    public ValueTask OnErrorAsync(ZLinkStreamError error, CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        evidence.Add($"session-error|rid={evidence.Rid}|error={error}");
        return ValueTask.CompletedTask;
    }

    public async ValueTask OnDispatchAsync(
        ZLinkSessionDispatchContext dispatch,
        ZLinkMessage payload,
        CancellationToken cancellationToken)
    {
        if (await Context.Handlers.TryHandleAsync(dispatch, payload, cancellationToken)) return;

        var actorId = dispatch.Metadata.Find(SpotServiceNames.ActorIdMetadata);
        var actor = string.IsNullOrWhiteSpace(actorId)
            ? RequireSingleBoundActor()
            : Context.Actors.Find(actorId);
        if (actor is null)
        {
            throw new InvalidOperationException($"Actor route not found: {actorId}");
        }

        await actor.RelayAsync(payload, cancellationToken);
    }

    private IZLinkSessionActor RequireSingleBoundActor()
    {
        return Context.Actors.Bound.Count switch
        {
            1 => Context.Actors.Bound.Single(),
            0 => throw new InvalidOperationException("No actor is bound."),
            _ => throw new InvalidOperationException("ActorRouteNotFound: actor-id metadata is required.")
        };
    }
}

internal sealed class AuthSessionHandler(
    IZLinkRouteClient routes,
    IZLinkActorManager actors,
    NodeOptions node,
    EvidenceStore evidence)
    : IZLinkSessionPacketHandler<IZLinkSessionContext, AuthReq>
{
    public async ValueTask HandleAsync(
        IZLinkSessionContext context,
        ZLinkSessionDispatchContext dispatch,
        AuthReq request,
        CancellationToken cancellationToken)
    {
        _ = dispatch;
        var ensured = string.Equals(request.NodeRid, node.Rid, StringComparison.Ordinal)
            ? await EnsureLocalActorAsync(actors, node, evidence, request, cancellationToken)
            : await routes.RequestToNode(
                    SpotServiceNames.ControlChannel,
                    RoutingId.From(request.NodeRid),
                    new EnsureActorReq(request.ActorId, request.DisplayName, request.NodeRid))
                .Async<EnsureActorRes>(cancellationToken);
        await context.Actors.BindAsync(
            new ActorRef(RoutingId.From(ensured.NodeRid), ensured.ActorId, ensured.Generation),
            cancellationToken);
        await context.Client.Reply(new AuthRes(ensured.ActorId, ensured.NodeRid))
            .SubmitAsync(cancellationToken);
    }

    private static async ValueTask<EnsureActorRes> EnsureLocalActorAsync(
        IZLinkActorManager actors,
        NodeOptions node,
        EvidenceStore evidence,
        AuthReq request,
        CancellationToken cancellationToken)
    {
        var actor = await actors.GetOrCreateAsync(
            request.ActorId,
            SpotServiceNames.ActorType,
            new ScenarioActorCreateReq(request.DisplayName),
            cancellationToken);

        evidence.Add($"ensure-actor|rid={node.Rid}|actor={request.ActorId}");
        evidence.Add($"entry-joined|rid={node.Rid}|actor={request.ActorId}");
        return new EnsureActorRes(
            actor.ActorId,
            actor.NodeRid.ToString(),
            actor.Generation);
    }
}

internal sealed class MultiBindSessionHandler(
    IZLinkRouteClient routes)
    : IZLinkSessionPacketHandler<IZLinkSessionContext, MultiBindReq>
{
    public async ValueTask HandleAsync(
        IZLinkSessionContext context,
        ZLinkSessionDispatchContext dispatch,
        MultiBindReq request,
        CancellationToken cancellationToken)
    {
        _ = dispatch;
        foreach (var actorId in new[] { request.FirstActorId, request.SecondActorId })
        {
            var ensured = await routes.RequestToNode(
                    SpotServiceNames.ControlChannel,
                    RoutingId.From(request.NodeRid),
                    new EnsureActorReq(actorId, actorId, request.NodeRid))
                .Async<EnsureActorRes>(cancellationToken);
            await context.Actors.BindAsync(
                new ActorRef(RoutingId.From(ensured.NodeRid), ensured.ActorId, ensured.Generation),
                cancellationToken);
        }

        await context.Client.Reply(new MultiBindRes(context.Actors.Bound.Count))
            .SubmitAsync(cancellationToken);
    }
}

internal sealed class UserSpotAuthSessionHandler(
    IZLinkActorManager actors,
    IZLinkRouteClient routes,
    NodeOptions node,
    EvidenceStore evidence)
    : IZLinkSessionPacketHandler<IZLinkSessionContext, UserSpotAuthReq>
{
    public async ValueTask HandleAsync(
        IZLinkSessionContext context,
        ZLinkSessionDispatchContext dispatch,
        UserSpotAuthReq request,
        CancellationToken cancellationToken)
    {
        _ = dispatch;
        var ensured = string.Equals(request.NodeRid, node.Rid, StringComparison.Ordinal)
            ? await EnsureLocalActorAsync(actors, evidence, request, cancellationToken)
            : await routes.RequestToNode(
                    SpotServiceNames.ControlChannel,
                    RoutingId.From(request.NodeRid),
                    new EnsureActorReq(request.ActorId, request.DisplayName, request.NodeRid))
                .Async<EnsureActorRes>(cancellationToken);
        await context.Actors.BindAsync(
            new ActorRef(RoutingId.From(ensured.NodeRid), ensured.ActorId, ensured.Generation),
            cancellationToken);
        await context.Client.Reply(new AuthRes(ensured.ActorId, ensured.NodeRid))
            .SubmitAsync(cancellationToken);
    }

    private static async ValueTask<EnsureActorRes> EnsureLocalActorAsync(
        IZLinkActorManager actors,
        EvidenceStore evidence,
        UserSpotAuthReq request,
        CancellationToken cancellationToken)
    {
        var actor = await actors.GetOrCreateAsync(
            request.ActorId,
            SpotServiceNames.ActorType,
            new ScenarioActorCreateReq(request.SpotRid),
            cancellationToken);

        evidence.Add(
            $"ensure-user-spot-actor|rid={evidence.Rid}|spot={request.SpotRid}"
            + $"|actor={request.ActorId}");
        return new EnsureActorRes(
            actor.ActorId,
            request.NodeRid,
            actor.Generation);
    }
}
