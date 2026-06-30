using YieldDispatch.Server.Play.Spots;
using YieldDispatch.Shared;
using Zlink.Framework.Contracts.Errors;
using Zlink.Framework.Contracts.Handlers;
using Zlink.Framework.Contracts.Spots;

namespace YieldDispatch.Server.Play.Handlers;

[ZLinkSpotRequestHandler("YieldTimeoutReq")]
internal sealed class YieldTimeoutHandler(EvidenceStore evidence)
    : IZLinkSpotRequestHandler<YieldProbeSpot, YieldTimeoutReq, YieldTimeoutRes>
{
    public async ValueTask<YieldTimeoutRes> HandleAsync(
        YieldProbeSpot spot,
        YieldTimeoutReq request,
        CancellationToken cancellationToken)
    {
        evidence.Add(
            $"timeout-yield-started|rid={evidence.Rid}|spot={spot.Context.SpotRid}|request={request.RequestId}|handler=spot");
        try
        {
            var call = spot.Context.Outbound.RequestToChannel(
                    YieldDispatchNames.DelayChannel,
                    new DelayReq(request.RequestId, request.DelayMs, "timeout"))
                .PacketName("DelayReq")
                .Timeout(TimeSpan.FromMilliseconds(request.TimeoutMs));
            evidence.Add(
                $"timeout-yield-released|rid={evidence.Rid}|spot={spot.Context.SpotRid}|request={request.RequestId}|handler=spot");
            await call.Yield<DelayRes>(cancellationToken);
            evidence.Add(
                $"timeout-yield-unexpected-resumed|rid={evidence.Rid}|spot={spot.Context.SpotRid}|request={request.RequestId}|handler=spot");
            return new YieldTimeoutRes("YD-E1", request.RequestId, spot.Context.SpotRid.ToString(),
                spot.Context.NodeRid.ToString(), false, "");
        }
        catch (Exception ex) when (ex is TimeoutException or ZLinkFrameworkException)
        {
            evidence.Add(
                $"timeout-yield-completed|rid={evidence.Rid}|spot={spot.Context.SpotRid}|request={request.RequestId}"
                + $"|error={ex.GetType().Name}|handler=spot");
            return new YieldTimeoutRes(
                "YD-E1",
                request.RequestId,
                spot.Context.SpotRid.ToString(),
                spot.Context.NodeRid.ToString(),
                true,
                ex.GetType().Name);
        }
    }
}

[ZLinkSpotPacketHandler("YieldTimeoutMsg")]
internal sealed class YieldTimeoutCommandHandler(EvidenceStore evidence)
    : IZLinkSpotPacketHandler<YieldProbeSpot, YieldTimeoutMsg>
{
    public async ValueTask HandleAsync(
        YieldProbeSpot spot,
        YieldTimeoutMsg request,
        CancellationToken cancellationToken)
    {
        evidence.Add(
            $"timeout-yield-started|rid={evidence.Rid}|spot={spot.Context.SpotRid}|request={request.RequestId}|handler=spot");
        try
        {
            var call = spot.Context.Outbound.RequestToChannel(
                    YieldDispatchNames.DelayChannel,
                    new DelayReq(request.RequestId, request.DelayMs, "timeout"))
                .PacketName("DelayReq")
                .Timeout(TimeSpan.FromMilliseconds(request.TimeoutMs));
            evidence.Add(
                $"timeout-yield-released|rid={evidence.Rid}|spot={spot.Context.SpotRid}|request={request.RequestId}|handler=spot");
            await call.Yield<DelayRes>(cancellationToken);
            evidence.Add(
                $"timeout-yield-unexpected-resumed|rid={evidence.Rid}|spot={spot.Context.SpotRid}|request={request.RequestId}|handler=spot");
        }
        catch (Exception ex) when (ex is TimeoutException or ZLinkFrameworkException)
        {
            evidence.Add(
                $"timeout-yield-completed|rid={evidence.Rid}|spot={spot.Context.SpotRid}|request={request.RequestId}"
                + $"|error={ex.GetType().Name}|handler=spot");
        }
    }
}

