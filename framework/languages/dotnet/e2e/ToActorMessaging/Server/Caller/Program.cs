using System.Collections.Concurrent;
using Systems.Zlink;
using ToActorMessaging.Caller;
using ToActorMessaging.Shared;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Configuration;
using Zlink.Framework.Contracts.Errors;
using Zlink.Framework.Locations.Redis;
using Zlink.Framework.E2E.Configuration;

var options = ServerOptions.Parse(args, "caller");
Directory.CreateDirectory(options.LogDir);

var builder = WebApplication.CreateBuilder(args);
builder.WebHost.UseUrls(options.HttpUrl);
var cachedActors = new ConcurrentDictionary<string, ActorRef>(StringComparer.Ordinal);
IZLinkEndpointConnections? actorConnections = null;
builder.Services.AddZLinkFramework(framework =>
{
    framework.AddLocationStore(new ZLinkRedisLocationStore(redis => redis
        .SetConnectionString(options.RedisEndpoint)
        .SetKeyPrefix(options.RedisKeyPrefix)));
    var spot = framework.AddSpotMesh("to-actor")
        .EnableRouter(options.RouterEndpoint)
        .SetRoutingId(RoutingId.From(options.Rid))
        .SetEntrySpotRoutingId(RoutingId.From(options.Rid))
        .EnablePubSub(options.PubSubEndpoint);
    if (options.ConnectActorRoutes)
        spot
            .ConnectRouter(RoutingId.From(options.ActorRid), options.ActorRouterEndpoint)
            .ConnectRouter(RoutingId.From(options.ActorBRid), options.ActorBRouterEndpoint);
    actorConnections = spot.RouterConnections;
});

var app = builder.Build();
app.MapGet("/health", () => Results.Ok(new { status = "ok" }));
app.MapPost("/send", async (
    ActorCallRequest request,
    IZLinkActorDirectory actorDirectory,
    IZLinkActorClient actors,
    CancellationToken ct) =>
{
    try
    {
        var actor = await ResolveActorAsync(request, actorDirectory, ct);
        actors.SendToActor(actor, new ActorNotify(request.Scenario, request.ActorId, request.Value))
            .Submit(ct);
        return Results.Ok(new ActorCallResponse(request.Scenario, request.ActorId, "sent"));
    }
    catch (ZLinkFrameworkException error)
    {
        return Results.Ok(new ActorCallResponse(request.Scenario, request.ActorId, "failed", error.Kind.ToString()));
    }
});
app.MapPost("/request", async (
    ActorCallRequest request,
    IZLinkActorDirectory actorDirectory,
    IZLinkActorClient actors,
    CancellationToken ct) =>
{
    try
    {
        var actor = await ResolveActorAsync(request, actorDirectory, ct);
        var reply = await actors.RequestToActor(actor, new ActorAsk(request.Scenario, request.ActorId, request.Value))
            .Timeout(TimeSpan.FromSeconds(5))
            .Async<ActorReply>(ct);
        return Results.Ok(new ActorCallResponse(request.Scenario, request.ActorId, reply.Value));
    }
    catch (ZLinkFrameworkException error)
    {
        return Results.Ok(new ActorCallResponse(request.Scenario, request.ActorId, "failed", error.Kind.ToString()));
    }
});
app.MapPost("/refs/{actorId}/capture", async (
    string actorId,
    IZLinkActorDirectory actorDirectory,
    CancellationToken ct) =>
{
    var actor = await actorDirectory.FindAsync(actorId, ct)
                ?? throw new ZLinkFrameworkException(
                    ZLinkFrameworkErrorKind.ActorRouteNotFound,
                    $"Actor route '{actorId}' was not found.");
    cachedActors[actorId] = actor;
    return Results.Ok(ActorRefSnapshot.From(actor));
});
app.MapGet("/directory/{actorId}", async (
    string actorId,
    IZLinkActorDirectory actorDirectory,
    CancellationToken ct) =>
{
    var actor = await actorDirectory.FindAsync(actorId, ct);
    return Results.Ok(new ActorRouteStatus(actorId, actor is not null));
});
app.MapPost("/cached/request", async (
    ActorCallRequest request,
    IZLinkActorClient actors,
    CancellationToken ct) =>
{
    try
    {
        if (!cachedActors.TryGetValue(request.ActorId, out var actor))
            throw new InvalidOperationException($"Actor ref '{request.ActorId}' was not captured.");
        var reply = await actors.RequestToActor(actor, new ActorAsk(request.Scenario, request.ActorId, request.Value))
            .Timeout(TimeSpan.FromSeconds(2))
            .Async<ActorReply>(ct);
        return Results.Ok(new ActorCallResponse(request.Scenario, request.ActorId, reply.Value));
    }
    catch (ZLinkFrameworkException error)
    {
        return Results.Ok(new ActorCallResponse(request.Scenario, request.ActorId, "failed", error.Kind.ToString()));
    }
});
app.MapPost("/route/disconnect", () =>
{
    var connections = actorConnections
                      ?? throw new InvalidOperationException("Actor router connections are unavailable.");
    connections.Disconnect(options.ActorRouterEndpoint);
    connections.Disconnect(options.ActorBRouterEndpoint);
    return Results.Ok(new { status = "disconnected" });
});
app.MapPost("/route/reconnect", () =>
{
    var connections = actorConnections
                      ?? throw new InvalidOperationException("Actor router connections are unavailable.");
    connections.Connect(options.ActorRouterEndpoint);
    connections.Connect(options.ActorBRouterEndpoint);
    return Results.Ok(new { status = "connected" });
});
app.MapPost("/shutdown", async (IHostApplicationLifetime lifetime) =>
{
    await Task.Yield();
    lifetime.StopApplication();
    return Results.Ok();
});
await app.RunAsync();

static async ValueTask<ActorRef> ResolveActorAsync(
    ActorCallRequest request,
    IZLinkActorDirectory actorDirectory,
    CancellationToken cancellationToken)
{
    if (!string.IsNullOrWhiteSpace(request.TargetNodeRid)
        && request.TargetGeneration is { } generation)
        return new ActorRef(RoutingId.From(request.TargetNodeRid), request.ActorId, generation);

    return await actorDirectory.FindAsync(request.ActorId, cancellationToken)
           ?? throw new ZLinkFrameworkException(
               ZLinkFrameworkErrorKind.ActorRouteNotFound,
               $"Actor route '{request.ActorId}' was not found.");
}

namespace ToActorMessaging.Caller
{
    internal sealed record ServerOptions(
        string Rid,
        string HttpUrl,
        string RedisEndpoint,
        string RedisKeyPrefix,
        string RouterEndpoint,
        string PubSubEndpoint,
        string ActorRid,
        string ActorRouterEndpoint,
        string ActorBRid,
        string ActorBRouterEndpoint,
        string LogDir,
        bool ConnectActorRoutes)
    {
        public static ServerOptions Parse(string[] args, string role)
            => E2eConfiguration.Load<ServerOptions>(args);
    }
}
