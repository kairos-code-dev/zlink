using Systems.Zlink;
using Zlink.Framework.Codecs.Protobuf;
using Zlink.Framework.Contracts.Spots;
using Bingo.Server.Play.Infrastructure.ZLink.Actors;
using Bingo.Server.Play.Domain.Bingo;
using Bingo.Server.Play.Infrastructure.ZLink.Spots.BingoRoomSpot.Notifications;
using Bingo.Server.Play.Infrastructure.ZLink.Spots.BingoRoomSpot.Handlers;
using Bingo.Server.Configuration;
using Bingo.Shared.Contracts;
using Microsoft.Extensions.Logging;
using Zlink.Framework.Contracts.Messaging;

namespace Bingo.Server.Play.Infrastructure.ZLink.Spots.BingoRoomSpot;

internal sealed class BingoRoom(
    IZLinkSpotContext context,
    BingoRoomEventMapper eventMapper,
    BingoNotificationPublisher notifications,
    ILogger<BingoRoom> logger) : IZLinkSpot<PlayerActor>
{
    private static readonly BingoRoomSettings DefaultSettings = BingoRoomSettings.Create(BingoSampleModes.TwoPlayer, 0);

    private readonly Dictionary<string, PlayerActor> _actors = new(StringComparer.Ordinal);
    private BingoRoomSettings _settings = DefaultSettings;
    private BingoRoomGame? _game = new(context.SpotRid.ToString(), DefaultSettings);
    private PlayerActor? _observerActor;
    private bool _cleanupStarted;

    public IZLinkSpotContext Context { get; } = context;

    public void Configure()
    {
        Context.Handlers.AddSubscribe<BingoRewardAcquiredEventHandler>(SampleNames.RewardTopic);
    }

    public ValueTask OnClosingAsync(CancellationToken cancellationToken)
    {
        _ = cancellationToken;
        return ValueTask.CompletedTask;
    }

    public async ValueTask OnJoinedActorAsync(
        PlayerActor actor,
        CancellationToken cancellationToken)
    {
        logger.LogInformation(
            "bingo room: actor joined. room={RoomId}, actor={ActorId}",
            Context.SpotRid.ToString(),
            actor.ActorId);

        // PublishAsync excludes the joining actor, so the room sends the
        // start event here when this join fills the room.
        if (_game is not null && _game.Status == BingoRoomStatus.Running)
        {
            await actor.Context.BoundSession
                .Send(new BingoGameStartedNotify { State = _game.Snapshot() })
                .Async(cancellationToken);
        }
    }

    public ValueTask OnLeaveActorAsync(
        PlayerActor actor,
        CancellationToken cancellationToken)
    {
        _ = cancellationToken;
        _actors.Remove(actor.ActorId);
        if (_observerActor is not null
            && string.Equals(_observerActor.ActorId, actor.ActorId, StringComparison.Ordinal))
        {
            _observerActor = null;
        }
        logger.LogInformation(
            "bingo room: actor left. room={RoomId}, actor={ActorId}",
            Context.SpotRid.ToString(),
            actor.ActorId);
        return ValueTask.CompletedTask;
    }

    public ValueTask OnDisconnectActorAsync(
        PlayerActor actor,
        CancellationToken cancellationToken)
    {
        _ = cancellationToken;
        actor.MarkDisconnected();
        logger.LogInformation(
            "bingo room: actor disconnected. room={RoomId}, actor={ActorId}",
            Context.SpotRid.ToString(),
            actor.ActorId);
        return ValueTask.CompletedTask;
    }

    public async ValueTask<ZLinkSpotActorJoinResult> OnActorJoinAsync(
        PlayerActor actor,
        ZLinkMessage request,
        CancellationToken cancellationToken)
    {
        var reply = await JoinAsync(actor, request.Decode<BingoRoomJoinReq>(), cancellationToken);
        return ZLinkSpotActorJoinResult.Accept(reply);
    }

    public ValueTask<ZLinkSpotCreateResponse> OnCreateAsync(
        ZLinkMessage request,
        CancellationToken cancellationToken)
    {
        _ = cancellationToken;
        var settings = BingoRoomSettingsPayloadMapper.FromMessage(request, DefaultSettings);
        ApplySettings(settings);
        logger.LogInformation(
            "bingo room: created. room={RoomId}, roomName={RoomName}, mode={Mode}, purpose={Purpose}, observedRoom={ObservedRoomId}, requiredPlayers={RequiredPlayers}, maxDrawNumber={MaxDrawNumber}",
            Context.SpotRid.ToString(),
            settings.RoomName,
            settings.Mode,
            settings.Purpose,
            settings.ObservedRoomId ?? "-",
            settings.RequiredPlayers,
            settings.MaxDrawNumber);
        return ValueTask.FromResult(ZLinkSpotCreateResponse.Accept());
    }

    public async ValueTask<BingoRoomJoinRes> JoinAsync(
        PlayerActor actor,
        BingoRoomJoinReq request,
        CancellationToken cancellationToken)
    {
        return request.ObserveOnly
            ? JoinObserver(actor, request)
            : await JoinPlayerAsync(actor, request, cancellationToken);
    }

    internal bool IsReadyToDraw => _game?.IsReadyToDraw == true;

    internal BingoGameChange SubmitCard(string actorId, BingoCard card)
    {
        return RequireGame().SubmitCard(actorId, card);
    }

    internal BingoGameChange DrawNextNumber()
    {
        return RequireGame().DrawNextNumber();
    }

    internal async ValueTask PublishAsync(
        BingoGameChange change,
        CancellationToken cancellationToken)
    {
        if (change.Events.Count == 0)
        {
            return;
        }

        await notifications.PublishAsync(eventMapper.Map(change.Events, _actors), cancellationToken);
        if (change.State.Status == BingoRoomStatus.Finished && change.State.Winners.Count > 0)
        {
            logger.LogInformation(
                "bingo reward: publishing. room={RoomId}, actor={ActorId}, item={ItemId}, nodeRid={NodeRid}",
                change.State.RoomId,
                change.State.Winners[0],
                BingoRewardItems.GoldenDauberId,
                Context.NodeRid.ToString());
            await Context.Outbound.Publish(
                    SampleNames.RewardTopic,
                    new BingoRewardAcquiredEvent
                    {
                        RoomId = change.State.RoomId,
                        ActorId = change.State.Winners[0],
                        DrawSeq = change.State.DrawSeq,
                        ItemId = BingoRewardItems.GoldenDauberId,
                        ItemName = BingoRewardItems.GoldenDauberName,
                        Rarity = BingoRewardItems.LegendaryRarity,
                    })
                .Async(cancellationToken);
            logger.LogInformation(
                "bingo reward: published. room={RoomId}, actor={ActorId}, item={ItemId}, nodeRid={NodeRid}",
                change.State.RoomId,
                change.State.Winners[0],
                BingoRewardItems.GoldenDauberId,
                Context.NodeRid.ToString());
        }
    }

    internal async ValueTask LeaveFinishedActorsAsync(CancellationToken cancellationToken)
    {
        if (_cleanupStarted || _game?.Status != BingoRoomStatus.Finished)
        {
            return;
        }

        _cleanupStarted = true;
        foreach (var actor in _actors.Values.ToArray())
        {
            actor.MarkForDestroyAfterRoomLeave();
            await Context.leaveActor(actor, cancellationToken);
        }
    }

    public void ApplySettings(BingoRoomSettings settings)
    {
        _settings = settings;
        _game = settings.IsObserver ? null : new BingoRoomGame(Context.SpotRid.ToString(), settings);
    }

    internal void EnsureRoomId(string roomId)
    {
        if (!string.Equals(roomId, Context.SpotRid.ToString(), StringComparison.Ordinal))
        {
            throw new InvalidOperationException($"Player is not submitting to this room. room={roomId}");
        }
    }

    internal async ValueTask AnnounceRewardAsync(BingoRewardAcquiredEvent message, CancellationToken cancellationToken)
    {
        if (!_settings.IsObserver
            || _observerActor is null
            || !string.Equals(message.RoomId, _settings.ObservedRoomId, StringComparison.Ordinal))
        {
            logger.LogInformation(
                "bingo reward: ignored. room={RoomId}, actor={ActorId}, item={ItemId}, observer={IsObserver}, hasActor={HasActor}, observedRoom={ObservedRoomId}, nodeRid={NodeRid}",
                message.RoomId,
                message.ActorId,
                message.ItemId,
                _settings.IsObserver,
                _observerActor is not null,
                _settings.ObservedRoomId ?? "-",
                Context.NodeRid.ToString());
            return;
        }

        logger.LogInformation(
            "bingo reward: announcing. room={RoomId}, actor={ActorId}, item={ItemId}, observer={ObserverActorId}, nodeRid={NodeRid}",
            message.RoomId,
            message.ActorId,
            message.ItemId,
            _observerActor.ActorId,
            Context.NodeRid.ToString());
        await _observerActor.Context.BoundSession
            .Send(
                new BingoRewardAnnouncedNotify
                {
                    RoomId = message.RoomId,
                    ActorId = message.ActorId,
                    DrawSeq = message.DrawSeq,
                    ItemId = message.ItemId,
                    ItemName = message.ItemName,
                    Rarity = message.Rarity,
                    ReceivingSpotNodeRid = Context.NodeRid.ToString(),
                })
            .Async(cancellationToken);
    }

    internal async ValueTask<bool> StopObservingAsync(PlayerActor actor, string roomId, CancellationToken cancellationToken)
    {
        if (!_settings.IsObserver
            || _observerActor is null
            || !string.Equals(_observerActor.ActorId, actor.ActorId, StringComparison.Ordinal)
            || !string.Equals(_settings.ObservedRoomId, roomId, StringComparison.Ordinal))
        {
            return false;
        }

        _observerActor = null;
        await Context.leaveActor(actor, cancellationToken);
        logger.LogInformation(
            "bingo observer room: actor left. observedRoom={ObservedRoomId}, observer={ActorId}",
            roomId,
            actor.ActorId);
        return true;
    }

    private BingoRoomGame RequireGame()
    {
        return _game ?? throw new InvalidOperationException("Observer BingoRoom does not own game state.");
    }

    private BingoRoomJoinRes JoinObserver(
        PlayerActor actor,
        BingoRoomJoinReq request)
    {
        if (!_settings.IsObserver)
        {
            throw new InvalidOperationException("Observe-only actor can join only an observer BingoRoom.");
        }

        if (!string.Equals(request.RoomId, _settings.ObservedRoomId, StringComparison.Ordinal))
        {
            throw new InvalidOperationException($"Observer room is not watching room '{request.RoomId}'.");
        }

        actor.SetDisplayName(request.DisplayName);
        actor.JoinRoom(request.RoomId);
        _observerActor = actor;
        logger.LogInformation(
            "bingo observer room: actor joined. observedRoom={ObservedRoomId}, observer={ActorId}, nodeRid={NodeRid}",
            _settings.ObservedRoomId,
            actor.ActorId,
            Context.NodeRid.ToString());
        return new BingoRoomJoinRes
        {
            State = new BingoRoomState
            {
                RoomId = request.RoomId,
                Status = BingoRoomStatus.Running,
            },
        };
    }

    private async ValueTask<BingoRoomJoinRes> JoinPlayerAsync(
        PlayerActor actor,
        BingoRoomJoinReq request,
        CancellationToken cancellationToken)
    {
        var game = RequireGame();
        actor.SetDisplayName(request.DisplayName);
        actor.JoinRoom(request.RoomId);
        _actors[actor.ActorId] = actor;

        var change = game.JoinPlayer(actor.ActorId, actor.DisplayName);
        logger.LogInformation(
            "bingo room: actor accepted. room={RoomId}, actor={ActorId}, status={Status}, events={EventCount}",
            request.RoomId,
            actor.ActorId,
            change.State.Status,
            change.Events.Count);
        await PublishAsync(change, cancellationToken);
        logger.LogInformation(
            "bingo room: actor join reply ready. room={RoomId}, actor={ActorId}, status={Status}",
            request.RoomId,
            actor.ActorId,
            change.State.Status);
        return new BingoRoomJoinRes { State = change.State };
    }
}
