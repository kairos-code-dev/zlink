using YieldDispatch.Shared;
using Zlink.Framework.Contracts.Errors;
using Zlink.Framework.Contracts.Handlers;
using Zlink.Framework.Contracts.Spots;

namespace YieldDispatch.Server.Play;

[ZLinkSpotRequestHandler("YieldTimeoutReq")]
internal sealed class YieldTimeoutHandler(EvidenceStore evidence)
    : IZLinkSpotRequestHandler<YieldProbeSpot, YieldTimeoutReq, YieldTimeoutReply>
{
    public async ValueTask<YieldTimeoutReply> HandleAsync(
        YieldProbeSpot spot,
        YieldTimeoutReq request,
        CancellationToken cancellationToken)
    {
        evidence.Add($"timeout-yield-started|rid={evidence.Rid}|spot={spot.Context.SpotRid}|request={request.RequestId}|handler=spot");
        try
        {
            var call = spot.Context.Outbound.RequestToChannel(
                    YieldDispatchNames.DelayChannel,
                    new DelayReq(request.RequestId, request.DelayMs, "timeout"))
                .PacketName("DelayReq")
                .Timeout(TimeSpan.FromMilliseconds(request.TimeoutMs));
            evidence.Add($"timeout-yield-released|rid={evidence.Rid}|spot={spot.Context.SpotRid}|request={request.RequestId}|handler=spot");
            await call.Yield<DelayReply>(cancellationToken);
            evidence.Add($"timeout-yield-unexpected-resumed|rid={evidence.Rid}|spot={spot.Context.SpotRid}|request={request.RequestId}|handler=spot");
            return new YieldTimeoutReply("YD-E1", request.RequestId, spot.Context.SpotRid.ToString(), spot.Context.NodeRid.ToString(), false, "");
        }
        catch (Exception ex) when (ex is TimeoutException or ZLinkFrameworkException)
        {
            evidence.Add(
                $"timeout-yield-completed|rid={evidence.Rid}|spot={spot.Context.SpotRid}|request={request.RequestId}"
                + $"|error={ex.GetType().Name}|handler=spot");
            return new YieldTimeoutReply(
                "YD-E1",
                request.RequestId,
                spot.Context.SpotRid.ToString(),
                spot.Context.NodeRid.ToString(),
                true,
                ex.GetType().Name);
        }
    }
}

[ZLinkSpotPacketHandler("YieldTimeoutCommand")]
internal sealed class YieldTimeoutCommandHandler(EvidenceStore evidence)
    : IZLinkSpotPacketHandler<YieldProbeSpot, YieldTimeoutCommand>
{
    public async ValueTask HandleAsync(
        YieldProbeSpot spot,
        YieldTimeoutCommand request,
        CancellationToken cancellationToken)
    {
        evidence.Add($"timeout-yield-started|rid={evidence.Rid}|spot={spot.Context.SpotRid}|request={request.RequestId}|handler=spot");
        try
        {
            var call = spot.Context.Outbound.RequestToChannel(
                    YieldDispatchNames.DelayChannel,
                    new DelayReq(request.RequestId, request.DelayMs, "timeout"))
                .PacketName("DelayReq")
                .Timeout(TimeSpan.FromMilliseconds(request.TimeoutMs));
            evidence.Add($"timeout-yield-released|rid={evidence.Rid}|spot={spot.Context.SpotRid}|request={request.RequestId}|handler=spot");
            await call.Yield<DelayReply>(cancellationToken);
            evidence.Add($"timeout-yield-unexpected-resumed|rid={evidence.Rid}|spot={spot.Context.SpotRid}|request={request.RequestId}|handler=spot");
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
    : IZLinkSpotRequestHandler<YieldProbeSpot, YieldCancelReq, YieldCancelReply>
{
    public async ValueTask<YieldCancelReply> HandleAsync(
        YieldProbeSpot spot,
        YieldCancelReq request,
        CancellationToken cancellationToken)
    {
        evidence.Add($"cancel-yield-started|rid={evidence.Rid}|spot={spot.Context.SpotRid}|request={request.RequestId}|handler=spot");
        using var cts = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        cts.CancelAfter(TimeSpan.FromMilliseconds(request.CancelAfterMs));
        try
        {
            var call = spot.Context.Outbound.RequestToChannel(
                    YieldDispatchNames.DelayChannel,
                    new DelayReq(request.RequestId, request.DelayMs, "cancel"))
                .PacketName("DelayReq")
                .Timeout(TimeSpan.FromSeconds(5));
            evidence.Add($"cancel-yield-released|rid={evidence.Rid}|spot={spot.Context.SpotRid}|request={request.RequestId}|handler=spot");
            await call.Yield<DelayReply>(cts.Token);
            evidence.Add($"cancel-yield-unexpected-resumed|rid={evidence.Rid}|spot={spot.Context.SpotRid}|request={request.RequestId}|handler=spot");
            return new YieldCancelReply("YD-E2", request.RequestId, spot.Context.SpotRid.ToString(), spot.Context.NodeRid.ToString(), false, "");
        }
        catch (Exception ex) when (ex is OperationCanceledException or ZLinkFrameworkException)
        {
            evidence.Add(
                $"cancel-yield-completed|rid={evidence.Rid}|spot={spot.Context.SpotRid}|request={request.RequestId}"
                + $"|error={ex.GetType().Name}|handler=spot");
            return new YieldCancelReply(
                "YD-E2",
                request.RequestId,
                spot.Context.SpotRid.ToString(),
                spot.Context.NodeRid.ToString(),
                true,
                ex.GetType().Name);
        }
    }
}

[ZLinkSpotPacketHandler("YieldCancelCommand")]
internal sealed class YieldCancelCommandHandler(EvidenceStore evidence)
    : IZLinkSpotPacketHandler<YieldProbeSpot, YieldCancelCommand>
{
    public async ValueTask HandleAsync(
        YieldProbeSpot spot,
        YieldCancelCommand request,
        CancellationToken cancellationToken)
    {
        evidence.Add($"cancel-yield-started|rid={evidence.Rid}|spot={spot.Context.SpotRid}|request={request.RequestId}|handler=spot");
        using var cts = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        cts.CancelAfter(TimeSpan.FromMilliseconds(request.CancelAfterMs));
        try
        {
            var call = spot.Context.Outbound.RequestToChannel(
                    YieldDispatchNames.DelayChannel,
                    new DelayReq(request.RequestId, request.DelayMs, "cancel"))
                .PacketName("DelayReq")
                .Timeout(TimeSpan.FromSeconds(5));
            evidence.Add($"cancel-yield-released|rid={evidence.Rid}|spot={spot.Context.SpotRid}|request={request.RequestId}|handler=spot");
            await call.Yield<DelayReply>(cts.Token);
            evidence.Add($"cancel-yield-unexpected-resumed|rid={evidence.Rid}|spot={spot.Context.SpotRid}|request={request.RequestId}|handler=spot");
        }
        catch (Exception ex) when (ex is OperationCanceledException or ZLinkFrameworkException)
        {
            evidence.Add(
                $"cancel-yield-completed|rid={evidence.Rid}|spot={spot.Context.SpotRid}|request={request.RequestId}"
                + $"|error={ex.GetType().Name}|handler=spot");
        }
    }
}
