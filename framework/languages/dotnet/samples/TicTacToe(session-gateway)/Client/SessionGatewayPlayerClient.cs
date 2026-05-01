using System.Collections.Concurrent;
using Systems.Zlink.Stream.Connector.Contracts;
using Systems.Zlink.Stream.Connector.Json;
using Systems.Zlink.Stream.Connector.Runtime;
using TicTacToe.SessionGateway.Configuration;
using TicTacToe.SessionGateway.Contracts;

namespace TicTacToe.SessionGateway.Client;

internal sealed class SessionGatewayPlayerClient(
    string playerId,
    ZlinkStreamConnector connector) : IAsyncDisposable
{
    private readonly ConcurrentQueue<GameStateNotify> _stateNotifications = new();
    private readonly ConcurrentQueue<PlayerJoinedNotify> _playerJoinedNotifications = new();

    public string PlayerId => playerId;

    public static async ValueTask<SessionGatewayPlayerClient> ConnectAsync(
        string playerId,
        string streamEndpoint,
        CancellationToken cancellationToken)
    {
        var connector = await ZlinkStreamConnector.ConnectAsync(new ZlinkStreamConnectorOptions
        {
            Endpoint = new Uri(streamEndpoint),
            ConnectTimeout = SampleTimings.ConnectTimeout,
            RequestTimeout = SampleTimings.RequestTimeout,
        }, cancellationToken);

        var client = new SessionGatewayPlayerClient(playerId, connector);
        client.RegisterNotifications();
        return client;
    }

    public IReadOnlyList<GameStateNotify> StateNotifications => _stateNotifications.ToArray();

    public IReadOnlyList<PlayerJoinedNotify> PlayerJoinedNotifications => _playerJoinedNotifications.ToArray();

    public ValueTask<AuthenticateRes> AuthenticateAsync(CancellationToken cancellationToken)
    {
        return connector
            .Request(new AuthenticateReq(playerId))
            .WithTimeout(SampleTimings.RequestTimeout)
            .Async<AuthenticateRes>(cancellationToken);
    }

    public ValueTask<JoinGameRes> JoinAsync(
        string gameId,
        CancellationToken cancellationToken)
    {
        return connector
            .Request(new JoinGameReq(gameId))
            .WithTimeout(SampleTimings.RequestTimeout)
            .Async<JoinGameRes>(cancellationToken);
    }

    public ValueTask<CreateGameRes> CreateGameAsync(
        string preferredGameId,
        CancellationToken cancellationToken)
    {
        return connector
            .Request(new CreateGameReq(preferredGameId))
            .WithTimeout(SampleTimings.RequestTimeout)
            .Async<CreateGameRes>(cancellationToken);
    }

    public ValueTask<PlaceMarkRes> PlaceMarkAsync(
        string gameId,
        int cell,
        CancellationToken cancellationToken)
    {
        return connector
            .Request(new PlaceMarkReq(gameId, cell))
            .WithTimeout(SampleTimings.RequestTimeout)
            .Async<PlaceMarkRes>(cancellationToken);
    }

    public ValueTask DisposeAsync()
    {
        return connector.DisposeAsync();
    }

    private void RegisterNotifications()
    {
        _ = connector.On<GameStateNotify>(
            SampleNames.GameStatePacket,
            (message, _) =>
            {
                _stateNotifications.Enqueue(message.Body);
                return ValueTask.CompletedTask;
            });
        _ = connector.On<PlayerJoinedNotify>(
            SampleNames.PlayerJoinedPacket,
            (message, _) =>
            {
                _playerJoinedNotifications.Enqueue(message.Body);
                return ValueTask.CompletedTask;
            });
    }
}
