using Zlink.Framework.Contracts.Handlers;
using ZoneWorld.Server.Configuration;
using Zlink.Framework.Contracts.Spots;
using Zlink.Framework.Contracts.Timers;
using ZoneWorld.Shared.Contracts;

namespace ZoneWorld.Server.ZoneNode.Infrastructure.ZLink.Spots.Handlers;

/// <summary>The 100ms world tick (§2.5).</summary>
internal sealed class ZoneTickHandler : IZLinkSpotTimerHandler<ZoneSpot>
{
    private static int _faultsInjected;

    public ValueTask HandleAsync(ZoneSpot spot, ZLinkTimerTick tick, CancellationToken cancellationToken)
    {
        // Fault injection for ZW-C4. The scenario has to see a real spot runtime event —
        // a timer handler that throws — and the only way to get one is to make a timer
        // handler throw. It fires once so the world keeps running afterwards.
        var faultZone = ZoneWorldSettings.Text("ZONEWORLD_FAULT_TICK_ZONE", string.Empty);
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
        spot.ApplyBorderSnapshot(message);
        return ValueTask.CompletedTask;
    }
}
