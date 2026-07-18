using Microsoft.Extensions.Configuration;

using System.Collections.Concurrent;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Http;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using SpotService.Shared;
using Systems.Zlink;
using Zlink.Framework;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Dispatch;
using Zlink.Framework.Contracts.Errors;
using Zlink.Framework.Contracts.Locations;
using Zlink.Framework.Contracts.Spots;

using Zlink.Framework.Locations.Redis;

namespace SpotService.Server.Gateway;

using Zlink.Framework.E2E.Configuration;

internal static class GatewayHostFactory
{
    public static WebApplication Create(string[] args)
    {
        var options = GatewayOptions.Parse(args);
        Directory.CreateDirectory(options.LogDir);

        var builder = WebApplication.CreateBuilder(args);
        builder.Configuration.Sources.Clear();
        builder.Configuration.AddInMemoryCollection();
        builder.Logging.ClearProviders();
        builder.Logging.AddSimpleConsole(console =>
        {
            console.SingleLine = true;
            console.TimestampFormat = "HH:mm:ss.fff ";
        });
        builder.WebHost.UseUrls(options.HttpUrl);
        builder.Services.AddSingleton(new EvidenceStore(options.Rid, options.EvidenceFile));
        builder.Services.AddZLinkFramework(framework =>
        {
            if (!string.IsNullOrWhiteSpace(options.RedisEndpoint))
            {
                framework.AddLocationStore(new ZLinkRedisLocationStore(redis => redis
                    .SetConnectionString(options.RedisEndpoint)
                    .SetKeyPrefix(options.RedisKeyPrefix
                                  ?? throw new InvalidOperationException("Shared.RedisKeyPrefix is required."))));
                // Crash-recovery scenarios re-claim actors from a killed
                // node; a short owner lease keeps that takeover window
                // within the scenario's patience.
                var locations = framework.ConfigureLocations();
                locations.HeartbeatInterval = TimeSpan.FromSeconds(1);
                locations.OwnerLeaseTtl = TimeSpan.FromSeconds(3);
                locations.PollingInterval = TimeSpan.FromMilliseconds(500);
            }
            framework.ConfigureDispatch()
                .MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
                .TraceLogFile(Path.Combine(options.LogDir, $"{options.Rid}-flow.log"))
                .TraceLabel(options.Rid);
            framework.AddRouteMeshChannel(SpotServiceNames.ExternalSpotChannel)
                .EnableClient(Require(options.ExternalSpotEndpoint, "ExternalSpotEndpoint"));
            var mesh19 = framework.AddRouteMesh(SpotServiceNames.SpotChannel)
                .Listen(Require(options.SpotRouterEndpoint, "SpotRouterEndpoint"))
                .SetRoutingId(RoutingId.From(options.Rid));
            mesh19.ChannelName(SpotServiceNames.SpotChannel);
        });

        var app = builder.Build();
        app.MapGet("/health", () => Results.Ok(new { status = "ready", options.Rid }));
        app.MapGet("/evidence", (EvidenceStore evidence) => Results.Ok(evidence.Snapshot()));
        app.MapPost("/evidence/wait", async (
            EvidenceWaitReq request,
            EvidenceStore evidence,
            CancellationToken cancellationToken) =>
        {
            var timeout = TimeSpan.FromMilliseconds(Math.Clamp(request.TimeoutMilliseconds, 1, 30000));
            var snapshot = await evidence.WaitUntilAsync(
                entries => request.ContainsAll.All(expected =>
                    entries.Any(entry => entry.Contains(expected, StringComparison.Ordinal))),
                timeout,
                cancellationToken);
            return Results.Ok(snapshot);
        });
        app.MapPost("/spot/publish", (
            SpotPublishReq request,
            IZLinkSpotPublisherClient publisher,
            EvidenceStore evidence) =>
        {
            publisher.PublishSpot(
                    SpotServiceNames.SpotChannel,
                    SpotServiceNames.SpotMsgTopic,
                    new SpotMsg(request.Marker))
                .TrySubmit();
            evidence.Add($"spot-publish|rid={options.Rid}|spot={request.SpotRid}|marker={request.Marker}");
            return Results.Ok(new SpotPublishRes(
                "spot.sm-c4-publish",
                options.Rid,
                request.SpotRid,
                request.Marker,
                evidence.Snapshot()));
        });
        app.MapPost("/channel/route-ping", async (
            IZLinkRouteClient routes,
            ControlPingReq request,
            CancellationToken cancellationToken) =>
        {
            var reply = await routes.RequestToNode(
                    SpotServiceNames.ExternalSpotChannel,
                    RoutingId.From("play-a"),
                    request)
                .Async<ControlPingRes>(cancellationToken);
            return Results.Ok(reply);
        });
        app.MapPost("/spot/route-state", async (
            IZLinkRouteClient routes,
            IZLinkSpotHandleResolver handles,
            SpotStateRouteReq request,
            CancellationToken cancellationToken) =>
        {
            var target = await handles.ResolveRequiredAsync(request.SpotRid, cancellationToken);
            var reply = await routes.RequestToSpot(target, new StateReq(request.Operation, request.Delta))
                .Async<StateRes>(cancellationToken);
            return Results.Ok(reply);
        });
        app.MapPost("/actor/push", async (
            ActorPushByActorReq request,
            IZLinkActorDirectory actorDirectory,
            IZLinkActorClient actors,
            EvidenceStore evidence,
            CancellationToken cancellationToken) =>
        {
            evidence.Add($"actor-push-request|rid={options.Rid}|actor={request.ActorId}|value={request.Value}");
            try
            {
                var actor = await actorDirectory.FindAsync(request.ActorId, cancellationToken)
                            ?? throw new ZLinkFrameworkException(
                                ZLinkFrameworkErrorKind.ActorRouteNotFound,
                                $"Actor route '{request.ActorId}' was not found.");
                var reply = await actors.RequestToActor(
                        actor,
                        new ActorPushReq(request.Value))
                    .Timeout(TimeSpan.FromSeconds(10))
                    .Async<ActorPingRes>(cancellationToken);
                evidence.Add(
                    $"actor-push-delivered|rid={options.Rid}|actor={reply.ActorId}"
                    + $"|value={reply.Value}|node={reply.NodeRid}");
                return Results.Ok(new ActorPushByActorRes(reply.ActorId, reply.Value, true, string.Empty));
            }
            catch (ZLinkFrameworkException ex)
            {
                evidence.Add(
                    $"actor-push-failed|rid={options.Rid}|actor={request.ActorId}"
                    + $"|error={ex.GetType().Name}|kind={ex.Kind}");
                return Results.Ok(new ActorPushByActorRes(
                    request.ActorId,
                    request.Value,
                    false,
                    ex.Kind.ToString()));
            }
            catch (Exception ex)
            {
                evidence.Add(
                    $"actor-push-failed|rid={options.Rid}|actor={request.ActorId}"
                    + $"|error={ex.GetType().Name}");
                return Results.Ok(new ActorPushByActorRes(
                    request.ActorId,
                    request.Value,
                    false,
                    ex.GetType().Name));
            }
        });
        app.MapPost("/actor/wait-missing", async (
            ActorMissingWaitReq request,
            IZLinkActorDirectory actorDirectory,
            CancellationToken cancellationToken) =>
        {
            var deadline = DateTimeOffset.UtcNow
                           + TimeSpan.FromMilliseconds(Math.Clamp(request.TimeoutMilliseconds, 1, 30000));
            while (DateTimeOffset.UtcNow < deadline)
            {
                if (await actorDirectory.FindAsync(request.ActorId, cancellationToken) is null)
                    return Results.Ok();
                await Task.Delay(TimeSpan.FromMilliseconds(100), cancellationToken);
            }

            return Results.Problem(
                $"Actor route '{request.ActorId}' remained visible.",
                statusCode: StatusCodes.Status504GatewayTimeout);
        });
        app.MapPost("/actor/capture-ref", async (
            ActorRefSnapshotReq request,
            IZLinkActorDirectory actors,
            CancellationToken cancellationToken) =>
        {
            var actor = await actors.FindAsync(request.ActorId, cancellationToken)
                        ?? throw new ZLinkFrameworkException(
                            ZLinkFrameworkErrorKind.ActorRouteNotFound,
                            $"Actor route '{request.ActorId}' was not found.");
            return Results.Ok(new ActorRefSnapshotRes(
                actor.ActorId, actor.NodeRid.ToString(), actor.Generation));
        });
        app.MapPost("/actor/request-ref", async (
            ActorRefRequestReq request,
            IZLinkActorClient actors,
            CancellationToken cancellationToken) =>
        {
            try
            {
                var actor = new ActorRef(
                    RoutingId.From(request.Actor.NodeRid),
                    request.Actor.ActorId,
                    request.Actor.Generation);
                var call = request.DelayMilliseconds > 0
                    ? actors.RequestToActor(actor,
                        new SlowActorPingReq(request.Value, request.DelayMilliseconds))
                    : actors.RequestToActor(actor, new ActorPingReq(request.Value));
                var reply = await call
                    .Timeout(TimeSpan.FromMilliseconds(
                        Math.Clamp(request.TimeoutMilliseconds, 1, 30000)))
                    .Async<ActorPingRes>(cancellationToken);
                return Results.Ok(new ActorRefRequestRes(true, string.Empty, reply));
            }
            catch (ZLinkFrameworkException error)
            {
                return Results.Ok(new ActorRefRequestRes(false, error.Kind.ToString(), null));
            }
            catch (TimeoutException)
            {
                return Results.Ok(new ActorRefRequestRes(false, "Timeout", null));
            }
        });
        app.MapPost("/node/wait-ready", async (
            NodeReadinessWaitReq request,
            IZLinkLocationRuntimeQuery locations,
            IZLinkSpotHandleResolver handles,
            IZLinkRouteClient routes,
            CancellationToken cancellationToken) =>
        {
            var deadline = DateTimeOffset.UtcNow
                           + TimeSpan.FromMilliseconds(Math.Clamp(request.TimeoutMilliseconds, 1, 30000));
            while (DateTimeOffset.UtcNow < deadline)
            {
                var peers = await locations.ListMeshNodeDescriptorsAsync(
                    SpotServiceNames.SpotChannel, cancellationToken);
                var peerReady = peers.Any(row => string.Equals(
                    row.Rid.ToString(), request.NodeRid, StringComparison.Ordinal));
                var entry = await handles.ResolveSpotHandleAsync(
                    RoutingId.From(request.NodeRid), cancellationToken);
                if (peerReady && entry is not null)
                    try
                    {
                        var marker = $"ready-{Guid.NewGuid():N}";
                        var reply = await routes.RequestToSpot(entry, new EntryReadinessReq(marker))
                            .Timeout(TimeSpan.FromSeconds(1))
                            .Async<EntryReadinessRes>(cancellationToken);
                        if (reply.NodeRid == request.NodeRid && reply.Marker == marker)
                            return Results.Ok(new NodeReadinessWaitRes(request.NodeRid, true, true));
                    }
                    catch (Exception) when (!cancellationToken.IsCancellationRequested)
                    {
                        // The location row can precede the router connection.
                    }
                await Task.Delay(100, cancellationToken);
            }
            return Results.Problem(
                $"Node '{request.NodeRid}' peer and Entry Spot readiness did not converge.",
                statusCode: StatusCodes.Status504GatewayTimeout);
        });
        app.MapPost("/entry/join", async (
            EntryJoinRouteReq request,
            IZLinkRouteClient routes,
            IZLinkSpotHandleResolver handles,
            IZLinkActorDirectory actors,
            CancellationToken cancellationToken) =>
        {
            var entry = await handles.ResolveSpotHandleAsync(
                            RoutingId.From(request.NodeRid), cancellationToken)
                        ?? throw new InvalidOperationException(
                            $"Entry Spot for node '{request.NodeRid}' is not ready.");
            var joined = await routes.RequestToSpot(entry, request.Join)
                .Async<JoinRes>(cancellationToken);
            var deadline = DateTimeOffset.UtcNow + TimeSpan.FromSeconds(10);
            while (DateTimeOffset.UtcNow < deadline)
            {
                var actor = await actors.FindAsync(request.Join.ActorId, cancellationToken);
                if (actor is not null) return Results.Ok(joined);
                await Task.Delay(100, cancellationToken);
            }
            return Results.Problem(
                $"Actor '{request.Join.ActorId}' did not become ready after Entry Spot JoinReq.",
                statusCode: StatusCodes.Status504GatewayTimeout);
        });
        app.MapPost("/shutdown", (IHostApplicationLifetime lifetime) =>
        {
            lifetime.StopApplication();
            return Results.Ok(new { status = "stopping" });
        });
        return app;
    }

