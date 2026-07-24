using System.Reflection;
using System.Net.Http.Json;
using System.Text;
using RuntimeMonitoring.Client.Support;
using RuntimeMonitoring.Shared;
using Zlink.Framework.Contracts.Configuration;
using Zlink.HttpClient;

namespace RuntimeMonitoring.Client.Scenarios;

internal static class MonBPublishMonitoringAbsenceScenario
{
    private static readonly string[] ForbiddenNames =
    [
        "ZLinkLogicalMulticastSnapshot",
        "RemoteSnapshotCount",
        "RemoteAdmittedCount",
        "RemoteDroppedCount",
        "RemoteUnreachableCount",
        "LocalSnapshotCount",
        "LocalAdmittedCount",
        "LocalDroppedCount",
        "zlink.mesh_node.multicast.submits",
        "zlink.mesh_node.multicast.targets",
        "zlink.mesh_node.multicast.pending",
        "zlink.mesh_node.multicast.backpressures",
        "zlink.mesh_node.multicast.drops",
        "zlink.runtime.mesh_node.multicast_backpressured",
        "zlink.runtime.mesh_node.multicast_dropped"
    ];

    public static Task RunZeroTargetAsync(ClientOptions options) =>
        RunAsync(options, "MON-B1", "monitor.none", createSubject: false);

    public static Task RunLocalTargetAsync(ClientOptions options) =>
        RunAsync(options, "MON-B2", "monitor.dynamic", createSubject: true);

    private static async Task RunAsync(
        ClientOptions options,
        string scenario,
        string topic,
        bool createSubject)
    {
        AssertPublicContractShape();

        using var service = ZLinkHttpClient.Create(options.ServiceUrl)
            .Timeout(TimeSpan.FromSeconds(10))
            .Build();
        var evidenceStart =
            (await service.Get("/evidence").Async<string[]>()).Body.Length;

        using var http = new HttpClient { BaseAddress = new Uri(options.ServiceUrl) };
        if (createSubject)
            (await http.PostAsync("/admin/subject/create", content: null))
                .EnsureSuccessStatusCode();

        using var publishBody = JsonContent.Create(
            new ProfileReq("publish", scenario.ToLowerInvariant()));
        (await http.PostAsync($"/spot/publish/{topic}", publishBody))
            .EnsureSuccessStatusCode();

        if (createSubject)
        {
            await service.Post("/evidence/wait")
                .Body(new EvidenceWaitReq(
                    [$"logical-publish|topic={topic}|marker={scenario.ToLowerInvariant()}"],
                    [],
                    TimeoutMilliseconds: 5000,
                    AfterIndex: evidenceStart))
                .Async<string[]>();
            await Task.Delay(TimeSpan.FromMilliseconds(100));
        }

        var snapshotJson =
            await http.GetStringAsync($"/runtime/snapshot/{RuntimeMonitoringNames.SpotChannel}");
        ZlinkStreamAssert.Ensure(
            snapshotJson.Contains("\"peers\"", StringComparison.Ordinal)
            && snapshotJson.Contains("\"channels\"", StringComparison.Ordinal)
            && snapshotJson.Contains("\"claims\"", StringComparison.Ordinal)
            && snapshotJson.Contains("\"drain\"", StringComparison.Ordinal),
            $"{scenario} generic RouteMesh monitoring fields are missing.");
        AssertForbiddenTextAbsent(snapshotJson, $"{scenario} snapshot");

        var evidence = (await service.Get("/evidence").Async<string[]>()).Body
            .Skip(evidenceStart)
            .ToArray();
        if (createSubject)
        {
            ZlinkStreamAssert.Ensure(
                evidence.Count(line => line.Contains(
                    $"logical-publish|topic={topic}|marker={scenario.ToLowerInvariant()}",
                    StringComparison.Ordinal)) == 1,
                $"{scenario} local subscriber did not process the event exactly once.");
        }
        AssertForbiddenTextAbsent(string.Join('\n', evidence), $"{scenario} events");

        Console.WriteLine($"scenario {scenario} passed");
    }

    private static void AssertPublicContractShape()
    {
        var snapshot = typeof(ZLinkMeshNodeSnapshot);
        var runtimeEvent = typeof(ZLinkMeshRuntimeEvent);
        var assembly = snapshot.Assembly;

        ZlinkStreamAssert.Ensure(
            snapshot.GetProperty("Multicast", BindingFlags.Public | BindingFlags.Instance)
                is null,
            "MeshNode snapshot still exposes Publish monitoring.");
        ZlinkStreamAssert.Ensure(
            assembly.GetType("Zlink.Framework.Contracts.Configuration.ZLinkLogicalMulticastSnapshot")
                is null,
            "Publish monitoring snapshot type still exists.");

        var publicMembers = string.Join(
            '\n',
            snapshot.GetMembers(BindingFlags.Public | BindingFlags.Instance)
                .Concat(runtimeEvent.GetMembers(BindingFlags.Public | BindingFlags.Instance))
                .Select(member => member.Name));
        AssertForbiddenTextAbsent(publicMembers, "public monitoring contract");

        var bytes = File.ReadAllBytes(assembly.Location);
        AssertForbiddenTextAbsent(
            Encoding.UTF8.GetString(bytes),
            "framework metric/event literals");
        AssertForbiddenTextAbsent(
            Encoding.Unicode.GetString(bytes),
            "framework metric/event literals");
    }

    private static void AssertForbiddenTextAbsent(string text, string source)
    {
        foreach (var forbidden in ForbiddenNames)
        {
            ZlinkStreamAssert.Ensure(
                !text.Contains(forbidden, StringComparison.OrdinalIgnoreCase),
                $"{source} contains removed Publish monitoring name '{forbidden}'.");
        }
    }
}
