// Verifies Config 7 MON-B2 local target drop with the public Logical
// Multicast detail, typed runtime event, and follow-up snapshot.
using RuntimeMonitoring.Client.Support;
using RuntimeMonitoring.Shared;
using Zlink.Framework.Contracts.Errors;
using Zlink.HttpClient;

namespace RuntimeMonitoring.Client.Scenarios;

internal static class MonB2LocalTargetDropScenario
{
    public static async Task RunAsync(ClientOptions options)
    {
        using var service = ZLinkHttpClient.Create(options.ServiceUrl)
            .Timeout(TimeSpan.FromSeconds(40))
            .Build();
        await service.Post("/admin/subject/create/mon-b2-fast").AsyncRaw();
        await service.Post("/admin/slow-subject/create/mon-b2-slow").AsyncRaw();
        try
        {
            var baseline = (await service.Get("/evidence").Async<string[]>()).Body.Length;

            var marker = $"mon-b2-{Guid.NewGuid():N}";
            var result = (await service.Post("/spot/local-drop")
                .Body(new MulticastPublishReq(
                    marker,
                    PayloadBytes: 1024 * 1024,
                    MaxAttempts: 50000,
                    ExpectedLocalDropped: 1))
                .Async<MulticastPublishRes>()).Body;

            ZlinkStreamAssert.Ensure(
                result.SnapshotLocal == 2
                && result.AdmittedLocal == 1
                && result.DroppedLocal == 1,
                $"MON-B2 local target result was {result.SnapshotLocal}/"
                + $"{result.AdmittedLocal}/{result.DroppedLocal}, expected 2/1/1.");
            ZlinkStreamAssert.Ensure(
                result.DroppedTotal > 0,
                "MON-B2 follow-up snapshot did not retain the dropped-target count.");

            var evidence = (await service.Post("/evidence/wait")
                .Body(new EvidenceWaitReq(
                    [
                        "identifier=zlink.runtime.mesh_node.multicast_dropped",
                        "local-snapshot=2",
                        "local-admitted=1",
                        "local-dropped=1"
                    ],
                    [],
                    TimeoutMilliseconds: 3000,
                    AfterIndex: baseline))
                .Async<string[]>()).Body;
            ZlinkStreamAssert.Ensure(
                evidence.Any(line =>
                    line.Contains(
                        "identifier=zlink.runtime.mesh_node.multicast_dropped",
                        StringComparison.Ordinal)
                    && line.Contains("local-snapshot=2", StringComparison.Ordinal)
                    && line.Contains("local-admitted=1", StringComparison.Ordinal)
                    && line.Contains("local-dropped=1", StringComparison.Ordinal)),
                "MON-B2 typed runtime event did not carry the local target counts.");
        }
        finally
        {
            await CloseSubjectBestEffortAsync(service, "mon-b2-fast");
            await CloseSubjectBestEffortAsync(service, "mon-b2-slow");
        }
        Console.WriteLine("scenario MON-B2 passed");
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
}
