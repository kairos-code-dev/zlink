using Systems.Zlink;
using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Messaging;
using Zlink.Framework.Contracts.Streams;
using ZoneWorld.Shared.Contracts;

namespace ZoneWorld.Server.Gateway.Infrastructure.ZLink.Sessions;

/// <summary>
/// One browser connection. It terminates the WebSocket, binds the session to the player's
/// actor, and relays everything else to it.
///
/// The binding is what keeps the connection alive across a zone change: when the actor
/// transfers to another node, the bound session follows it, so the client never
/// reconnects (ZW-B2).
/// </summary>
public sealed class PlayerSession(
    IZLinkSessionContext context,
    PlayerSessionBinder binder,
    ILogger<PlayerSession> logger) : IZLinkSession
{
    public IZLinkSessionContext Context { get; } = context;

    public ValueTask OnConnectedAsync(CancellationToken cancellationToken)
    {
        logger.LogInformation("client connected. session={SessionId}", Context.SessionId);
        return ValueTask.CompletedTask;
    }

    public async ValueTask OnDisconnectedAsync(CancellationToken cancellationToken)
    {
        foreach (var actor in Context.Actors.Bound.ToArray())
            await actor.NotifyDisconnectedAsync(cancellationToken);

        logger.LogInformation("client disconnected. session={SessionId}", Context.SessionId);
    }

    public ValueTask OnErrorAsync(ZLinkStreamError error, CancellationToken cancellationToken)
    {
        logger.LogWarning(
            "stream error. session={SessionId}, code={Code}, message={Message}",
            Context.SessionId,
            error.Error,
            error.Diagnostic?.Message);
        return ValueTask.CompletedTask;
    }

    public async ValueTask OnDispatchAsync(
        ZLinkSessionDispatchContext dispatch,
        ZLinkMessage payload,
        CancellationToken cancellationToken)
    {
        if (Context.Actors.Bound.Count == 0)
        {
            if (dispatch.PacketName != nameof(JoinWorldReq))
                throw new InvalidOperationException(
                    $"Client must join the world before sending '{dispatch.PacketName}'.");

            await binder.BindAsync(Context, payload.Decode<JoinWorldReq>().PlayerId, cancellationToken);
        }

        var actor = Context.Actors.Bound.Single();

        // Everything, including the join itself, is relayed to the actor. The relay is what
        // tells the node hosting the actor where to push: binding the session registers the
        // route on this node only, and the actor's node learns it from the first relayed
        // packet. Answer the join without relaying it and a player who never moves would
        // sit in the world receiving nothing.
        await actor.RelayAsync(payload, cancellationToken);
    }
}

/// <summary>
/// Finds or creates the player's actor on the node that owns the spawn zone, and binds this
/// session to it. The Gateway hosts no actors of its own (§4) — it takes part in the spot
/// mesh only so it can bind to one living on a zone node and relay to it.
/// </summary>
public sealed class PlayerSessionBinder(
    IZLinkChannelClient channels,
    ILogger<PlayerSessionBinder> logger)
{
    public async ValueTask BindAsync(
        IZLinkSessionContext context,
        string playerId,
        CancellationToken cancellationToken)
    {
        var ensured = await channels
            .RequestToChannel(ZoneWorldNames.ActorsChannel, new EnsurePlayerActorReq(playerId))
            .Async<EnsurePlayerActorRes>(cancellationToken);

        var actorRef = new ActorRef(
            RoutingId.FromHex(ensured.Actor.NodeRid),
            ensured.Actor.ActorId,
            ensured.Actor.Generation);

        await context.Actors.BindOrGetAsync(actorRef, cancellationToken);
        logger.LogInformation("session bound to player actor. player={PlayerId}", playerId);
    }
}
