using Zlink.Framework.Contracts.Handlers;
using ZoneWorld.Server.Configuration;
using Zlink.Framework.Contracts.Spots;
using Zlink.Framework.Contracts.Timers;
using ZoneWorld.Shared.Contracts;
using ZoneWorld.Server.ZoneNode.Infrastructure.ZLink.Actors;

namespace ZoneWorld.Server.ZoneNode.Infrastructure.ZLink.Spots.Handlers;

/// <summary>The 100ms world tick (§2.5).</summary>
internal sealed class ZoneTickHandler(ZoneNodeSettings settings) : IZLinkSpotTimerHandler<ZoneSpot>
{
    private static int _faultsInjected;

    public ValueTask HandleAsync(ZoneSpot spot, ZLinkTimerTick tick, CancellationToken cancellationToken)
    {
        // Fault injection for ZW-C4. The scenario has to see a real spot runtime event —
        // a timer handler that throws — and the only way to get one is to make a timer
        // handler throw. It fires once so the world keeps running afterwards.
        var faultZone = settings.FaultTickZone;
        if (faultZone == spot.ZoneId && Interlocked.Exchange(ref _faultsInjected, 1) == 0)
            throw new InvalidOperationException(
                $"injected tick failure for ZW-C4. zone={spot.ZoneId}");

        return spot.TickAsync(cancellationToken);
    }
}

/// <summary>The 500ms bot tick (§2.7). Separate from the world tick because bots move on
/// their own cadence.</summary>
internal sealed class BotTickHandler : IZLinkSpotTimerHandler<ZoneSpot>
{
    public ValueTask HandleAsync(ZoneSpot spot, ZLinkTimerTick tick, CancellationToken cancellationToken) =>
        spot.BotTickAsync(cancellationToken);
}

/// <summary>
/// The announcement arriving from this node's fanout subscriber, through the spot bridge
/// (§8.2). The subscriber sends only to the zones its own node hosts: a spot publish
/// would reach the whole mesh and every zone spot would receive the announcement once
/// per node.
/// </summary>
[ZLinkSpotPacketHandler(nameof(DeliverAnnounceMsg))]
internal sealed class DeliverAnnounceHandler : IZLinkSpotPacketHandler<ZoneSpot, DeliverAnnounceMsg>
{
    public ValueTask HandleAsync(
        ZoneSpot spot,
        DeliverAnnounceMsg message,
        CancellationToken cancellationToken) =>
        spot.DeliverAnnounceAsync(message, cancellationToken);
}

/// <summary>
/// A border snapshot from an adjacent zone (§4.1). The topic is registered per zone in
/// <see cref="ZoneSpot.Configure"/> rather than by attribute, because which topics a zone
/// listens to depends on which zone it is.
/// </summary>
internal sealed class ZoneBorderSubscriptionHandler : IZLinkSpotSubscriptionHandler<ZoneSpot, ZoneBorderEvent>
{
    public ValueTask HandleAsync(
        ZoneSpot spot,
        ZoneBorderEvent message,
        CancellationToken cancellationToken)
    {
        if (string.Equals(message.ToZoneId, spot.ZoneId, StringComparison.Ordinal))
            spot.ApplyBorderSnapshot(message);
        return ValueTask.CompletedTask;
    }
}

/// <summary>
/// Handles a reconnect after the player has already joined a zone. Rebinding the
/// session must not reset the actor's authoritative coordinate or move it back to
/// the spawn zone.
/// </summary>
[ZLinkSpotActorRequestHandler(nameof(JoinWorldReq))]
internal sealed class RejoinWorldHandler :
    IZLinkSpotActorRequestHandler<ZoneSpot, PlayerActor, JoinWorldReq, JoinWorldRes>
{
    public ValueTask<JoinWorldRes> HandleAsync(
        ZoneSpot spot,
        PlayerActor actor,
        ZLinkSpotActorRequestContext context,
        JoinWorldReq message,
        CancellationToken cancellationToken)
    {
        var position = actor.Position;
        return ValueTask.FromResult(new JoinWorldRes(
            message.PlayerId,
            actor.ZoneId,
            ZoneTopology.NodeOf(actor.ZoneId),
            position.X,
            position.Y,
            null));
    }
}
