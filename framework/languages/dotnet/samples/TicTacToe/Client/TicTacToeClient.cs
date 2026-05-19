using System.Net.Http.Json;
using System.Collections.Concurrent;
using Systems.Zlink.Stream.Connector;
using Systems.Zlink.Stream.Connector.Json;
using Systems.Zlink.Stream.Connector.Contracts;
using TicTacToe.Shared.Contracts;

namespace TicTacToe.Client;

public sealed class TicTacToeClient
{
    public async Task<TicTacToeClientResult> RunAsync(
        TicTacToeClientOptions options,
        CancellationToken cancellationToken = default)
    {
        using var http = new HttpClient
        {
            BaseAddress = options.ApiUrl,
            Timeout = options.HttpTimeout,
        };

        var game = await CreateGameAsync(http, options.GameName, cancellationToken);

        await using var xConnector = await ConnectPlayerAsync(game.PlayEndpoint, options, cancellationToken);
        await using var oConnector = await ConnectPlayerAsync(game.PlayEndpoint, options, cancellationToken);

        var stateNotifications = new ConcurrentQueue<GameStateNotify>();
        var playerJoinedNotifications = new ConcurrentQueue<PlayerJoinedNotify>();
        using var xStateHandler = xConnector.On<GameStateNotify>((message, _) =>
        {
            stateNotifications.Enqueue(message.Payload);
            return ValueTask.CompletedTask;
        });
        using var oStateHandler = oConnector.On<GameStateNotify>((message, _) =>
        {
            stateNotifications.Enqueue(message.Payload);
            return ValueTask.CompletedTask;
        });
        using var xPlayerJoinedHandler = xConnector.On<PlayerJoinedNotify>((message, _) =>
        {
            playerJoinedNotifications.Enqueue(message.Payload);
            return ValueTask.CompletedTask;
        });
        using var oPlayerJoinedHandler = oConnector.On<PlayerJoinedNotify>((message, _) =>
        {
            playerJoinedNotifications.Enqueue(message.Payload);
            return ValueTask.CompletedTask;
        });

        var xAuthentication = await xConnector
            .Request(new AuthenticateReq(options.XActorId))
            .SubmitAsync<AuthenticateRes>(cancellationToken);
        var oAuthentication = await oConnector
            .Request(new AuthenticateReq(options.OActorId))
            .SubmitAsync<AuthenticateRes>(cancellationToken);

        var xJoin = await xConnector
            .Request(new JoinGameReq(game.GameId))
            .SubmitAsync<JoinGameRes>(cancellationToken);
        var oJoin = await oConnector
            .Request(new JoinGameReq(game.GameId))
            .SubmitAsync<JoinGameRes>(cancellationToken);

        var moves = new List<PlaceMarkRes>
        {
            await PlaceAsync(xConnector, 0, cancellationToken),
            await PlaceAsync(oConnector, 3, cancellationToken),
            await PlaceAsync(xConnector, 1, cancellationToken),
            await PlaceAsync(oConnector, 4, cancellationToken),
            await PlaceAsync(xConnector, 2, cancellationToken),
        };

        if (!string.Equals(moves[^1].State.Status, "Won", StringComparison.Ordinal)
            || !string.Equals(moves[^1].State.Winner, options.XActorId, StringComparison.Ordinal))
        {
            throw new InvalidOperationException(
                $"Unexpected final game state: board={moves[^1].State.Board}, status={moves[^1].State.Status}, winner={moves[^1].State.Winner ?? "-"}");
        }

        await Task.Delay(TimeSpan.FromMilliseconds(250), cancellationToken);
        return new TicTacToeClientResult(
            game,
            xAuthentication,
            oAuthentication,
            xJoin,
            oJoin,
            moves,
            stateNotifications.ToArray(),
            playerJoinedNotifications.ToArray());
    }

    private static ValueTask<PlaceMarkRes> PlaceAsync(
        IZlinkStreamConnector connector,
        int cell,
        CancellationToken cancellationToken)
    {
        return connector
            .Request(new PlaceMarkReq(cell))
            .SubmitAsync<PlaceMarkRes>(cancellationToken);
    }

    private static async ValueTask<IZlinkStreamConnector> ConnectPlayerAsync(
        string playEndpoint,
        TicTacToeClientOptions options,
        CancellationToken cancellationToken)
    {
        var connector = ZlinkStreamConnectorFactory.Create(new ZlinkStreamConnectorOptions
        {
            Endpoint = new Uri(playEndpoint),
            ConnectTimeout = options.StreamTimeout,
            RequestTimeout = options.StreamTimeout,
        });
        await connector.ConnectAsync(cancellationToken).ConfigureAwait(false);
        return connector;
    }

    private static async Task<CreateGameHttpRes> CreateGameAsync(
        HttpClient http,
        string gameName,
        CancellationToken cancellationToken)
    {
        using var response = await http.PostAsJsonAsync(
            "/games",
            new CreateGameHttpReq(gameName),
            cancellationToken);

        response.EnsureSuccessStatusCode();
        return await response.Content.ReadFromJsonAsync<CreateGameHttpRes>(cancellationToken)
               ?? throw new InvalidOperationException("API returned an empty game response.");
    }
}
