using SpotActorTransfer.Shared;
using System.Diagnostics;
using Systems.Zlink;
using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Configuration;
using Zlink.Framework.Contracts.Errors;
using Zlink.Framework.Contracts.Locations;
using Zlink.Framework.Contracts.Messaging;
using Zlink.Framework.Contracts.Spots;
using Zlink.Framework.Runtime.Actors;

namespace SpotActorTransfer.ActorNode;

internal static class ActorNodeEndpoints
{
    public static void Map(WebApplication app, ServerOptions options)
    {
        app.MapGet("/health", () => Results.Ok(new { status = "ok", options.Rid }));
        app.MapPost("/placement-weight", async (
            PlacementWeightReq request,
            IZLinkRouteMeshRuntimeOptions runtimeOptions,
            IZLinkLocationRuntimeQuery locations,
            CancellationToken cancellationToken) =>
        {
            runtimeOptions.Mesh(SpotActorTransferNames.Mesh).PlacementWeight =
                request.Weight;
            var deadline = DateTimeOffset.UtcNow + TimeSpan.FromSeconds(5);
            while (DateTimeOffset.UtcNow < deadline)
            {
                var page = await locations.ListMeshNodeDescriptorsAsync(
                    SpotActorTransferNames.Mesh,
                    cancellationToken: cancellationToken);
                if (page.Items.Any(descriptor =>
                        descriptor.Rid.ToString().StartsWith(
                            options.Rid + "-",
                            StringComparison.Ordinal)
                        && descriptor.PlacementWeight == request.Weight))
                {
                    return Results.Ok(new PlacementWeightRes(request.Weight));
                }
                await Task.Delay(TimeSpan.FromMilliseconds(20), cancellationToken);
            }
            return Results.StatusCode(StatusCodes.Status503ServiceUnavailable);
        });
        app.MapGet("/mesh/ready", (
            IZLinkRouteMeshRuntime meshRuntime) =>
        {
            var snapshot = meshRuntime.Snapshot(SpotActorTransferNames.Mesh);
            return Results.Ok(new MeshReadyRes(
                snapshot.Rid.ToString(),
                snapshot.Peers
                    .Where(static peer => peer.Ready)
                    .Select(static peer => peer.Rid.ToString())
                    .ToArray(),
                snapshot.ObjectCapabilities
                    .Where(static capability =>
                        capability.ObjectKind == ZLinkPlacementObjectKind.UserSpot)
                    .Select(static capability => capability.StableType)
                    .ToArray()));
        });
        app.MapGet("/evidence", (EvidenceStore evidence) => Results.Ok(evidence.Snapshot()));
        app.MapGet(
            "/relocation-blobs",
            (RelocationBlobObserver observer) =>
                Results.Ok(observer.Snapshot()));
        app.MapPost(
            "/relocation-blobs/reset",
            (RelocationBlobObserver observer) =>
            {
                observer.Reset();
                return Results.Ok();
            });
        app.MapGet("/process-memory", () =>
        {
            using var process = Process.GetCurrentProcess();
            return Results.Ok(new ProcessMemoryRes(
                process.WorkingSet64,
                process.PeakWorkingSet64));
        });
        app.MapPost("/payload-spots/user/{spotId}", async (
            string spotId,
            RelocationPayloadSpotReq request,
            IZLinkSpotManager spots,
            CancellationToken cancellationToken) =>
        {
            var created = await spots
                .GetOrCreate(
                    spotId,
                    SpotActorTransferNames.RelocationPayloadUserSpotType)
                .InMesh(SpotActorTransferNames.Mesh)
                .Request(request)
                .Async(cancellationToken);
            var state = TransferActorStateCodec.CreateState(
                spotId,
                request.ApplicationStateBytes);
            return Results.Ok(new RelocationPayloadSpotRes(
                created.Spot.SpotId,
                created.Spot.NodeRid.ToString(),
                state.Length,
                TransferActorStateCodec.Sha256(state)));
        });
        app.MapPost("/payload-spots/instance/{spotId}", async (
            string spotId,
            RelocationPayloadSpotReq request,
            IZLinkSpotClient spots,
            CancellationToken cancellationToken) =>
        {
            var result = await spots
                .RequestToSpot(spotId, request)
                .InstanceSpot(
                    SpotActorTransferNames.RelocationPayloadInstanceSpotType)
                .InMesh(SpotActorTransferNames.Mesh)
                .Timeout(TimeSpan.FromSeconds(30))
                .Async<RelocationPayloadSpotRes>(cancellationToken);
            return Results.Ok(result);
        });
        app.MapPost("/payload-spots/{spotId}/close", async (
            string spotId,
            IZLinkSpotManager spots,
            CancellationToken cancellationToken) =>
        {
            var spot = await spots.FindAsync(spotId, cancellationToken);
            return Results.Ok(spot is not null
                && await spots.CloseAsync(spot.Value, cancellationToken));
        });
        app.MapPost("/relocate", async (
            RelocateHostReq request,
            IZLinkFrameworkRuntime runtime,
            CancellationToken cancellationToken) =>
        {
            var mode = request.TargetApplicationVersion is null
                ? ZLinkFrameworkRelocationMode.PlannedMaintenance
                : ZLinkFrameworkRelocationMode.RollingUpdate;
            var result = await runtime.RelocateAsync(
                new ZLinkFrameworkRelocationOptions
                {
                    Mode = mode,
                    TargetApplicationVersion =
                        request.TargetApplicationVersion,
                    Deadline = TimeSpan.FromMilliseconds(
                        request.DeadlineMilliseconds)
                },
                cancellationToken);
            return Results.Ok(new RelocateHostRes(
                result.Outcome.ToString(),
                result.Reason.ToString(),
                runtime.Status.State.ToString()));
        });
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
        app.MapPost("/joined-gates/{spotId}/release", (string spotId, JoinedGateStore gates) =>
            Results.Ok(new GateReleaseRes(spotId, gates.Release(spotId))));
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
        app.MapPost("/transport-delivery/{operationId}/arm", (
            string operationId,
            TransportDeliveryArmReq request,
            TransportDeliveryGate gate) =>
            Results.Ok(gate.Arm(operationId, request)));
        app.MapPost("/transport-delivery/{operationId}/wait", async (
            string operationId,
            TransportDeliveryGate gate,
            CancellationToken cancellationToken) =>
            Results.Ok(await gate.WaitCapturedAsync(
                operationId,
                TimeSpan.FromSeconds(10),
                cancellationToken)));
        app.MapPost("/transport-delivery/{operationId}/release", (
            string operationId,
            TransportDeliveryGate gate) =>
            Results.Ok(gate.Release(operationId)));
        app.MapGet("/transport-delivery/{operationId}", (
            string operationId,
            TransportDeliveryGate gate) =>
            Results.Ok(gate.GetSnapshot(operationId)));
        app.MapPost("/spots", async (CreateSpotReq request, IZLinkSpotManager spots,
            CancellationToken cancellationToken) =>
        {
            var result = await spots
                .GetOrCreate(
                    request.SpotId,
                    SpotActorTransferNames.UserSpotType)
                .InMesh(SpotActorTransferNames.Mesh)
                .Request(request)
                .Async(cancellationToken);
            return Results.Ok(new CreateSpotRes(
                result.Spot.SpotId, result.Spot.NodeRid.ToString(), result.State.ToString()));
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
                actor.ActorId, request.ActorType, actor.NodeRid.ToString(), checked((long)actor.ObjectGeneration)));
        });
        app.MapGet("/actors/{actorId}/ref", async (string actorId, IZLinkActorManager actors,
            CancellationToken cancellationToken) =>
        {
            var actor = await FindActorAsync(actors, actorId, cancellationToken);
            return Results.Ok(new ActorRefSnapshotRes(
                actor.ActorId, actor.NodeRid.ToString(), checked((long)actor.ObjectGeneration)));
        });
        app.MapPost("/actors/{actorId}/destroy", async (
            string actorId,
            IZLinkActorManager actors,
            CancellationToken cancellationToken) =>
        {
            var actor = await FindActorAsync(actors, actorId, cancellationToken);
            var destroyed = await actors.DestroyAsync(actor, cancellationToken);
            return Results.Ok(new ActorDestroyRes(
                actor.ActorId,
                checked((long)actor.ObjectGeneration),
                destroyed));
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
                actor.ActorId, actor.NodeRid.ToString(), checked((long)actor.ObjectGeneration));
            evidence.Add(scenario, actorId, marker,
                $"node={snapshot.NodeRid};generation={snapshot.Generation}");
            return Results.Ok(snapshot);
        });
        app.MapPost("/actors/{actorId}/join", async (string actorId, JoinTargetReq request,
            IZLinkActorManager actors,
            IZLinkActorClient actorClient,
            JoinCompletionStore joinCompletions,
            EvidenceStore evidence,
            CancellationToken cancellationToken) =>
        {
            var actor = await FindActorAsync(actors, actorId, cancellationToken);
            var completion = joinCompletions.Register(actorId, request.Scenario);
            try
            {
                _ = await actorClient.RequestToActor(actor.ActorId, request)
                    .Timeout(TimeSpan.FromSeconds(10)).Async<JoinTargetRes>(cancellationToken);
                var outcome = await completion
                    .WaitAsync(TimeSpan.FromSeconds(10), cancellationToken);
                return outcome.Reply is { } reply
                    ? Results.Ok(reply)
                    : Results.Ok(new
                    {
                        request.Scenario,
                        ActorId = actorId,
                        Accepted = false,
                        ErrorKind = outcome.ErrorKind
                    });
            }
            catch (Exception error) when (error is ZLinkFrameworkException or InvalidOperationException)
            {
                joinCompletions.Cancel(actorId, request.Scenario);
                var kind = error is ZLinkFrameworkException frameworkError
                    ? frameworkError.Kind.ToString()
                    : error.Message;
                evidence.Add(request.Scenario, actorId, "join_failed", kind);
                return Results.Ok(new { request.Scenario, ActorId = actorId, Accepted = false, ErrorKind = kind });
            }
            catch
            {
                joinCompletions.Cancel(actorId, request.Scenario);
                throw;
            }
        });
        app.MapPost("/actors/{actorId}/probe", async (string actorId, ProbeReq request,
            IZLinkActorManager actors, IZLinkActorClient actorClient, EvidenceStore evidence,
            CancellationToken cancellationToken) =>
        {
            var actor = await FindActorAsync(actors, actorId, cancellationToken);
            evidence.Add(request.Scenario, actorId, "probe_submitted", request.Marker);
            return Results.Ok(await actorClient.RequestToActor(actor.ActorId, request)
                .Timeout(TimeSpan.FromSeconds(10)).Async<ProbeRes>(cancellationToken));
        });
        app.MapPost("/actors/{actorId}/probe-from-node", async (
            string actorId,
            NodeActorCallReq request,
            IZLinkActorClient actorClient, CancellationToken cancellationToken) =>
        {
            // The HTTP endpoint selects the process that submits the call.
            // Framework routing still uses only the public global Actor ID.
            try
            {
                var call = actorClient.RequestToActor(
                        actorId,
                        new ProbeReq(request.Scenario, request.Marker))
                    .Timeout(TimeSpan.FromMilliseconds(request.TimeoutMs));
                if (request.TransportOperationId is { Length: > 0 })
                    call.Metadata(
                        ZLinkActorTransportDeliveryMetadata.OperationId,
                        request.TransportOperationId);
                var response = await call.Async<ProbeRes>(cancellationToken);
                return Results.Ok(new NodeActorProbeRes(true, response, null));
            }
            catch (ZLinkFrameworkException error)
            {
                return Results.Ok(new NodeActorProbeRes(false, null, error.Kind.ToString()));
            }
            catch (Exception error) when (error is InvalidOperationException or TimeoutException)
            {
                return Results.Ok(new NodeActorProbeRes(false, null, error.GetType().Name));
            }
        });
        app.MapPost("/actors/{actorId}/send-from-node", async (
            string actorId,
            NodeActorCallReq request,
            IZLinkActorClient actorClient, CancellationToken cancellationToken) =>
        {
            // The selected process may hold a stale bounded route. The
            // application does not supply an owner RID or ObjectGeneration.
            var call = actorClient.SendToActor(actorId,
                new HandoffPacket(request.Scenario, request.Marker));
            if (request.TransportOperationId is { Length: > 0 })
                call.Metadata(
                    ZLinkActorTransportDeliveryMetadata.OperationId,
                    request.TransportOperationId);
            await call.Async(cancellationToken);
            return Results.Ok();
        });
        app.MapPost("/actors/{actorId}/bound-push", async (string actorId, BoundPushReq request,
            IZLinkActorManager actors, IZLinkActorClient actorClient, CancellationToken cancellationToken) =>
        {
            var actor = await FindActorAsync(actors, actorId, cancellationToken);
            return Results.Ok(await actorClient.RequestToActor(actor.ActorId, request)
                .Timeout(TimeSpan.FromSeconds(10)).Async<BoundPushRes>(cancellationToken));
        });
        app.MapPost("/shutdown", (IHostApplicationLifetime lifetime) =>
        {
            lifetime.StopApplication();
            return Results.Ok(new { status = "stopping" });
        });
        app.MapPost("/drain", async (
            IZLinkFrameworkRuntime runtime,
            CancellationToken cancellationToken) =>
        {
            var result = await runtime.ShutdownAsync(TimeSpan.FromSeconds(10), cancellationToken);
            if (result.Outcome != ZLinkFrameworkTerminationOutcome.Stopped)
                throw new InvalidOperationException($"Target drain did not complete: {result}.");
            return Results.Ok(new { status = "drained" });
        });
    }

    private static async ValueTask<ActorRef> FindActorAsync(
        IZLinkActorManager actors, string actorId, CancellationToken cancellationToken) =>
        await actors.FindAsync(actorId, cancellationToken)
        ?? throw new InvalidOperationException($"Actor '{actorId}' was not found.");

    private static string EvidenceText(ActorEvidence evidence) =>
        $"{evidence.Scenario}|{evidence.ActorId}|{evidence.Kind}|{evidence.Value}|{evidence.NodeRid}";
}
