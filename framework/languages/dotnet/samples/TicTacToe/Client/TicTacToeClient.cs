using System.Net.Http.Json;
using Systems.Zlink.Stream.Connector;
using Systems.Zlink.Stream.Connector.Contracts;
using Systems.Zlink.Stream.Connector.Json;
using TicTacToe.Shared.Contracts;

namespace TicTacToe.Client;

public sealed class TicTacToeClient
{
    public async ValueTask RunAsync(
        TicTacToeClientOptions options,
        CancellationToken cancellationToken = default)
    {
        using var http = new HttpClient
        {
            BaseAddress = options.ApiUrl,
            Timeout = options.HttpTimeout,
        };

        // The scenario starts at the API server. The API creates a game through
        // the Play channel and returns the Play stream endpoint to the client.
        using var createGameResponse = await http.PostAsJsonAsync(
            "/games",
            new CreateGameHttpReq(options.GameName),
            cancellationToken);
        createGameResponse.EnsureSuccessStatusCode();

        var game = await createGameResponse.Content.ReadFromJsonAsync<CreateGameHttpRes>(cancellationToken)
                   ?? throw new InvalidOperationException("API returned an empty game response.");
        Require(!string.IsNullOrWhiteSpace(game.GameId), "API must return a game id.");
        Require(!string.IsNullOrWhiteSpace(game.PlayEndpoint), "API must return the Play stream endpoint.");
        Require(game.GameName == options.GameName, "API must echo the requested game name.");

        await using var xConnector = ZlinkStreamConnectorFactory.Create(new ZlinkStreamConnectorOptions
        {
            Endpoint = new Uri(game.PlayEndpoint),
            ConnectTimeout = options.StreamTimeout,
            RequestTimeout = options.StreamTimeout,
            DispatchMode = ZlinkStreamDispatchMode.Immediate,
        });
        await using var oConnector = ZlinkStreamConnectorFactory.Create(new ZlinkStreamConnectorOptions
        {
            Endpoint = new Uri(game.PlayEndpoint),
            ConnectTimeout = options.StreamTimeout,
            RequestTimeout = options.StreamTimeout,
            DispatchMode = ZlinkStreamDispatchMode.Immediate,
        });

        // Both players connect to the Play stream endpoint returned by the API.
        await xConnector.ConnectAsync(cancellationToken);
        await oConnector.ConnectAsync(cancellationToken);

        // Player X authenticates and joins the empty game.
        var xAuthentication = await xConnector
            .Request(new AuthenticateReq(options.XActorId))
            .Timeout(options.StreamTimeout)
            .SubmitAsync<AuthenticateRes>(cancellationToken);
        Require(xAuthentication.ActorId == options.XActorId, "Player X must authenticate as the requested actor.");

        var xJoin = await xConnector
            .Request(new JoinGameReq(game.GameId))
            .Timeout(options.StreamTimeout)
            .SubmitAsync<JoinGameRes>(cancellationToken);
        Require(xJoin.State.GameId == game.GameId, "Player X must join the created game.");
        Require(xJoin.State.Status == "WaitingForPlayers", "Player X must wait for the second player.");
        Require(xJoin.State.XActorId == options.XActorId, "Player X must receive mark X.");
        Require(xConnector.ReceivedCount(nameof(PlayerJoinedNotify)) == 0, "Player X must not receive a self-join notification.");

        // Player O authenticates and joins the same game.
        var oAuthentication = await oConnector
            .Request(new AuthenticateReq(options.OActorId))
            .Timeout(options.StreamTimeout)
            .SubmitAsync<AuthenticateRes>(cancellationToken);
        Require(oAuthentication.ActorId == options.OActorId, "Player O must authenticate as the requested actor.");
        Require(oAuthentication.ActorId != xAuthentication.ActorId, "Players must authenticate as distinct actors.");

        var oJoin = await oConnector
            .Request(new JoinGameReq(game.GameId))
            .Timeout(options.StreamTimeout)
            .SubmitAsync<JoinGameRes>(cancellationToken);
        Require(oJoin.State.GameId == game.GameId, "Player O must join the same game.");
        Require(oJoin.State.Status == "InProgress", "Joining player O must start the game.");
        Require(oJoin.State.OActorId == options.OActorId, "Player O must receive mark O.");

        // Existing room members receive push packets when another player joins.
        var xSawOJoin = await xConnector.WaitForAsync<PlayerJoinedNotify>(
            message => message.Payload.ActorId == options.OActorId,
            options.StreamTimeout,
            cancellationToken);
        Require(xSawOJoin.Payload.Mark == "O", "Player X must be notified that player O joined.");
        Require(xSawOJoin.Payload.State.Status == "InProgress", "Player O join notification must start the game.");
        Require(oConnector.ReceivedCount(nameof(PlayerJoinedNotify)) == 0, "Player O must not receive a self-join notification.");

        var xSawGameStart = await xConnector.WaitForAsync<GameStateNotify>(
            message => message.Payload.State.Status == "InProgress"
                       && message.Payload.State.OActorId == options.OActorId,
            options.StreamTimeout,
            cancellationToken);
        Require(xSawGameStart.Payload.State.NextTurn == "X", "Player X must see the first turn.");

        // The move sequence is deterministic: X completes the top row.
        var xMove1 = await xConnector
            .Request(new PlaceMarkReq(0))
            .Timeout(options.StreamTimeout)
            .SubmitAsync<PlaceMarkRes>(cancellationToken);
        Require(xMove1.State.Board == "X........", "Player X must place the first mark at cell 0.");
        Require(xMove1.State.NextTurn == "O", "Turn must move to O after X places a mark.");

        var oSawXMove1 = await oConnector.WaitForAsync<GameStateNotify>(
            message => message.Payload.State.LastMoveActorId == options.XActorId
                       && message.Payload.State.LastMoveCell == 0,
            options.StreamTimeout,
            cancellationToken);
        Require(oSawXMove1.Payload.State.Board == xMove1.State.Board, "Player O must receive player X's first move.");

        var oMove1 = await oConnector
            .Request(new PlaceMarkReq(3))
            .Timeout(options.StreamTimeout)
            .SubmitAsync<PlaceMarkRes>(cancellationToken);
        Require(oMove1.State.Board == "X..O.....", "Player O must place a mark at cell 3.");
        Require(oMove1.State.NextTurn == "X", "Turn must move back to X.");

        var xSawOMove1 = await xConnector.WaitForAsync<GameStateNotify>(
            message => message.Payload.State.LastMoveActorId == options.OActorId
                       && message.Payload.State.LastMoveCell == 3,
            options.StreamTimeout,
            cancellationToken);
        Require(xSawOMove1.Payload.State.Board == oMove1.State.Board, "Player X must receive player O's first move.");

        var xMove2 = await xConnector
            .Request(new PlaceMarkReq(1))
            .Timeout(options.StreamTimeout)
            .SubmitAsync<PlaceMarkRes>(cancellationToken);
        Require(xMove2.State.Board == "XX.O.....", "Player X must place the second top-row mark.");
        Require(xMove2.State.NextTurn == "O", "Turn must move to O after X's second move.");

        var oSawXMove2 = await oConnector.WaitForAsync<GameStateNotify>(
            message => message.Payload.State.LastMoveActorId == options.XActorId
                       && message.Payload.State.LastMoveCell == 1,
            options.StreamTimeout,
            cancellationToken);
        Require(oSawXMove2.Payload.State.Board == xMove2.State.Board, "Player O must receive player X's second move.");

        var oMove2 = await oConnector
            .Request(new PlaceMarkReq(4))
            .Timeout(options.StreamTimeout)
            .SubmitAsync<PlaceMarkRes>(cancellationToken);
        Require(oMove2.State.Board == "XX.OO....", "Player O must place a mark at cell 4.");
        Require(oMove2.State.NextTurn == "X", "Turn must move back to X before the final move.");

        var xSawOMove2 = await xConnector.WaitForAsync<GameStateNotify>(
            message => message.Payload.State.LastMoveActorId == options.OActorId
                       && message.Payload.State.LastMoveCell == 4,
            options.StreamTimeout,
            cancellationToken);
        Require(xSawOMove2.Payload.State.Board == oMove2.State.Board, "Player X must receive player O's second move.");

        var xFinalMove = await xConnector
            .Request(new PlaceMarkReq(2))
            .Timeout(options.StreamTimeout)
            .SubmitAsync<PlaceMarkRes>(cancellationToken);
        Require(xFinalMove.State.Board == "XXXOO....", "Player X must complete the top row.");
        Require(xFinalMove.State.Status == "Won", "The final move must finish the game.");
        Require(xFinalMove.State.Winner == options.XActorId, "The deterministic scenario must end with player X winning.");

        var oSawFinal = await oConnector.WaitForAsync<GameStateNotify>(
            message => message.Payload.State.Status == "Won"
                       && message.Payload.State.Winner == options.XActorId,
            options.StreamTimeout,
            cancellationToken);
        Require(oSawFinal.Payload.State.Board == xFinalMove.State.Board, "Player O must receive the final game state.");
    }

    private static void Require(bool condition, string message)
    {
        if (!condition)
        {
            throw new InvalidOperationException(message);
        }
    }
}
