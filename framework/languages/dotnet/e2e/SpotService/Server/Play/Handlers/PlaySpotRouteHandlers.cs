using System.Collections.Concurrent;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Http;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using SpotService.Shared;
using Systems.Zlink;
using Systems.Zlink.Stream.Connector.Contracts;
using Zlink.Framework;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Codecs.Json;
using Zlink.Framework.Contracts.Dispatch;
using Zlink.Framework.Contracts.Errors;
using Zlink.Framework.Contracts.Handlers;
using Zlink.Framework.Contracts.Messaging;
using Zlink.Framework.Contracts.Spots;
using Zlink.Framework.Contracts.Streams;
using Zlink.Framework.Contracts.Timers;

namespace SpotService.Server.Play;


internal static partial class PlayHostFactory
{
[ZLinkSpotRequestHandler("SpotToSpotReq")]
internal sealed class SpotToSpotHandler(EvidenceStore evidence)
    : IZLinkSpotRequestHandler<ScenarioUserSpot, SpotToSpotReq, SpotToSpotReply>
{
    public async ValueTask<SpotToSpotReply> HandleAsync(
        ScenarioUserSpot spot,
        SpotToSpotReq request,
        CancellationToken cancellationToken)
    {
        var targetRid = RoutingId.From(request.TargetSpotRid);
        var reply = await spot.Context.Outbound
            .RequestToSpot(targetRid, new StateReq("add", 3))
            .PacketName("StateReq")
            .Async<StateReply>(cancellationToken);
        await spot.Context.Outbound
            .SendToSpot(targetRid, new StateCommand($"sm-c3-send-{request.Marker}"))
            .PacketName("StateCommand")
            .Async(cancellationToken);
        await spot.Context.Outbound
            .Publish(SpotServiceNames.SpotEventTopic, new SpotEvent($"sm-c3-publish-{request.Marker}"))
            .PacketName("SpotEvent")
            .Async(cancellationToken);
        evidence.Add(
            $"spot-to-spot|rid={evidence.Rid}|source={spot.Context.SpotRid}"
            + $"|target={request.TargetSpotRid}|value={reply.Value}");
        return new SpotToSpotReply(
            spot.Context.SpotRid.ToString(),
            request.TargetSpotRid,
            reply.Value);
    }
}

[ZLinkSpotRequestHandler("SpotToSpotTimeoutReq")]
internal sealed class SpotToSpotTimeoutHandler(EvidenceStore evidence)
    : IZLinkSpotRequestHandler<ScenarioUserSpot, SpotToSpotTimeoutReq, SpotToSpotTimeoutReply>
{
    public async ValueTask<SpotToSpotTimeoutReply> HandleAsync(
        ScenarioUserSpot spot,
        SpotToSpotTimeoutReq request,
        CancellationToken cancellationToken)
    {
        var targetRid = RoutingId.From(request.TargetSpotRid);
        var failed = false;
        try
        {
            await spot.Context.Outbound
                .RequestToSpot(targetRid, new SlowSpotReq(request.Marker, 1500))
                .PacketName("SlowSpotReq")
                .Timeout(TimeSpan.FromMilliseconds(100))
                .Async<SlowSpotReply>(cancellationToken);
        }
        catch
        {
            failed = true;
        }

        evidence.Add(
            $"spot-to-spot-timeout|rid={evidence.Rid}|source={spot.Context.SpotRid}"
            + $"|target={request.TargetSpotRid}|failed={failed}");
        return new SpotToSpotTimeoutReply(
            spot.Context.SpotRid.ToString(),
            request.TargetSpotRid,
            failed);
    }
}

[ZLinkSpotRequestHandler("SpotToSpotNegativeReq")]
internal sealed class SpotToSpotNegativeHandler(EvidenceStore evidence)
    : IZLinkSpotRequestHandler<ScenarioUserSpot, SpotToSpotNegativeReq, SpotToSpotNegativeReply>
{
    public async ValueTask<SpotToSpotNegativeReply> HandleAsync(
        ScenarioUserSpot spot,
        SpotToSpotNegativeReq request,
        CancellationToken cancellationToken)
    {
        var targetRid = RoutingId.From(request.TargetSpotRid);
        var requestFailed = false;
        try
        {
            await spot.Context.Outbound
                .RequestToSpot(targetRid, new StateReq("noop", 0))
                .PacketName("MissingSpotReq")
                .Timeout(TimeSpan.FromSeconds(2))
                .Async<StateReply>(cancellationToken);
        }
        catch
        {
            requestFailed = true;
        }

        await spot.Context.Outbound
            .SendToSpot(targetRid, new StateCommand($"missing-{request.Marker}"))
            .PacketName("MissingSpotCommand")
            .Async(cancellationToken);
        evidence.Add(
            $"spot-to-spot-negative|rid={evidence.Rid}|source={spot.Context.SpotRid}"
            + $"|target={request.TargetSpotRid}|requestFailed={requestFailed}");
        return new SpotToSpotNegativeReply(
            spot.Context.SpotRid.ToString(),
            request.TargetSpotRid,
            requestFailed);
    }
}

[ZLinkSpotPacketHandler("SpotOutboundReq")]
internal sealed class SpotOutboundHandler(EvidenceStore evidence)
    : IZLinkSpotPacketHandler<ScenarioUserSpot, SpotOutboundReq>
{
    public async ValueTask HandleAsync(
        ScenarioUserSpot spot,
        SpotOutboundReq request,
        CancellationToken cancellationToken)
    {
        var echo = await spot.Context.Outbound
            .RequestToChannel(
                SpotServiceNames.ExternalClientChannel,
                new ChannelEchoReq(request.Marker))
            .PacketName("ChannelEchoReq")
            .Async<ChannelEchoReply>(cancellationToken);
        var notifyMarker = $"notify-{request.Marker}";
        await spot.Context.Outbound
            .SendToChannel(
                SpotServiceNames.ExternalClientChannel,
                new ChannelNotify(notifyMarker))
            .PacketName("ChannelNotify")
            .Async(cancellationToken);
        await spot.Context.Outbound
            .Publish(
                SpotServiceNames.SpotEventTopic,
                new SpotEvent("sm-c2-publish"))
            .PacketName("SpotEvent")
            .Async(cancellationToken);
        evidence.Add(
            $"spot-outbound|rid={evidence.Rid}|spot={spot.Context.SpotRid}"
            + $"|echo={echo.Value}|notify={notifyMarker}");
    }
}

[ZLinkSpotPacketHandler("SpotOutboundNegativeReq")]
internal sealed class SpotOutboundNegativeHandler(EvidenceStore evidence)
    : IZLinkSpotPacketHandler<ScenarioUserSpot, SpotOutboundNegativeReq>
{
    public async ValueTask HandleAsync(
        ScenarioUserSpot spot,
        SpotOutboundNegativeReq request,
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
                .Async<ChannelEchoReply>(cancellationToken);
        }
        catch
        {
            requestFailed = true;
        }

        await spot.Context.Outbound
            .SendToChannel(
                SpotServiceNames.ExternalClientChannel,
                new ChannelNotify($"missing-{request.Marker}"))
            .PacketName("MissingChannelSend")
            .Async(cancellationToken);
        evidence.Add(
            $"spot-outbound-negative|rid={evidence.Rid}|spot={spot.Context.SpotRid}"
            + $"|requestFailed={requestFailed}");
    }
}

}
