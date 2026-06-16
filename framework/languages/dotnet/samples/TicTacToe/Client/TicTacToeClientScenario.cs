using System.Net.Http.Json;
using System.Runtime.CompilerServices;
using Systems.Zlink.Stream.Connector.Contracts;
using Systems.Zlink.Stream.Connector.Json;
using TicTacToe.Shared.Contracts;

namespace TicTacToe.Client;

public sealed class TicTacToeClientScenario
{
    public async ValueTask RunAsync(
        TicTacToeClientOptions options,
        CancellationToken cancellationToken = default)
    {
        using var api = new HttpClient
        {
            BaseAddress = options.ApiUrl,
            Timeout = options.HttpTimeout,
        };

        using var createGameResponse = await api.PostAsJsonAsync(
            "/games",
            new CreateGameHttpReq(options.GameName),
            cancellationToken);
        createGameResponse.EnsureSuccessStatusCode();

        var room = await createGameResponse.Content.ReadFromJsonAsync<CreateGameHttpRes>(
            cancellationToken)
                   ?? throw new InvalidOperationException("API returned an empty room response.");

        Ensure(!string.IsNullOrWhiteSpace(room.RoomId));
        Ensure(!string.IsNullOrWhiteSpace(room.PlayEndpoint));
        Ensure(room.GameName == options.GameName);

        await using var client1 = TicTacToeClientConnections.CreateStreamClient(room.PlayEndpoint, options);
        await using var client2 = TicTacToeClientConnections.CreateStreamClient(room.PlayEndpoint, options);

        // Client 1 connects, authenticates as player X, and joins the empty room.
        await client1.Connect.Async(cancellationToken);

        var client1Authentication = await client1.Request(new AuthenticateReq(options.XActorId)).Async<AuthenticateRes>(cancellationToken);
        Ensure(client1Authentication.ActorId == options.XActorId);

        var client1Join = await client1.Request(new JoinGameReq(room.RoomId)).Async<JoinGameRes>(cancellationToken);
        Ensure(client1Join.State.RoomId == room.RoomId);
        Ensure(client1Join.State.Status == TicTacToeGameStatuses.WaitingForPlayers);
        Ensure(client1Join.State.XActorId == options.XActorId);
        Ensure(client1.ReceivedCount(nameof(PlayerJoinedNotify)) == 0);

        // Client 2 connects, authenticates as player O, and joins the same room.
        await client2.Connect.Async(cancellationToken);

        var client2Authentication = await client2.Request(new AuthenticateReq(options.OActorId)).Async<AuthenticateRes>(cancellationToken);
        Ensure(client2Authentication.ActorId == options.OActorId);
        Ensure(client2Authentication.ActorId != client1Authentication.ActorId);

        var client2Join = await client2.Request(new JoinGameReq(room.RoomId)).Async<JoinGameRes>(cancellationToken);
        Ensure(client2Join.State.RoomId == room.RoomId);
        Ensure(client2Join.State.Status == TicTacToeGameStatuses.InProgress);
        Ensure(client2Join.State.OActorId == options.OActorId);

        // Existing room members receive push packets when another player joins.
        var client1SawClient2Join = await client1.WaitFor<PlayerJoinedNotify>()
            .Where(message => message.Payload.ActorId == options.OActorId)
            .Async(cancellationToken);
        Ensure(client1SawClient2Join.Payload.ActorId == options.OActorId);
        Ensure(client1SawClient2Join.Payload.Mark == TicTacToeMarks.O);
        Ensure(client1SawClient2Join.Payload.State.Status == TicTacToeGameStatuses.InProgress);
        Ensure(client2.ReceivedCount(nameof(PlayerJoinedNotify)) == 0);

        var client1SawGameStart = await client1.WaitFor<GameStateNotify>()
            .Where(message => message.Payload.State.Status == TicTacToeGameStatuses.InProgress
                              && message.Payload.State.OActorId == options.OActorId)
            .Async(cancellationToken);

        Ensure(client1SawGameStart.Payload.State.Status == TicTacToeGameStatuses.InProgress);
        Ensure(client1SawGameStart.Payload.State.OActorId == options.OActorId);
        Ensure(client1SawGameStart.Payload.State.NextTurn == TicTacToeMarks.X);

        // The move sequence is deterministic: client 1 completes the top row.
        var client1Move1 = await client1.Request(new PlaceMarkReq(0)).Async<PlaceMarkRes>(cancellationToken);
        Ensure(client1Move1.State.Board == "X........");
        Ensure(client1Move1.State.NextTurn == TicTacToeMarks.O);

        var client2SawClient1Move1 = await client2.WaitFor<GameStateNotify>()
            .Where(message => message.Payload.State.LastMoveActorId == options.XActorId
                              && message.Payload.State.LastMoveCell == 0)
            .Async(cancellationToken);
        Ensure(client2SawClient1Move1.Payload.State.LastMoveActorId == options.XActorId);
        Ensure(client2SawClient1Move1.Payload.State.LastMoveCell == 0);
        Ensure(client2SawClient1Move1.Payload.State.Board == client1Move1.State.Board);

        var client2Move1 = await client2.Request(new PlaceMarkReq(3)).Async<PlaceMarkRes>(cancellationToken);
        Ensure(client2Move1.State.Board == "X..O.....");
        Ensure(client2Move1.State.NextTurn == TicTacToeMarks.X);

        var client1SawClient2Move1 = await client1.WaitFor<GameStateNotify>()
            .Where(message => message.Payload.State.LastMoveActorId == options.OActorId
                              && message.Payload.State.LastMoveCell == 3)
            .Async(cancellationToken);
        Ensure(client1SawClient2Move1.Payload.State.LastMoveActorId == options.OActorId);
        Ensure(client1SawClient2Move1.Payload.State.LastMoveCell == 3);
        Ensure(client1SawClient2Move1.Payload.State.Board == client2Move1.State.Board);

        var client1Move2 = await client1.Request(new PlaceMarkReq(1)).Async<PlaceMarkRes>(cancellationToken);
        Ensure(client1Move2.State.Board == "XX.O.....");
        Ensure(client1Move2.State.NextTurn == TicTacToeMarks.O);

        var client2SawClient1Move2 = await client2.WaitFor<GameStateNotify>()
            .Where(message => message.Payload.State.LastMoveActorId == options.XActorId
                              && message.Payload.State.LastMoveCell == 1)
            .Async(cancellationToken);
        Ensure(client2SawClient1Move2.Payload.State.LastMoveActorId == options.XActorId);
        Ensure(client2SawClient1Move2.Payload.State.LastMoveCell == 1);
        Ensure(client2SawClient1Move2.Payload.State.Board == client1Move2.State.Board);

        var client2Move2 = await client2.Request(new PlaceMarkReq(4)).Async<PlaceMarkRes>(cancellationToken);
        Ensure(client2Move2.State.Board == "XX.OO....");
        Ensure(client2Move2.State.NextTurn == TicTacToeMarks.X);

        var client1SawClient2Move2 = await client1.WaitFor<GameStateNotify>()
            .Where(message => message.Payload.State.LastMoveActorId == options.OActorId
                              && message.Payload.State.LastMoveCell == 4)
            .Async(cancellationToken);
        Ensure(client1SawClient2Move2.Payload.State.LastMoveActorId == options.OActorId);
        Ensure(client1SawClient2Move2.Payload.State.LastMoveCell == 4);
        Ensure(client1SawClient2Move2.Payload.State.Board == client2Move2.State.Board);

        var client1FinalMove = await client1.Request(new PlaceMarkReq(2)).Async<PlaceMarkRes>(cancellationToken);
        Ensure(client1FinalMove.State.Board == "XXXOO....");
        Ensure(client1FinalMove.State.Status == TicTacToeGameStatuses.Won);
        Ensure(client1FinalMove.State.Winner == options.XActorId);

        var client2SawFinal = await client2.WaitFor<GameStateNotify>()
            .Where(message => message.Payload.State.Status == TicTacToeGameStatuses.Won
                              && message.Payload.State.Winner == options.XActorId)
            .Async(cancellationToken);
        Ensure(client2SawFinal.Payload.State.Status == TicTacToeGameStatuses.Won);
        Ensure(client2SawFinal.Payload.State.Winner == options.XActorId);
        Ensure(client2SawFinal.Payload.State.Board == client1FinalMove.State.Board);
    }

    private static void Ensure(
        bool condition,
        [CallerArgumentExpression(nameof(condition))] string? expression = null)
    {
        if (!condition)
        {
            throw new InvalidOperationException($"Ensure failed: {expression}");
        }
    }
}
