using Systems.Zlink;
using YieldDispatch.Shared;
using Zlink.Framework.Contracts.Channels;

using Zlink.Framework.Contracts.Locations;

namespace YieldDispatch.Server.Session.Support;

internal sealed partial class YieldSession
{
    private static async Task<YieldShutdownScenarioRes> RunShutdownThroughSpotRouteAsync(
        IZLinkRouteClient routes,
        IZLinkSpotRefResolver spots,
        YieldShutdownScenarioReq request,
        CancellationToken cancellationToken)
    {
        await RequestPlayControlWithRetryAsync<EnsureSpotRes>(
            routes,
            new EnsureSpotReq(request.SpotRid),
            "EnsureSpotReq",
            cancellationToken);
        var address = await spots.ResolveSpotRefAsync(
                          RoutingId.From(request.SpotRid), cancellationToken)
                      ?? throw new InvalidOperationException(
                          $"Spot '{request.SpotRid}' has no live address.");
        await routes.RequestToSpot(
                YieldDispatchNames.SpotRouteChannel,
                address,
                new YieldReq(request.RequestId, request.DelayMs, "shutdown"))
            .PacketName("YieldReq")
            .Timeout(TimeSpan.FromSeconds(90))
            .Async<YieldDispatchRes>(cancellationToken);

        var evidence = await RequestPlayControlWithRetryAsync<YieldEvidenceRes>(
            routes,
            new YieldEvidenceReq(request.RequestId),
            "YieldEvidenceReq",
            cancellationToken);
        return new YieldShutdownScenarioRes("yield.e3-shutdown-unexpected-completion", request.SpotRid, evidence.Evidence);
    }

    private static async Task<YieldShutdownRecoveryRes> RunShutdownRecoveryThroughSpotRouteAsync(
        IZLinkRouteClient routes,
        IZLinkSpotRefResolver spots,
        YieldShutdownRecoveryReq request,
        CancellationToken cancellationToken)
    {
        await RequestPlayControlWithRetryAsync<EnsureSpotRes>(
            routes,
            new EnsureSpotReq(request.SpotRid),
            "EnsureSpotReq",
            cancellationToken);
        await RequestSpotWithRetryAsync<YieldDispatchRes>(
            routes,
            spots,
            request.SpotRid,
            new ProbeReq(request.RequestId, "shutdown-recovery-probe"),
            "ProbeReq",
            cancellationToken);
        await RequestPlayControlWithRetryAsync<YieldEvidenceRes>(
            routes,
            new YieldEvidenceWaitReq(request.RequestId, "probe-completed"),
            "YieldEvidenceWaitReq",
            cancellationToken);

        var evidence = await RequestPlayControlWithRetryAsync<YieldEvidenceRes>(
            routes,
            new YieldEvidenceReq(request.RequestId),
            "YieldEvidenceReq",
            cancellationToken);
        Ensure(
            evidence.Evidence.Any(line =>
                line.Contains("probe-completed", StringComparison.Ordinal)
                && line.Contains($"rid=play-a|spot={request.SpotRid}", StringComparison.Ordinal)
                && line.Contains("marker=shutdown-recovery-probe", StringComparison.Ordinal)),
            "YD-E3 recovery probe marker missing.");
        return new YieldShutdownRecoveryRes("yield.e3-shutdown-recovery", request.SpotRid, evidence.Evidence);
    }
}
