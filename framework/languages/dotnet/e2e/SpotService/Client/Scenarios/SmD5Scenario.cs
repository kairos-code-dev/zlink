using SpotService.Shared;
using Systems.Zlink.Stream.Connector.Contracts;
using Zlink.HttpClient;
using SpotService.Client.Support;

namespace SpotService.Client.Scenarios;

internal static class SmD5Scenario
{
    public static async Task RunAsync(
        ZLinkHttpClient sessionA,
        string sessionAStreamEndpoint)
    {
        var actorId = $"actor-sm-d5-notified-{Guid.NewGuid():N}";
        IZlinkStreamConnector? client = null;
        try
        {
            var deadline = DateTimeOffset.UtcNow + TimeSpan.FromSeconds(10);
            Exception? last = null;
            while (DateTimeOffset.UtcNow < deadline)
            {
                var candidate = ZlinkStreamConnectorFactory.Create(new ZlinkStreamConnectorOptions
                {
                    Endpoint = new Uri(sessionAStreamEndpoint),
                    ConnectTimeout = TimeSpan.FromSeconds(5),
                    RequestTimeout = TimeSpan.FromSeconds(5),
                    Heartbeat = new ZlinkStreamHeartbeatOptions { Enabled = false },
                    DispatchMode = ZlinkStreamDispatchMode.Immediate,
                    MaxReceivedMessages = 1024,
                });
                try
                {
                    await candidate.Connect.Async();
                    await candidate.Request(new AuthReq(actorId, "disconnect", "session-a"))
                        .PacketName("AuthReq")
                        .Async<AuthReply>();
                    client = candidate;
                    break;
                }
                catch (Exception ex) when (ex is ZlinkStreamException or TimeoutException)
                {
                    last = ex;
                    await candidate.DisposeAsync();
                    await Task.Delay(200);
                }
            }

            if (client is null)
            {
                throw new InvalidOperationException(
                    last is null ? $"Actor auth did not become routable: {actorId}" : $"Actor auth did not become routable: {actorId}. Last error: {last.Message}",
                    last);
            }

            await client.Close.Async();
        }
        finally
        {
            if (client is not null)
            {
                await client.DisposeAsync();
            }
        }

        var evidence = (await sessionA.Post("/evidence/wait")
            .Body(new EvidenceWaitRequest([$"entry-disconnected|rid=session-a|actor={actorId}"]))
            .SubmitAsync<string[]>()).Body;
        ScenarioAssert.That(
            evidence.Any(line => line.Contains($"entry-disconnected|rid=session-a|actor={actorId}", StringComparison.Ordinal)),
            "SM-D5 expected only the selected bound actor to receive disconnect notification.");

        Console.WriteLine("operation SpotService.sm-d5 passed");
    }
}
