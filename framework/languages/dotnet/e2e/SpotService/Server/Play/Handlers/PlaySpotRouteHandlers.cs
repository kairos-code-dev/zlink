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
    IZLinkSpotHandleResolver spots)
    : IZLinkSpotRequestHandler<ScenarioUserSpot, SpotToSpotReq, SpotToSpotRes>
{
    public async ValueTask<SpotToSpotRes> HandleAsync(
        ScenarioUserSpot spot,
        SpotToSpotReq request,
        CancellationToken cancellationToken)
    {
        // Resolve one opaque handle for the interaction; the framework owns its
        // location snapshot and safe request refresh behavior.
        var target = await spots.ResolveSpotHandleAsync(
                         spot.Context.MeshName,
                         request.TargetSpotRid,
                         cancellationToken)
                     ?? throw new InvalidOperationException(
                         $"Target spot '{request.TargetSpotRid}' has no live location row.");
        var reply = await spot.Context.Outbound
            .RequestToSpot(target, new StateReq("add", 3))
            .Async<StateRes>(cancellationToken);
        await spot.Context.Outbound.SendToSpot(target, new StateMsg($"sm-c3-send-{request.Marker}"))
            .Async(cancellationToken);
        await spot.Context.Outbound.Publish(
                SpotServiceNames.SpotChannel,
                SpotServiceNames.SpotMsgTopic,
                new SpotMsg($"sm-c3-publish-{request.Marker}"))
            .Async(cancellationToken);
        evidence.Add(
            $"spot-to-spot|rid={evidence.Rid}|source={spot.Context.SpotId}"
            + $"|target={request.TargetSpotRid}|value={reply.Value}");
        return new SpotToSpotRes(
            spot.Context.SpotId.ToString(),
            request.TargetSpotRid,
            reply.Value);
    }
}

[ZLinkSpotRequestHandler("SpotToSpotTimeoutReq")]
internal sealed class SpotToSpotTimeoutHandler(
    EvidenceStore evidence,
    IZLinkSpotHandleResolver spots)
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
            var target = await spots.ResolveSpotHandleAsync(
                             spot.Context.MeshName,
                             request.TargetSpotRid,
                             cancellationToken)
                         ?? throw new InvalidOperationException(
                             $"Target spot '{request.TargetSpotRid}' has no live location row.");
            await spot.Context.Outbound
                .RequestToSpot(target, new SlowSpotReq(request.Marker, 1500))
                .Timeout(TimeSpan.FromMilliseconds(100))
                .Async<SlowSpotRes>(cancellationToken);
        }
        catch
        {
            failed = true;
        }

        evidence.Add(
            $"spot-to-spot-timeout|rid={evidence.Rid}|source={spot.Context.SpotId}"
            + $"|target={request.TargetSpotRid}|failed={failed}");
        return new SpotToSpotTimeoutRes(
            spot.Context.SpotId.ToString(),
            request.TargetSpotRid,
            failed);
    }
}

[ZLinkSpotRequestHandler("SpotToSpotNegativeReq")]
internal sealed class SpotToSpotNegativeHandler(
    EvidenceStore evidence,
    IZLinkSpotHandleResolver spots)
    : IZLinkSpotRequestHandler<ScenarioUserSpot, SpotToSpotNegativeReq, SpotToSpotNegativeRes>
{
    public async ValueTask<SpotToSpotNegativeRes> HandleAsync(
        ScenarioUserSpot spot,
        SpotToSpotNegativeReq request,
        CancellationToken cancellationToken)
    {
        // The negative here is the missing HANDLER on a live target spot:
        // the handle resolves, the request reply-errors, and the
        // best-effort send is dropped at the target with evidence.
        var target = await spots.ResolveSpotHandleAsync(
                         spot.Context.MeshName,
                         request.TargetSpotRid,
                         cancellationToken)
                     ?? throw new InvalidOperationException(
                         $"Target spot '{request.TargetSpotRid}' has no live location row.");
        var requestFailed = false;
        try
        {
            await spot.Context.Outbound
                .RequestToSpot(target, new MissingSpotReq("noop"))
                .Timeout(TimeSpan.FromSeconds(2))
                .Async<StateRes>(cancellationToken);
        }
        catch
        {
            requestFailed = true;
        }

        await spot.Context.Outbound.SendToSpot(target, new MissingSpotMsg($"missing-{request.Marker}"))
            .Async(cancellationToken);

        evidence.Add(
            $"spot-to-spot-negative|rid={evidence.Rid}|source={spot.Context.SpotId}"
            + $"|target={request.TargetSpotRid}|requestFailed={requestFailed}");
        return new SpotToSpotNegativeRes(
            spot.Context.SpotId.ToString(),
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
            .Async<ChannelEchoRes>(cancellationToken);
        var notifyMarker = $"notify-{request.Marker}";
        await spot.Context.Outbound.SendToChannel(
                SpotServiceNames.ExternalClientChannel,
                new ChannelNotify(notifyMarker))
            .Async(cancellationToken);
        await spot.Context.Outbound.Publish(
                SpotServiceNames.SpotChannel,
                SpotServiceNames.SpotMsgTopic,
                new SpotMsg("sm-c2-publish"))
            .Async(cancellationToken);
        evidence.Add(
            $"spot-outbound|rid={evidence.Rid}|spot={spot.Context.SpotId}"
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
                    new MissingChannelReq(request.Marker))
                .Timeout(TimeSpan.FromSeconds(2))
                .Async<ChannelEchoRes>(cancellationToken);
        }
        catch
        {
            requestFailed = true;
        }

        await spot.Context.Outbound.SendToChannel(
                SpotServiceNames.ExternalClientChannel,
                new MissingChannelNotify($"missing-{request.Marker}"))
            .Async(cancellationToken);
        evidence.Add(
            $"spot-outbound-negative|rid={evidence.Rid}|spot={spot.Context.SpotId}"
            + $"|requestFailed={requestFailed}");
    }
}
