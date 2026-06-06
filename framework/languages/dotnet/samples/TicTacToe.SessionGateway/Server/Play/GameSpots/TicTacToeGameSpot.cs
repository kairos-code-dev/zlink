using Systems.Zlink;
using Systems.Zlink.Codecs.Json;
using TicTacToe.SessionGateway.Shared.Actors;
using TicTacToe.SessionGateway.Server.Play;
using TicTacToe.SessionGateway.Shared.Contracts;
using Microsoft.Extensions.Logging;
using TicTacToe.SessionGateway.Play.GameSpots.Handlers;
using Zlink.Framework.Contracts.Spots;

namespace TicTacToe.SessionGateway.Server.Play.GameSpots;

internal sealed class TicTacToeGameSpot(
    IZLinkSpotContext context,
    ILogger<TicTacToeGameSpot> logger) : IZLinkSpot<PlayerActor>
{
    private readonly TicTacToeMatchRoom _room = new(context.SpotRid.ToHex());

    public IZLinkSpotContext Context { get; } = context;

    public void Configure()
    {
        Context.Handlers.AddHandler<PlaceMarkHandler>();
    }

    public ValueTask OnPostActorJoinedAsync(
        PlayerActor actor,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        logger.LogInformation(
            "game spot: actor joined. actor={ActorId}, matchId={MatchId}",
            actor.ActorId,
            Context.SpotRid.ToHex());
        return ValueTask.CompletedTask;
    }

    public ValueTask OnActorLeftAsync(
        PlayerActor actor,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        logger.LogInformation(
            "game spot: actor left. actor={ActorId}, matchId={MatchId}",
            actor.ActorId,
            Context.SpotRid.ToHex());
        return ValueTask.CompletedTask;
    }

    public async ValueTask<ZLinkSpotActorJoinResult> OnActorJoinAsync(
        PlayerActor actor,
        Message request,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        _ = request.Decode<JoinMatchReq>();
        var reply = await JoinAsync(actor, cancellationToken);
        return ZLinkSpotActorJoinResult.Accept(reply.Encode());
    }

    public ValueTask<ZLinkSpotCreateResponse> OnCreateAsync(
        Message request,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        logger.LogInformation(
            "game spot: created. matchId={MatchId}, createPayloadBytes={CreatePayloadBytes}",
            Context.SpotRid.ToHex(),
            request.Size);
        return ValueTask.FromResult(ZLinkSpotCreateResponse.Accept());
    }

    public ValueTask OnInitializeAsync(CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        return ValueTask.CompletedTask;
    }

    public ValueTask OnClosingAsync(CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        return ValueTask.CompletedTask;
    }

    public async ValueTask<JoinMatchSpotResult> JoinAsync(
        PlayerActor actor,
        CancellationToken cancellationToken)
    {
        var actorId = actor.ActorId;
        var (slot, isNewActor) = _room.GetOrAddActor(actor);
        _room.StartWhenReady();
        var snapshot = _room.Snapshot();
        return new JoinMatchSpotResult(
            snapshot.MatchId,
            actorId,
            slot.Mark,
            snapshot,
            BuildJoinEvents(actorId, slot.Mark, snapshot, isNewActor));
    }

    public MoveResult PlaceMark(
        string actorId,
        int cell)
    {
        var snapshot = _room.PlaceMark(actorId, cell);
        var events = snapshot.Status is TicTacToeGameStatus.Won or TicTacToeGameStatus.Draw
            ? BuildGameEndedEvents(snapshot)
            : BuildTurnChangedEvents(snapshot);
        return new MoveResult(snapshot, events);
    }

    private IReadOnlyList<TicTacToeGameEvent> BuildJoinEvents(
        string joinedActorId,
        TicTacToeMark mark,
        TicTacToeGameSnapshot snapshot,
        bool isNewActor)
    {
        var events = new List<TicTacToeGameEvent>();
        if (isNewActor)
        {
            events.AddRange(_room.Actors
                .Where(actor => !string.Equals(actor.ActorId, joinedActorId, StringComparison.Ordinal))
                .Select(actor => new TicTacToeGameEvent(
                    TicTacToeGameEventKind.OpponentJoined,
                    actor,
                    snapshot,
                    joinedActorId,
                    mark)));
        }

        events.AddRange(BuildTurnChangedEvents(snapshot));
        return events;
    }

    private IReadOnlyList<TicTacToeGameEvent> BuildTurnChangedEvents(TicTacToeGameSnapshot snapshot)
    {
        if (snapshot.Status is TicTacToeGameStatus.Won or TicTacToeGameStatus.Draw)
        {
            return [];
        }

        return _room.Actors
            .Select(actor => new TicTacToeGameEvent(
                TicTacToeGameEventKind.TurnChanged,
                actor,
                snapshot))
            .ToArray();
    }

    private IReadOnlyList<TicTacToeGameEvent> BuildGameEndedEvents(TicTacToeGameSnapshot snapshot)
    {
        return _room.Actors
            .Select(actor => new TicTacToeGameEvent(
                TicTacToeGameEventKind.GameEnded,
                actor,
                snapshot))
            .ToArray();
    }
}
