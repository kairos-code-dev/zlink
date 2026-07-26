using SpotService.Shared;
using Systems.Zlink;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Spots;

using Zlink.Framework.Contracts.Locations;

namespace SpotService.Server.Play.Endpoints;

using static PlayHostFactory;

internal static class SpotFailureEndpoints
{
    public static void MapSpotFailureEndpoints(WebApplication app)
    {
        app.MapPost("/spot/missing-handler/request", async (
            IZLinkSpotClient routes,
            EvidenceStore evidence,
            SpotMissingHandlerReq request) =>
        {
            var before = evidence.Snapshot();
            var failed = await FailsAsync(
                routes.RequestToSpot(request.SpotRid,
                        new MissingSpotReq("sm-e1"))
                    .Timeout(TimeSpan.FromSeconds(2))
                    .Async<StateRes>().AsTask());
            await WaitUntilAsync(
                () => CountNew(evidence.Snapshot(), before,
                          "dispatch-error|surface=SpotRoute|reason=HandlerMissing|action=ReplyError|packet=MissingSpotReq") >=
                      1,
                "Expected missing spot request handler evidence.");
            return Results.Ok(new SpotMissingHandlerRes(request.SpotRid, failed, evidence.Snapshot()));
        });
        app.MapPost("/spot/missing-handler/command", async (
            IZLinkSpotClient routes,
            EvidenceStore evidence,
            SpotMissingCommandReq request) =>
        {
            var before = evidence.Snapshot();
            using (var missingSendCts = new CancellationTokenSource(TimeSpan.FromSeconds(2)))
            {
                try
                {
                    await routes.SendToSpot(request.SpotRid,
                            new MissingSpotMsg(request.Marker)).Async(missingSendCts.Token);
                }
                catch (OperationCanceledException) when (missingSendCts.IsCancellationRequested)
                {
                }
            }

            await WaitUntilAsync(
                () => CountNew(evidence.Snapshot(), before,
                          "dispatch-error|surface=SpotRoute|reason=HandlerMissing|action=Drop|packet=MissingSpotMsg") >=
                      1,
                "Expected missing spot command handler evidence.");
            return Results.Ok(new SpotMissingCommandRes(request.SpotRid, request.Marker, true, evidence.Snapshot()));
        });
        app.MapPost("/spot/missing-target/request", async (
            IZLinkSpotClient routes,
            SpotMissingTargetReq request) =>
        {
            // Direct routing resolves the missing global SpotId inside the
            // awaited operation and returns the typed route failure.
            var failed = await FailsAsync(RequestMissingTargetAsync());
            return Results.Ok(new SpotMissingTargetRes(request.SpotRid, failed));

            async Task<StateRes> RequestMissingTargetAsync()
            {
                return await routes.RequestToSpot(request.SpotRid,
                        new StateReq("noop", 0))
                    .Timeout(TimeSpan.FromSeconds(2))
                    .Async<StateRes>();
            }
        });
        app.MapPost("/spot/slow/request", async (
            IZLinkSpotClient routes,
            SpotSlowRouteReq request) =>
        {
            var timedOut = await FailsAsync(
                routes.RequestToSpot(request.SpotRid,
                        new SlowSpotReq(request.Marker, request.DelayMs))
                    .Timeout(TimeSpan.FromMilliseconds(request.TimeoutMs))
                    .Async<SlowSpotRes>().AsTask());
            return Results.Ok(new SpotSlowRouteRes(request.SpotRid, request.Marker, timedOut));
        });
        app.MapPost("/spot/to-spot/timeout", async (
            IZLinkSpotClient routes,
            SpotToSpotTimeoutRouteReq request) =>
        {
            var result = await routes.RequestToSpot(request.SourceSpotRid,
                    new SpotToSpotTimeoutReq(request.TargetSpotRid, request.Marker))
                .Timeout(TimeSpan.FromSeconds(5))
                .Async<SpotToSpotTimeoutRes>();
            return Results.Ok(result);
        });
        app.MapPost("/spot/to-spot/negative", async (
            IZLinkSpotClient routes,
            EvidenceStore evidence,
            NodeOptions node,
            SpotToSpotNegativeRouteReq request) =>
        {
            var result = await routes.RequestToSpot(request.SourceSpotRid,
                    new SpotToSpotNegativeReq(request.TargetSpotRid, request.Marker))
                .Timeout(TimeSpan.FromSeconds(5))
                .Async<SpotToSpotNegativeRes>();
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
                               "dispatch-error|surface=SpotRoute|reason=HandlerMissing|action=Drop|packet=MissingSpotMsg") >=
                           1;
                },
                "Expected spot-to-spot negative evidence.");
            return Results.Ok(result);
        });
    }
}
