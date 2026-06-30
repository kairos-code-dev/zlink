using YieldDispatch.Server.Play.Spots;
using YieldDispatch.Shared;
using Zlink.Framework.Contracts.Handlers;
using Zlink.Framework.Contracts.Spots;

namespace YieldDispatch.Server.Play.Handlers;

[ZLinkSpotRequestHandler("HoldReq")]
internal sealed class HoldHandler(EvidenceStore evidence)
    : IZLinkSpotRequestHandler<YieldProbeSpot, HoldReq, YieldDispatchRes>
{
    public async ValueTask<YieldDispatchRes> HandleAsync(
        YieldProbeSpot spot,
        HoldReq request,
        CancellationToken cancellationToken)
    {
        evidence.Add(
            $"hold-started|rid={evidence.Rid}|spot={spot.Context.SpotRid}|request={request.RequestId}|handler=spot");
        await spot.Context.Outbound.RequestToChannel(
                YieldDispatchNames.DelayChannel,
                new DelayReq(request.RequestId, request.DelayMs, "hold"))
            .PacketName("DelayReq")
            .Timeout(TimeSpan.FromSeconds(5))
            .Async<DelayRes>(cancellationToken);
        evidence.Add(
            $"hold-resumed|rid={evidence.Rid}|spot={spot.Context.SpotRid}|request={request.RequestId}|handler=spot");
        evidence.Add(
            $"hold-completed|rid={evidence.Rid}|spot={spot.Context.SpotRid}|request={request.RequestId}|handler=spot");
        return YieldReplies.Reply("YD-A1", request.RequestId, spot, "hold-completed");
    }
}

[ZLinkSpotPacketHandler("HoldMsg")]
internal sealed class HoldCommandHandler(EvidenceStore evidence)
    : IZLinkSpotPacketHandler<YieldProbeSpot, HoldMsg>
{
    public async ValueTask HandleAsync(
        YieldProbeSpot spot,
        HoldMsg request,
        CancellationToken cancellationToken)
    {
        evidence.Add(
            $"hold-started|rid={evidence.Rid}|spot={spot.Context.SpotRid}|request={request.RequestId}|handler=spot");
        await spot.Context.Outbound.RequestToChannel(
                YieldDispatchNames.DelayChannel,
                new DelayReq(request.RequestId, request.DelayMs, "hold"))
            .PacketName("DelayReq")
            .Timeout(TimeSpan.FromSeconds(5))
            .Async<DelayRes>(cancellationToken);
        evidence.Add(
            $"hold-resumed|rid={evidence.Rid}|spot={spot.Context.SpotRid}|request={request.RequestId}|handler=spot");
        evidence.Add(
            $"hold-completed|rid={evidence.Rid}|spot={spot.Context.SpotRid}|request={request.RequestId}|handler=spot");
    }
}

[ZLinkSpotRequestHandler("YieldReq")]
internal sealed class YieldHandler(EvidenceStore evidence)
    : IZLinkSpotRequestHandler<YieldProbeSpot, YieldReq, YieldDispatchRes>
{
    public async ValueTask<YieldDispatchRes> HandleAsync(
        YieldProbeSpot spot,
        YieldReq request,
        CancellationToken cancellationToken)
    {
        evidence.Add(
            $"yield-started|rid={evidence.Rid}|spot={spot.Context.SpotRid}|request={request.RequestId}"
            + $"|correlation={request.CorrelationId}|handler=spot");
        var call = spot.Context.Outbound.RequestToChannel(
                YieldDispatchNames.DelayChannel,
                new DelayReq(request.RequestId, request.DelayMs, "yield"))
            .PacketName("DelayReq")
            .Timeout(TimeSpan.FromSeconds(5));
        evidence.Add(
            $"yield-released|rid={evidence.Rid}|spot={spot.Context.SpotRid}|request={request.RequestId}"
            + $"|correlation={request.CorrelationId}|handler=spot");
        await call.Yield<DelayRes>(cancellationToken);
        evidence.Add(
            $"yield-resumed|rid={evidence.Rid}|spot={spot.Context.SpotRid}|request={request.RequestId}"
            + $"|correlation={request.CorrelationId}|handler=spot");
        evidence.Add(
            $"yield-completed|rid={evidence.Rid}|spot={spot.Context.SpotRid}|request={request.RequestId}"
            + $"|correlation={request.CorrelationId}|handler=spot");
        return YieldReplies.Reply("YD-A2", request.RequestId, spot, "yield-completed");
    }
}

[ZLinkSpotPacketHandler("YieldMsg")]
internal sealed class YieldCommandHandler(EvidenceStore evidence)
    : IZLinkSpotPacketHandler<YieldProbeSpot, YieldMsg>
{
    public async ValueTask HandleAsync(
        YieldProbeSpot spot,
        YieldMsg request,
        CancellationToken cancellationToken)
    {
        evidence.Add(
            $"yield-started|rid={evidence.Rid}|spot={spot.Context.SpotRid}|request={request.RequestId}"
            + $"|correlation={request.CorrelationId}|handler=spot");
        var call = spot.Context.Outbound.RequestToChannel(
                YieldDispatchNames.DelayChannel,
                new DelayReq(request.RequestId, request.DelayMs, "yield"))
            .PacketName("DelayReq")
            .Timeout(TimeSpan.FromSeconds(5));
        evidence.Add(
            $"yield-released|rid={evidence.Rid}|spot={spot.Context.SpotRid}|request={request.RequestId}"
            + $"|correlation={request.CorrelationId}|handler=spot");
        await call.Yield<DelayRes>(cancellationToken);
        evidence.Add(
            $"yield-resumed|rid={evidence.Rid}|spot={spot.Context.SpotRid}|request={request.RequestId}"
            + $"|correlation={request.CorrelationId}|handler=spot");
        evidence.Add(
            $"yield-completed|rid={evidence.Rid}|spot={spot.Context.SpotRid}|request={request.RequestId}"
            + $"|correlation={request.CorrelationId}|handler=spot");
    }
}