[ZLinkSpotRequestHandler("YieldCancelReq")]
internal sealed class YieldCancelHandler(EvidenceStore evidence)
    : IZLinkSpotRequestHandler<YieldProbeSpot, YieldCancelReq, YieldCancelRes>
{
    public async ValueTask<YieldCancelRes> HandleAsync(
        YieldProbeSpot spot,
        YieldCancelReq request,
        CancellationToken cancellationToken)
    {
        evidence.Add(
            $"cancel-yield-started|rid={evidence.Rid}|spot={spot.Context.SpotRid}|request={request.RequestId}|handler=spot");
        using var cts = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        cts.CancelAfter(TimeSpan.FromMilliseconds(request.CancelAfterMs));
        try
        {
            var call = spot.Context.Outbound.RequestToChannel(
                    YieldDispatchNames.DelayChannel,
                    new DelayReq(request.RequestId, request.DelayMs, "cancel"))
                .PacketName("DelayReq")
                .Timeout(TimeSpan.FromSeconds(5));
            evidence.Add(
                $"cancel-yield-released|rid={evidence.Rid}|spot={spot.Context.SpotRid}|request={request.RequestId}|handler=spot");
            await call.Yield<DelayRes>(cts.Token);
            evidence.Add(
                $"cancel-yield-unexpected-resumed|rid={evidence.Rid}|spot={spot.Context.SpotRid}|request={request.RequestId}|handler=spot");
            return new YieldCancelRes("YD-E2", request.RequestId, spot.Context.SpotRid.ToString(),
                spot.Context.NodeRid.ToString(), false, "");
        }
        catch (Exception ex) when (ex is OperationCanceledException or ZLinkFrameworkException)
        {
            evidence.Add(
                $"cancel-yield-completed|rid={evidence.Rid}|spot={spot.Context.SpotRid}|request={request.RequestId}"
                + $"|error={ex.GetType().Name}|handler=spot");
            return new YieldCancelRes(
                "YD-E2",
                request.RequestId,
                spot.Context.SpotRid.ToString(),
                spot.Context.NodeRid.ToString(),
                true,
                ex.GetType().Name);
        }
    }
}

[ZLinkSpotPacketHandler("YieldCancelMsg")]
internal sealed class YieldCancelCommandHandler(EvidenceStore evidence)
    : IZLinkSpotPacketHandler<YieldProbeSpot, YieldCancelMsg>
{
    public async ValueTask HandleAsync(
        YieldProbeSpot spot,
        YieldCancelMsg request,
        CancellationToken cancellationToken)
    {
        evidence.Add(
            $"cancel-yield-started|rid={evidence.Rid}|spot={spot.Context.SpotRid}|request={request.RequestId}|handler=spot");
        using var cts = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        cts.CancelAfter(TimeSpan.FromMilliseconds(request.CancelAfterMs));
        try
        {
            var call = spot.Context.Outbound.RequestToChannel(
                    YieldDispatchNames.DelayChannel,
                    new DelayReq(request.RequestId, request.DelayMs, "cancel"))
                .PacketName("DelayReq")
                .Timeout(TimeSpan.FromSeconds(5));
            evidence.Add(
                $"cancel-yield-released|rid={evidence.Rid}|spot={spot.Context.SpotRid}|request={request.RequestId}|handler=spot");
            await call.Yield<DelayRes>(cts.Token);
            evidence.Add(
                $"cancel-yield-unexpected-resumed|rid={evidence.Rid}|spot={spot.Context.SpotRid}|request={request.RequestId}|handler=spot");
        }
        catch (Exception ex) when (ex is OperationCanceledException or ZLinkFrameworkException)
        {
            evidence.Add(
                $"cancel-yield-completed|rid={evidence.Rid}|spot={spot.Context.SpotRid}|request={request.RequestId}"
                + $"|error={ex.GetType().Name}|handler=spot");
        }
    }
}