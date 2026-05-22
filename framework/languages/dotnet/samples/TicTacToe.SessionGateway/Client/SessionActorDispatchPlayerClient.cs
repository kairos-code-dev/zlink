using Systems.Zlink.Stream.Connector;
using Systems.Zlink.Stream.Connector.Contracts;
using Systems.Zlink.Stream.Connector.Json;
using TicTacToe.SessionGateway.Shared.Configuration;
using TicTacToe.SessionGateway.Shared.Contracts;

namespace TicTacToe.SessionGateway.Client;

internal sealed class SessionActorDispatchPlayerClient(
    string actorId,
    SessionActorNotificationInbox notifications,
    IZlinkStreamConnector connector) : IAsyncDisposable
{
    public string ActorId => actorId;

    public static async ValueTask<SessionActorDispatchPlayerClient> ConnectAsync(
        string actorId,
        string streamEndpoint,
        CancellationToken cancellationToken)
    {
        var connector = ZlinkStreamConnectorFactory.Create(new ZlinkStreamConnectorOptions
        {
            Endpoint = new Uri(streamEndpoint),
            ConnectTimeout = SampleTimings.ConnectTimeout,
            RequestTimeout = SampleTimings.RequestTimeout,
            DispatchMode = ZlinkStreamDispatchMode.Immediate,
        });
        await connector.ConnectAsync(cancellationToken);

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
            .Timeout(SampleTimings.RequestTimeout)
            .SubmitAsync<AuthenticateRes>(cancellationToken);
    }

    public ValueTask<JoinMatchRes> JoinAsync(
        string matchId,
        CancellationToken cancellationToken)
    {
        return connector
            .Request(new JoinMatchReq(matchId))
            .Timeout(SampleTimings.RequestTimeout)
            .SubmitAsync<JoinMatchRes>(cancellationToken);
    }

    public ValueTask<CreateMatchRes> CreateMatchAsync(CancellationToken cancellationToken)
    {
        return connector
            .Request(new CreateMatchReq())
            .Timeout(SampleTimings.RequestTimeout)
            .SubmitAsync<CreateMatchRes>(cancellationToken);
    }

    public ValueTask<PlaceMarkRes> PlaceMarkAsync(
        string matchId,
        int cell,
        CancellationToken cancellationToken)
    {
        return connector
            .Request(new PlaceMarkReq(matchId, cell))
            .Timeout(SampleTimings.RequestTimeout)
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
