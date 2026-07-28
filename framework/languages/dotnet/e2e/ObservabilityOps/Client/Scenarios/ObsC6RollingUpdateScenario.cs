// Verifies OBS-C6 relocates a host only to the requested higher version.
using ObservabilityOps.Client.Support;
using ObservabilityOps.Shared;
using Zlink.Framework.Contracts.Errors;

namespace ObservabilityOps.Client.Scenarios;

internal static class ObsC6RollingUpdateScenario
{
    public static async Task RunAsync(ScenarioContext context)
    {
        var sourceNode = await context.PlayNodeIdAsync("play-a");
        var targetNode = await context.PlayNodeIdAsync("play-b");
        await using var workload = await context.PreparePlayWorkloadAsync(
            sourceNode, "c6");
        using var trafficCancellation = new CancellationTokenSource();
        var traffic = RunTrafficAsync(workload, trafficCancellation.Token);

        var result = (await context.PlayA.Post("/relocate/direct")
            .Body(new RelocateHostReq("rolling-update", 2, 30000))
            .Timeout(TimeSpan.FromSeconds(35))
            .Async<RelocateHostRes>()).Body;
        trafficCancellation.Cancel();
        var trafficResult = await traffic;

        ZlinkStreamAssert.Ensure(
            result is
            {
                Mode: "RollingUpdate",
                TargetApplicationVersion: 2,
                Outcome: "Relocated",
                Reason: "None"
            },
            $"OBS-C6 rolling update returned "
            + $"{result.Mode}/{result.TargetApplicationVersion}/"
            + $"{result.Outcome}/{result.Reason}.");
        ZlinkStreamAssert.Ensure(
            trafficResult.Completed > 0
            && trafficResult.TerminalAttempts > 0,
            "OBS-C6 did not complete finite traffic attempts during the update.");

        var relocated = await context.WaitForPlayWorkloadAsync(
            workload.Room.RoomRid,
            workload.ActorId,
            targetNode,
            TimeSpan.FromSeconds(15));
        ZlinkStreamAssert.Ensure(
            relocated.ActorRows.Single(row =>
                row.ActorId == workload.ActorId).Generation
                == workload.ActorGeneration
            && relocated.SpotRows.Single(row =>
                row.SpotRid == workload.Room.RoomRid).Generation
                == workload.RoomGeneration,
            "OBS-C6 changed Actor or User Spot ObjectGeneration.");
        var instance = (await context.PlayB.Get("/evidence")
            .Query("spotRid", workload.Instance.SpotId)
            .Async<EvidenceSnapshot>()).Body;
        ZlinkStreamAssert.Ensure(
            instance.SpotRows.Any(row =>
                row.SpotRid == workload.Instance.SpotId
                && row.NodeRid == targetNode),
            "OBS-C6 Instance Spot did not move to application version 2.");

        var action = await workload.Connector.Request(
                new GameActionReq("obs-c6-after"))
            .Async<GameActionRes>();
        ZlinkStreamAssert.Ensure(
            action.NodeRid == targetNode,
            "OBS-C6 bound session did not continue on the new version.");
        var status = (await context.PlayA.Get("/runtime/status")
            .Async<RuntimeStatusRes>()).Body;
        ZlinkStreamAssert.Ensure(
            status.State == "Relocated" && !status.AcceptingWork,
            "OBS-C6 source did not remain in Relocated state.");

        var shutdown = (await context.PlayA.Post("/shutdown/direct")
            .Body(new ShutdownHostReq())
            .Async<ShutdownHostRes>()).Body;
        ZlinkStreamAssert.Ensure(
            shutdown is { Outcome: "Stopped", Reason: "None" },
            $"OBS-C6 source shutdown returned "
            + $"{shutdown.Outcome}/{shutdown.Reason}.");
        Console.WriteLine("scenario OBS-C6 passed");
    }

    private static async Task<TrafficResult> RunTrafficAsync(
        PlayWorkload workload,
        CancellationToken cancellationToken)
    {
        var completed = 0;
        var terminalAttempts = 0;
        var sequence = 0;
        while (!cancellationToken.IsCancellationRequested)
        {
            var marker = $"obs-c6-live-{sequence++}";
            try
            {
                var response = await workload.Connector.Request(
                        new GameActionReq(marker))
                    .Async<GameActionRes>(cancellationToken);
                ZlinkStreamAssert.Ensure(
                    response.Marker == marker,
                    "OBS-C6 request reply ordering changed.");
                if (response.ActorId == workload.ActorId) completed++;
                terminalAttempts++;
            }
            catch (OperationCanceledException)
                when (cancellationToken.IsCancellationRequested)
            {
                break;
            }
            catch (ZLinkFrameworkException exception)
                when (exception.Kind is
                    ZLinkFrameworkErrorKind.Unavailable
                    or ZLinkFrameworkErrorKind.DeadlineExceeded
                    or ZLinkFrameworkErrorKind.ShuttingDown)
            {
                // A new request receives an explicit moving or timeout result.
                // The same operation is not retried.
                terminalAttempts++;
            }
        }

        return new TrafficResult(completed, terminalAttempts);
    }

    private sealed record TrafficResult(
        int Completed,
        int TerminalAttempts);
}
