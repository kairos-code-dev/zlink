using System.Runtime.CompilerServices;
using Microsoft.Extensions.Logging;
using Systems.Zlink.Stream.Connector.Contracts;
using TicTacToe.Shared.Contracts;
using Zlink.HttpClient;

namespace TicTacToe.Client;

public sealed class TicTacToeClientScenario(ILogger logger)
{
    // End-to-end client story:
    // 1. Create a game through HTTP and choose separate stream endpoints for host, guest, and observer.
    // 2. Connect the host and authenticate as player X.
    // 3. Connect the observer and subscribe to win milestone notifications.
    // 4. Join the host to the empty room, then connect the guest and verify join/start pushes.
    // 5. Play a deterministic move sequence where player X wins the top row.
    // 6. Verify the guest sees the final state and the observer receives the win milestone.
    public async ValueTask RunAsync(
        TicTacToeClientOptions options,
        CancellationToken cancellationToken = default)
    {
        using var api = ZLinkHttpClient.Create(options.ApiUrl.ToString())
            .Timeout(options.HttpTimeout)
            .Build();
        var room = api.Post("/games")
            .Body(new CreateGameHttpReq(options.GameName))
            .Async<CreateGameHttpRes>().AsTask().GetAwaiter().GetResult().Body;

        Ensure(!string.IsNullOrWhiteSpace(room.RoomId));
        Ensure(!string.IsNullOrWhiteSpace(room.OwnerPlayEndpoint));
        Ensure(room.PlayEndpoints.Count >= 2);
        Ensure(room.PlayEndpoints.Contains(room.OwnerPlayEndpoint, StringComparer.Ordinal));
        Ensure(room.PlayNodes.Count == room.PlayEndpoints.Count);
        Ensure(room.PlayNodes.Any(node =>
            string.Equals(node.StreamEndpoint, room.OwnerPlayEndpoint, StringComparison.Ordinal)));
        Ensure(room.RequiredLevel == 3);
        Ensure(room.GameName == options.GameName);

        var guestPlayEndpoint = room.PlayEndpoints.First(endpoint =>
            !string.Equals(endpoint, room.OwnerPlayEndpoint, StringComparison.Ordinal));
        var observerPlayEndpoint = guestPlayEndpoint;
        var observerPlayNode = room.PlayNodes.Single(node =>
            string.Equals(node.StreamEndpoint, observerPlayEndpoint, StringComparison.Ordinal));

        await using var client1 =
            TicTacToeClientConnections.CreateStreamClient(room.OwnerPlayEndpoint, options, "host", logger);
        await using var client2 =
            TicTacToeClientConnections.CreateStreamClient(guestPlayEndpoint, options, "guest", logger);
        await using var observer =
            TicTacToeClientConnections.CreateStreamClient(observerPlayEndpoint, options, "observer", logger);

        // Client 1 connects, authenticates as player X, and joins the empty room.
        await client1.Connect.Async(cancellationToken);

        var client1Authentication = await client1.Request(new AuthenticateReq(options.XActorId))
            .Async<AuthenticateRes>(cancellationToken);
        Ensure(client1Authentication.Player.ActorId == options.XActorId);
        Ensure(client1Authentication.Player.Level >= room.RequiredLevel);
        Ensure(client1Authentication.Player.Wins == 99);

        await observer.Connect.Async(cancellationToken);
        var observerAuthentication = await observer.Request(new AuthenticateReq(options.ObserverActorId))
            .Async<AuthenticateRes>(cancellationToken);
        Ensure(observerAuthentication.Player.ActorId == options.ObserverActorId);
        var observerSubscription =
            await observer.Request(new ObserveMilestoneReq()).Async<ObserveMilestoneRes>(cancellationToken);
        Ensure(observerSubscription.Subscribed);

        var client1Join = await client1.Request(new JoinGameReq(room.RoomId)).Async<JoinGameRes>(cancellationToken);
        Ensure(client1Join.State.RoomId == room.RoomId);
        Ensure(client1Join.State.Status == TicTacToeGameStatuses.WaitingForPlayers);
        Ensure(client1Join.State.XActorId == options.XActorId);
        Ensure(client1.ReceivedCount(nameof(PlayerJoinedNotify)) == 0);

        // Client 2 connects, authenticates as player O, and joins the same room.
        await client2.Connect.Async(cancellationToken);

        var client2Authentication = await client2.Request(new AuthenticateReq(options.OActorId))
            .Async<AuthenticateRes>(cancellationToken);
        Ensure(client2Authentication.Player.ActorId == options.OActorId);
        Ensure(client2Authentication.Player.Level >= room.RequiredLevel);
        Ensure(client2Authentication.Player.ActorId != client1Authentication.Player.ActorId);

        var client2Join = await client2.Request(new JoinGameReq(room.RoomId)).Async<JoinGameRes>(cancellationToken);
        Ensure(client2Join.State.RoomId == room.RoomId);
        Ensure(client2Join.State.Status == TicTacToeGameStatuses.InProgress);
        Ensure(client2Join.State.OActorId == options.OActorId);

        // Existing room members receive push packets when another player joins.
        var client1SawClient2Join = await client1.WaitFor<PlayerJoinedNotify>()
            .Where(message => message.Payload.ActorId == options.OActorId)
            .Async(cancellationToken);
        Ensure(client1SawClient2Join.Payload.ActorId == options.OActorId);
        Ensure(client1SawClient2Join.Payload.DisplayName == client2Authentication.Player.DisplayName);
        Ensure(client1SawClient2Join.Payload.Level == client2Authentication.Player.Level);
        Ensure(client1SawClient2Join.Payload.Mark == TicTacToeMarks.O);
        Ensure(client1SawClient2Join.Payload.RoomId == room.RoomId);
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
        Ensure(client1Move1.State.LastMoveActorId == options.XActorId);
        Ensure(client1Move1.State.LastMoveCell == 0);

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
        Ensure(client2Move1.State.LastMoveActorId == options.OActorId);
        Ensure(client2Move1.State.LastMoveCell == 3);

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
        Ensure(client1Move2.State.LastMoveActorId == options.XActorId);
        Ensure(client1Move2.State.LastMoveCell == 1);

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
        Ensure(client2Move2.State.LastMoveActorId == options.OActorId);
        Ensure(client2Move2.State.LastMoveCell == 4);

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
        Ensure(client1FinalMove.State.LastMoveActorId == options.XActorId);
        Ensure(client1FinalMove.State.LastMoveCell == 2);

        var client2SawFinal = await client2.WaitFor<GameStateNotify>()
            .Where(message => message.Payload.State.Status == TicTacToeGameStatuses.Won
                              && message.Payload.State.Winner == options.XActorId)
            .Async(cancellationToken);
        Ensure(client2SawFinal.Payload.State.Status == TicTacToeGameStatuses.Won);
        Ensure(client2SawFinal.Payload.State.Winner == options.XActorId);
        Ensure(client2SawFinal.Payload.State.Board == client1FinalMove.State.Board);

        var observerSawMilestone = await observer.WaitFor<WinMilestoneNotify>()
            .Where(message => message.Payload.ActorId == options.XActorId
                              && message.Payload.RoomId == room.RoomId)
            .Async(cancellationToken);
        Ensure(observerSawMilestone.Payload.DisplayName == client1Authentication.Player.DisplayName);
        Ensure(observerSawMilestone.Payload.Wins == 100);
        Ensure(observerSawMilestone.Payload.ReceivingSpotNodeRid == observerPlayNode.SpotNodeRid);
        logger.LogInformation(
            "observer-win-milestone=verified actor={0} wins={1} receivingSpotNodeRid={2}",
            observerSawMilestone.Payload.ActorId,
            observerSawMilestone.Payload.Wins,
            observerSawMilestone.Payload.ReceivingSpotNodeRid);

        client1.Send(new LeaveGameReq(room.RoomId)).Submit(cancellationToken);
        client2.Send(new LeaveGameReq(room.RoomId)).Submit(cancellationToken);
    }

    private static void Ensure(
        bool condition,
        [CallerArgumentExpression(nameof(condition))]
        string? expression = null)
    {
        if (!condition) throw new InvalidOperationException($"Ensure failed: {expression}");
    }
}
