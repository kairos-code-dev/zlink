using TicTacToe.SessionGateway.Shared.Configuration;
using TicTacToe.SessionGateway.Shared.Contracts;

namespace TicTacToe.SessionGateway.Client;

public sealed class SessionActorDispatchClient
{
    public async ValueTask<SessionActorDispatchClientResult> RunAsync(
        SessionActorDispatchClientOptions options,
        CancellationToken cancellationToken = default)
    {
        await using var xClient = await SessionActorDispatchPlayerClient.ConnectAsync(
            options.XActorId,
            options.PrimaryStreamEndpoint,
            cancellationToken);
        await using var oClient = await SessionActorDispatchPlayerClient.ConnectAsync(
            options.OActorId,
            options.ReconnectStreamEndpoint,
            cancellationToken);

        var xAuth = await xClient.AuthenticateAsync(cancellationToken);
        var oAuth = await oClient.AuthenticateAsync(cancellationToken);
        var created = await xClient.CreateMatchAsync(cancellationToken);

        await xClient.DisposeAsync();
        await using var reconnectedXClient = await SessionActorDispatchPlayerClient.ConnectAsync(
            options.XActorId,
            options.ReconnectStreamEndpoint,
            cancellationToken);
        var xReconnectAuth = await reconnectedXClient.AuthenticateAsync(cancellationToken);
        var xJoin = await reconnectedXClient.JoinAsync(created.MatchId, cancellationToken);
        var oJoin = await oClient.JoinAsync(created.MatchId, cancellationToken);
        var moves = new[]
        {
            await reconnectedXClient.PlaceMarkAsync(created.MatchId, 0, cancellationToken),
            await oClient.PlaceMarkAsync(created.MatchId, 3, cancellationToken),
            await reconnectedXClient.PlaceMarkAsync(created.MatchId, 1, cancellationToken),
            await oClient.PlaceMarkAsync(created.MatchId, 4, cancellationToken),
            await reconnectedXClient.PlaceMarkAsync(created.MatchId, 2, cancellationToken),
        };

        ValidateReconnect(options, xAuth, xReconnectAuth, xJoin.State);
        ValidateFinalState(options, moves[^1].State);
        await WaitForNotificationsAsync(
                reconnectedXClient,
                oClient,
                cancellationToken)
            .ConfigureAwait(false);

        ValidateNotifications(reconnectedXClient, oClient);
        return new SessionActorDispatchClientResult(
            xAuth,
            oAuth,
            xReconnectAuth,
            created,
            xJoin,
            oJoin,
            moves,
            reconnectedXClient.TurnChangedNotifications.Concat(oClient.TurnChangedNotifications).ToArray(),
            reconnectedXClient.OpponentJoinedNotifications.Concat(oClient.OpponentJoinedNotifications).ToArray(),
            reconnectedXClient.GameEndedNotifications.Concat(oClient.GameEndedNotifications).ToArray());
    }

    private static void ValidateFinalState(
        SessionActorDispatchClientOptions options,
        TicTacToeState state)
    {
        if (!string.Equals(state.Status, "Won", StringComparison.Ordinal)
            || !string.Equals(state.WinnerActorId, options.XActorId, StringComparison.Ordinal)
            || !string.Equals(state.Board, "XXXOO....", StringComparison.Ordinal))
        {
            throw new InvalidOperationException(
                $"Unexpected final state: board={state.Board}, status={state.Status}, winner={state.WinnerActorId ?? "-"}");
        }
    }

    private static void ValidateReconnect(
        SessionActorDispatchClientOptions options,
        AuthenticateRes firstAuth,
        AuthenticateRes reconnectAuth,
        TicTacToeState reconnectedJoinState)
    {
        if (!string.Equals(firstAuth.ActorId, options.XActorId, StringComparison.Ordinal)
            || !string.Equals(reconnectAuth.ActorId, options.XActorId, StringComparison.Ordinal)
            || !string.Equals(reconnectedJoinState.XActorId, options.XActorId, StringComparison.Ordinal))
        {
            throw new InvalidOperationException("Reconnect did not recover the same actor binding.");
        }
    }

