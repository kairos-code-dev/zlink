// Verifies Config 7 MON-A3 ChannelName readiness against actual selection.
using System.Diagnostics;
using RuntimeMonitoring.Client.Support;
using RuntimeMonitoring.Shared;
using Zlink.HttpClient;

namespace RuntimeMonitoring.Client.Scenarios;

internal static class MonA3SpotEventsScenario
{
    public static async Task RunAsync(ClientOptions options)
    {
        using var serviceA = ZLinkHttpClient.Create(options.ServiceUrl)
            .Timeout(TimeSpan.FromSeconds(35))
            .Build();
        using var serviceB = ZLinkHttpClient.Create(options.ServiceBUrl)
            .Timeout(TimeSpan.FromSeconds(35))
            .Build();

        var initialA = await WaitForReadyMembersAsync(serviceA, expected: 2);
        var initialB = await WaitForReadyMembersAsync(serviceB, expected: 2);
        AssertChannel(initialA, localWeight: 100, readyMembers: 2);
        AssertChannel(initialB, localWeight: 100, readyMembers: 2);
        ZlinkStreamAssert.Ensure(
            initialA.Peers.Single(peer => peer.Rid == "svc-b").ChannelNames
                .Contains(RuntimeMonitoringNames.Channel, StringComparer.Ordinal),
            "MON-A3 svc-a peer snapshot did not expose svc-b ChannelName membership.");

        var evidenceBaseline = (await serviceA.Get("/evidence").Async<string[]>()).Body.Length;
        ZlinkStreamAssert.Ensure(
            await ObserveProviderAsync(serviceA, "svc-b", attempts: 8),
            "MON-A3 baseline selection never reached svc-b.");

        await serviceB.Post("/admin/weight/exclude").AsyncRaw();
        var excludedA = await WaitForReadyMembersAsync(serviceA, expected: 1);
        var excludedB = await WaitForLocalWeightAsync(serviceB, expected: 0);
        AssertChannel(excludedA, localWeight: 100, readyMembers: 1);
        AssertChannel(excludedB, localWeight: 0, readyMembers: 1);
        ZlinkStreamAssert.Ensure(
            !await ObserveProviderAsync(serviceA, "svc-b", attempts: 8),
            "MON-A3 weight-0 svc-b remained selectable.");

        var eventEvidence = (await serviceA.Post("/evidence/wait")
            .Body(new EvidenceWaitReq(
                [
                    "identifier=zlink.runtime.mesh_node.channel_changed",
                    "routing=svc-b",
                    $"channel={RuntimeMonitoringNames.Channel}"
                ],
                [],
                TimeoutMilliseconds: 3000,
                AfterIndex: evidenceBaseline))
            .Async<string[]>()).Body;
        ZlinkStreamAssert.Ensure(
            eventEvidence.Any(line =>
                line.Contains("identifier=zlink.runtime.mesh_node.channel_changed",
                    StringComparison.Ordinal)
                && line.Contains("routing=svc-b", StringComparison.Ordinal)),
            "MON-A3 channel_changed event was missing.");

        await serviceB.Post("/admin/weight/include").AsyncRaw();
        var restoredA = await WaitForReadyMembersAsync(serviceA, expected: 2);
        var restoredB = await WaitForLocalWeightAsync(serviceB, expected: 100);
        AssertChannel(restoredA, localWeight: 100, readyMembers: 2);
        AssertChannel(restoredB, localWeight: 100, readyMembers: 2);
        ZlinkStreamAssert.Ensure(
            await ObserveProviderAsync(serviceA, "svc-b", attempts: 8),
            "MON-A3 restored svc-b did not become selectable.");

        Console.WriteLine("scenario MON-A3 passed");
    }

    private static void AssertChannel(
        MeshRuntimeSnapshotRes snapshot,
        int localWeight,
        int readyMembers)
    {
        var channel = snapshot.Channels.Single(candidate =>
            candidate.ChannelName == RuntimeMonitoringNames.Channel);
        ZlinkStreamAssert.Ensure(
            channel.LocalWeight == localWeight
            && channel.ReadyMemberCount == readyMembers
            && channel.Selectable,
            $"MON-A3 channel snapshot was weight={channel.LocalWeight},"
            + $" ready={channel.ReadyMemberCount}, selectable={channel.Selectable}.");
    }

    private static async Task<bool> ObserveProviderAsync(
        ZLinkHttpClient service,
        string expectedRid,
        int attempts)
    {
        for (var attempt = 0; attempt < attempts; attempt++)
        {
            var response = (await service.Post("/profile/request")
                .Body(new ProfileReq($"weight-{attempt}", $"mon-a3-{attempt}"))
                .Async<ProfileRes>()).Body;
            if (response.ProviderRid == expectedRid)
                return true;
        }
        return false;
    }

    private static async Task<MeshRuntimeSnapshotRes> WaitForReadyMembersAsync(
        ZLinkHttpClient service,
        int expected)
    {
        return await WaitForChannelAsync(service, channel =>
            channel.ReadyMemberCount == expected);
    }

    private static async Task<MeshRuntimeSnapshotRes> WaitForLocalWeightAsync(
        ZLinkHttpClient service,
        int expected)
    {
        return await WaitForChannelAsync(service, channel =>
            channel.LocalWeight == expected);
    }

    private static async Task<MeshRuntimeSnapshotRes> WaitForChannelAsync(
        ZLinkHttpClient service,
        Func<MeshRuntimeChannelRes, bool> predicate)
    {
        var elapsed = Stopwatch.StartNew();
        while (true)
        {
            var snapshot = (await service.Get(
                    $"/runtime/snapshot/{RuntimeMonitoringNames.Channel}")
                .Async<MeshRuntimeSnapshotRes>()).Body;
            var channel = snapshot.Channels.Single(candidate =>
                candidate.ChannelName == RuntimeMonitoringNames.Channel);
            if (predicate(channel))
                return snapshot;
            if (elapsed.Elapsed >= TimeSpan.FromSeconds(3))
                throw new InvalidOperationException(
                    "MON-A3 channel snapshot did not reach the expected state.");
            await Task.Delay(TimeSpan.FromMilliseconds(50));
        }
    }
}
