using AutomaticTurnDispatch.Server.Play.Spots;
using AutomaticTurnDispatch.Shared;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Errors;
using Zlink.Framework.Contracts.Handlers;
using Zlink.Framework.Contracts.Locations;
using Zlink.Framework.Contracts.Spots;

namespace AutomaticTurnDispatch.Server.Play.Handlers;

[ZLinkSpotPacketHandler("SelfCycleMsg")]
internal sealed class SelfCycleHandler(
    EvidenceStore evidence,
    IZLinkSpotHandleResolver spots)
    : IZLinkSpotPacketHandler<AwaitProbeSpot, SelfCycleMsg>
{
    public async ValueTask HandleAsync(
        AwaitProbeSpot spot,
        SelfCycleMsg request,
        CancellationToken cancellationToken)
    {
        evidence.Add(
            $"self-cycle-started|rid={evidence.Rid}|spot={spot.Context.SpotRid}|request={request.RequestId}");
        try
        {
            var handle = await spots.ResolveSpotHandleAsync(
                             spot.Context.MeshName,
                             spot.Context.SpotRid,
                             cancellationToken)
                         ?? throw new InvalidOperationException("The current Spot handle was not found.");
            await spot.Context.Outbound.RequestToSpot(handle, new ProbeReq(request.RequestId, "self-cycle"))
                .Timeout(TimeSpan.FromMilliseconds(request.TimeoutMs))
                .Async<AutomaticTurnDispatchRes>(cancellationToken);
            evidence.Add(
                $"self-cycle-unexpected-completed|rid={evidence.Rid}|spot={spot.Context.SpotRid}|request={request.RequestId}");
        }
        catch (Exception ex) when (ex is TimeoutException or ZLinkFrameworkException)
        {
            evidence.Add(
                $"self-cycle-timed-out|rid={evidence.Rid}|spot={spot.Context.SpotRid}|request={request.RequestId}"
                + $"|error={ex.GetType().Name}");
        }
    }
}

[ZLinkSpotRequestHandler("AwaitTimeoutReq")]
internal sealed class AwaitTimeoutHandler(
    EvidenceStore evidence,
    IZLinkRouteClient routeClient)
    : IZLinkSpotRequestHandler<AwaitProbeSpot, AwaitTimeoutReq, AwaitTimeoutRes>
{
    public async ValueTask<AwaitTimeoutRes> HandleAsync(
        AwaitProbeSpot spot,
        AwaitTimeoutReq request,
        CancellationToken cancellationToken)
    {
        evidence.Add(
            $"timeout-await-started|rid={evidence.Rid}|spot={spot.Context.SpotRid}|request={request.RequestId}|handler=spot");
        try
        {
            var call = routeClient.RequestToChannel(
                    AutomaticTurnDispatchNames.DelayChannel,
                    AutomaticTurnDispatchNames.DelayChannel,
                    new DelayReq(request.RequestId, request.DelayMs, "timeout"))
                .Timeout(TimeSpan.FromMilliseconds(request.TimeoutMs));
            evidence.Add(
                $"timeout-await-released|rid={evidence.Rid}|spot={spot.Context.SpotRid}|request={request.RequestId}|handler=spot");
            await call.Async<DelayRes>(cancellationToken);
            evidence.Add(
                $"timeout-await-unexpected-resumed|rid={evidence.Rid}|spot={spot.Context.SpotRid}|request={request.RequestId}|handler=spot");
            return new AwaitTimeoutRes("probe-E1", request.RequestId, spot.Context.SpotRid.ToString(),
                spot.Context.NodeRid.ToString(), false, "");
        }
        catch (Exception ex) when (ex is TimeoutException or ZLinkFrameworkException)
        {
            evidence.Add(
                $"timeout-await-completed|rid={evidence.Rid}|spot={spot.Context.SpotRid}|request={request.RequestId}"
                + $"|error={ex.GetType().Name}|handler=spot");
            return new AwaitTimeoutRes(
                "probe-E1",
                request.RequestId,
                spot.Context.SpotRid.ToString(),
                spot.Context.NodeRid.ToString(),
                true,
                ex.GetType().Name);
        }
    }
}