    private static void ValidateNotifications(
        SessionActorDispatchPlayerClient xClient,
        SessionActorDispatchPlayerClient oClient)
    {
        if (HasRequiredNotificationSmoke(xClient, oClient))
        {
            return;
        }

        throw new InvalidOperationException(
            "Session actor dispatch push notifications did not reach both actors. "
            + NotificationCounts(xClient, oClient));
    }

    private static async ValueTask WaitForNotificationsAsync(
        SessionActorDispatchPlayerClient xClient,
        SessionActorDispatchPlayerClient oClient,
        CancellationToken cancellationToken)
    {
        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        timeout.CancelAfter(SampleTimings.RequestTimeout);
        while (!timeout.IsCancellationRequested)
        {
            if (HasRequiredNotificationSmoke(xClient, oClient))
            {
                return;
            }

            await Task.WhenAny(
                    xClient.WaitForNotificationAsync(timeout.Token),
                    oClient.WaitForNotificationAsync(timeout.Token))
                .ConfigureAwait(false);
        }

        if (HasRequiredNotificationSmoke(xClient, oClient))
        {
            return;
        }

        throw new TimeoutException(
            "Timed out waiting for session actor dispatch push notifications. "
            + NotificationCounts(xClient, oClient));
    }

    private static bool HasRequiredNotificationSmoke(
        SessionActorDispatchPlayerClient xClient,
        SessionActorDispatchPlayerClient oClient)
    {
        return NotificationCount(xClient) > 0
            && NotificationCount(oClient) > 0;
    }

    private static int NotificationCount(SessionActorDispatchPlayerClient client)
    {
        return client.TurnChangedNotifications.Count
            + client.OpponentJoinedNotifications.Count
            + client.GameEndedNotifications.Count;
    }

    private static string NotificationCounts(
        SessionActorDispatchPlayerClient xClient,
        SessionActorDispatchPlayerClient oClient)
    {
        return $"x(turn={xClient.TurnChangedNotifications.Count}, joined={xClient.OpponentJoinedNotifications.Count}, ended={xClient.GameEndedNotifications.Count}), "
            + $"o(turn={oClient.TurnChangedNotifications.Count}, joined={oClient.OpponentJoinedNotifications.Count}, ended={oClient.GameEndedNotifications.Count})";
    }
}

public sealed record SessionActorDispatchClientResult(
    AuthenticateRes XAuthentication,
    AuthenticateRes OAuthentication,
    AuthenticateRes XReconnectAuthentication,
    CreateMatchRes CreatedMatch,
    JoinMatchRes XJoin,
    JoinMatchRes OJoin,
    IReadOnlyList<PlaceMarkRes> Moves,
    IReadOnlyList<TurnChangedNotify> TurnChangedNotifications,
    IReadOnlyList<OpponentJoinedNotify> OpponentJoinedNotifications,
    IReadOnlyList<GameEndedNotify> GameEndedNotifications)
{
    private TicTacToeState FinalState => Moves[^1].State;

    public void WriteTo(TextWriter writer)
    {
        writer.WriteLine(
            $"auth={XAuthentication.ActorId},{OAuthentication.ActorId}");
        writer.WriteLine(
            $"created={CreatedMatch.MatchId}, reconnect={XReconnectAuthentication.ActorId}");
        writer.WriteLine(
            $"players=X:{XJoin.State.XActorId}, O:{OJoin.State.OActorId}");
        writer.WriteLine(
            $"final={FinalState.Board}, status={FinalState.Status}, winner={FinalState.WinnerActorId}");
        writer.WriteLine(
            $"notifications=turn:{TurnChangedNotifications.Count}, joined:{OpponentJoinedNotifications.Count}, ended:{GameEndedNotifications.Count}");
    }
}