    static string Require(string? value, string optionName)
        => string.IsNullOrWhiteSpace(value)
            ? throw new InvalidOperationException($"{optionName} is required.")
            : value;
}

internal sealed class EvidenceStore
{
    readonly ConcurrentQueue<string> _entries = new();
    readonly string? _file;
    readonly SemaphoreSlim _signal = new(0);

    public EvidenceStore(string rid, string? file)
    {
        _file = file;
        Add($"start|rid={rid}");
    }

    public void Add(string value)
    {
        _entries.Enqueue(value);
        _signal.Release();
        if (!string.IsNullOrWhiteSpace(_file))
        {
            File.AppendAllLines(_file, new[] { value });
        }
    }

    public string[] Snapshot() => _entries.ToArray();

    public async Task<string[]> WaitUntilAsync(
        Func<string[], bool> condition,
        TimeSpan timeout,
        CancellationToken cancellationToken)
    {
        var deadline = DateTimeOffset.UtcNow + timeout;
        while (true)
        {
            var snapshot = Snapshot();
            if (condition(snapshot)) return snapshot;

            var remaining = deadline - DateTimeOffset.UtcNow;
            if (remaining <= TimeSpan.Zero) throw new TimeoutException("Timed out waiting for spot service evidence.");

            await _signal.WaitAsync(remaining, cancellationToken);
        }
    }
}

internal sealed record GatewayOptions(
    string Rid,
    string HttpUrl,
    string LogDir,
    string? EvidenceFile = null,
    string? RedisEndpoint = null,
    string? RedisKeyPrefix = null,
    string? SpotRouterEndpoint = null,
    string? SpotPubEndpoint = null,
    string? ExternalSpotEndpoint = null)
{
    public static GatewayOptions Parse(string[] args)
        => E2eConfiguration.Load<GatewayOptions>(args);
}