[ZLinkSpotRequestHandler("WorkerYieldReq")]
internal sealed class WorkerYieldHandler(EvidenceStore evidence)
    : IZLinkSpotRequestHandler<YieldProbeSpot, WorkerYieldReq, YieldDispatchRes>
{
    public async ValueTask<YieldDispatchRes> HandleAsync(
        YieldProbeSpot spot,
        WorkerYieldReq request,
        CancellationToken cancellationToken)
    {
        evidence.Add(
            $"worker-yield-started|rid={evidence.Rid}|spot={spot.Context.SpotRid}|request={request.RequestId}|handler=spot");
        var call = spot.Context.RunWorker(ct =>
        {
            ct.ThrowIfCancellationRequested();
            Thread.Sleep(TimeSpan.FromMilliseconds(request.DelayMs));
            return request.RequestId;
        });
        evidence.Add(
            $"worker-yield-released|rid={evidence.Rid}|spot={spot.Context.SpotRid}|request={request.RequestId}|handler=spot");
        await call.Yield(cancellationToken);
        evidence.Add(
            $"worker-yield-resumed|rid={evidence.Rid}|spot={spot.Context.SpotRid}|request={request.RequestId}|handler=spot");
        evidence.Add(
            $"worker-yield-completed|rid={evidence.Rid}|spot={spot.Context.SpotRid}|request={request.RequestId}|handler=spot");
        return YieldReplies.Reply("YD-A4", request.RequestId, spot, "worker-yield-completed");
    }
}

[ZLinkSpotPacketHandler("WorkerYieldMsg")]
internal sealed class WorkerYieldCommandHandler(EvidenceStore evidence)
    : IZLinkSpotPacketHandler<YieldProbeSpot, WorkerYieldMsg>
{
    public async ValueTask HandleAsync(
        YieldProbeSpot spot,
        WorkerYieldMsg request,
        CancellationToken cancellationToken)
    {
        evidence.Add(
            $"worker-yield-started|rid={evidence.Rid}|spot={spot.Context.SpotRid}|request={request.RequestId}|handler=spot");
        var call = spot.Context.RunWorker(ct =>
        {
            ct.ThrowIfCancellationRequested();
            Thread.Sleep(TimeSpan.FromMilliseconds(request.DelayMs));
            return request.RequestId;
        });
        evidence.Add(
            $"worker-yield-released|rid={evidence.Rid}|spot={spot.Context.SpotRid}|request={request.RequestId}|handler=spot");
        await call.Yield(cancellationToken);
        evidence.Add(
            $"worker-yield-resumed|rid={evidence.Rid}|spot={spot.Context.SpotRid}|request={request.RequestId}|handler=spot");
        evidence.Add(
            $"worker-yield-completed|rid={evidence.Rid}|spot={spot.Context.SpotRid}|request={request.RequestId}|handler=spot");
    }
}

[ZLinkSpotRequestHandler("ProbeReq")]
internal sealed class ProbeHandler(EvidenceStore evidence)
    : IZLinkSpotRequestHandler<YieldProbeSpot, ProbeReq, YieldDispatchRes>
{
    public ValueTask<YieldDispatchRes> HandleAsync(
        YieldProbeSpot spot,
        ProbeReq request,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        evidence.Add(
            $"probe-started|rid={evidence.Rid}|spot={spot.Context.SpotRid}|request={request.RequestId}"
            + $"|marker={request.Marker}|handler=spot");
        evidence.Add(
            $"probe-completed|rid={evidence.Rid}|spot={spot.Context.SpotRid}|request={request.RequestId}"
            + $"|marker={request.Marker}|handler=spot");
        return ValueTask.FromResult(YieldReplies.Reply("YD-PROBE", request.RequestId, spot, request.Marker));
    }
}

[ZLinkSpotPacketHandler("ProbeMsg")]
internal sealed class ProbeCommandHandler(EvidenceStore evidence)
    : IZLinkSpotPacketHandler<YieldProbeSpot, ProbeMsg>
{
    public ValueTask HandleAsync(
        YieldProbeSpot spot,
        ProbeMsg request,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        evidence.Add(
            $"probe-started|rid={evidence.Rid}|spot={spot.Context.SpotRid}|request={request.RequestId}"
            + $"|marker={request.Marker}|handler=spot");
        evidence.Add(
            $"probe-completed|rid={evidence.Rid}|spot={spot.Context.SpotRid}|request={request.RequestId}"
            + $"|marker={request.Marker}|handler=spot");
        return ValueTask.CompletedTask;
    }
}