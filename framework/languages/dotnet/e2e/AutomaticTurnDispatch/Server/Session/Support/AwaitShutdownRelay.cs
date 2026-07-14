using Systems.Zlink;
using AutomaticTurnDispatch.Shared;
using Zlink.Framework.Contracts.Channels;

using Zlink.Framework.Contracts.Locations;

namespace AutomaticTurnDispatch.Server.Session.Support;

internal sealed partial class AwaitSession
{
    private static async Task<AwaitShutdownScenarioRes> RunShutdownThroughSpotRouteAsync(
        IZLinkRouteClient routes,
        IZLinkSpotHandleResolver spots,
        AwaitShutdownScenarioReq request,
        CancellationToken cancellationToken)
    {
        await RequestPlayControlWithRetryAsync<EnsureSpotRes>(
            routes,
            new EnsureSpotReq(request.SpotRid),
            "EnsureSpotReq",
            cancellationToken);
        var address = await spots.ResolveSpotHandleAsync(
                          RoutingId.From(request.SpotRid), cancellationToken)
                      ?? throw new InvalidOperationException(
                          $"Spot '{request.SpotRid}' has no live location row.");
        await routes.RequestToSpot(address,
                new AwaitReq(request.RequestId, request.DelayMs, "shutdown"))
            // The session gateway owns a shorter downstream deadline than the
            // client request, so a stopped play runtime becomes a public
            // remote error instead of a client-side timeout.
            .Timeout(TimeSpan.FromSeconds(10))
            .Async<AutomaticTurnDispatchRes>(cancellationToken);

        var evidence = await RequestPlayControlWithRetryAsync<AwaitEvidenceRes>(
            routes,
            new AwaitEvidenceReq(request.RequestId),
            "AwaitEvidenceReq",
            cancellationToken);
        return new AwaitShutdownScenarioRes("await.e3-shutdown-unexpected-completion", request.SpotRid, evidence.Evidence);
    }

    private static async Task<AwaitShutdownRecoveryRes> RunShutdownRecoveryThroughSpotRouteAsync(
        IZLinkRouteClient routes,
        IZLinkSpotHandleResolver spots,
        AwaitShutdownRecoveryReq request,
        CancellationToken cancellationToken)
    {
        await RequestPlayControlWithRetryAsync<EnsureSpotRes>(
            routes,
            new EnsureSpotReq(request.SpotRid),
            "EnsureSpotReq",
            cancellationToken);
        await RequestSpotWithRetryAsync<AutomaticTurnDispatchRes>(
            routes,
            spots,
            request.SpotRid,
            new ProbeReq(request.RequestId, "shutdown-recovery-probe"),
            "ProbeReq",
            cancellationToken);
        await RequestPlayControlWithRetryAsync<AwaitEvidenceRes>(
            routes,
            new AwaitEvidenceWaitReq(request.RequestId, "probe-completed"),
            "AwaitEvidenceWaitReq",
            cancellationToken);

        var evidence = await RequestPlayControlWithRetryAsync<AwaitEvidenceRes>(
            routes,
            new AwaitEvidenceReq(request.RequestId),
            "AwaitEvidenceReq",
            cancellationToken);
        Ensure(
            evidence.Evidence.Any(line =>
                line.Contains("probe-completed", StringComparison.Ordinal)
                && line.Contains($"rid=play-a|spot={request.SpotRid}", StringComparison.Ordinal)
                && line.Contains("marker=shutdown-recovery-probe", StringComparison.Ordinal)),
            "probe-E3 recovery probe marker missing.");
        return new AwaitShutdownRecoveryRes("await.e3-shutdown-recovery", request.SpotRid, evidence.Evidence);
    }
}
