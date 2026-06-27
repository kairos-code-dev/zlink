using YieldDispatch.Shared;
using Zlink.Framework.Contracts.Handlers;
using Zlink.Framework.Contracts.Spots;
using YieldDispatch.Server.Play.Handlers;
using YieldDispatch.Server.Play.Spots;

namespace YieldDispatch.Server.Play.Handlers;

[ZLinkSpotRequestHandler("HoldReq")]
internal sealed class HoldHandler(EvidenceStore evidence)
    : IZLinkSpotRequestHandler<YieldProbeSpot, HoldReq, YieldDispatchReply>
{
    public async ValueTask<YieldDispatchReply> HandleAsync(
        YieldProbeSpot spot,
        HoldReq request,
        CancellationToken cancellationToken)
    {
        evidence.Add($"hold-started|rid={evidence.Rid}|spot={spot.Context.SpotRid}|request={request.RequestId}|handler=spot");
        await spot.Context.Outbound.RequestToChannel(
                YieldDispatchNames.DelayChannel,
                new DelayReq(request.RequestId, request.DelayMs, "hold"))
            .PacketName("DelayReq")
            .Timeout(TimeSpan.FromSeconds(5))
            .Async<DelayReply>(cancellationToken);
        evidence.Add($"hold-resumed|rid={evidence.Rid}|spot={spot.Context.SpotRid}|request={request.RequestId}|handler=spot");
        evidence.Add($"hold-completed|rid={evidence.Rid}|spot={spot.Context.SpotRid}|request={request.RequestId}|handler=spot");
        return YieldReplies.Reply("YD-A1", request.RequestId, spot, "hold-completed");
    }
}

[ZLinkSpotPacketHandler("HoldCommand")]
internal sealed class HoldCommandHandler(EvidenceStore evidence)
    : IZLinkSpotPacketHandler<YieldProbeSpot, HoldCommand>
{
    public async ValueTask HandleAsync(
        YieldProbeSpot spot,
        HoldCommand request,
        CancellationToken cancellationToken)
    {
        evidence.Add($"hold-started|rid={evidence.Rid}|spot={spot.Context.SpotRid}|request={request.RequestId}|handler=spot");
        await spot.Context.Outbound.RequestToChannel(
                YieldDispatchNames.DelayChannel,
                new DelayReq(request.RequestId, request.DelayMs, "hold"))
            .PacketName("DelayReq")
            .Timeout(TimeSpan.FromSeconds(5))
            .Async<DelayReply>(cancellationToken);
        evidence.Add($"hold-resumed|rid={evidence.Rid}|spot={spot.Context.SpotRid}|request={request.RequestId}|handler=spot");
        evidence.Add($"hold-completed|rid={evidence.Rid}|spot={spot.Context.SpotRid}|request={request.RequestId}|handler=spot");
    }
}

[ZLinkSpotRequestHandler("YieldReq")]
internal sealed class YieldHandler(EvidenceStore evidence)
    : IZLinkSpotRequestHandler<YieldProbeSpot, YieldReq, YieldDispatchReply>
{
    public async ValueTask<YieldDispatchReply> HandleAsync(
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
        await call.Yield<DelayReply>(cancellationToken);
        evidence.Add(
            $"yield-resumed|rid={evidence.Rid}|spot={spot.Context.SpotRid}|request={request.RequestId}"
            + $"|correlation={request.CorrelationId}|handler=spot");
        evidence.Add(
            $"yield-completed|rid={evidence.Rid}|spot={spot.Context.SpotRid}|request={request.RequestId}"
            + $"|correlation={request.CorrelationId}|handler=spot");
        return YieldReplies.Reply("YD-A2", request.RequestId, spot, "yield-completed");
    }
}

[ZLinkSpotPacketHandler("YieldCommand")]
internal sealed class YieldCommandHandler(EvidenceStore evidence)
    : IZLinkSpotPacketHandler<YieldProbeSpot, YieldCommand>
{
    public async ValueTask HandleAsync(
        YieldProbeSpot spot,
        YieldCommand request,
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
        await call.Yield<DelayReply>(cancellationToken);
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
    : IZLinkSpotRequestHandler<YieldProbeSpot, WorkerYieldReq, YieldDispatchReply>
{
    public async ValueTask<YieldDispatchReply> HandleAsync(
        YieldProbeSpot spot,
        WorkerYieldReq request,
        CancellationToken cancellationToken)
    {
        evidence.Add($"worker-yield-started|rid={evidence.Rid}|spot={spot.Context.SpotRid}|request={request.RequestId}|handler=spot");
        var call = spot.Context.RunWorker(ct =>
        {
            ct.ThrowIfCancellationRequested();
            Thread.Sleep(TimeSpan.FromMilliseconds(request.DelayMs));
            return request.RequestId;
        });
        evidence.Add($"worker-yield-released|rid={evidence.Rid}|spot={spot.Context.SpotRid}|request={request.RequestId}|handler=spot");
        await call.Yield(cancellationToken);
        evidence.Add($"worker-yield-resumed|rid={evidence.Rid}|spot={spot.Context.SpotRid}|request={request.RequestId}|handler=spot");
        evidence.Add($"worker-yield-completed|rid={evidence.Rid}|spot={spot.Context.SpotRid}|request={request.RequestId}|handler=spot");
        return YieldReplies.Reply("YD-A4", request.RequestId, spot, "worker-yield-completed");
    }
}

[ZLinkSpotPacketHandler("WorkerYieldCommand")]
internal sealed class WorkerYieldCommandHandler(EvidenceStore evidence)
    : IZLinkSpotPacketHandler<YieldProbeSpot, WorkerYieldCommand>
{
    public async ValueTask HandleAsync(
        YieldProbeSpot spot,
        WorkerYieldCommand request,
        CancellationToken cancellationToken)
    {
        evidence.Add($"worker-yield-started|rid={evidence.Rid}|spot={spot.Context.SpotRid}|request={request.RequestId}|handler=spot");
        var call = spot.Context.RunWorker(ct =>
        {
            ct.ThrowIfCancellationRequested();
            Thread.Sleep(TimeSpan.FromMilliseconds(request.DelayMs));
            return request.RequestId;
        });
        evidence.Add($"worker-yield-released|rid={evidence.Rid}|spot={spot.Context.SpotRid}|request={request.RequestId}|handler=spot");
        await call.Yield(cancellationToken);
        evidence.Add($"worker-yield-resumed|rid={evidence.Rid}|spot={spot.Context.SpotRid}|request={request.RequestId}|handler=spot");
        evidence.Add($"worker-yield-completed|rid={evidence.Rid}|spot={spot.Context.SpotRid}|request={request.RequestId}|handler=spot");
    }
}

[ZLinkSpotRequestHandler("ProbeReq")]
internal sealed class ProbeHandler(EvidenceStore evidence)
    : IZLinkSpotRequestHandler<YieldProbeSpot, ProbeReq, YieldDispatchReply>
{
    public ValueTask<YieldDispatchReply> HandleAsync(
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

[ZLinkSpotPacketHandler("ProbeCommand")]
internal sealed class ProbeCommandHandler(EvidenceStore evidence)
    : IZLinkSpotPacketHandler<YieldProbeSpot, ProbeCommand>
{
    public ValueTask HandleAsync(
        YieldProbeSpot spot,
        ProbeCommand request,
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
