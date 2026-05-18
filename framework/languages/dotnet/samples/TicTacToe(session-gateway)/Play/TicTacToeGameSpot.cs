using TicTacToe.SessionActorDispatch.Play;
using TicTacToe.SessionGateway.Shared.Contracts;
using Zlink.Framework.Contracts.Spots;

namespace TicTacToe.SessionGateway.Play;

internal sealed class TicTacToeGameSpot(IZLinkSpotContext context) : IZLinkSpot
{
    private readonly TicTacToeMatchRoom _room = new(context.SpotId.Value);

    public IZLinkSpotContext Context { get; } = context;

    public void Configure()
    {
        Context.AddActorJoin<TicTacToeGameJoinHandler, PlayerActor, JoinMatchReq, JoinMatchSpotResult>();
        Context.AddActorPacket<PlaceMarkHandler, PlayerActor>();
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
        var (slot, isNewActor) = _room.GetOrAddActor(actorId);
        _room.StartWhenReady();
        await Context.JoinActorAsync(actor, cancellationToken)
            .ConfigureAwait(false);
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
            events.AddRange(_room.ActorIds
                .Where(actorId => !string.Equals(actorId, joinedActorId, StringComparison.Ordinal))
                .Select(actorId => new TicTacToeGameEvent(
                    TicTacToeGameEventKind.OpponentJoined,
                    actorId,
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

        return _room.ActorIds
            .Select(actorId => new TicTacToeGameEvent(
                TicTacToeGameEventKind.TurnChanged,
                actorId,
                snapshot))
            .ToArray();
    }

    private IReadOnlyList<TicTacToeGameEvent> BuildGameEndedEvents(TicTacToeGameSnapshot snapshot)
    {
        return _room.ActorIds
            .Select(actorId => new TicTacToeGameEvent(
                TicTacToeGameEventKind.GameEnded,
                actorId,
                snapshot))
            .ToArray();
    }
}
