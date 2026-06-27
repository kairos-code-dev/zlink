using YieldDispatch.Shared;
using Zlink.Framework.Contracts.Channels;

namespace YieldDispatch.Server.Session;

internal sealed partial class YieldSession
{
    private static async Task<YieldScenarioResult> RunShutdownThroughSpotRouteAsync(
        IZLinkRouteClient routes,
        YieldShutdownScenarioReq request,
        CancellationToken cancellationToken)
    {
        await RequestPlayControlWithRetryAsync<EnsureSpotReply>(
            routes,
            new EnsureSpotReq(request.SpotRid),
            "EnsureSpotReq",
            cancellationToken);
        await RequestSpotWithRetryAsync<YieldDispatchReply>(
            routes,
            request.SpotRid,
            new YieldReq(request.RequestId, request.DelayMs, "shutdown"),
            "YieldReq",
            cancellationToken);

        var evidence = await RequestPlayControlWithRetryAsync<YieldEvidenceReply>(
            routes,
            new YieldEvidenceReq(request.RequestId),
            "YieldEvidenceReq",
            cancellationToken);
        return new YieldScenarioResult("yield.e3-shutdown-unexpected-completion", request.SpotRid, evidence.Evidence);
    }

    private static async Task<YieldScenarioResult> RunShutdownRecoveryThroughSpotRouteAsync(
        IZLinkRouteClient routes,
        YieldShutdownRecoveryReq request,
        CancellationToken cancellationToken)
    {
        await RequestPlayControlWithRetryAsync<EnsureSpotReply>(
            routes,
            new EnsureSpotReq(request.SpotRid),
            "EnsureSpotReq",
            cancellationToken);
        await SendSpotWithRetryAsync(
            routes,
            request.SpotRid,
            new ProbeCommand(request.RequestId, "shutdown-recovery-probe"),
            "ProbeCommand",
            cancellationToken);
        await RequestPlayControlWithRetryAsync<YieldEvidenceReply>(
            routes,
            new YieldEvidenceWaitReq(request.RequestId, "probe-completed"),
            "YieldEvidenceWaitReq",
            cancellationToken);

        var evidence = await RequestPlayControlWithRetryAsync<YieldEvidenceReply>(
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
        return new YieldScenarioResult("yield.e3-shutdown-recovery", request.SpotRid, evidence.Evidence);
    }
}
