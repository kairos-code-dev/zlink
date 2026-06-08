using Systems.Zlink.Stream.Connector;
using Systems.Zlink.Stream.Connector.Contracts;
using Systems.Zlink.Stream.Connector.Json;
using TicTacToe.Shared.Contracts;

namespace TicTacToe.Client;

public sealed class TicTacToeClientApp
{
    public async ValueTask RunAsync(
        CreateGameHttpRes game,
        IZlinkStreamConnector client1,
        IZlinkStreamConnector client2,
        TicTacToeClientOptions options,
        CancellationToken cancellationToken = default)
    {
        Require(!string.IsNullOrWhiteSpace(game.GameId), "API must return a game id.");
        Require(!string.IsNullOrWhiteSpace(game.PlayEndpoint), "API must return the Play stream endpoint.");
        Require(game.GameName == options.GameName, "API must echo the requested game name.");

        // Client 1 connects, authenticates as player X, and joins the empty game.
        await client1.ConnectAsync(cancellationToken);

        var client1Authentication = await client1
            .Request(new AuthenticateReq(options.XActorId))
            .Timeout(options.StreamTimeout)
            .SubmitAsync<AuthenticateRes>(cancellationToken);
        Require(client1Authentication.ActorId == options.XActorId, "Client 1 must authenticate as player X.");

        var client1Join = await client1
            .Request(new JoinGameReq(game.GameId))
            .Timeout(options.StreamTimeout)
            .SubmitAsync<JoinGameRes>(cancellationToken);
        Require(client1Join.State.GameId == game.GameId, "Client 1 must join the created game.");
        Require(client1Join.State.Status == "WaitingForPlayers", "Client 1 must wait for the second player.");
        Require(client1Join.State.XActorId == options.XActorId, "Client 1 must receive mark X.");
        Require(
            client1.ReceivedCount(nameof(PlayerJoinedNotify)) == 0,
            "Client 1 must not receive a self-join notification.");

        // Client 2 connects, authenticates as player O, and joins the same game.
        await client2.ConnectAsync(cancellationToken);

        var client2Authentication = await client2
            .Request(new AuthenticateReq(options.OActorId))
            .Timeout(options.StreamTimeout)
            .SubmitAsync<AuthenticateRes>(cancellationToken);
        Require(client2Authentication.ActorId == options.OActorId, "Client 2 must authenticate as player O.");
        Require(
            client2Authentication.ActorId != client1Authentication.ActorId,
            "Clients must authenticate as distinct actors.");

        var client2Join = await client2
            .Request(new JoinGameReq(game.GameId))
            .Timeout(options.StreamTimeout)
            .SubmitAsync<JoinGameRes>(cancellationToken);
        Require(client2Join.State.GameId == game.GameId, "Client 2 must join the same game.");
        Require(client2Join.State.Status == "InProgress", "Joining client 2 must start the game.");
        Require(client2Join.State.OActorId == options.OActorId, "Client 2 must receive mark O.");

        // Existing room members receive push packets when another player joins.
        var client1SawClient2Join = await client1.WaitForAsync<PlayerJoinedNotify>(
            message => message.Payload.ActorId == options.OActorId,
            options.StreamTimeout,
            cancellationToken);
        Require(client1SawClient2Join.Payload.Mark == "O", "Client 1 must be notified that client 2 joined.");
        Require(client1SawClient2Join.Payload.State.Status == "InProgress", 
            "Client 2 join notification must start the game.");
        Require(
            client2.ReceivedCount(nameof(PlayerJoinedNotify)) == 0,
            "Client 2 must not receive a self-join notification.");

        var client1SawGameStart = await client1.WaitForAsync<GameStateNotify>(
            message => message.Payload.State.Status == "InProgress"
                       && message.Payload.State.OActorId == options.OActorId,
            options.StreamTimeout,
            cancellationToken);
        Require(client1SawGameStart.Payload.State.NextTurn == "X", "Client 1 must see the first turn.");

        // The move sequence is deterministic: client 1 completes the top row.
        var client1Move1 = await client1
            .Request(new PlaceMarkReq(0))
            .Timeout(options.StreamTimeout)
            .SubmitAsync<PlaceMarkRes>(cancellationToken);
        Require(client1Move1.State.Board == "X........", "Client 1 must place the first mark at cell 0.");
        Require(client1Move1.State.NextTurn == "O", "Turn must move to client 2 after client 1 places a mark.");

        var client2SawClient1Move1 = await client2.WaitForAsync<GameStateNotify>(
            message => message.Payload.State.LastMoveActorId == options.XActorId
                       && message.Payload.State.LastMoveCell == 0,
            options.StreamTimeout,
            cancellationToken);
        Require(
            client2SawClient1Move1.Payload.State.Board == client1Move1.State.Board,
            "Client 2 must receive client 1's first move.");

        var client2Move1 = await client2
            .Request(new PlaceMarkReq(3))
            .Timeout(options.StreamTimeout)
            .SubmitAsync<PlaceMarkRes>(cancellationToken);
        Require(client2Move1.State.Board == "X..O.....", "Client 2 must place a mark at cell 3.");
        Require(client2Move1.State.NextTurn == "X", "Turn must move back to client 1.");

        var client1SawClient2Move1 = await client1.WaitForAsync<GameStateNotify>(
            message => message.Payload.State.LastMoveActorId == options.OActorId
                       && message.Payload.State.LastMoveCell == 3,
            options.StreamTimeout,
            cancellationToken);
        Require(
            client1SawClient2Move1.Payload.State.Board == client2Move1.State.Board,
            "Client 1 must receive client 2's first move.");

        var client1Move2 = await client1
            .Request(new PlaceMarkReq(1))
            .Timeout(options.StreamTimeout)
            .SubmitAsync<PlaceMarkRes>(cancellationToken);
        Require(client1Move2.State.Board == "XX.O.....", "Client 1 must place the second top-row mark.");
        Require(client1Move2.State.NextTurn == "O", "Turn must move to client 2 after client 1's second move.");

        var client2SawClient1Move2 = await client2.WaitForAsync<GameStateNotify>(
            message => message.Payload.State.LastMoveActorId == options.XActorId
                       && message.Payload.State.LastMoveCell == 1,
            options.StreamTimeout,
            cancellationToken);
        Require(
            client2SawClient1Move2.Payload.State.Board == client1Move2.State.Board,
            "Client 2 must receive client 1's second move.");

        var client2Move2 = await client2
            .Request(new PlaceMarkReq(4))
            .Timeout(options.StreamTimeout)
            .SubmitAsync<PlaceMarkRes>(cancellationToken);
        Require(client2Move2.State.Board == "XX.OO....", "Client 2 must place a mark at cell 4.");
        Require(client2Move2.State.NextTurn == "X", "Turn must move back to client 1 before the final move.");

        var client1SawClient2Move2 = await client1.WaitForAsync<GameStateNotify>(
            message => message.Payload.State.LastMoveActorId == options.OActorId
                       && message.Payload.State.LastMoveCell == 4,
            options.StreamTimeout,
            cancellationToken);
        Require(
            client1SawClient2Move2.Payload.State.Board == client2Move2.State.Board,
            "Client 1 must receive client 2's second move.");

        var client1FinalMove = await client1
            .Request(new PlaceMarkReq(2))
            .Timeout(options.StreamTimeout)
            .SubmitAsync<PlaceMarkRes>(cancellationToken);
        Require(client1FinalMove.State.Board == "XXXOO....", "Client 1 must complete the top row.");
        Require(client1FinalMove.State.Status == "Won", "The final move must finish the game.");
        Require(
            client1FinalMove.State.Winner == options.XActorId,
            "The deterministic scenario must end with client 1 winning.");

        var client2SawFinal = await client2.WaitForAsync<GameStateNotify>(
            message => message.Payload.State.Status == "Won"
                       && message.Payload.State.Winner == options.XActorId,
            options.StreamTimeout,
            cancellationToken);
        Require(
            client2SawFinal.Payload.State.Board == client1FinalMove.State.Board,
            "Client 2 must receive the final game state.");
    }

    private static void Require(bool condition, string message)
    {
        if (!condition)
        {
            throw new InvalidOperationException(message);
        }
    }
}

public sealed record TicTacToeClientOptions(
    Uri ApiUrl,
    string GameName,
    string XActorId,
    string OActorId,
    TimeSpan HttpTimeout,
    TimeSpan StreamTimeout)
{
    public static TicTacToeClientOptions CreateDefault()
        => new(
            new Uri("http://127.0.0.1:18080"),
            "tictactoe-game",
            "player-x",
            "player-o",
            TimeSpan.FromSeconds(10),
            TimeSpan.FromSeconds(5));
}

public static class TicTacToeClientConnections
{
    public static IZlinkStreamConnector CreateStreamClient(
        string streamEndpoint,
        TicTacToeClientOptions options)
    {
        return ZlinkStreamConnectorFactory.Create(new ZlinkStreamConnectorOptions
        {
            Endpoint = new Uri(streamEndpoint),
            ConnectTimeout = options.StreamTimeout,
            RequestTimeout = options.StreamTimeout,
            DispatchMode = ZlinkStreamDispatchMode.Immediate,
        });
    }
}
