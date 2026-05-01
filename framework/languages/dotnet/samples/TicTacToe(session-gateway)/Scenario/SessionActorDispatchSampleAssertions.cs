using TicTacToe.SessionActorDispatch.Client;
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

    public static void ValidateFinalState(TicTacToeState state)
    {
        if (!string.Equals(state.Status, "Won", StringComparison.Ordinal)
            || !string.Equals(state.WinnerActorId, SampleNames.XActorId, StringComparison.Ordinal)
            || !string.Equals(state.Board, "XXXOO....", StringComparison.Ordinal))
        {
            throw new InvalidOperationException(
                $"Unexpected final state: board={state.Board}, status={state.Status}, winner={state.WinnerActorId ?? "-"}");
        }
    }

    public static void ValidateReconnect(
        AuthenticateRes firstAuth,
        AuthenticateRes reconnectAuth,
        TicTacToeState reconnectedJoinState)
    {
        if (!string.Equals(firstAuth.ActorId, SampleNames.XActorId, StringComparison.Ordinal)
            || !string.Equals(reconnectAuth.ActorId, SampleNames.XActorId, StringComparison.Ordinal)
            || !string.Equals(reconnectedJoinState.XActorId, SampleNames.XActorId, StringComparison.Ordinal))
        {
            throw new InvalidOperationException("Reconnect did not recover the same actor binding.");
        }
    }

    public static void ValidateNotifications(
        SessionActorDispatchPlayerClient xClient,
        SessionActorDispatchPlayerClient oClient)
    {
        if (HasExpectedNotifications(xClient, oClient))
        {
            return;
        }

        throw new InvalidOperationException(
            "Session actor dispatch notifications did not reach both actors. "
            + NotificationCounts(xClient, oClient));
    }

    public static async ValueTask WaitForNotificationsAsync(
        SessionActorDispatchPlayerClient xClient,
        SessionActorDispatchPlayerClient oClient,
        CancellationToken cancellationToken)
    {
        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        timeout.CancelAfter(SampleTimings.RequestTimeout);
        while (!timeout.IsCancellationRequested)
        {
            if (HasExpectedNotifications(xClient, oClient))
            {
                return;
            }

            await Task.WhenAny(
                    xClient.WaitForNotificationAsync(timeout.Token),
                    oClient.WaitForNotificationAsync(timeout.Token))
                .ConfigureAwait(false);
        }

        if (HasExpectedNotifications(xClient, oClient))
        {
            return;
        }

        throw new TimeoutException(
            "Timed out waiting for session actor dispatch notifications. "
            + NotificationCounts(xClient, oClient));
    }

    private static bool HasExpectedNotifications(
        SessionActorDispatchPlayerClient xClient,
        SessionActorDispatchPlayerClient oClient)
    {
        return xClient.TurnChangedNotifications.Count >= 2
            && oClient.TurnChangedNotifications.Count >= 2
            && xClient.OpponentJoinedNotifications.Count >= 1
            && xClient.GameEndedNotifications.Count >= 1
            && oClient.GameEndedNotifications.Count >= 1;
    }

    private static string NotificationCounts(
        SessionActorDispatchPlayerClient xClient,
        SessionActorDispatchPlayerClient oClient)
    {
        return $"x(turn={xClient.TurnChangedNotifications.Count}, joined={xClient.OpponentJoinedNotifications.Count}, ended={xClient.GameEndedNotifications.Count}), "
            + $"o(turn={oClient.TurnChangedNotifications.Count}, joined={oClient.OpponentJoinedNotifications.Count}, ended={oClient.GameEndedNotifications.Count})";
    }
}
