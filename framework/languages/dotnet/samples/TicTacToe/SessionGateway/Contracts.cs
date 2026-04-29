namespace TicTacToe.SessionGateway;

public sealed record CreateMatchReq(string? MatchName);

public sealed record CreateMatchRes(string MatchId, string OwnerPlayerId);

public sealed record CreateMatchRoomReq(string MatchName);

public sealed record CreateMatchRoomRes(string MatchId);

public sealed record AuthenticatePlayerReq(string AccessToken);

public sealed record AuthenticatePlayerRes(bool Accepted, string? PlayerId, string? Reason);

public sealed record AuthenticateReq(string AccessToken);

public sealed record AuthenticateRes(string PlayerId, string ActorId);

public sealed record JoinMatchReq(string MatchId);

public sealed record JoinMatchRes(string MatchId, string PlayerId, string Mark, TicTacToeState State);

public sealed record PlaceMarkReq(string MatchId, int Cell);

public sealed record PlaceMarkRes(TicTacToeState State);

public sealed record OpponentJoinedNotify(string MatchId, string OpponentPlayerId);

public sealed record TurnChangedNotify(string MatchId, string TurnPlayerId, TicTacToeState State);

public sealed record GameEndedNotify(string MatchId, string? WinnerPlayerId, bool Draw, TicTacToeState State);

public sealed record TicTacToeState(
    string MatchId,
    string Board,
    string TurnPlayerId,
    string? WinnerPlayerId,
    bool Draw);

public sealed record SessionGatewaySmokeResult(
    string MatchId,
    TicTacToeState FinalState,
    IReadOnlyList<string> LogLines,
    int RoutedSequenceReplies,
    bool ReconnectedActorMovedToSession2,
    bool NotificationReachedReconnectedSession)
{
    public void AssertPassed()
    {
        if (FinalState.WinnerPlayerId != "player-a" || FinalState.Board[..3] != "XXX")
        {
            throw new InvalidOperationException(
                $"SessionGateway smoke did not finish with player-a win. board={FinalState.Board}, winner={FinalState.WinnerPlayerId ?? "-"}");
        }

        if (RoutedSequenceReplies < 5)
        {
            throw new InvalidOperationException("SessionGateway smoke did not observe routed sequence replies.");
        }

        if (!ReconnectedActorMovedToSession2 || !NotificationReachedReconnectedSession)
        {
            throw new InvalidOperationException("SessionGateway reconnect flow did not update actor binding.");
        }
    }
}
