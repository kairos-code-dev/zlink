namespace Bingo.SessionGateway;

internal static class BingoScenarioAssertions
{
    public static void Validate(BingoScenarioResult result)
    {
        Require(result.Authentications.Select(auth => auth.ActorId).Distinct(StringComparer.Ordinal).Count() == 4,
            "Four client connectors must authenticate as distinct actors.");
        Require(result.Matches.Select(match => match.RoomId).Distinct(StringComparer.Ordinal).Count() == 1,
            "MatchBingoReq must allocate all actors to the same room.");
        Require(result.Matches[0].State.HostActorId == result.Authentications[0].ActorId,
            "First joined actor must become HostActorId.");
        Require(result.EarlyHostStartError is not null,
            "Host start must be rejected before four players join.");
        Require(result.NonHostStartError is not null,
            "Non-host start must be rejected.");
        Require(result.Started.State.Status == BingoRoomStatus.Finished,
            "Host start should run the timer until the sample game finishes.");
        Require(result.ClientPushes.All(pushes => pushes.OfType<BingoGameStartedNotify>().Any()),
            "Client push must include BingoGameStartedNotify through actor session binding.");
        Require(result.ClientPushes.All(pushes => pushes.OfType<BingoNumberDrawnNotify>().Any()),
            "Client push must include timer draw notifications.");
        Require(result.ClientPushes.All(pushes => pushes.OfType<BingoGameEndedNotify>().Any()),
            "Client push must include game end notification.");
        Require(result.FinalState.Players.Count == 4,
            "Final state must contain exactly four players.");
        Require(result.FinalState.Winners.Count > 0,
            "Final state must contain at least one winner.");
        Require(result.FinalState.Winners.Count > 1,
            "Deterministic deck should demonstrate multiple winners in the same draw sequence.");
        Require(result.FinalState.Players.All(player => player.Card.Length == BingoCard.CellCount),
            "Public player state must expose a 25-cell row-major card.");
        Require(result.FinalState.Players.All(player => player.Marks[BingoCard.FreeCellIndex]),
            "Center free cell must be marked from the start.");
    }

    private static void Require(bool condition, string message)
    {
        if (!condition)
        {
            throw new InvalidOperationException(message);
        }
    }
}
