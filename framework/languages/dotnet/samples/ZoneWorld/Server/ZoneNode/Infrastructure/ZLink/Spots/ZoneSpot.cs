using System.Text;
using Systems.Zlink;
using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Messaging;
using Zlink.Framework.Contracts.Spots;
using Zlink.Framework.Contracts.Timers;
using ZoneWorld.Server.Configuration;
using ZoneWorld.Server.ZoneNode.Application.Node;
using ZoneWorld.Server.ZoneNode.Application.Zone;
using ZoneWorld.Server.ZoneNode.Domain.ZoneWorld;
using ZoneWorld.Server.ZoneNode.Infrastructure.ZLink.Actors;
using ZoneWorld.Server.ZoneNode.Infrastructure.ZLink.Spots.Handlers;
using ZoneWorld.Shared.Contracts;

namespace ZoneWorld.Server.ZoneNode.Infrastructure.ZLink.Spots;

/// <summary>
/// One geographic zone. Its spotRid is the ZoneId, so entering a zone means joining the
/// spot named after it — and when that spot lives on another node, the join is what
/// transfers the actor (§2.6). Nothing else moves a player between zones.
///
/// The spot keeps a copy of each resident's position for rendering and border sync. The
/// authority is the player actor (§2.1); this copy never overrules it.
/// </summary>
public sealed class ZoneSpot(
    IZLinkSpotContext context,
    NodeMaintenancePolicy maintenance,
    NodePlayerCensus census,
    IZLinkActorClient actors,
    IZLinkActorDirectory directory,
    ILogger<ZoneSpot> logger) : IZLinkSpot<PlayerActor>
{
    private readonly ZoneState _state = new(DecodeZoneId(context.SpotRid));
    private readonly Dictionary<string, PlayerActor> _residents = new(StringComparer.Ordinal);
    private readonly Dictionary<string, ActorRef> _botRefs = new(StringComparer.Ordinal);
    private readonly Dictionary<string, EnterZoneMsg> _pendingJoins = new(StringComparer.Ordinal);
    private IZLinkTimer? _tick;
    private IZLinkTimer? _botTick;

    public IZLinkSpotContext Context { get; } = context;

    public string ZoneId => _state.ZoneId;

    public string NodeId => maintenance.OwnNodeId;

    public async ValueTask OnInitializeAsync(CancellationToken cancellationToken)
    {
        _tick = await Context.AddTimer<ZoneTickHandler>(
            $"zone-tick-{ZoneId}",
            TimeSpan.FromMilliseconds(ZoneWorldSpec.TickPeriodMs),
            cancellationToken: cancellationToken);
        _botTick = await Context.AddTimer<BotTickHandler>(
            $"bot-tick-{ZoneId}",
            TimeSpan.FromMilliseconds(ZoneWorldSpec.BotTickPeriodMs),
            cancellationToken: cancellationToken);
        logger.LogInformation("zone spot ready. zone={ZoneId}, node={NodeId}", ZoneId, NodeId);
    }

    public async ValueTask OnClosingAsync(CancellationToken cancellationToken)
    {
        if (_tick is not null) await _tick.CancelAsync();
        if (_botTick is not null) await _botTick.CancelAsync();
    }

    public ValueTask<ZLinkSpotCreateResponse> OnCreateAsync(
        ZLinkMessage request,
        CancellationToken cancellationToken) =>
        ValueTask.FromResult(ZLinkSpotCreateResponse.Accept());

    /// <summary>
    /// Admission (§2.3). This node is the authority on its own maintenance state, so a
    /// stale cache on the node the player is leaving cannot let anyone in. Maintenance
    /// blocks arrivals from another node and blocks new entries into the world; it does
    /// not block a player who is already on this node from moving between its zones.
    /// The framework does not hand the source node to this callback, so the join payload
    /// carries it.
    /// </summary>
    public ValueTask<ZLinkSpotActorJoinResult> OnActorJoinAsync(
        string actorId,
        ZLinkMessage request,
        CancellationToken cancellationToken)
    {
        var enter = request.Decode<EnterZoneMsg>();
        var arriving = enter.FromNodeId != NodeId;

        if (arriving && maintenance.IsOwnNodeUnderMaintenance)
        {
            logger.LogInformation(
                "zone spot: join rejected, node under maintenance. zone={ZoneId}, player={PlayerId}",
                ZoneId,
                enter.PlayerId);
            return ValueTask.FromResult<ZLinkSpotActorJoinResult>(
                ZLinkSpotActorJoinResult.Reject(
                    new EnterZoneRes(ZoneId, NodeId, MoveRejectReasons.ZoneMaintenance)));
        }

        _pendingJoins[actorId] = enter;
        return ValueTask.FromResult<ZLinkSpotActorJoinResult>(
            ZLinkSpotActorJoinResult.Accept(new EnterZoneRes(ZoneId, NodeId)));
    }

    public ValueTask OnJoinedActorAsync(PlayerActor actor, CancellationToken cancellationToken)
    {
        if (!_pendingJoins.Remove(actor.ActorId, out var enter)) return ValueTask.CompletedTask;

        actor.Restore(enter.X, enter.Y, ZoneId, enter.IsBot, actor.DirX, actor.DirY);
        _residents[enter.PlayerId] = actor;
        _state.Enter(enter.PlayerId, enter.X, enter.Y, enter.IsBot);
        census.Record(ZoneId, _state.PlayerCount);

        // A brand-new entry learns its zone from JoinWorldRes, so only a zone *change*
        // is announced here. Announcing from the target spot covers both kinds of change
        // with one path: the source actor instance no longer exists after a cross-node
        // transfer, so it could not have sent this itself.
        if (!enter.IsBot && enter.FromNodeId is not null)
            actor.Context.BoundSession
                .Send(new ZoneChangedNotify(
                    enter.PlayerId,
                    ZoneId,
                    NodeId,
                    Transferred: enter.FromNodeId != NodeId))
                .Submit();

        logger.LogInformation(
            "zone spot: player entered. zone={ZoneId}, player={PlayerId}, bot={IsBot}, from={FromNodeId}",
            ZoneId,
            enter.PlayerId,
            enter.IsBot,
            enter.FromNodeId ?? "(new)");
        return ValueTask.CompletedTask;
    }

    public ValueTask OnLeaveActorAsync(PlayerActor actor, CancellationToken cancellationToken)
    {
        // The player id is the actor id: the ensure path creates the actor under the
        // player's own id, so the two never diverge.
        Remove(actor.ActorId);
        return ValueTask.CompletedTask;
    }

    public ValueTask OnDisconnectActorAsync(PlayerActor actor, CancellationToken cancellationToken)
    {
        // A client that goes away leaves the world. Keeping the player in the zone would mean
        // pushing a tick at a session that no longer exists, every 100ms, for as long as the
        // node runs — and it would leave the player visible to everyone else as a body that
        // never moves.
        Remove(actor.ActorId);
        logger.LogInformation(
            "zone spot: player left, client gone. zone={ZoneId}, player={PlayerId}",
            ZoneId,
            actor.ActorId);
        return ValueTask.CompletedTask;
    }

    internal void Remove(string playerId)
    {
        _state.Leave(playerId);
        _residents.Remove(playerId);
        _botRefs.Remove(playerId);
        census.Record(ZoneId, _state.PlayerCount);
    }

    internal void UpdatePosition(string playerId, int x, int y) =>
        _state.UpdatePosition(playerId, x, y);

    internal void ApplyBorderSnapshot(ZoneBorderEvent snapshot) =>
        _state.ApplyBorderSnapshot(snapshot.FromZoneId, snapshot.Tick, snapshot.Players);

    internal ValueTask TickAsync(CancellationToken cancellationToken)
    {
        var output = ZoneTickUseCase.Advance(_state);

        PushToClients(output.PushTargets, output.Notify, cancellationToken);

        foreach (var borderEvent in output.BorderEvents)
        {
            Context.Outbound
                .Publish(ZoneWorldNames.BorderTopic(ZoneId, borderEvent.ToZoneId), borderEvent)
                .Submit();
        }

        return ValueTask.CompletedTask;
    }

    /// <summary>
    /// Drives this zone's bots (§2.7). A bot moves down the same path a human does, and
    /// that path can end in a JoinSpot, which only an actor handler turn may start — so
    /// the bot is driven by a packet addressed to it rather than by calling into it here.
    /// </summary>
    internal async ValueTask BotTickAsync(CancellationToken cancellationToken)
    {
        foreach (var playerId in ZoneTickUseCase.Bots(_state))
        {
            var actorRef = await ResolveBotAsync(playerId, cancellationToken);
            if (actorRef is null) continue;
            actors.SendToActor(actorRef.Value, new BotTickMsg()).Submit();
        }
    }

    /// <summary>Delivers an announcement to every human in this zone (§8.2).</summary>
    internal ValueTask DeliverAnnounceAsync(
        DeliverAnnounceMsg announce,
        CancellationToken cancellationToken)
    {
        // The canonical asks that *every* zone spot receive the announcement (§11 ZW-D1), and
        // no client can see another zone's spot — so the spot says so where the runner can read it.
        logger.LogInformation(
            "zone spot: announcement delivered. zone={ZoneId}, announcement={AnnouncementId}",
            ZoneId,
            announce.AnnouncementId);

        PushToClients(
            ZoneTickUseCase.Humans(_state),
            new WorldAnnounceNotify(announce.AnnouncementId, announce.Text),
            cancellationToken);
        return ValueTask.CompletedTask;
    }

    /// <summary>
    /// Pushes through the resident's own bound session. Bots never appear in
    /// <paramref name="playerIds"/>: they have no bound session, so a push addressed to
    /// one would be a message with nowhere to go (ZW-F3).
    /// </summary>
    private void PushToClients<TMessage>(
        IReadOnlyList<string> playerIds,
        TMessage message,
        CancellationToken cancellationToken)
    {
        foreach (var playerId in playerIds)
        {
            if (!_residents.TryGetValue(playerId, out var actor)) continue;

            try
            {
                actor.Context.BoundSession.Send(message).Submit();
            }
            catch (Exception error)
            {
                // A player whose client went away is still in the world — the actor keeps
                // its coordinate so the same PlayerId can come back to it (§2.4). What it
                // no longer has is somewhere to push to. That must not cost the other
                // players their tick, so the failure stops here rather than unwinding the
                // whole push loop and killing the timer with it.
                logger.LogWarning(
                    error,
                    "push skipped, no session. zone={ZoneId}, player={PlayerId}",
                    ZoneId,
                    playerId);
            }
        }
    }

    /// <summary>
    /// A bot's ActorRef, read from the actor directory rather than carried in the join
    /// payload: that payload is built on the node the bot is leaving, so a ref inside it
    /// would name the wrong node the moment the transfer lands (§8.3). The lookup happens
    /// on the bot tick, by which point the location row for the arrival is committed.
    /// </summary>
    private async ValueTask<ActorRef?> ResolveBotAsync(string playerId, CancellationToken cancellationToken)
    {
        if (_botRefs.TryGetValue(playerId, out var cached)) return cached;

        var resolved = await directory.FindAsync(playerId, cancellationToken);
        if (resolved is null) return null;

        _botRefs[playerId] = resolved.Value;
        return resolved;
    }

    private static string DecodeZoneId(RoutingId spotRid) =>
        Encoding.UTF8.GetString(spotRid.ToBytes());
}
