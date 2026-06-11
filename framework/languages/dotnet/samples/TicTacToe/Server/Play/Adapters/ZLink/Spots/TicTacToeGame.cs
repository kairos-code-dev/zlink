using Systems.Zlink;
using Systems.Zlink.Codecs.MessagePack;
using System.Text;
using TicTacToe.Server.Play.Adapters.ZLink.Actors;
using TicTacToe.Server.Play.Domain.TicTacToe;
using TicTacToe.Server.Play.Adapters.ZLink.Spots.Handlers;
using TicTacToe.Shared.Contracts;
using Zlink.Framework.Contracts.Spots;
using Zlink.Framework.Contracts.Timers;

namespace TicTacToe.Server.Play.Adapters.ZLink.Spots;

sealed class TicTacToeGame(
    IZLinkSpotContext context,
    ILogger<TicTacToeGame> logger) : IZLinkSpot<PlayActor>
{
    private static readonly TimeSpan GameTickPeriod = TimeSpan.FromSeconds(1);
    private static readonly TimeSpan TurnTimeout = TimeSpan.FromSeconds(15);

    private readonly Dictionary<string, PlayActor> _actors = new(StringComparer.Ordinal);
    private readonly string _roomId = DecodeRoomId(context.SpotRid);
    private readonly TicTacToeMatch _match = new(DecodeRoomId(context.SpotRid), TurnTimeout);
    private IZLinkTimer? _gameTick;

    public IZLinkSpotContext Context { get; } = context;

    public void Configure()
    {
        Context.Handlers.AddHandler<PlayActorPlaceMarkHandler>();
    }

    public ValueTask OnPostActorJoinedAsync(
        PlayActor actor,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        logger.LogInformation(
            "game spot: actor joined. actor={ActorId}, roomId={RoomId}",
            actor.ActorId,
            _roomId);
        return ValueTask.CompletedTask;
    }

    public ValueTask OnActorLeftAsync(
        PlayActor actor,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        logger.LogInformation(
            "game spot: actor left. actor={ActorId}, roomId={RoomId}",
            actor.ActorId,
            _roomId);
        return ValueTask.CompletedTask;
    }

    public async ValueTask<ZLinkSpotActorJoinResult> OnActorJoinAsync(
        PlayActor player,
        Message request,
        CancellationToken cancellationToken)
    {
        var joinRequest = request.FromMsgPack<TicTacToeGameJoinReq>();
        var reply = await JoinPlayerAsync(player, joinRequest.RoomId, cancellationToken);
        logger.LogInformation(
            "TicTacToeGame: actor join accepted. actor={ActorId}, roomId={RoomId}, mark={Mark}",
            player.ActorId,
            joinRequest.RoomId,
            reply.State.XActorId == player.ActorId ? "X" : "O");

        return ZLinkSpotActorJoinResult.Accept(reply.ToMsgPack());
    }

    public ValueTask<ZLinkSpotCreateResponse> OnCreateAsync(
        Message request,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        logger.LogInformation(
            "game spot: created. roomId={RoomId}, createPayloadBytes={CreatePayloadBytes}",
            _roomId,
            request.Size);
        return ValueTask.FromResult(ZLinkSpotCreateResponse.Accept());
    }

    public async ValueTask OnInitializeAsync(CancellationToken cancellationToken)
    {
        _gameTick = await Context.AddTimer<TicTacToeGameTimerHandler>(
            "game-tick",
            GameTickPeriod,
            cancellationToken: cancellationToken);
    }

    public async ValueTask OnClosingAsync(CancellationToken cancellationToken)
    {
        _ = cancellationToken;
        if (_gameTick is not null)
        {
            await _gameTick.CancelAsync();
        }
    }

    public async ValueTask<TicTacToeGameJoinRes> JoinPlayerAsync(
        PlayActor actor,
        string roomId,
        CancellationToken cancellationToken)
    {
        actor.JoinRoom(roomId);
        _actors[actor.ActorId] = actor;

        var change = _match.JoinPlayer(actor.ActorId, DateTimeOffset.UtcNow);
        if (change.IsNewPlayer)
        {
            await NotifyPlayerJoinedAsync(actor, change.Mark, change.State, cancellationToken);
        }

        await BroadcastAsync(change.State, actor.ActorId, cancellationToken);
        return new TicTacToeGameJoinRes(change.State);
    }

    public async ValueTask<PlaceMarkRes> PlaceMarkAsync(
        PlayActor actor,
        int cell,
        CancellationToken cancellationToken)
    {
        var change = _match.PlaceMark(actor.ActorId, cell, DateTimeOffset.UtcNow);
        await BroadcastAsync(change.State, actor.ActorId, cancellationToken);
        return new PlaceMarkRes(change.State);
    }

    internal async ValueTask TickAsync(CancellationToken cancellationToken)
    {
        var change = _match.Tick(DateTimeOffset.UtcNow);
        if (!change.HasChanged)
        {
            return;
        }

        await BroadcastAsync(change.State, null, cancellationToken);
    }

    private ValueTask BroadcastAsync(
        GameState state,
        string? excludedActorId,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        var message = new GameStateNotify(state);
        var recipients = _actors.Values
            .Where(actor => !string.Equals(actor.ActorId, excludedActorId, StringComparison.Ordinal))
            .ToArray();
        return SendSessionPushAsync(
            recipients,
            actor => actor.Context.BoundSession.Send(message).Async(cancellationToken));
    }

    private ValueTask NotifyPlayerJoinedAsync(
        PlayActor joinedActor,
        string mark,
        GameState state,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        var message = new PlayerJoinedNotify(
            state.RoomId,
            joinedActor.ActorId,
            mark,
            state);

        var recipients = _actors.Values
            .Where(actor => !string.Equals(actor.ActorId, joinedActor.ActorId, StringComparison.Ordinal))
            .ToArray();
        return SendSessionPushAsync(
            recipients,
            actor => actor.Context.BoundSession.Send(message).Async(cancellationToken));
    }

    private static async ValueTask SendSessionPushAsync(
        IReadOnlyList<PlayActor> recipients,
        Func<PlayActor, ValueTask> sendAsync)
    {
        foreach (var recipient in recipients)
        {
            await sendAsync(recipient);
        }
    }

    private static string DecodeRoomId(RoutingId spotRid)
    {
        return Encoding.UTF8.GetString(spotRid.ToBytes());
    }
}
