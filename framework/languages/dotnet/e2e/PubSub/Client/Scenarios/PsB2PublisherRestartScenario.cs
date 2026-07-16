// Verifies PS-B2 Publisher Restart behavior.
using System.Diagnostics;
using PubSub.Client.Support;
using PubSub.Shared;
using Systems.Zlink.Stream.Connector.Contracts;
using Zlink.HttpClient;

namespace PubSub.Client.Scenarios;

// PS-B2: verifies that subscribers receive new events after the publisher process restarts.
internal static class PsB2PublisherRestartScenario
{
    public static async Task<Process> RunAsync(
        ZLinkHttpClient publisher,
        IReadOnlyList<ZLinkHttpClient> subscribers,
        ServerProcessLauncher processes)
    {
        var runId = Guid.NewGuid().ToString("N");

        // Establish a baseline delivery before stopping the publisher process.
        await publisher.Post("/publish/event")
            .Query("topic", PubSubNames.MainTopic)
            .Query("runId", runId)
            .Query("sequence", "1")
            .Query("value", "before-publisher-restart")
            .AsyncRaw();
        await WaitForSubscribersAsync(subscribers, new EvidenceWaitReq(
            ["event|", $"run={runId}", $"topic={PubSubNames.MainTopic}", "seq=1"],
            []));
        var baselineRows = (await subscribers[0].Get("/locations/peers")
            .Query("mesh", PubSubNames.Channel)
            .Async<PeerLocationRowRes[]>()).Body
            .Where(row => row.Endpoint == processes.PublisherEndpoint)
            .ToArray();
        ZlinkStreamAssert.Ensure(
            baselineRows.Length == 1 && baselineRows[0].NodeRid.Length > 0,
            "PS-B2 expected exactly one live publisher row before drain.");
        var publisherNodeRid = baselineRows[0].NodeRid;
        var evidenceOffsets = await Task.WhenAll(
            subscribers.Select(SubscriberObservation.EvidenceCountAsync));

        // A normal replacement must reach terminal Drained and remove the old
        // owner row without waiting for lease expiry.
        var drain = (await publisher.Post("/admin/drain")
            .Timeout(TimeSpan.FromSeconds(35))
            .Async<DrainResultRes>()).Body;
        ZlinkStreamAssert.Ensure(
            drain.Result == nameof(Zlink.Framework.Contracts.Configuration.Drained),
            $"PS-B2 expected terminal Drained, got {drain.Result}:{drain.Reason}.");
        await subscribers[0].Post("/locations/peers/wait")
            .Body(new PeerLocationWaitReq(
                PubSubNames.Channel,
                publisherNodeRid,
                Present: false))
            .Timeout(TimeSpan.FromSeconds(31))
            .AsyncRaw();

        await publisher.Post("/shutdown").AsyncRaw();
        await StateObservation.WaitUntilAsync(
            async () =>
            {
                try
                {
                    return (await publisher.Get("/health").AsyncRaw()).Status != 200;
                }
                catch (Exception ex) when ((ex is HttpRequestException || ex.InnerException is HttpRequestException))
                {
                    return true;
                }
            },
            "PS-B2 expected publisher to stop before restart.");
        await Task.WhenAll(subscribers.Select((subscriber, index) =>
            SubscriberObservation.WaitForSocketEvidenceAsync(
                subscriber,
                PubSubNames.SubscriberSocketSource,
                "Disconnected",
                evidenceOffsets[index])));
        evidenceOffsets = await Task.WhenAll(
            subscribers.Select(SubscriberObservation.EvidenceCountAsync));

        // A publish while the process is down should fail at the HTTP boundary.
        await ZlinkStreamAssert.ExpectFailureAsync(async cancellationToken =>
            _ = await publisher.Post("/publish/event")
                .Query("topic", PubSubNames.MainTopic)
                .Query("runId", runId)
                .Query("sequence", "2")
                .Query("value", "during-publisher-down")
                .AsyncRaw(cancellationToken));

        // Restart the same publisher role and wait for the health endpoint before measuring recovery.
        var restartedPublisher = processes.StartPublisher();
        await StateObservation.WaitUntilAsync(
            async () =>
            {
                try
                {
                    return (await publisher.Get("/health").AsyncRaw()).Status == 200;
                }
                catch (Exception ex) when ((ex is HttpRequestException || ex.InnerException is HttpRequestException))
                {
                    return false;
                }
            },
            "PS-B2 expected restarted publisher to become healthy.");
        await subscribers[0].Post("/locations/peers/wait")
            .Body(new PeerLocationWaitReq(
                PubSubNames.Channel,
                publisherNodeRid,
                Present: true,
                Endpoint: processes.PublisherEndpoint))
            .Timeout(TimeSpan.FromSeconds(31))
            .AsyncRaw();
        await Task.WhenAll(subscribers.Select((subscriber, index) =>
            SubscriberObservation.WaitForConnectionAsync(subscriber, evidenceOffsets[index])));

        // Every subscriber has observed its public socket reconnect before this single measurement.
        await publisher.Post("/publish/event")
            .Query("topic", PubSubNames.MainTopic)
            .Query("runId", runId)
            .Query("sequence", "3")
            .Query("value", "after-publisher-restart")
            .AsyncRaw();

        // Recovery is proven by this exact first post-readiness event at every subscriber.
        await WaitForSubscribersAsync(subscribers, new EvidenceWaitReq(
            [],
            [])
        {
            ContainsAllLineGroups =
            [
                ["event|", $"run={runId}", $"topic={PubSubNames.MainTopic}", "seq=3|"]
            ]
        });
        Console.WriteLine("scenario PS-B2 passed");
        return restartedPublisher;
    }

    private static async Task WaitForSubscribersAsync(
        IReadOnlyList<ZLinkHttpClient> subscribers,
        EvidenceWaitReq request)
    {
        var waits = subscribers
            .Select(subscriber => subscriber.Post("/evidence/wait").Body(request).Async<string[]>().AsTask())
            .ToArray();
        await Task.WhenAll(waits);
    }
}
