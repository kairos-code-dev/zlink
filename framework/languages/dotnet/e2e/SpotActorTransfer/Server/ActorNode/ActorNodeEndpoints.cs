using SpotActorTransfer.Shared;
using Systems.Zlink;
using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Configuration;
using Zlink.Framework.Contracts.Errors;
using Zlink.Framework.Contracts.Messaging;
using Zlink.Framework.Contracts.Spots;

namespace SpotActorTransfer.ActorNode;

internal static class ActorNodeEndpoints
{
    public static void Map(WebApplication app, ServerOptions options)
    {
        app.MapGet("/health", () => Results.Ok(new { status = "ok", options.Rid }));
        app.MapGet("/mesh/ready", (IZLinkRouteMeshRuntime meshRuntime) =>
        {
            var snapshot = meshRuntime.Snapshot(SpotActorTransferNames.Mesh);
            return Results.Ok(new MeshReadyRes(
                options.Rid,
                snapshot.Peers
                    .Where(static peer => peer.Ready)
                    .Select(static peer => peer.Rid.ToString())
                    .ToArray()));
        });
        app.MapGet("/evidence", (EvidenceStore evidence) => Results.Ok(evidence.Snapshot()));
        app.MapPost("/evidence/wait", async (EvidenceWaitReq request, EvidenceStore evidence,
            CancellationToken cancellationToken) =>
        {
            var snapshot = await evidence.WaitUntilAsync(
                entries => request.ContainsAll.All(expected => entries.Any(entry =>
                    EvidenceText(entry).Contains(expected, StringComparison.Ordinal))),
                TimeSpan.FromMilliseconds(Math.Clamp(request.TimeoutMilliseconds, 1, 30000)),
                cancellationToken);
            return Results.Ok(snapshot);
        });
        app.MapPost("/runtime-evidence/wait", async (EvidenceWaitReq request,
            RuntimeEvidenceStore evidence, CancellationToken cancellationToken) =>
        {
            var snapshot = await evidence.WaitUntilAsync(
                request.ContainsAll,
                TimeSpan.FromMilliseconds(Math.Clamp(request.TimeoutMilliseconds, 1, 30000)),
                cancellationToken);
            return Results.Ok(snapshot);
        });
        app.MapPost("/joined-gates/{spotRid}/release", (string spotRid, JoinedGateStore gates) =>
            Results.Ok(new GateReleaseRes(spotRid, gates.Release(spotRid))));
        app.MapPost("/transfer-gates/{actorId}/release", (string actorId, TransferGateStore gates) =>
            Results.Ok(new GateReleaseRes(actorId, gates.Release(actorId))));
        app.MapPost("/cleanup-gates/{actorId}/arm", (
            string actorId, CleanupGateArmReq request, ActorCleanupGateStore gates) =>
            Results.Ok(new CleanupGateRes(actorId, gates.Arm(actorId, request.Scenario))));
        app.MapPost("/cleanup-gates/{actorId}/allow-attempt", (
            string actorId, ActorCleanupGateStore gates) =>
            Results.Ok(new CleanupGateRes(actorId, gates.AllowAttempt(actorId))));
        app.MapPost("/cleanup-gates/{actorId}/release", (string actorId, ActorCleanupGateStore gates) =>
            Results.Ok(new CleanupGateRes(actorId, gates.Release(actorId))));
        app.MapPost("/spots", async (CreateSpotReq request, IZLinkSpotManager spots,
            CancellationToken cancellationToken) =>
        {
            var result = await spots.GetOrCreateAsync<TransferUserSpot, CreateSpotReq>(
                RoutingId.From(request.SpotRid), request, cancellationToken);
            return Results.Ok(new CreateSpotRes(
                result.SpotRid.ToString(), options.Rid, result.State.ToString()));
        });
        app.MapPost("/actors", async (ActorCreateReq request, IZLinkActorManager actors,
            CancellationToken cancellationToken) =>
        {
            var actor = (await actors.GetOrCreate(request.ActorId, request.ActorType)
                .Request(request).Async(cancellationToken)) switch
            {
                ZLinkActorCreateResult.Existing value => value.Actor,
                ZLinkActorCreateResult.Created value => value.Actor,
                _ => throw new InvalidOperationException("Actor creation was rejected.")
            };
            return Results.Ok(new ActorCreateRes(
                actor.ActorId, request.ActorType, actor.NodeRid.ToString(), checked((long)actor.Generation)));
        });
        app.MapGet("/actors/{actorId}/ref", async (string actorId, IZLinkActorManager actors,
            CancellationToken cancellationToken) =>
        {
            var actor = await FindActorAsync(actors, actorId, cancellationToken);
            return Results.Ok(new ActorRefSnapshotRes(
                actor.ActorId, actor.NodeRid.ToString(), checked((long)actor.Generation)));
        });
        app.MapGet("/actors/{actorId}/ref-evidence/{scenario}/{marker}", async (
            string actorId,
            string scenario,
            string marker,
            IZLinkActorManager actors,
            EvidenceStore evidence,
            CancellationToken cancellationToken) =>
        {
            var actor = await FindActorAsync(actors, actorId, cancellationToken);
            var snapshot = new ActorRefSnapshotRes(
                actor.ActorId, actor.NodeRid.ToString(), checked((long)actor.Generation));
            evidence.Add(scenario, actorId, marker,
                $"node={snapshot.NodeRid};generation={snapshot.Generation}");
            return Results.Ok(snapshot);
        });
        app.MapPost("/actors/{actorId}/join", async (string actorId, JoinTargetReq request,
            IZLinkActorManager actors, IZLinkActorClient actorClient, EvidenceStore evidence,
            CancellationToken cancellationToken) =>
        {
            var actor = await FindActorAsync(actors, actorId, cancellationToken);
            try
            {
                var result = await actorClient.RequestToActor(SpotActorTransferNames.Mesh, actor, request)
                    .Timeout(TimeSpan.FromSeconds(10)).Async<JoinTargetRes>(cancellationToken);
                evidence.Add(request.Scenario, actorId,
                    result.Accepted ? "success_reply" : "reject_reply", request.TargetSpotRid);
                return Results.Ok(result);
            }
            catch (Exception error) when (error is ZLinkFrameworkException or InvalidOperationException)
            {
                var kind = error is ZLinkFrameworkException frameworkError
                    ? frameworkError.Kind.ToString()
                    : error.Message;
                evidence.Add(request.Scenario, actorId, "join_failed", kind);
                return Results.Ok(new { request.Scenario, ActorId = actorId, Accepted = false, ErrorKind = kind });
            }
        });
        app.MapPost("/actors/{actorId}/probe", async (string actorId, ProbeReq request,
            IZLinkActorManager actors, IZLinkActorClient actorClient, EvidenceStore evidence,
            CancellationToken cancellationToken) =>
        {
            var actor = await FindActorAsync(actors, actorId, cancellationToken);
            evidence.Add(request.Scenario, actorId, "probe_submitted", request.Marker);
            return Results.Ok(await actorClient.RequestToActor(SpotActorTransferNames.Mesh, actor, request)
                .Timeout(TimeSpan.FromSeconds(10)).Async<ProbeRes>(cancellationToken));
        });
        app.MapPost("/actors/{actorId}/probe-ref", async (string actorId, ActorRefProbeReq request,
            IZLinkActorClient actorClient, CancellationToken cancellationToken) =>
        {
            try
            {
                var actor = ToActorRef(actorId, request);
                var response = await actorClient.RequestToActor(
                        SpotActorTransferNames.Mesh,
                        actor,
                        new ProbeReq(request.Scenario, request.Marker))
                    .Timeout(TimeSpan.FromMilliseconds(request.TimeoutMs))
                    .Async<ProbeRes>(cancellationToken);
                return Results.Ok(new ActorRefProbeRes(true, response, null));
            }
            catch (ZLinkFrameworkException error)
            {
                return Results.Ok(new ActorRefProbeRes(false, null, error.Kind.ToString()));
            }
            catch (Exception error) when (error is InvalidOperationException or TimeoutException)
            {
                return Results.Ok(new ActorRefProbeRes(false, null, error.GetType().Name));
            }
        });
        app.MapPost("/actors/{actorId}/send-ref", async (string actorId, ActorRefProbeReq request,
            IZLinkActorClient actorClient, CancellationToken cancellationToken) =>
        {
            await actorClient.SendToActor(SpotActorTransferNames.Mesh, ToActorRef(actorId, request),
                    new HandoffPacket(request.Scenario, request.Marker))
                .SubmitAsync(cancellationToken);
            return Results.Ok();
        });
        app.MapPost("/actors/{actorId}/bound-push", async (string actorId, BoundPushReq request,
            IZLinkActorManager actors, IZLinkActorClient actorClient, CancellationToken cancellationToken) =>
        {
            var actor = await FindActorAsync(actors, actorId, cancellationToken);
            return Results.Ok(await actorClient.RequestToActor(SpotActorTransferNames.Mesh, actor, request)
                .Timeout(TimeSpan.FromSeconds(10)).Async<BoundPushRes>(cancellationToken));
        });
        app.MapPost("/shutdown", (IHostApplicationLifetime lifetime) =>
        {
            lifetime.StopApplication();
            return Results.Ok(new { status = "stopping" });
        });
        app.MapPost("/drain", async (IZLinkDrainControl drain, CancellationToken cancellationToken) =>
        {
            var result = await drain.DrainAsync(TimeSpan.FromSeconds(10), cancellationToken);
            if (result is not Drained)
                throw new InvalidOperationException($"Target drain did not complete: {result}.");
            return Results.Ok(new { status = "drained" });
        });
    }

    private static async ValueTask<ActorRef> FindActorAsync(
        IZLinkActorManager actors, string actorId, CancellationToken cancellationToken) =>
        await actors.FindAsync(actorId, cancellationToken)
        ?? throw new InvalidOperationException($"Actor '{actorId}' was not found.");

    private static ActorRef ToActorRef(string actorId, ActorRefProbeReq request) => new(
        RoutingId.From(request.NodeRid), actorId, checked((ulong)request.Generation));

    private static string EvidenceText(ActorEvidence evidence) =>
        $"{evidence.Scenario}|{evidence.ActorId}|{evidence.Kind}|{evidence.Value}|{evidence.NodeRid}";
}
