using Bingo.Shared.Configuration;
using Bingo.Shared.Contracts;
using Systems.Zlink.Stream.Connector.Contracts;
using Systems.Zlink.Stream.Connector.Json;

namespace Bingo.Client;

internal sealed class BingoClientApp
{
    public async ValueTask RunAsync(
        IZlinkStreamConnector client1,
        IZlinkStreamConnector client2,
        CancellationToken cancellationToken = default)
    {
        // Client 1 connects, authenticates, and creates the waiting room.
        await client1.ConnectAsync(cancellationToken);
        var client1Auth = await client1
            .Request(new AuthenticateReq("player-1"))
            .Timeout(SampleTimings.RequestTimeout)
            .SubmitAsync<AuthenticateRes>(cancellationToken);

        Require(client1Auth.ActorId == "player-1", "Client 1 must authenticate as player-1.");

        var client1MatchRes = await client1
            .Request(new MatchBingoReq("two-player"))
            .Timeout(SampleTimings.RequestTimeout)
            .SubmitAsync<MatchBingoRes>(cancellationToken);

        Require(client1MatchRes.State.Status == "WaitingForPlayers", "Client 1 match must leave room waiting.");
        Require(client1MatchRes.State.HostActorId == client1Auth.ActorId, "Client 1 must become room host.");
        Require(client1.ReceivedCount(nameof(PlayerJoinedNotify)) == 0, "Client 1 must not receive a self-join notification.");

        // Client 2 connects, authenticates, and joins the same room.
        await client2.ConnectAsync(cancellationToken);

        var client2Auth = await client2
            .Request(new AuthenticateReq("player-2"))
            .Timeout(SampleTimings.RequestTimeout)
            .SubmitAsync<AuthenticateRes>(cancellationToken);

        Require(client2Auth.ActorId == "player-2", "Client 2 must authenticate as player-2.");
        Require(client2Auth.ActorId != client1Auth.ActorId, "Clients must authenticate as distinct actors.");

        var client2MatchRes = await client2
            .Request(new MatchBingoReq("two-player"))
            .Timeout(SampleTimings.RequestTimeout)
            .SubmitAsync<MatchBingoRes>(cancellationToken);

        Require(client2MatchRes.RoomId == client1MatchRes.RoomId, "Both clients must match into one room.");
        Require(client2MatchRes.State.Status == "Running", "Client 2 match must start the room.");

        // Joining another player is delivered as a push to existing room members.
        var client1SawClient2Join = await client1.WaitForAsync<PlayerJoinedNotify>(
            SampleTimings.RequestTimeout,
            cancellationToken);

        Require(client1SawClient2Join.Payload.ActorId == client2Auth.ActorId, "Client 1 must be notified when client 2 joins.");
        Require(client2.ReceivedCount(nameof(PlayerJoinedNotify)) == 0, "Client 2 must not receive a self-join notification.");

        // The room starts automatically after both players have joined.
        var client1StartedTask = client1.WaitForAsync<BingoGameStartedNotify>(
            SampleTimings.RequestTimeout,
            cancellationToken).AsTask();
        var client2StartedTask = client2.WaitForAsync<BingoGameStartedNotify>(
            SampleTimings.RequestTimeout,
            cancellationToken).AsTask();
        await Task.WhenAll(client1StartedTask, client2StartedTask);

        var client1Started = await client1StartedTask;
        Require(client1Started.Payload.State.Status == "Running", "Client 1 must receive game start.");

        var client2Started = await client2StartedTask;
        Require(client2Started.Payload.State.Status == "Running", "Client 2 must receive game start.");

        // Each client submits its bingo card; the server starts drawing after both cards arrive.
        var client2Card = await client2
            .Request(new SubmitBingoCardReq(client2MatchRes.RoomId, BingoClientCards.Player2))
            .Timeout(SampleTimings.RequestTimeout)
            .SubmitAsync<SubmitBingoCardRes>(cancellationToken);
        Require(client2Card.State.Status == "Running", "Client 2 card submission must keep room running.");

        var client1Card = await client1
            .Request(new SubmitBingoCardReq(client1MatchRes.RoomId, BingoClientCards.Player1))
            .Timeout(SampleTimings.RequestTimeout)
            .SubmitAsync<SubmitBingoCardRes>(cancellationToken);
        Require(client1Card.State.Status == "Running", "Client 1 card submission must keep room running.");

        var drawnNumbers = new List<BingoNumberDrawnNotify>();
        // Number drawing is server-driven; clients only wait for draw notifications.
        while (true)
        {
            cancellationToken.ThrowIfCancellationRequested();
            var expectedSeq = drawnNumbers.Count + 1;
            var client1DrawnTask = client1.WaitForAsync<BingoNumberDrawnNotify>(
                message => message.Payload.DrawSeq == expectedSeq,
                SampleTimings.RequestTimeout,
                cancellationToken).AsTask();
            var client2DrawnTask = client2.WaitForAsync<BingoNumberDrawnNotify>(
                message => message.Payload.DrawSeq == expectedSeq,
                SampleTimings.RequestTimeout,
                cancellationToken).AsTask();
            await Task.WhenAll(client1DrawnTask, client2DrawnTask);

            var client1Drawn = await client1DrawnTask;
            var client2Drawn = await client2DrawnTask;
            drawnNumbers.Add(client1Drawn.Payload);
            Require(client2Drawn.Payload.DrawSeq == client1Drawn.Payload.DrawSeq, "Both clients must receive the same draw sequence.");
            Require(client2Drawn.Payload.Number == client1Drawn.Payload.Number, "Both clients must receive the same drawn number.");

            if (client1Drawn.Payload.State.Status == "Finished")
            {
                break;
            }
        }
        Require(drawnNumbers.Count > 0, "Server timer must draw numbers.");
        Require(drawnNumbers[^1].State.Status == "Finished", "Server timer must finish the room.");

        // The final result is pushed to both clients when the server detects bingo.
        var client1ResultTask = client1.WaitForAsync<BingoGameEndedNotify>(
            message => message.Payload.State.Status == "Finished",
            SampleTimings.RequestTimeout,
            cancellationToken).AsTask();
        var client2ResultTask = client2.WaitForAsync<BingoGameEndedNotify>(
            message => message.Payload.State.Status == "Finished",
            SampleTimings.RequestTimeout,
            cancellationToken).AsTask();
        await Task.WhenAll(client1ResultTask, client2ResultTask);

        var client1Result = await client1ResultTask;
        Require(client1Result.Payload.State.Status == "Finished", "Client 1 must receive game result.");
        var client2Result = await client2ResultTask;
        Require(client2Result.Payload.State.Status == "Finished", "Client 2 must receive game result.");

        Require(
            client1Result.Payload.State.DrawnNumbers.SequenceEqual(drawnNumbers.Select(static draw => draw.Number)),
            "Final state must contain the drawn number sequence.");
        Require(
            client1Result.Payload.State.Winners.SequenceEqual([client1Auth.ActorId]),
            "The deterministic sample must finish with client 1 as the winner.");
        Require(
            client1Result.Payload.State.Players.All(static player => player.Card.Length == 9),
            "Each player card must contain 9 cells.");
        Require(
            client1Result.Payload.State.Players.All(static player => player.Marks[4]),
            "Center free cell must start marked.");
    }

    private static void Require(bool condition, string message)
    {
        if (!condition)
        {
            throw new InvalidOperationException(message);
        }
    }
}

internal static class BingoClientCards
{
    public static readonly int[] Player1 = [1, 2, 3, 4, 0, 6, 7, 8, 9];

    public static readonly int[] Player2 = [10, 11, 12, 13, 0, 14, 4, 5, 6];
}
