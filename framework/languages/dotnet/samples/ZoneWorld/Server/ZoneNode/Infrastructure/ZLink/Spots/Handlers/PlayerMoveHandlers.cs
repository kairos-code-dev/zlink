using Systems.Zlink;
using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Handlers;
using Zlink.Framework.Contracts.Spots;
using ZoneWorld.Server.Configuration;
using ZoneWorld.Server.ZoneNode.Application.Node;
using ZoneWorld.Server.ZoneNode.Application.Zone;
using ZoneWorld.Server.ZoneNode.Domain.ZoneWorld;
using ZoneWorld.Server.ZoneNode.Infrastructure.ZLink.Actors;
using ZoneWorld.Shared.Contracts;

namespace ZoneWorld.Server.ZoneNode.Infrastructure.ZLink.Spots.Handlers;

/// <summary>
/// A human's move request, relayed from the client's session to its actor (§2.1).
/// </summary>
[ZLinkSpotActorSendHandler(nameof(MoveMsg))]
internal sealed class PlayerMoveHandler(PlayerMovement movement)
    : IZLinkSpotActorSendHandler<ZoneSpot, PlayerActor, MoveMsg>
{
    public ValueTask HandleAsync(
        ZoneSpot spot,
        PlayerActor actor,
        ZLinkSpotActorSendContext context,
        MoveMsg message,
        CancellationToken cancellationToken) =>
        movement.MoveAsync(spot, actor, message.X, message.Y, cancellationToken);
}

/// <summary>
/// One bot step (§2.7). A bot is the same actor type as a human and walks the same
/// movement path — validation, zone change and transfer are identical. The only
/// difference is that a rejection turns it around instead of being pushed to a client.
/// </summary>
[ZLinkSpotActorRequestHandler(nameof(BotTickReq))]
internal sealed class PlayerBotTickHandler(PlayerMovement movement)
    : IZLinkSpotActorRequestHandler<ZoneSpot, PlayerActor, BotTickReq, BotTickRes>
{
    public async ValueTask<BotTickRes> HandleAsync(
        ZoneSpot spot,
        PlayerActor actor,
        ZLinkSpotActorRequestContext context,
        BotTickReq request,
        CancellationToken cancellationToken)
    {
        if (!actor.IsBot) return new BotTickRes();

        var target = BotPatrolPolicy.NextStep(actor.Position, actor.DirX, actor.DirY);
        await movement.MoveAsync(spot, actor, target.X, target.Y, cancellationToken);
        return new BotTickRes();
    }
}

/// <summary>
/// The one movement path both humans and bots take (§2.1, §2.7). It lives here rather
/// than in the Application layer because a zone change is a JoinSpot, and JoinSpot is a
/// ZLink call that must run inside the actor's own handler turn.
/// </summary>
internal sealed class PlayerMovement(
    MoveUseCase moves,
    NodeMaintenancePolicy maintenance,
    ILogger<PlayerMovement> logger)
{
    public async ValueTask MoveAsync(
        ZoneSpot spot,
        PlayerActor actor,
        int toX,
        int toY,
        CancellationToken cancellationToken)
    {
        switch (moves.Decide(actor.Position, toX, toY))
        {
            case MoveDecision.Rejected rejected:
                await RejectAsync(actor, rejected.Reason, cancellationToken);
                return;

            case MoveDecision.Accepted { ZoneChanged: false } stayed:
                actor.MoveTo(stayed.To);
                spot.UpdatePosition(actor.ActorId, stayed.To.X, stayed.To.Y);
                return;

            case MoveDecision.Accepted accepted:
                await ChangeZoneAsync(actor, accepted.To, cancellationToken);
                return;
        }
    }

    /// <summary>
    /// Joining the target zone's spot is what moves the player, and when that spot lives
    /// on another node the join is the transfer (§2.6). The target spot is the authority
    /// on its own maintenance state, so it may still refuse — in which case the departing
    /// node's cache was stale and the coordinate stays where it was (§2.3).
    /// </summary>
    private async ValueTask ChangeZoneAsync(
        PlayerActor actor,
        PlayerPosition to,
        CancellationToken cancellationToken)
    {
        var joined = await actor.Context
            .JoinSpot(
                RoutingId.From(to.ZoneId),
                new EnterZoneMsg(actor.ActorId, to.X, to.Y, actor.IsBot, maintenance.OwnNodeId))
            .Yield(cancellationToken);

        if (joined is ZLinkActorJoinResult.Rejected rejected)
        {
            var reply = rejected.Reply.Decode<EnterZoneRes>();
            logger.LogInformation(
                "zone change refused by target node. player={PlayerId}, zone={ZoneId}, reason={Reason}",
                actor.ActorId,
                to.ZoneId,
                reply.Error);
            await RejectAsync(actor, reply.Error ?? MoveRejectReasons.ZoneMaintenance, cancellationToken);
            return;
        }

        // The join succeeded. Across nodes this actor instance no longer owns the player —
        // the target node materialised a new one from the transfer state — so nothing may
        // touch it from here. The target zone spot announces the change after commit.
    }

    /// <summary>
    /// A refused move leaves the coordinate untouched (§2.2). A human is told why; a bot
    /// has no client to tell, so it turns around and walks back (§2.7).
    /// </summary>
    private async ValueTask RejectAsync(
        PlayerActor actor,
        string reason,
        CancellationToken cancellationToken)
    {
        if (actor.IsBot)
        {
            actor.ReverseDirection();
            return;
        }

        await actor.Context.BoundSession
            .Send(new MoveRejectedNotify(reason, actor.Position.X, actor.Position.Y))
            .Async(cancellationToken);
    }
}
