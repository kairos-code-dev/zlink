using SpotService.Server.Play.Spots;
using SpotService.Shared;
using Systems.Zlink;
using Zlink.Framework.Contracts.Handlers;
using Zlink.Framework.Contracts.Locations;
using Zlink.Framework.Contracts.Spots;

namespace SpotService.Server.Play.Handlers;

[ZLinkSpotRequestHandler("SpotToSpotReq")]
internal sealed class SpotToSpotHandler(
    EvidenceStore evidence,
    IZLinkSpotAddressResolver spots)
    : IZLinkSpotRequestHandler<ScenarioUserSpot, SpotToSpotReq, SpotToSpotRes>
{
    public async ValueTask<SpotToSpotRes> HandleAsync(
        ScenarioUserSpot spot,
        SpotToSpotReq request,
        CancellationToken cancellationToken)
    {
        // Resolve once, hold the address for the interaction (spot-address
        // messaging draft §6).
        var target = await spots.ResolveSpotAddressAsync(
                         RoutingId.From(request.TargetSpotRid), cancellationToken)
                     ?? throw new InvalidOperationException(
                         $"Target spot '{request.TargetSpotRid}' has no live address.");
        var reply = await spot.Context.Outbound
            .RequestToSpot(target, new StateReq("add", 3))
            .PacketName("StateReq")
            .Async<StateRes>(cancellationToken);
        spot.Context.Outbound.SendToSpot(target, new StateMsg($"sm-c3-send-{request.Marker}"))
            .PacketName("StateMsg")
            .Submit(cancellationToken);
        spot.Context.Outbound.Publish(SpotServiceNames.SpotMsgTopic, new SpotMsg($"sm-c3-publish-{request.Marker}"))
            .PacketName("SpotMsg")
            .Submit(cancellationToken);
        evidence.Add(
            $"spot-to-spot|rid={evidence.Rid}|source={spot.Context.SpotRid}"
            + $"|target={request.TargetSpotRid}|value={reply.Value}");
        return new SpotToSpotRes(
            spot.Context.SpotRid.ToString(),
            request.TargetSpotRid,
            reply.Value);
    }
}

[ZLinkSpotRequestHandler("SpotToSpotTimeoutReq")]
internal sealed class SpotToSpotTimeoutHandler(
    EvidenceStore evidence,
    IZLinkSpotAddressResolver spots)
    : IZLinkSpotRequestHandler<ScenarioUserSpot, SpotToSpotTimeoutReq, SpotToSpotTimeoutRes>
{
    public async ValueTask<SpotToSpotTimeoutRes> HandleAsync(
        ScenarioUserSpot spot,
        SpotToSpotTimeoutReq request,
        CancellationToken cancellationToken)
    {
        var failed = false;
        try
        {
            var target = await spots.ResolveSpotAddressAsync(
                             RoutingId.From(request.TargetSpotRid), cancellationToken)
                         ?? throw new InvalidOperationException(
                             $"Target spot '{request.TargetSpotRid}' has no live address.");
            await spot.Context.Outbound
                .RequestToSpot(target, new SlowSpotReq(request.Marker, 1500))
                .PacketName("SlowSpotReq")
                .Timeout(TimeSpan.FromMilliseconds(100))
                .Async<SlowSpotRes>(cancellationToken);
        }
        catch
        {
            failed = true;
        }

        evidence.Add(
            $"spot-to-spot-timeout|rid={evidence.Rid}|source={spot.Context.SpotRid}"
            + $"|target={request.TargetSpotRid}|failed={failed}");
        return new SpotToSpotTimeoutRes(
            spot.Context.SpotRid.ToString(),
            request.TargetSpotRid,
            failed);
    }
}

