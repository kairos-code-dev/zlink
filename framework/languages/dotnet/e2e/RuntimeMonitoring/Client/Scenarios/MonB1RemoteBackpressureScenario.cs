// Verifies Config 7 MON-B1 remote ROUTER backpressure with the public
// Logical Multicast result, typed runtime event, and follow-up snapshot.
using System.Diagnostics;
using System.Runtime.InteropServices;
using RuntimeMonitoring.Client.Support;
using RuntimeMonitoring.Shared;
using Zlink.Framework.Contracts.Errors;
using Zlink.HttpClient;

namespace RuntimeMonitoring.Client.Scenarios;

internal static class MonB1RemoteBackpressureScenario
{
    private const int SigContinue = 18;
    private const int SigStop = 19;

    public static async Task RunAsync(ClientOptions options)
    {
        using var serviceA = ZLinkHttpClient.Create(options.ServiceUrl)
            .Timeout(TimeSpan.FromSeconds(40))
            .Build();
        using var serviceB = ZLinkHttpClient.Create(options.ServiceBUrl)
            .Timeout(TimeSpan.FromSeconds(10))
            .Build();
        await using var serviceC = await EphemeralService.StartAsync(options, "svc-mon-b1-c");
        using var serviceCHttp = ZLinkHttpClient.Create(serviceC.Url)
            .Timeout(TimeSpan.FromSeconds(10))
            .Build();

        await CreateSubjectAsync(serviceA, "mon-b1-a");
        await CreateSubjectAsync(serviceB, "mon-b1-b");
        await CreateSubjectAsync(serviceCHttp, "mon-b1-c");
        await WaitForReadyPeersAsync(serviceA, 2);

        var baseline = (await serviceA.Get("/evidence").Async<string[]>()).Body.Length;
        Signal(options.ServiceBProcessId, SigStop);
        try
        {
            var marker = $"mon-b1-{Guid.NewGuid():N}";
            var result = (await serviceA.Post("/spot/publish-until")
                .Body(new MulticastPublishReq(
                    marker,
                    PayloadBytes: 1024 * 1024,
                    MaxAttempts: 20000,
                    ExpectedRemoteDropped: 1))
                .Async<MulticastPublishRes>()).Body;

            ZlinkStreamAssert.Ensure(
                result.Status == "Backpressured",
                $"MON-B1 status was {result.Status}, expected Backpressured.");
            ZlinkStreamAssert.Ensure(
                result.SnapshotRemote == 2
                && result.AdmittedRemote == 1
                && result.DroppedRemote == 1,
                $"MON-B1 remote target result was {result.SnapshotRemote}/"
                + $"{result.AdmittedRemote}/{result.DroppedRemote}, expected 2/1/1.");
            ZlinkStreamAssert.Ensure(
                result.BackpressuredTotal > 0,
                "MON-B1 follow-up snapshot did not retain the backpressure count.");

            var evidence = (await serviceA.Post("/evidence/wait")
                .Body(new EvidenceWaitReq(
                    [
                        "identifier=zlink.runtime.mesh_node.multicast_backpressured",
                        "remote-snapshot=2",
                        "remote-admitted=1",
                        "remote-dropped=1"
                    ],
                    [],
                TimeoutMilliseconds: 3000,
                    AfterIndex: baseline))
                .Async<string[]>()).Body;
            ZlinkStreamAssert.Ensure(
                evidence.Any(line =>
                    line.Contains(
                        "identifier=zlink.runtime.mesh_node.multicast_backpressured",
                        StringComparison.Ordinal)
                    && line.Contains("remote-snapshot=2", StringComparison.Ordinal)
                    && line.Contains("remote-admitted=1", StringComparison.Ordinal)
                    && line.Contains("remote-dropped=1", StringComparison.Ordinal)),
                "MON-B1 typed runtime event did not carry the operation target counts.");
        }
        finally
        {
            Signal(options.ServiceBProcessId, SigContinue);
            await CloseSubjectBestEffortAsync(serviceA, "mon-b1-a");
            await CloseSubjectBestEffortAsync(serviceB, "mon-b1-b");
            await CloseSubjectBestEffortAsync(serviceCHttp, "mon-b1-c");
        }

        Console.WriteLine("scenario MON-B1 passed");
    }

    private static async Task CreateSubjectAsync(ZLinkHttpClient service, string spotRid)
    {
        await service.Post($"/admin/subject/create/{spotRid}").AsyncRaw();
    }

    private static async Task CloseSubjectBestEffortAsync(
        ZLinkHttpClient service,
        string spotRid)
    {
        try
        {
            await service.Post($"/admin/subject/close/{spotRid}").AsyncRaw();
        }
        catch (HttpRequestException)
        {
        }
        catch (TaskCanceledException)
        {
        }
        catch (ZLinkFrameworkException)
        {
        }
    }

    private static async Task WaitForReadyPeersAsync(ZLinkHttpClient service, int expected)
    {
        var elapsed = Stopwatch.StartNew();
        while (true)
        {
            var snapshot = (await service.Get("/runtime/snapshot")
                .Async<MeshSnapshot>()).Body;
            if (snapshot.Peers.Count(peer => peer.Ready) == expected)
                return;
            if (elapsed.Elapsed >= TimeSpan.FromSeconds(3))
                throw new InvalidOperationException(
                    $"MON-B1 expected {expected} ready peers.");
            await Task.Delay(TimeSpan.FromMilliseconds(50));
        }
    }

    private static void Signal(int processId, int signal)
    {
        if (kill(processId, signal) != 0)
            throw new InvalidOperationException(
                $"Could not send signal {signal} to process {processId}: errno={Marshal.GetLastWin32Error()}.");
    }

    [DllImport("libc", SetLastError = true)]
    private static extern int kill(int processId, int signal);

    private sealed record MeshSnapshot(MeshPeer[] Peers);

    private sealed record MeshPeer(bool Ready);
}
