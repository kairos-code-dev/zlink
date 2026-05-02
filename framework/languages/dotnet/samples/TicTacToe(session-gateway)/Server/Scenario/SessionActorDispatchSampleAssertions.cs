using TicTacToe.SessionActorDispatch.Configuration;
using TicTacToe.SessionActorDispatch.Contracts;
using Zlink;
using Zlink.Framework.Spots;

namespace TicTacToe.SessionActorDispatch;

internal static class SessionActorDispatchSampleAssertions
{
    public static void ValidateCreatedMatch(CreateMatchRes created)
    {
        if (RoutingId.FromString(created.MatchId).ToHex() != created.MatchId)
        {
            throw new InvalidOperationException(
                $"Created match id must be a SPOT room routing id. matchId={created.MatchId}");
        }
    }

    public static async ValueTask ValidateGameRoomSpotAsync(
        IZLinkSpotManager spots,
        string matchId,
        CancellationToken cancellationToken)
    {
        var rooms = await spots.ListAsync(cancellationToken).ConfigureAwait(false);
        if (!rooms.Any(room => string.Equals(room.SpotRid.ToHex(), matchId, StringComparison.Ordinal)))
        {
            throw new InvalidOperationException($"Created game room SPOT was not found. matchId={matchId}");
        }
    }

}
