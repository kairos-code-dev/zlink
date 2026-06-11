using System.Runtime.CompilerServices;
using Bingo.Shared.Contracts;
using Systems.Zlink.Stream.Connector.Contracts;
using Systems.Zlink.Stream.Connector.Protobuf;

namespace Bingo.Client;

internal sealed class BingoClientScenario
{
    public async ValueTask RunAsync(
        IZlinkStreamConnector client1,
        IZlinkStreamConnector client2,
        CancellationToken cancellationToken = default)
    {
        // Client 1 connects, authenticates, and creates the waiting room.
        await client1.Connect.Async(cancellationToken);
        var client1Auth = await client1.Request(new AuthenticateReq { AccessToken = "player-1" }).Async<AuthenticateRes>(cancellationToken);

        Ensure(client1Auth.ActorId == "player-1");

        var client1MatchRes = await client1.Request(new MatchBingoReq { Mode = "two-player" }).Async<MatchBingoRes>(cancellationToken);

        Ensure(client1MatchRes.State.Status == "WaitingForPlayers");
        Ensure(client1MatchRes.State.HostActorId == client1Auth.ActorId);
        Ensure(client1.ReceivedCount(nameof(PlayerJoinedNotify)) == 0);

        // Client 2 connects, authenticates, and joins the same room.
        await client2.Connect.Async(cancellationToken);

        var client2Auth = await client2.Request(new AuthenticateReq { AccessToken = "player-2" }).Async<AuthenticateRes>(cancellationToken);

        Ensure(client2Auth.ActorId == "player-2");
        Ensure(client2Auth.ActorId != client1Auth.ActorId);

        var client2MatchRes = await client2.Request(new MatchBingoReq { Mode = "two-player" }).Async<MatchBingoRes>(cancellationToken);

        Ensure(client2MatchRes.RoomId == client1MatchRes.RoomId);
        Ensure(client2MatchRes.State.Status == "Running");

        // Joining another player is delivered as a push to existing room members.
        var client1SawClient2Join = await client1.WaitFor<PlayerJoinedNotify>()
            .Where(message => message.Payload.ActorId == client2Auth.ActorId)
            .Async(cancellationToken);

        Ensure(client1SawClient2Join.Payload.ActorId == client2Auth.ActorId);
        Ensure(client2.ReceivedCount(nameof(PlayerJoinedNotify)) == 0);

        // The room starts automatically after both players have joined.
        var client1StartedTask = client1.WaitFor<BingoGameStartedNotify>().Async(cancellationToken).AsTask();
        var client2StartedTask = client2.WaitFor<BingoGameStartedNotify>().Async(cancellationToken).AsTask();
        await Task.WhenAll(client1StartedTask, client2StartedTask);

        var client1Started = await client1StartedTask;
        Ensure(client1Started.Payload.State.Status == "Running");

        var client2Started = await client2StartedTask;
        Ensure(client2Started.Payload.State.Status == "Running");

        // Each client submits its bingo card; the server starts drawing after both cards arrive.
        var client2Card = await client2
            .Request(new SubmitBingoCardReq
            {
                RoomId = client2MatchRes.RoomId,
                Card = { BingoClientCards.Player2 },
            })
            .Async<SubmitBingoCardRes>(cancellationToken);
        Ensure(client2Card.State.Status == "Running");

        var client1Card = await client1
            .Request(new SubmitBingoCardReq
            {
                RoomId = client1MatchRes.RoomId,
                Card = { BingoClientCards.Player1 },
            })
            .Async<SubmitBingoCardRes>(cancellationToken);
        Ensure(client1Card.State.Status == "Running");

        var drawnNumbers = new List<BingoNumberDrawnNotify>();
        // Number drawing is server-driven; clients only wait for draw notifications.
        while (true)
        {
            cancellationToken.ThrowIfCancellationRequested();
            var expectedSeq = drawnNumbers.Count + 1;
            var client1DrawnTask = client1.WaitFor<BingoNumberDrawnNotify>()
                .Where(message => message.Payload.DrawSeq == expectedSeq)
                .Async(cancellationToken).AsTask();
            var client2DrawnTask = client2.WaitFor<BingoNumberDrawnNotify>()
                .Where(message => message.Payload.DrawSeq == expectedSeq)
                .Async(cancellationToken).AsTask();
            await Task.WhenAll(client1DrawnTask, client2DrawnTask);

            var client1Drawn = await client1DrawnTask;
            var client2Drawn = await client2DrawnTask;
            drawnNumbers.Add(client1Drawn.Payload);
            Ensure(client1Drawn.Payload.DrawSeq == expectedSeq);
            Ensure(client2Drawn.Payload.DrawSeq == expectedSeq);
            Ensure(client2Drawn.Payload.DrawSeq == client1Drawn.Payload.DrawSeq);
            Ensure(client2Drawn.Payload.Number == client1Drawn.Payload.Number);

            if (client1Drawn.Payload.State.Status == "Finished")
            {
                break;
            }
        }
        Ensure(drawnNumbers.Count > 0);
        Ensure(drawnNumbers[^1].State.Status == "Finished");

        // The final result is pushed to both clients when the server detects bingo.
        var client1ResultTask = client1.WaitFor<BingoGameEndedNotify>()
            .Where(message => message.Payload.State.Status == "Finished")
            .Async(cancellationToken).AsTask();
        var client2ResultTask = client2.WaitFor<BingoGameEndedNotify>()
            .Where(message => message.Payload.State.Status == "Finished")
            .Async(cancellationToken).AsTask();
        await Task.WhenAll(client1ResultTask, client2ResultTask);

        var client1Result = await client1ResultTask;
        Ensure(client1Result.Payload.State.Status == "Finished");
        var client2Result = await client2ResultTask;
        Ensure(client2Result.Payload.State.Status == "Finished");
        Ensure(
            client2Result.Payload.State.DrawnNumbers.SequenceEqual(client1Result.Payload.State.DrawnNumbers));
        Ensure(
            client2Result.Payload.State.Winners.SequenceEqual(client1Result.Payload.State.Winners));
        Ensure(
            client2Result.Payload.State.Players.Select(static player => player.ActorId)
                .SequenceEqual(client1Result.Payload.State.Players.Select(static player => player.ActorId)));

        Ensure(
            client1Result.Payload.State.DrawnNumbers.SequenceEqual(drawnNumbers.Select(static draw => draw.Number)));
        Ensure(
            client1Result.Payload.State.Winners.SequenceEqual([client1Auth.ActorId]));
        Ensure(
            client1Result.Payload.State.Players.All(static player => player.Card.Count == 9));
        Ensure(
            client1Result.Payload.State.Players.All(static player => player.Marks[4]));
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

internal static class BingoClientCards
{
    public static readonly int[] Player1 = [1, 2, 3, 4, 0, 6, 7, 8, 9];

    public static readonly int[] Player2 = [10, 11, 12, 13, 0, 14, 4, 5, 6];
}
