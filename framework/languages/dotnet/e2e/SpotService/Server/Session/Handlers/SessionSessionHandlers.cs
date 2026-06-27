using System.Collections.Concurrent;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Http;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using SpotService.Shared;
using Systems.Zlink;
using Systems.Zlink.Stream.Connector.Contracts;
using Zlink.Framework;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Codecs.Json;
using Zlink.Framework.Contracts.Dispatch;
using Zlink.Framework.Contracts.Errors;
using Zlink.Framework.Contracts.Handlers;
using Zlink.Framework.Contracts.Messaging;
using Zlink.Framework.Contracts.Spots;
using Zlink.Framework.Contracts.Streams;
using Zlink.Framework.Contracts.Timers;
using SpotService.Server.Session.Handlers;
using SpotService.Server.Session.Spots;

namespace SpotService.Server.Session.Handlers;


internal sealed class ScenarioSession(
    IZLinkSessionContext context,
    IZLinkSessionPacketDispatcher<IZLinkSessionContext> handlers,
    EvidenceStore evidence) : IZLinkSession
{
    public IZLinkSessionContext Context { get; } = context;

    public ValueTask OnConnectedAsync(CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        evidence.Add($"session-connected|rid={evidence.Rid}|session={Context.SessionId}");
        return ValueTask.CompletedTask;
    }

    public async ValueTask OnDisconnectedAsync(CancellationToken cancellationToken)
    {
        foreach (var actor in Context.Actors.Bound.Take(1))
        {
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
        Zlink.Framework.Contracts.Messaging.ZLinkMessage payload,
        CancellationToken cancellationToken)
    {
        if (await handlers.TryHandleAsync(Context, dispatch, payload, cancellationToken))
        {
            return;
        }

        var actorId = dispatch.Metadata.Find(SpotServiceNames.ActorIdMetadata);
        var actor = string.IsNullOrWhiteSpace(actorId)
            ? RequireSingleBoundActor()
            : Context.Actors.Find(actorId)
              ?? throw new InvalidOperationException($"Actor route not found: {actorId}");
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
    : IZLinkSessionPacketHandler<IZLinkSessionContext>
{
    public string PacketName => "AuthReq";

    public async ValueTask HandleAsync(
        IZLinkSessionContext context,
        ZLinkSessionDispatchContext dispatch,
        Zlink.Framework.Contracts.Messaging.ZLinkMessage payload,
        CancellationToken cancellationToken)
    {
        _ = dispatch;
        var request = payload.Decode<AuthReq>();
        var ensured = string.Equals(request.NodeRid, node.Rid, StringComparison.Ordinal)
            ? await EnsureLocalActorAsync(actors, node, evidence, request, cancellationToken)
            : await routes.Request(
                    SpotServiceNames.ControlChannel,
                    RoutingId.From(request.NodeRid),
                    new EnsureActorReq(request.ActorId, request.DisplayName, request.NodeRid))
                .PacketName("EnsureActorReq")
                .Async<EnsureActorReply>(cancellationToken);
        await context.Actors.BindAsync(
            new ActorRef(RoutingId.From(ensured.NodeRid), ensured.ActorId, ensured.Generation),
            cancellationToken);
        await context.Client.Reply(new AuthReply(ensured.ActorId, ensured.NodeRid)).Async();
    }

    private static async ValueTask<EnsureActorReply> EnsureLocalActorAsync(
        IZLinkActorManager actors,
        NodeOptions node,
        EvidenceStore evidence,
        AuthReq request,
        CancellationToken cancellationToken)
    {
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

internal sealed class MultiBindSessionHandler(
    IZLinkRouteClient routes)
    : IZLinkSessionPacketHandler<IZLinkSessionContext>
{
    public string PacketName => "MultiBindReq";

    public async ValueTask HandleAsync(
        IZLinkSessionContext context,
        ZLinkSessionDispatchContext dispatch,
        Zlink.Framework.Contracts.Messaging.ZLinkMessage payload,
        CancellationToken cancellationToken)
    {
        _ = dispatch;
        var request = payload.Decode<MultiBindReq>();
        foreach (var actorId in new[] { request.FirstActorId, request.SecondActorId })
        {
            var ensured = await routes.Request(
                    SpotServiceNames.ControlChannel,
                    RoutingId.From(request.NodeRid),
                    new EnsureActorReq(actorId, actorId, request.NodeRid))
                .PacketName("EnsureActorReq")
                .Async<EnsureActorReply>(cancellationToken);
            await context.Actors.BindAsync(
                new ActorRef(RoutingId.From(ensured.NodeRid), ensured.ActorId, ensured.Generation),
                cancellationToken);
        }

        await context.Client.Reply(new MultiBindReply(context.Actors.Bound.Count)).Async();
    }
}

internal sealed class UserSpotAuthSessionHandler(
    IZLinkActorManager actors,
    IZLinkRouteClient routes,
    NodeOptions node,
    EvidenceStore evidence)
    : IZLinkSessionPacketHandler<IZLinkSessionContext>
{
    public string PacketName => "UserSpotAuthReq";

    public async ValueTask HandleAsync(
        IZLinkSessionContext context,
        ZLinkSessionDispatchContext dispatch,
        Zlink.Framework.Contracts.Messaging.ZLinkMessage payload,
        CancellationToken cancellationToken)
    {
        _ = dispatch;
        var request = payload.Decode<UserSpotAuthReq>();
        var joined = string.Equals(request.NodeRid, node.Rid, StringComparison.Ordinal)
            ? await JoinLocalUserSpotAsync(actors, evidence, request, cancellationToken)
            : await JoinRemoteUserSpotAsync(routes, request, cancellationToken);

        EnsureAccepted(joined);
        await context.Actors.BindAsync(
            new ActorRef(RoutingId.From(request.NodeRid), joined.ActorId, joined.Generation),
            cancellationToken);
        await context.Client.Reply(new AuthReply(joined.ActorId, request.NodeRid)).Async();
    }

    private static async ValueTask<JoinUserSpotActorReply> JoinLocalUserSpotAsync(
        IZLinkActorManager actors,
        EvidenceStore evidence,
        UserSpotAuthReq request,
        CancellationToken cancellationToken)
    {
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
            Accepted: true,
            actor.Generation);
    }

    private static async ValueTask<JoinUserSpotActorReply> JoinRemoteUserSpotAsync(
        IZLinkRouteClient routes,
        UserSpotAuthReq request,
        CancellationToken cancellationToken)
    {
        await routes.Request(
                SpotServiceNames.ControlChannel,
                RoutingId.From(request.NodeRid),
                new CreateSpotReq(request.SpotRid))
            .PacketName("CreateSpotReq")
            .Async<CreateSpotReply>(cancellationToken);
        return await routes.Request(
                SpotServiceNames.ControlChannel,
                RoutingId.From(request.NodeRid),
                new JoinUserSpotActorReq(request.SpotRid, request.ActorId))
            .PacketName("JoinUserSpotActorReq")
            .Async<JoinUserSpotActorReply>(cancellationToken);
    }

    private static void EnsureAccepted(JoinUserSpotActorReply joined)
    {
        if (!joined.Accepted)
        {
            throw new InvalidOperationException($"User spot actor join was rejected: {joined.ActorId}");
        }
    }
}
