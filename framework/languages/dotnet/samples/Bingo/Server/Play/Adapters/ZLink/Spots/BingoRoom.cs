using Systems.Zlink;
using Systems.Zlink.Codecs.Protobuf;
using Zlink.Framework.Contracts.Spots;
using Zlink.Framework.Contracts.Timers;
using Bingo.Server.Play.Adapters.ZLink.Actors;
using Bingo.Server.Play.Domain.Bingo;
using Bingo.Server.Play.Adapters.ZLink.Notifications;
using Bingo.Server.Play.Adapters.ZLink.Spots.Handlers;
using Bingo.Shared.Contracts;
using Microsoft.Extensions.Logging;

namespace Bingo.Server.Play.Adapters.ZLink.Spots;

internal sealed class BingoRoom(
    IZLinkSpotContext context,
    BingoRoomEventMapper eventMapper,
    BingoNotificationPublisher notifications,
    ILogger<BingoRoom> logger) : IZLinkSpot<PlayerActor>
{
    private static readonly BingoRoomSettings DefaultSettings = BingoRoomSettings.Create("two-player", 0);
    internal static readonly TimeSpan DrawPeriod = TimeSpan.FromMilliseconds(200);

    private readonly Dictionary<string, PlayerActor> _actors = new(StringComparer.Ordinal);
    private readonly BingoRoomGame _game = new(context.SpotRid.ToHex(), DefaultSettings);
    private IZLinkTimer? _drawTimer;

    public IZLinkSpotContext Context { get; } = context;

    public void Configure()
    {
        Context.Handlers.AddHandler<SubmitBingoCardHandler>();
    }

    public async ValueTask OnClosingAsync(CancellationToken cancellationToken)
    {
        _ = cancellationToken;
        if (_drawTimer is not null)
        {
            await _drawTimer.CancelAsync();
            _drawTimer = null;
        }
    }

    public ValueTask OnPostActorJoinedAsync(
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

    public ValueTask OnActorLeftAsync(
        PlayerActor actor,
        CancellationToken cancellationToken)
    {
        _ = cancellationToken;
        logger.LogInformation(
            "bingo room: actor left. room={RoomId}, actor={ActorId}",
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

    internal async ValueTask StartDrawTimerAsync(CancellationToken cancellationToken)
    {
        if (!_game.IsReadyToDraw
            || _drawTimer is not null
            || _game.Status != BingoRoomStatus.Running)
        {
            return;
        }

        _drawTimer = await Context.AddTimer<BingoRoomDrawTimerHandler>(
            "bingo-draw",
            DrawPeriod,
            new ZLinkTimerOptions
            {
                OverrunPolicy = ZLinkTimerOverrunPolicy.DelayNextTick,
                StopOnUnhandledException = true,
            },
            cancellationToken);
    }

    internal void StopDrawTimerAfterTick()
    {
        var timer = _drawTimer;
        if (timer is null)
        {
            return;
        }

        _drawTimer = null;
        _ = timer.CancelAsync().AsTask();
    }

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
