using Systems.Zlink;
using AutomaticTurnDispatch.Shared;
using Systems.Zlink.Stream.Connector.Contracts;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Locations;
using Zlink.Framework.Contracts.Spots;

namespace AutomaticTurnDispatch.Server.Session.Support;

internal sealed partial class AwaitSession
{
    private static async Task<AwaitShutdownScenarioRes> RunShutdownThroughSpotRouteAsync(
        IZLinkRouteClient routes,
        IZLinkSpotClient spotsClient,
        IZLinkSpotHandleResolver spots,
        AwaitShutdownScenarioReq request,
        CancellationToken cancellationToken)
    {
        await RequestPlayControlAsync<EnsureSpotRes>(
            routes,
            new EnsureSpotReq(request.SpotRid),
            cancellationToken);
        var address = await spots.ResolveSpotHandleAsync(
                          RoutingId.From(request.SpotRid), cancellationToken)
                      ?? throw new InvalidOperationException(
                          $"Spot '{request.SpotRid}' has no live location row.");
        await spotsClient.RequestToSpot(address,
                new AwaitReq(request.RequestId, request.DelayMs, "shutdown"))
            // The session gateway owns a shorter downstream deadline than the
            // client request, so a stopped play runtime becomes a public
            // remote error instead of a client-side timeout.
            .Timeout(TimeSpan.FromSeconds(10))
            .Async<AutomaticTurnDispatchRes>(cancellationToken);

        var evidence = await RequestPlayControlAsync<AwaitEvidenceRes>(
            routes,
            new AwaitEvidenceReq(request.RequestId),
            cancellationToken);
        return new AwaitShutdownScenarioRes("await.e3-shutdown-unexpected-completion", request.SpotRid, evidence.Evidence);
    }

    private static async Task<AwaitShutdownRecoveryRes> RunShutdownRecoveryThroughSpotRouteAsync(
        IZLinkRouteClient routes,
        IZLinkSpotClient spotsClient,
        IZLinkSpotHandleResolver spots,
        AwaitShutdownRecoveryReq request,
        CancellationToken cancellationToken)
    {
        await RequestPlayControlAsync<EnsureSpotRes>(
            routes,
            new EnsureSpotReq(request.SpotRid),
            cancellationToken);
        await RequestSpotAsync<AutomaticTurnDispatchRes>(
            spotsClient,
            spots,
            request.SpotRid,
            new ProbeReq(request.RequestId, "shutdown-recovery-probe"),
            cancellationToken);
        await RequestPlayControlAsync<AwaitEvidenceRes>(
            routes,
            new AwaitEvidenceWaitReq(request.RequestId, "probe-completed"),
            cancellationToken);

        var evidence = await RequestPlayControlAsync<AwaitEvidenceRes>(
            routes,
            new AwaitEvidenceReq(request.RequestId),
            cancellationToken);
        ZlinkStreamAssert.Ensure(
            evidence.Evidence.Any(line =>
                line.Contains("probe-completed", StringComparison.Ordinal)
                && line.Contains($"rid=play-a|spot={request.SpotRid}", StringComparison.Ordinal)
                && line.Contains("marker=shutdown-recovery-probe", StringComparison.Ordinal)),
            "probe-E3 recovery probe marker missing.");
        return new AwaitShutdownRecoveryRes("await.e3-shutdown-recovery", request.SpotRid, evidence.Evidence);
    }
}
