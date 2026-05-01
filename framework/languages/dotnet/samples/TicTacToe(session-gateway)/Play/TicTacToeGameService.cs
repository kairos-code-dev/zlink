namespace TicTacToe.SessionActorDispatch.Play;

internal sealed class TicTacToeGameService
{
    private readonly object _gate = new();
    private readonly Dictionary<string, TicTacToeMatchRoom> _matches = new(StringComparer.Ordinal);

    public CreateMatchRoomResult Create(string matchName)
    {
        lock (_gate)
        {
            return new CreateMatchRoomResult(GetOrCreateMatch(matchName).MatchId);
        }
    }

    public JoinResult Join(
        string matchId,
        string actorId)
    {
        lock (_gate)
        {
            var match = GetOrCreateMatch(matchId);
            var (slot, isNewActor) = match.GetOrAddActor(actorId);
            match.StartWhenReady();
            var snapshot = match.Snapshot();
            return new JoinResult(
                snapshot.MatchId,
                actorId,
                slot.Mark,
                snapshot,
                BuildJoinEvents(match, actorId, slot.Mark, snapshot, isNewActor));
        }
    }

    public MoveResult PlaceMark(
        string matchId,
        string actorId,
        int cell)
    {
        lock (_gate)
        {
            var match = _matches.TryGetValue(matchId, out var existing)
                ? existing
                : throw new InvalidOperationException($"Match '{matchId}' does not exist.");
            var snapshot = match.PlaceMark(actorId, cell);
            var events = snapshot.Status is TicTacToeGameStatus.Won or TicTacToeGameStatus.Draw
                ? BuildGameEndedEvents(match, snapshot)
                : BuildTurnChangedEvents(match, snapshot);
            return new MoveResult(snapshot, events);
        }
    }

    private TicTacToeMatchRoom GetOrCreateMatch(string matchId)
    {
        if (!_matches.TryGetValue(matchId, out var match))
        {
            match = new TicTacToeMatchRoom(matchId);
            _matches.Add(matchId, match);
        }

        return match;
    }

    private static IReadOnlyList<TicTacToeGameEvent> BuildJoinEvents(
        TicTacToeMatchRoom match,
        string joinedActorId,
        TicTacToeMark mark,
        TicTacToeGameSnapshot snapshot,
        bool isNewActor)
    {
        var events = new List<TicTacToeGameEvent>();
        if (isNewActor)
        {
            events.AddRange(match.ActorIds
                .Where(actorId => !string.Equals(actorId, joinedActorId, StringComparison.Ordinal))
                .Select(actorId => new OpponentJoinedGameEvent(actorId, joinedActorId, mark, snapshot)));
        }

        events.AddRange(BuildTurnChangedEvents(match, snapshot));
        return events;
    }

    private static IReadOnlyList<TicTacToeGameEvent> BuildTurnChangedEvents(
        TicTacToeMatchRoom match,
        TicTacToeGameSnapshot snapshot)
    {
        if (snapshot.Status is TicTacToeGameStatus.Won or TicTacToeGameStatus.Draw)
        {
            return [];
        }

        return match.ActorIds
            .Select(actorId => new TurnChangedGameEvent(actorId, snapshot))
            .ToArray();
    }

    private static IReadOnlyList<TicTacToeGameEvent> BuildGameEndedEvents(
        TicTacToeMatchRoom match,
        TicTacToeGameSnapshot snapshot)
    {
        return match.ActorIds
            .Select(actorId => new GameEndedGameEvent(actorId, snapshot))
            .ToArray();
    }
}
