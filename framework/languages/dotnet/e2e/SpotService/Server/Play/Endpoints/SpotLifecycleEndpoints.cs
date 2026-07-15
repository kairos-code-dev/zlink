using SpotService.Server.Play.Spots;
using SpotService.Shared;
using Systems.Zlink;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Errors;
using Zlink.Framework.Contracts.Spots;

using Zlink.Framework.Contracts.Locations;

namespace SpotService.Server.Play.Endpoints;

using static PlayHostFactory;

internal static class SpotLifecycleEndpoints
{
    public static void MapSpotLifecycleEndpoints(WebApplication app)
    {
        app.MapPost("/spot/create", async (
            IZLinkSpotManager spots,
            EvidenceStore evidence,
            NodeOptions node,
            CreateSpotReq request) =>
        {
            var createdSpot = await spots.GetOrCreateAsync<ScenarioUserSpot>(RoutingId.From(request.SpotRid));
            evidence.Add($"create-spot|rid={node.Rid}|spot={createdSpot.SpotRid}|state={createdSpot.State}");
            return Results.Ok(new CreateSpotRes(
                createdSpot.SpotRid.ToString(),
                node.Rid,
                createdSpot.State.ToString()));
        });
        app.MapPost("/spot/create-alternate", async (
            IZLinkSpotManager spots,
            EvidenceStore evidence,
            NodeOptions node,
            CreateSpotReq request) =>
        {
            var createdSpot = await spots.GetOrCreateAsync<ScenarioAlternateSpot>(RoutingId.From(request.SpotRid));
            evidence.Add(
                $"create-unsubscribed-spot|rid={node.Rid}|spot={createdSpot.SpotRid}|state={createdSpot.State}");
            return Results.Ok(new CreateSpotRes(
                createdSpot.SpotRid.ToString(),
                node.Rid,
                createdSpot.State.ToString()));
        });
        app.MapPost("/spot/type-mismatch", async (
            IZLinkSpotManager spots,
            EvidenceStore evidence,
            NodeOptions node,
            SpotTypeMismatchReq request) =>
        {
            var rid = RoutingId.From(request.SpotRid);
            var first = await spots.GetOrCreateAsync<ScenarioUserSpot>(rid);
            try
            {
                await spots.GetOrCreateAsync<ScenarioAlternateSpot>(rid);
            }
            catch (ZLinkFrameworkException ex) when (ex.Kind == ZLinkFrameworkErrorKind.SpotTypeMismatch)
            {
                evidence.Add($"spot-type-mismatch|rid={node.Rid}|spot={request.SpotRid}|kind={ex.Kind}");
                return Results.Ok(new SpotTypeMismatchRes(
                    request.SpotRid,
                    true,
                    ex.Kind.ToString(),
                    first.State.ToString()));
            }

            throw new InvalidOperationException("Expected SpotTypeMismatch for reused spot rid.");
        });
        app.MapPost("/spot/close", async (
            IZLinkSpotManager spots,
            EvidenceStore evidence,
            NodeOptions node,
            CloseSpotReq request) =>
        {
            var closed = await spots.CloseAsync(RoutingId.From(request.SpotRid));
            evidence.Add($"close-spot|rid={node.Rid}|spot={request.SpotRid}|closed={closed}");
            await WaitUntilAsync(
                () => evidence.Snapshot().Any(line =>
                    line.Contains($"spot-closing|rid={node.Rid}|spot={request.SpotRid}", StringComparison.Ordinal)),
                "Expected spot closing evidence.");
            return Results.Ok(new CloseSpotRes(request.SpotRid, closed));
        });
        app.MapPost("/spot/state/request", async (
            IZLinkRouteClient routes,
            IZLinkSpotHandleResolver locator,
            SpotStateRouteReq request) =>
        {
            var result = await RequestSpotStateAsync(
                routes,
                locator,
                request.SpotRid,
                new StateReq(request.Operation, request.Delta));
            return Results.Ok(result);
        });
        app.MapPost("/spot/state/command", async (
            IZLinkRouteClient routes,
            IZLinkSpotHandleResolver locator,
            EvidenceStore evidence,
            NodeOptions node,
            SpotStateCommandReq request) =>
        {
            var before = evidence.Snapshot();
            await SendSpotCommandAsync(
                routes,
                locator,
                request.SpotRid,
                new StateMsg(request.Marker));
            await WaitUntilAsync(
                () => CountNew(evidence.Snapshot(), before,
                    $"spot-state-command|rid={node.Rid}|spot={request.SpotRid}|marker={request.Marker}") == 1,
                "Expected spot state command evidence.");
            return Results.Ok(new SpotStateCommandRes(
                request.SpotRid,
                request.Marker,
                true,
                evidence.Snapshot()));
        });
    }
}