[ZLinkSpotRequestHandler("SpotToSpotNegativeReq")]
internal sealed class SpotToSpotNegativeHandler(
    EvidenceStore evidence,
    IZLinkSpotAddressResolver spots)
    : IZLinkSpotRequestHandler<ScenarioUserSpot, SpotToSpotNegativeReq, SpotToSpotNegativeRes>
{
    public async ValueTask<SpotToSpotNegativeRes> HandleAsync(
        ScenarioUserSpot spot,
        SpotToSpotNegativeReq request,
        CancellationToken cancellationToken)
    {
        // The negative here is the missing HANDLER on a live target spot:
        // the address resolves, the request reply-errors, and the
        // best-effort send is dropped at the target with evidence.
        var target = await spots.ResolveSpotAddressAsync(
                         RoutingId.From(request.TargetSpotRid), cancellationToken)
                     ?? throw new InvalidOperationException(
                         $"Target spot '{request.TargetSpotRid}' has no live address.");
        var requestFailed = false;
        try
        {
            await spot.Context.Outbound
                .RequestToSpot(target, new StateReq("noop", 0))
                .PacketName("MissingSpotReq")
                .Timeout(TimeSpan.FromSeconds(2))
                .Async<StateRes>(cancellationToken);
        }
        catch
        {
            requestFailed = true;
        }

        spot.Context.Outbound.SendToSpot(target, new StateMsg($"missing-{request.Marker}"))
            .PacketName("MissingSpotMsg")
            .Submit(cancellationToken);

        evidence.Add(
            $"spot-to-spot-negative|rid={evidence.Rid}|source={spot.Context.SpotRid}"
            + $"|target={request.TargetSpotRid}|requestFailed={requestFailed}");
        return new SpotToSpotNegativeRes(
            spot.Context.SpotRid.ToString(),
            request.TargetSpotRid,
            requestFailed);
    }
}

[ZLinkSpotPacketHandler("SpotOutboundMsg")]
internal sealed class SpotOutboundHandler(EvidenceStore evidence)
    : IZLinkSpotPacketHandler<ScenarioUserSpot, SpotOutboundMsg>
{
    public async ValueTask HandleAsync(
        ScenarioUserSpot spot,
        SpotOutboundMsg request,
        CancellationToken cancellationToken)
    {
        var echo = await spot.Context.Outbound
            .RequestToChannel(
                SpotServiceNames.ExternalClientChannel,
                new ChannelEchoReq(request.Marker))
            .PacketName("ChannelEchoReq")
            .Async<ChannelEchoRes>(cancellationToken);
        var notifyMarker = $"notify-{request.Marker}";
        spot.Context.Outbound.SendToChannel(
                SpotServiceNames.ExternalClientChannel,
                new ChannelNotify(notifyMarker))
            .PacketName("ChannelNotify")
            .Submit(cancellationToken);
        spot.Context.Outbound.Publish(
                SpotServiceNames.SpotMsgTopic,
                new SpotMsg("sm-c2-publish"))
            .PacketName("SpotMsg")
            .Submit(cancellationToken);
        evidence.Add(
            $"spot-outbound|rid={evidence.Rid}|spot={spot.Context.SpotRid}"
            + $"|echo={echo.Value}|notify={notifyMarker}");
    }
}

[ZLinkSpotPacketHandler("SpotOutboundNegativeMsg")]
internal sealed class SpotOutboundNegativeHandler(EvidenceStore evidence)
    : IZLinkSpotPacketHandler<ScenarioUserSpot, SpotOutboundNegativeMsg>
{
    public async ValueTask HandleAsync(
        ScenarioUserSpot spot,
        SpotOutboundNegativeMsg request,
        CancellationToken cancellationToken)
    {
        var requestFailed = false;
        try
        {
            await spot.Context.Outbound
                .RequestToChannel(
                    SpotServiceNames.ExternalClientChannel,
                    new ChannelEchoReq(request.Marker))
                .PacketName("MissingChannelReq")
                .Timeout(TimeSpan.FromSeconds(2))
                .Async<ChannelEchoRes>(cancellationToken);
        }
        catch
        {
            requestFailed = true;
        }

        spot.Context.Outbound.SendToChannel(
                SpotServiceNames.ExternalClientChannel,
                new ChannelNotify($"missing-{request.Marker}"))
            .PacketName("MissingChannelNotify")
            .Submit(cancellationToken);
        evidence.Add(
            $"spot-outbound-negative|rid={evidence.Rid}|spot={spot.Context.SpotRid}"
            + $"|requestFailed={requestFailed}");
    }
}
