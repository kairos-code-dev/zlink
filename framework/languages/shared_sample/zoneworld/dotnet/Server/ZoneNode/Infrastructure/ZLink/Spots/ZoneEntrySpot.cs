using Systems.Zlink;
using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Handlers;
using Zlink.Framework.Contracts.Messaging;
using Zlink.Framework.Contracts.Spots;
using ZoneWorld.Server.Configuration;
using ZoneWorld.Server.ZoneNode.Infrastructure.ZLink.Actors;
using ZoneWorld.Shared.Contracts;

namespace ZoneWorld.Server.ZoneNode.Infrastructure.ZLink.Spots;

/// <summary>
/// Where a player actor is born. An actor created through the actor manager lands here,
/// and it stays only long enough to join its zone spot — which is the step that actually
/// puts it in the world (§2.6).
/// </summary>
public sealed class ZoneEntrySpot(IZLinkEntrySpotContext context) : IZLinkEntrySpot<PlayerActor>
{
    public IZLinkEntrySpotContext Context { get; } = context;

    public ValueTask<ZLinkSpotActorJoinResult> OnActorJoinAsync(
        string actorId,
        ZLinkMessage request,
        CancellationToken cancellationToken) =>
        ValueTask.FromResult<ZLinkSpotActorJoinResult>(ZLinkSpotActorJoinResult.Accept());

    public ValueTask OnJoinedActorAsync(PlayerActor actor, CancellationToken cancellationToken) =>
        ValueTask.CompletedTask;

    public ValueTask OnLeaveActorAsync(PlayerActor actor, CancellationToken cancellationToken) =>
        ValueTask.CompletedTask;
}

/// <summary>
/// Puts a freshly created actor into the world. The join has to run inside the actor's
/// own handler turn, so the ensure path asks the actor to do it rather than doing it
/// from the outside.
/// </summary>
[ZLinkSpotActorRequestHandler(nameof(EnterWorldReq))]
internal sealed class PlayerEnterWorldHandler(ILogger<PlayerEnterWorldHandler> logger)
    : IZLinkEntrySpotActorRequestHandler<ZoneEntrySpot, PlayerActor, EnterWorldReq, EnterWorldRes>
{
    public async ValueTask<EnterWorldRes> HandleAsync(
        ZoneEntrySpot entrySpot,
        PlayerActor actor,
        ZLinkSpotActorRequestContext context,
        EnterWorldReq message,
        CancellationToken cancellationToken)
    {
        return await EnterWorld.RunAsync(actor, message, logger, cancellationToken);
    }
}

/// <summary>
/// A human's entry, relayed from its session (§7.1). It is handled by the actor rather than
/// by the Gateway so that the relay itself reaches the node hosting the actor — that relay is
/// what tells the node where to push.
/// </summary>
[ZLinkSpotActorRequestHandler(nameof(JoinWorldReq))]
internal sealed class PlayerJoinWorldHandler(ILogger<PlayerJoinWorldHandler> logger)
    : IZLinkEntrySpotActorRequestHandler<ZoneEntrySpot, PlayerActor, JoinWorldReq, JoinWorldRes>
{
    public async ValueTask<JoinWorldRes> HandleAsync(
        ZoneEntrySpot entrySpot,
        PlayerActor actor,
        ZLinkSpotActorRequestContext context,
        JoinWorldReq message,
        CancellationToken cancellationToken)
    {
        var entered = await EnterWorld.RunAsync(
            actor,
            new EnterWorldReq(ZoneWorldSpec.SpawnX, ZoneWorldSpec.SpawnY, IsBot: false),
            logger,
            cancellationToken);

        return new JoinWorldRes(
            message.PlayerId,
            entered.ZoneId,
            entered.NodeId,
            entered.X,
            entered.Y,
            entered.Error);
    }
}

internal static class EnterWorld
{
    public static async ValueTask<EnterWorldRes> RunAsync(
        PlayerActor actor,
        EnterWorldReq message,
        ILogger logger,
        CancellationToken cancellationToken)
    {
        // The patrol direction has to be on the actor before it joins, because the zone
        // spot preserves it across the join and a transfer carries it to the next node.
        actor.SetPatrol(message.DirX, message.DirY);

        var zoneId = ZoneTopology.ZoneOf(message.X, message.Y);
        var joined = await actor.Context
            .JoinSpot(
                RoutingId.From(zoneId),
                new EnterZoneMsg(actor.ActorId, message.X, message.Y, message.IsBot, FromNodeId: null))
            .Yield(cancellationToken);

        var reply = joined switch
        {
            ZLinkActorJoinResult.Accepted accepted => accepted.Reply.Decode<EnterZoneRes>(),
            ZLinkActorJoinResult.Rejected rejected => rejected.Reply.Decode<EnterZoneRes>(),
            _ => throw new InvalidOperationException("Unknown actor join result.")
        };

        logger.LogInformation(
            "player entering world. player={PlayerId}, zone={ZoneId}, bot={IsBot}, error={Error}",
            actor.ActorId,
            zoneId,
            message.IsBot,
            reply.Error ?? "(none)");

        return new EnterWorldRes(reply.ZoneId, reply.NodeId, message.X, message.Y, reply.Error);
    }
}
