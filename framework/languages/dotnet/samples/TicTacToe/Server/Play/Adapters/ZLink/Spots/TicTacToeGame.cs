using Systems.Zlink;
using Systems.Zlink.Codecs.Json;
using Microsoft.Extensions.Logging;
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
    private readonly TicTacToeMatch _match = new(context.SpotRid.ToHex(), TurnTimeout);
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
            "game spot: actor joined. actor={ActorId}, gameId={GameId}",
            actor.ActorId,
            Context.SpotRid.ToHex());
        return ValueTask.CompletedTask;
    }

    public ValueTask OnActorLeftAsync(
        PlayActor actor,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        logger.LogInformation(
            "game spot: actor left. actor={ActorId}, gameId={GameId}",
            actor.ActorId,
            Context.SpotRid.ToHex());
        return ValueTask.CompletedTask;
    }

    public async ValueTask<ZLinkSpotActorJoinResult> OnActorJoinAsync(
        PlayActor player,
        Message request,
        CancellationToken cancellationToken)
    {
        var joinRequest = request.Decode<TicTacToeGameJoinReq>();
        var reply = await JoinPlayerAsync(player, joinRequest.GameId, cancellationToken);
        logger.LogInformation(
            "TicTacToeGame: actor join accepted. actor={ActorId}, gameId={GameId}, mark={Mark}",
            player.ActorId,
            joinRequest.GameId,
            reply.State.XActorId == player.ActorId ? "X" : "O");

        return ZLinkSpotActorJoinResult.Accept(reply.Encode());
    }

    public ValueTask<ZLinkSpotCreateResponse> OnCreateAsync(
        Message request,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        logger.LogInformation(
            "game spot: created. gameId={GameId}, createPayloadBytes={CreatePayloadBytes}",
            Context.SpotRid.ToHex(),
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
        string gameId,
        CancellationToken cancellationToken)
    {
        actor.JoinGame(gameId);
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
            actor => actor.Context.BoundSession.Send(message).Submit(cancellationToken));
    }

    private ValueTask NotifyPlayerJoinedAsync(
        PlayActor joinedActor,
        string mark,
        GameState state,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        var message = new PlayerJoinedNotify(
            state.GameId,
            joinedActor.ActorId,
            mark,
            state);

        var recipients = _actors.Values
            .Where(actor => !string.Equals(actor.ActorId, joinedActor.ActorId, StringComparison.Ordinal))
            .ToArray();
        return SendSessionPushAsync(
            recipients,
            actor => actor.Context.BoundSession.Send(message).Submit(cancellationToken));
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
}
