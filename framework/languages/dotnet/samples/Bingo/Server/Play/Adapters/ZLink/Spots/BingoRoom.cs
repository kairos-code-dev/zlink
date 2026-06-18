using Systems.Zlink;
using Zlink.Framework.Codecs.Protobuf;
using Zlink.Framework.Contracts.Spots;
using Bingo.Server.Play.Adapters.ZLink.Actors;
using Bingo.Server.Play.Domain.Bingo;
using Bingo.Server.Play.Adapters.ZLink.Notifications;
using Bingo.Shared.Contracts;
using Microsoft.Extensions.Logging;

namespace Bingo.Server.Play.Adapters.ZLink.Spots;

internal sealed class BingoRoom(
    IZLinkSpotContext context,
    BingoRoomEventMapper eventMapper,
    BingoNotificationPublisher notifications,
    ILogger<BingoRoom> logger) : IZLinkSpot<PlayerActor>
{
    private static readonly BingoRoomSettings DefaultSettings = BingoRoomSettings.Create(BingoSampleModes.TwoPlayer, 0);

    private readonly Dictionary<string, PlayerActor> _actors = new(StringComparer.Ordinal);
    private readonly BingoRoomGame _game = new(context.SpotRid.ToHex(), DefaultSettings);
    private bool _cleanupStarted;

    public IZLinkSpotContext Context { get; } = context;

    public void Configure()
    {
    }

    public ValueTask OnClosingAsync(CancellationToken cancellationToken)
    {
        _ = cancellationToken;
        return ValueTask.CompletedTask;
    }

    public ValueTask OnJoinedActorAsync(
        PlayerActor actor,
        CancellationToken cancellationToken)
    {
        _ = cancellationToken;
        logger.LogInformation(
            "bingo room: actor joined. room={RoomId}, actor={ActorId}",
            Context.SpotRid.ToHex(),
            actor.ActorId);
        return ValueTask.CompletedTask;
    }

    public ValueTask OnLeaveActorAsync(
        PlayerActor actor,
        CancellationToken cancellationToken)
    {
        _ = cancellationToken;
        _actors.Remove(actor.ActorId);
        logger.LogInformation(
            "bingo room: actor left. room={RoomId}, actor={ActorId}",
            Context.SpotRid.ToHex(),
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
            Context.SpotRid.ToHex(),
            actor.ActorId);
        return ValueTask.CompletedTask;
    }

    public async ValueTask<ZLinkSpotActorJoinResult> OnActorJoinAsync(
        PlayerActor actor,
        Message request,
        CancellationToken cancellationToken)
    {
        var reply = await JoinAsync(actor, request.FromProto<BingoRoomJoinReq>(), cancellationToken);
        return ZLinkSpotActorJoinResult.Accept(reply.ToProto());
    }

    public ValueTask<ZLinkSpotCreateResponse> OnCreateAsync(
        Message request,
        CancellationToken cancellationToken)
    {
        _ = cancellationToken;
        var settings = DecodeSettings(request);
        ApplySettings(settings);
        logger.LogInformation(
            "bingo room: created. room={RoomId}, roomName={RoomName}, mode={Mode}, requiredPlayers={RequiredPlayers}, maxDrawNumber={MaxDrawNumber}",
            Context.SpotRid.ToHex(),
            settings.RoomName,
            settings.Mode,
            settings.RequiredPlayers,
            settings.MaxDrawNumber);
        return ValueTask.FromResult(ZLinkSpotCreateResponse.Accept());
    }

    private static BingoRoomSettings DecodeSettings(Message request)
    {
        if (request.Size == 0)
        {
            return DefaultSettings;
        }

        var payload = request.FromProto<BingoRoomSettingsPayload>();
        return new BingoRoomSettings(
            payload.RoomName,
            payload.Mode,
            payload.RequiredPlayers,
            payload.MaxDrawNumber);
    }

    public async ValueTask<BingoRoomJoinRes> JoinAsync(
        PlayerActor actor,
        BingoRoomJoinReq request,
        CancellationToken cancellationToken)
    {
        actor.SetDisplayName(request.DisplayName);
        actor.JoinRoom(request.RoomId);
        _actors[actor.ActorId] = actor;

        var change = _game.JoinPlayer(actor.ActorId, actor.DisplayName);
        await PublishAsync(change, cancellationToken);
        return new BingoRoomJoinRes { State = change.State };
    }

    internal bool IsReadyToDraw => _game.IsReadyToDraw;

    internal BingoGameChange SubmitCard(string actorId, BingoCard card)
    {
        return _game.SubmitCard(actorId, card);
    }

    internal BingoGameChange DrawNextNumber()
    {
        return _game.DrawNextNumber();
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
    }

    internal async ValueTask LeaveFinishedActorsAsync(CancellationToken cancellationToken)
    {
        if (_cleanupStarted || _game.Status != BingoRoomStatus.Finished)
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
        _game.ApplySettings(settings);
    }

    internal void EnsureRoomId(string roomId)
    {
        if (!string.Equals(roomId, Context.SpotRid.ToHex(), StringComparison.Ordinal))
        {
            throw new InvalidOperationException($"Player is not submitting to this room. room={roomId}");
        }
    }

}
