using SpotService.Shared;
using Systems.Zlink;
using Zlink.Framework.Contracts.Channels;

namespace SpotService.Server.Play.Endpoints;

using static PlayHostFactory;

internal static class SpotFailureEndpoints
{
    public static void MapSpotFailureEndpoints(WebApplication app)
    {
        app.MapPost("/spot/missing-handler/request", async (
            IZLinkRouteClient routes,
            EvidenceStore evidence,
            SpotMissingHandlerReq request) =>
        {
            var before = evidence.Snapshot();
            var failed = await FailsAsync(
                routes.Request(
                        SpotServiceNames.ExternalSpotChannel,
                        RoutingId.From(request.SpotRid),
                        new StateReq("noop", 0))
                    .PacketName("MissingSpotReq")
                    .Timeout(TimeSpan.FromSeconds(2))
                    .Async<StateReply>().AsTask());
            await WaitUntilAsync(
                () => CountNew(evidence.Snapshot(), before,
                          "dispatch-error|surface=SpotRoute|reason=HandlerMissing|action=ReplyError|packet=MissingSpotReq") >=
                      1,
                "Expected missing spot request handler evidence.");
            return Results.Ok(new SpotMissingHandlerReply(request.SpotRid, failed, evidence.Snapshot()));
        });
        app.MapPost("/spot/missing-handler/command", async (
            IZLinkRouteClient routes,
            EvidenceStore evidence,
            SpotMissingCommandReq request) =>
        {
            var before = evidence.Snapshot();
            using (var missingSendCts = new CancellationTokenSource(TimeSpan.FromSeconds(2)))
            {
                try
                {
                    await routes.Send(
                            SpotServiceNames.ExternalSpotChannel,
                            RoutingId.From(request.SpotRid),
                            new StateCommand(request.Marker))
                        .PacketName("MissingSpotCommand")
                        .Async(missingSendCts.Token);
                }
                catch (OperationCanceledException) when (missingSendCts.IsCancellationRequested)
                {
                }
            }

            await WaitUntilAsync(
                () => CountNew(evidence.Snapshot(), before,
                          "dispatch-error|surface=SpotRoute|reason=HandlerMissing|action=Drop|packet=MissingSpotCommand") >=
                      1,
                "Expected missing spot command handler evidence.");
            return Results.Ok(new SpotMissingCommandReply(request.SpotRid, request.Marker, true, evidence.Snapshot()));
        });
        app.MapPost("/spot/missing-target/request", async (
            IZLinkRouteClient routes,
            SpotMissingTargetReq request) =>
        {
            var failed = await FailsAsync(
                routes.Request(
                        SpotServiceNames.ExternalSpotChannel,
                        RoutingId.From(request.SpotRid),
                        new StateReq("noop", 0))
                    .PacketName("StateReq")
                    .Timeout(TimeSpan.FromSeconds(2))
                    .Async<StateReply>().AsTask());
            return Results.Ok(new SpotMissingTargetReply(request.SpotRid, failed));
        });
        app.MapPost("/spot/slow/request", async (
            IZLinkRouteClient routes,
            SpotSlowRouteReq request) =>
        {
            var timedOut = await FailsAsync(
                routes.Request(
                        SpotServiceNames.ExternalSpotChannel,
                        RoutingId.From(request.SpotRid),
                        new SlowSpotReq(request.Marker, request.DelayMs))
                    .PacketName("SlowSpotReq")
                    .Timeout(TimeSpan.FromMilliseconds(request.TimeoutMs))
                    .Async<SlowSpotReply>().AsTask());
            return Results.Ok(new SpotSlowRouteReply(request.SpotRid, request.Marker, timedOut));
        });
        app.MapPost("/spot/to-spot/timeout", async (
            IZLinkRouteClient routes,
            SpotToSpotTimeoutRouteReq request) =>
        {
            var result = await routes.Request(
                    SpotServiceNames.ExternalSpotChannel,
                    RoutingId.From(request.SourceSpotRid),
                    new SpotToSpotTimeoutReq(request.TargetSpotRid, request.Marker))
                .PacketName("SpotToSpotTimeoutReq")
                .Timeout(TimeSpan.FromSeconds(5))
                .Async<SpotToSpotTimeoutReply>();
            return Results.Ok(result);
        });
        app.MapPost("/spot/to-spot/negative", async (
            IZLinkRouteClient routes,
            EvidenceStore evidence,
            NodeOptions node,
            SpotToSpotNegativeRouteReq request) =>
        {
            var result = await routes.Request(
                    SpotServiceNames.ExternalSpotChannel,
                    RoutingId.From(request.SourceSpotRid),
                    new SpotToSpotNegativeReq(request.TargetSpotRid, request.Marker))
                .PacketName("SpotToSpotNegativeReq")
                .Timeout(TimeSpan.FromSeconds(5))
                .Async<SpotToSpotNegativeReply>();
            await WaitUntilAsync(
                () =>
                {
                    var after = evidence.Snapshot();
                    return CountNew(after, [],
                               $"spot-to-spot-negative|rid={node.Rid}|source={request.SourceSpotRid}|target={request.TargetSpotRid}|requestFailed=True") >=
                           1
                           && CountNew(after, [],
                               "dispatch-error|surface=SpotRoute|reason=HandlerMissing|action=ReplyError|packet=MissingSpotReq") >=
                           1
                           && CountNew(after, [],
                               "dispatch-error|surface=SpotRoute|reason=HandlerMissing|action=Drop|packet=MissingSpotCommand") >=
                           1;
                },
                "Expected spot-to-spot negative evidence.");
            return Results.Ok(result);
        });
    }
}