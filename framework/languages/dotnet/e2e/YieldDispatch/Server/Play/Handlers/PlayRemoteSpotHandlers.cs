using Systems.Zlink;
using YieldDispatch.Server.Play.Spots;
using YieldDispatch.Shared;
using Zlink.Framework.Contracts.Handlers;
using Zlink.Framework.Contracts.Locations;
using Zlink.Framework.Contracts.Spots;

namespace YieldDispatch.Server.Play.Handlers;

[ZLinkSpotRequestHandler("RemoteSpotYieldReq")]
internal sealed class RemoteSpotYieldHandler(
    EvidenceStore evidence,
    IZLinkSpotLocationResolver spots)
    : IZLinkSpotRequestHandler<YieldProbeSpot, RemoteSpotYieldReq, YieldDispatchRes>
{
    public async ValueTask<YieldDispatchRes> HandleAsync(
        YieldProbeSpot spot,
        RemoteSpotYieldReq request,
        CancellationToken cancellationToken)
    {
        evidence.Add(
            $"remote-yield-started|rid={evidence.Rid}|spot={spot.Context.SpotRid}"
            + $"|request={request.RequestId}|target={request.TargetSpotRid}|handler=spot");
        // Resolve once, then message with the held address (spot-address
        // messaging draft §6).
        var target = await spots.ResolveSpotAddressAsync(
                         RoutingId.From(request.TargetSpotRid), cancellationToken)
                     ?? throw new InvalidOperationException(
                         $"Target spot '{request.TargetSpotRid}' has no live address.");
        var call = spot.Context.Outbound.RequestToSpot(
                target,
                new YieldReq(request.RequestId, request.DelayMs, "remote-spot"))
            .PacketName("YieldReq")
            .Timeout(TimeSpan.FromSeconds(5));
        evidence.Add(
            $"remote-yield-released|rid={evidence.Rid}|spot={spot.Context.SpotRid}"
            + $"|request={request.RequestId}|target={request.TargetSpotRid}|handler=spot");
        var targetReply = await call.Yield<YieldDispatchRes>(cancellationToken);
        evidence.Add(
            $"remote-yield-resumed|rid={evidence.Rid}|spot={spot.Context.SpotRid}"
            + $"|request={request.RequestId}|target={request.TargetSpotRid}|targetNode={targetReply.NodeRid}|handler=spot");
        evidence.Add(
            $"remote-yield-completed|rid={evidence.Rid}|spot={spot.Context.SpotRid}"
            + $"|request={request.RequestId}|target={request.TargetSpotRid}|targetNode={targetReply.NodeRid}|handler=spot");
        return YieldReplies.Reply("YD-D2", request.RequestId, spot, "remote-yield-completed");
    }
}

[ZLinkSpotPacketHandler("RemoteSpotYieldMsg")]
internal sealed class RemoteSpotYieldCommandHandler(
    EvidenceStore evidence,
    IZLinkSpotLocationResolver spots)
    : IZLinkSpotPacketHandler<YieldProbeSpot, RemoteSpotYieldMsg>
{
    public async ValueTask HandleAsync(
        YieldProbeSpot spot,
        RemoteSpotYieldMsg request,
        CancellationToken cancellationToken)
    {
        evidence.Add(
            $"remote-yield-started|rid={evidence.Rid}|spot={spot.Context.SpotRid}"
            + $"|request={request.RequestId}|target={request.TargetSpotRid}|handler=spot");
        // Resolve once, then message with the held address (spot-address
        // messaging draft §6).
        var target = await spots.ResolveSpotAddressAsync(
                         RoutingId.From(request.TargetSpotRid), cancellationToken)
                     ?? throw new InvalidOperationException(
                         $"Target spot '{request.TargetSpotRid}' has no live address.");
        var call = spot.Context.Outbound.RequestToSpot(
                target,
                new YieldReq(request.RequestId, request.DelayMs, "remote-spot"))
            .PacketName("YieldReq")
            .Timeout(TimeSpan.FromSeconds(5));
        evidence.Add(
            $"remote-yield-released|rid={evidence.Rid}|spot={spot.Context.SpotRid}"
            + $"|request={request.RequestId}|target={request.TargetSpotRid}|handler=spot");
        var targetReply = await call.Yield<YieldDispatchRes>(cancellationToken);
        evidence.Add(
            $"remote-yield-resumed|rid={evidence.Rid}|spot={spot.Context.SpotRid}"
            + $"|request={request.RequestId}|target={request.TargetSpotRid}|targetNode={targetReply.NodeRid}|handler=spot");
        evidence.Add(
            $"remote-yield-completed|rid={evidence.Rid}|spot={spot.Context.SpotRid}"
            + $"|request={request.RequestId}|target={request.TargetSpotRid}|targetNode={targetReply.NodeRid}|handler=spot");
    }
}