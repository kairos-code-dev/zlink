using System.Diagnostics;
using Systems.Zlink;
using AutomaticTurnDispatch.Shared;
using Systems.Zlink.Stream.Connector.Contracts;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Errors;
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
                          AutomaticTurnDispatchNames.SpotChannel,
                          RoutingId.From(request.SpotRid),
                          cancellationToken)
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
        var handle = await WaitForRecoveredSpotAsync(
            spots,
            request.SpotRid,
            cancellationToken);
        await spotsClient.RequestToSpot(
                handle,
                new ProbeReq(request.RequestId, "shutdown-recovery-probe"))
            .Timeout(TimeSpan.FromSeconds(5))
            .Async<AutomaticTurnDispatchRes>(cancellationToken);
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

    private static async ValueTask<SpotHandle> WaitForRecoveredSpotAsync(
        IZLinkSpotHandleResolver spots,
        string spotRid,
        CancellationToken cancellationToken)
    {
        var deadline = Stopwatch.StartNew();
        while (deadline.Elapsed < TimeSpan.FromSeconds(3))
        {
            var handle = await spots.ResolveSpotHandleAsync(
                AutomaticTurnDispatchNames.SpotChannel,
                RoutingId.From(spotRid),
                cancellationToken);
            if (handle is not null)
                return handle;
            await Task.Delay(TimeSpan.FromMilliseconds(50), cancellationToken);
        }

        throw new ZLinkFrameworkException(
            ZLinkFrameworkErrorKind.SpotRouteNotFound,
            $"Spot '{spotRid}' did not become live within 3 seconds after recovery.");
    }
}
