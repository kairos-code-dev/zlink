using Systems.Zlink;
using AutomaticTurnDispatch.Server.Play.Spots;
using AutomaticTurnDispatch.Shared;
using Zlink.Framework.Contracts.Handlers;
using Zlink.Framework.Contracts.Locations;
using Zlink.Framework.Contracts.Spots;

namespace AutomaticTurnDispatch.Server.Play.Handlers;

[ZLinkSpotRequestHandler("RemoteSpotAwaitReq")]
internal sealed class RemoteSpotAwaitHandler(
    EvidenceStore evidence,
    IZLinkSpotHandleResolver spots)
    : IZLinkSpotRequestHandler<AwaitProbeSpot, RemoteSpotAwaitReq, AutomaticTurnDispatchRes>
{
    public async ValueTask<AutomaticTurnDispatchRes> HandleAsync(
        AwaitProbeSpot spot,
        RemoteSpotAwaitReq request,
        CancellationToken cancellationToken)
    {
        evidence.Add(
            $"remote-await-started|rid={evidence.Rid}|spot={spot.Context.SpotRid}"
            + $"|request={request.RequestId}|target={request.TargetSpotRid}|handler=spot");
        // Resolve once, then message with the held address (spot-address
        // messaging draft §6).
        var target = await spots.ResolveSpotHandleAsync(
                         RoutingId.From(request.TargetSpotRid), cancellationToken)
                     ?? throw new InvalidOperationException(
                         $"Target spot '{request.TargetSpotRid}' has no live address.");
        var call = spot.Context.Outbound.RequestToSpot(
                target,
                new AwaitReq(request.RequestId, request.DelayMs, "remote-spot"))
            .Timeout(TimeSpan.FromSeconds(5));
        evidence.Add(
            $"remote-await-released|rid={evidence.Rid}|spot={spot.Context.SpotRid}"
            + $"|request={request.RequestId}|target={request.TargetSpotRid}|handler=spot");
        var targetReply = await call.Async<AutomaticTurnDispatchRes>(cancellationToken);
        evidence.Add(
            $"remote-await-resumed|rid={evidence.Rid}|spot={spot.Context.SpotRid}"
            + $"|request={request.RequestId}|target={request.TargetSpotRid}|targetNode={targetReply.NodeRid}|handler=spot");
        evidence.Add(
            $"remote-await-completed|rid={evidence.Rid}|spot={spot.Context.SpotRid}"
            + $"|request={request.RequestId}|target={request.TargetSpotRid}|targetNode={targetReply.NodeRid}|handler=spot");
        return AwaitReplies.Reply("ATD-D2", request.RequestId, spot, "remote-await-completed");
    }
}

[ZLinkSpotPacketHandler("RemoteSpotAwaitMsg")]
internal sealed class RemoteSpotAwaitCommandHandler(
    EvidenceStore evidence,
    IZLinkSpotHandleResolver spots)
    : IZLinkSpotPacketHandler<AwaitProbeSpot, RemoteSpotAwaitMsg>
{
    public async ValueTask HandleAsync(
        AwaitProbeSpot spot,
        RemoteSpotAwaitMsg request,
        CancellationToken cancellationToken)
    {
        evidence.Add(
            $"remote-await-started|rid={evidence.Rid}|spot={spot.Context.SpotRid}"
            + $"|request={request.RequestId}|target={request.TargetSpotRid}|handler=spot");
        // Resolve once, then message with the held address (spot-address
        // messaging draft §6).
        var target = await spots.ResolveSpotHandleAsync(
                         RoutingId.From(request.TargetSpotRid), cancellationToken)
                     ?? throw new InvalidOperationException(
                         $"Target spot '{request.TargetSpotRid}' has no live address.");
        var call = spot.Context.Outbound.RequestToSpot(
                target,
                new AwaitReq(request.RequestId, request.DelayMs, "remote-spot"))
            .Timeout(TimeSpan.FromSeconds(5));
        evidence.Add(
            $"remote-await-released|rid={evidence.Rid}|spot={spot.Context.SpotRid}"
            + $"|request={request.RequestId}|target={request.TargetSpotRid}|handler=spot");
        var targetReply = await call.Async<AutomaticTurnDispatchRes>(cancellationToken);
        evidence.Add(
            $"remote-await-resumed|rid={evidence.Rid}|spot={spot.Context.SpotRid}"
            + $"|request={request.RequestId}|target={request.TargetSpotRid}|targetNode={targetReply.NodeRid}|handler=spot");
        evidence.Add(
            $"remote-await-completed|rid={evidence.Rid}|spot={spot.Context.SpotRid}"
            + $"|request={request.RequestId}|target={request.TargetSpotRid}|targetNode={targetReply.NodeRid}|handler=spot");
    }
}
