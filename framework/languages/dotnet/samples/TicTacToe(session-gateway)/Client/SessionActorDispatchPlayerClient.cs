using Systems.Zlink.Stream.Connector.Contracts;
using Systems.Zlink.Stream.Connector.Json;
using Systems.Zlink.Stream.Connector.Runtime;
using TicTacToe.SessionActorDispatch.Configuration;
using TicTacToe.SessionActorDispatch.Contracts;

namespace TicTacToe.SessionGateway.Client;

internal sealed class SessionActorDispatchPlayerClient(
    string actorId,
    SessionActorNotificationInbox notifications,
    ZlinkStreamConnector connector) : IAsyncDisposable
{
    public string ActorId => actorId;

    public static async ValueTask<SessionActorDispatchPlayerClient> ConnectAsync(
        string actorId,
        string streamEndpoint,
        CancellationToken cancellationToken)
    {
        var connector = await ZlinkStreamConnector.ConnectAsync(new ZlinkStreamConnectorOptions
        {
            Endpoint = new Uri(streamEndpoint),
            ConnectTimeout = SampleTimings.ConnectTimeout,
            RequestTimeout = SampleTimings.RequestTimeout,
        }, cancellationToken);

        var inbox = new SessionActorNotificationInbox();
        inbox.Register(connector);
        var client = new SessionActorDispatchPlayerClient(actorId, inbox, connector);
        return client;
    }

    public IReadOnlyList<TurnChangedNotify> TurnChangedNotifications => notifications.TurnChanged;

    public IReadOnlyList<OpponentJoinedNotify> OpponentJoinedNotifications => notifications.OpponentJoined;

    public IReadOnlyList<GameEndedNotify> GameEndedNotifications => notifications.GameEnded;

    public ValueTask<AuthenticateRes> AuthenticateAsync(CancellationToken cancellationToken)
    {
        return connector
            .Request(new AuthenticateReq(actorId))
            .WithTimeout(SampleTimings.RequestTimeout)
            .SubmitAsync<AuthenticateRes>(cancellationToken);
    }

    public ValueTask<JoinMatchRes> JoinAsync(
        string matchId,
        CancellationToken cancellationToken)
    {
        return connector
            .Request(new JoinMatchReq(matchId))
            .WithTimeout(SampleTimings.RequestTimeout)
            .SubmitAsync<JoinMatchRes>(cancellationToken);
    }

    public ValueTask<CreateMatchRes> CreateMatchAsync(CancellationToken cancellationToken)
    {
        return connector
            .Request(new CreateMatchReq())
            .WithTimeout(SampleTimings.RequestTimeout)
            .SubmitAsync<CreateMatchRes>(cancellationToken);
    }

    public ValueTask<PlaceMarkRes> PlaceMarkAsync(
        string matchId,
        int cell,
        CancellationToken cancellationToken)
    {
        return connector
            .Request(new PlaceMarkReq(matchId, cell))
            .WithTimeout(SampleTimings.RequestTimeout)
            .SubmitAsync<PlaceMarkRes>(cancellationToken);
    }

    public ValueTask DisposeAsync()
    {
        return connector.DisposeAsync();
    }

    public Task WaitForNotificationAsync(CancellationToken cancellationToken)
    {
        return notifications.WaitAsync(cancellationToken);
    }
}