[ZLinkSpotPacketHandler("AwaitTimeoutMsg")]
internal sealed class AwaitTimeoutCommandHandler(
    EvidenceStore evidence,
    IZLinkRouteClient routeClient)
    : IZLinkSpotPacketHandler<AwaitProbeSpot, AwaitTimeoutMsg>
{
    public async ValueTask HandleAsync(
        AwaitProbeSpot spot,
        AwaitTimeoutMsg request,
        CancellationToken cancellationToken)
    {
        evidence.Add(
            $"timeout-await-started|rid={evidence.Rid}|spot={spot.Context.SpotRid}|request={request.RequestId}|handler=spot");
        try
        {
            var call = routeClient.RequestToChannel(
                    AutomaticTurnDispatchNames.DelayChannel,
                    AutomaticTurnDispatchNames.DelayChannel,
                    new DelayReq(request.RequestId, request.DelayMs, "timeout"))
                .Timeout(TimeSpan.FromMilliseconds(request.TimeoutMs));
            evidence.Add(
                $"timeout-await-released|rid={evidence.Rid}|spot={spot.Context.SpotRid}|request={request.RequestId}|handler=spot");
            await call.Async<DelayRes>(cancellationToken);
            evidence.Add(
                $"timeout-await-unexpected-resumed|rid={evidence.Rid}|spot={spot.Context.SpotRid}|request={request.RequestId}|handler=spot");
        }
        catch (Exception ex) when (ex is TimeoutException or ZLinkFrameworkException)
        {
            evidence.Add(
                $"timeout-await-completed|rid={evidence.Rid}|spot={spot.Context.SpotRid}|request={request.RequestId}"
                + $"|error={ex.GetType().Name}|handler=spot");
        }
    }
}

[ZLinkSpotRequestHandler("AwaitCancelReq")]
internal sealed class AwaitCancelHandler(
    EvidenceStore evidence,
    IZLinkRouteClient routeClient)
    : IZLinkSpotRequestHandler<AwaitProbeSpot, AwaitCancelReq, AwaitCancelRes>
{
    public async ValueTask<AwaitCancelRes> HandleAsync(
        AwaitProbeSpot spot,
        AwaitCancelReq request,
        CancellationToken cancellationToken)
    {
        evidence.Add(
            $"cancel-await-started|rid={evidence.Rid}|spot={spot.Context.SpotRid}|request={request.RequestId}|handler=spot");
        using var cts = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        cts.CancelAfter(TimeSpan.FromMilliseconds(request.CancelAfterMs));
        try
        {
            var call = routeClient.RequestToChannel(
                    AutomaticTurnDispatchNames.DelayChannel,
                    AutomaticTurnDispatchNames.DelayChannel,
                    new DelayReq(request.RequestId, request.DelayMs, "cancel"))
                .Timeout(TimeSpan.FromSeconds(5));
            evidence.Add(
                $"cancel-await-released|rid={evidence.Rid}|spot={spot.Context.SpotRid}|request={request.RequestId}|handler=spot");
            await call.Async<DelayRes>(cts.Token);
            evidence.Add(
                $"cancel-await-unexpected-resumed|rid={evidence.Rid}|spot={spot.Context.SpotRid}|request={request.RequestId}|handler=spot");
            return new AwaitCancelRes("probe-E2", request.RequestId, spot.Context.SpotRid.ToString(),
                spot.Context.NodeRid.ToString(), false, "");
        }
        catch (Exception ex) when (ex is OperationCanceledException or ZLinkFrameworkException)
        {
            evidence.Add(
                $"cancel-await-completed|rid={evidence.Rid}|spot={spot.Context.SpotRid}|request={request.RequestId}"
                + $"|error={ex.GetType().Name}|handler=spot");
            return new AwaitCancelRes(
                "probe-E2",
                request.RequestId,
                spot.Context.SpotRid.ToString(),
                spot.Context.NodeRid.ToString(),
                true,
                ex.GetType().Name);
        }
    }
}

[ZLinkSpotPacketHandler("AwaitCancelMsg")]
internal sealed class AwaitCancelCommandHandler(
    EvidenceStore evidence,
    IZLinkRouteClient routeClient)
    : IZLinkSpotPacketHandler<AwaitProbeSpot, AwaitCancelMsg>
{
    public async ValueTask HandleAsync(
        AwaitProbeSpot spot,
        AwaitCancelMsg request,
        CancellationToken cancellationToken)
    {
        evidence.Add(
            $"cancel-await-started|rid={evidence.Rid}|spot={spot.Context.SpotRid}|request={request.RequestId}|handler=spot");
        using var cts = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        cts.CancelAfter(TimeSpan.FromMilliseconds(request.CancelAfterMs));
        try
        {
            var call = routeClient.RequestToChannel(
                    AutomaticTurnDispatchNames.DelayChannel,
                    AutomaticTurnDispatchNames.DelayChannel,
                    new DelayReq(request.RequestId, request.DelayMs, "cancel"))
                .Timeout(TimeSpan.FromSeconds(5));
            evidence.Add(
                $"cancel-await-released|rid={evidence.Rid}|spot={spot.Context.SpotRid}|request={request.RequestId}|handler=spot");
            await call.Async<DelayRes>(cts.Token);
            evidence.Add(
                $"cancel-await-unexpected-resumed|rid={evidence.Rid}|spot={spot.Context.SpotRid}|request={request.RequestId}|handler=spot");
        }
        catch (Exception ex) when (ex is OperationCanceledException or ZLinkFrameworkException)
        {
            evidence.Add(
                $"cancel-await-completed|rid={evidence.Rid}|spot={spot.Context.SpotRid}|request={request.RequestId}"
                + $"|error={ex.GetType().Name}|handler=spot");
        }
    }
}
