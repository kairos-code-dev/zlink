using Bingo.Shared.Configuration;
using Bingo.Shared.Contracts;

namespace Bingo.Client;

public sealed class BingoClientApp
{
    public async ValueTask<BingoClientRunResult> RunAsync(
        BingoClientOptions options,
        CancellationToken cancellationToken = default)
    {
        var clients = new List<BingoPlayerClient>();
        try
        {
            foreach (var actorId in SampleNames.ActorIds)
            {
                clients.Add(await BingoPlayerClient.ConnectAsync(
                        actorId,
                        options.StreamEndpoint,
                        cancellationToken)
                    .ConfigureAwait(false));
            }

            var auth = new List<AuthenticateRes>();
            foreach (var client in clients)
            {
                auth.Add(await client.AuthenticateAsync(cancellationToken).ConfigureAwait(false));
            }

            var firstMatch = await clients[0].MatchAsync(cancellationToken).ConfigureAwait(false);
            var earlyHostStartRejected = await IsRejectedAsync(
                () => clients[0].StartAsync(firstMatch.RoomId, cancellationToken));

            var matches = new List<MatchBingoRes> { firstMatch };
            foreach (var client in clients.Skip(1))
            {
                matches.Add(await client.MatchAsync(cancellationToken).ConfigureAwait(false));
            }

            var nonHostStartRejected = await IsRejectedAsync(
                () => clients[1].StartAsync(firstMatch.RoomId, cancellationToken));
            var started = await clients[0].StartAsync(firstMatch.RoomId, cancellationToken)
                .ConfigureAwait(false);
            var ended = await WaitForEndedAsync(clients, cancellationToken).ConfigureAwait(false);

            var result = new BingoClientRunResult(
                auth.ToArray(),
                matches.ToArray(),
                started,
                ended,
                clients.Select(client => client.PlayerJoinedNotifications.Count).ToArray(),
                clients.Select(client => client.StartedNotifications.Count).ToArray(),
                clients.Select(client => client.DrawnNotifications.Count).ToArray(),
                clients.Select(client => client.EndedNotifications.Count).ToArray(),
                earlyHostStartRejected,
                nonHostStartRejected);
            BingoClientRunAssertions.Validate(result);
            return result;
        }
        finally
        {
            foreach (var client in clients)
            {
                await client.DisposeAsync().ConfigureAwait(false);
            }
        }
    }

    private static async ValueTask<bool> IsRejectedAsync(Func<ValueTask<StartBingoGameRes>> action)
    {
        try
        {
            await action().ConfigureAwait(false);
            return false;
        }
        catch
        {
            return true;
        }
    }

    private static async ValueTask<BingoRoomState> WaitForEndedAsync(
        IReadOnlyList<BingoPlayerClient> clients,
        CancellationToken cancellationToken)
    {
        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        timeout.CancelAfter(SampleTimings.RequestTimeout);

        while (!timeout.IsCancellationRequested)
        {
            var ended = clients
                .SelectMany(static client => client.EndedNotifications)
                .LastOrDefault();
            if (ended is not null)
            {
                return ended.State;
            }

            await Task.WhenAny(clients.Select(client => client.WaitForNotificationAsync(timeout.Token)))
                .ConfigureAwait(false);
        }

        throw new TimeoutException("Timed out waiting for BingoGameEndedNotify.");
    }
}

public sealed record BingoClientRunResult(
    IReadOnlyList<AuthenticateRes> Authentications,
    IReadOnlyList<MatchBingoRes> Matches,
    StartBingoGameRes Started,
    BingoRoomState Ended,
    IReadOnlyList<int> PlayerJoinedPushCounts,
    IReadOnlyList<int> StartedPushCounts,
    IReadOnlyList<int> DrawnPushCounts,
    IReadOnlyList<int> EndedPushCounts,
    bool EarlyHostStartRejected,
    bool NonHostStartRejected)
{
    public void WriteTo(TextWriter writer)
    {
        writer.WriteLine($"auth={string.Join(",", Authentications.Select(static auth => auth.ActorId))}");
        writer.WriteLine($"room={Ended.RoomId}, host={Ended.HostActorId}, status={Ended.Status}");
        writer.WriteLine($"drawSeq={Ended.DrawSeq}, winners={string.Join(",", Ended.Winners)}");
        writer.WriteLine($"started={Started.State.Status}, earlyRejected={EarlyHostStartRejected}, nonHostRejected={NonHostStartRejected}");
        writer.WriteLine($"push=joined:{string.Join(",", PlayerJoinedPushCounts)}, started:{string.Join(",", StartedPushCounts)}, drawn:{string.Join(",", DrawnPushCounts)}, ended:{string.Join(",", EndedPushCounts)}");
    }
}

internal static class BingoClientRunAssertions
{
    public static void Validate(BingoClientRunResult result)
    {
        Require(result.Authentications.Select(static auth => auth.ActorId).Distinct(StringComparer.Ordinal).Count() == 4,
            "Four connectors must authenticate as distinct actors.");
        Require(result.Matches.Select(static match => match.RoomId).Distinct(StringComparer.Ordinal).Count() == 1,
            "All match requests must return the same room.");
        Require(result.Matches[0].State.HostActorId == result.Authentications[0].ActorId,
            "The first joined actor must become host.");
        Require(result.EarlyHostStartRejected, "Host start must be rejected before four players join.");
        Require(result.NonHostStartRejected, "Non-host start must be rejected.");
        Require(result.Started.State.Status == "Running", "Host start must put room into Running status.");
        Require(result.Ended.Status == "Finished", "Room must finish through timer draws.");
        Require(result.Ended.Winners.Count > 1, "The deterministic sample must include same-sequence winners.");
        Require(result.Ended.Players.All(static player => player.Card.Length == 25), "Each player card must contain 25 cells.");
        Require(result.Ended.Players.All(static player => player.Marks[12]), "Center free cell must start marked.");
        Require(result.StartedPushCounts.All(static count => count > 0), "Each connector must receive game-start push.");
        Require(result.DrawnPushCounts.All(static count => count > 0), "Each connector must receive draw push.");
        Require(result.EndedPushCounts.All(static count => count > 0), "Each connector must receive game-ended push.");
    }

    private static void Require(bool condition, string message)
    {
        if (!condition)
        {
            throw new InvalidOperationException(message);
        }
    }
}
