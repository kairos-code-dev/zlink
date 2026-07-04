using Systems.Zlink;
using ToActorMessaging.Caller;
using ToActorMessaging.Shared;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Errors;
using Zlink.Framework.Locations.Redis;

var options = ServerOptions.Parse(args, "caller");
Directory.CreateDirectory(options.LogDir);

var builder = WebApplication.CreateBuilder(args);
builder.WebHost.UseUrls(options.HttpUrl);
builder.Services.AddZLinkFramework(framework =>
{
    framework.AddLocationStore(new ZLinkRedisLocationStore(redis => redis
        .SetConnectionString(options.RedisEndpoint)
        .SetKeyPrefix(options.RedisKeyPrefix)));
    framework.AddSpotMesh("to-actor")
        .EnableRouter(options.RouterEndpoint)
        .SetRoutingId(RoutingId.From(options.Rid))
        .SetEntrySpotRoutingId(RoutingId.From(options.Rid))
        .EnablePubSub(options.PubSubEndpoint);
});

var app = builder.Build();
app.MapGet("/health", () => Results.Ok(new { status = "ok" }));
app.MapPost("/send", async (
    ActorCallRequest request,
    IZLinkActorClient actors,
    CancellationToken ct) =>
{
    try
    {
        await actors.SendToActor(request.ActorId, new ActorNotify(request.Scenario, request.ActorId, request.Value))
            .PacketName(nameof(ActorNotify))
            .Async(ct);
        return Results.Ok(new ActorCallResponse(request.Scenario, request.ActorId, "sent"));
    }
    catch (ZLinkFrameworkException error)
    {
        return Results.Ok(new ActorCallResponse(request.Scenario, request.ActorId, "failed", error.Kind.ToString()));
    }
});
app.MapPost("/request", async (
    ActorCallRequest request,
    IZLinkActorClient actors,
    CancellationToken ct) =>
{
    try
    {
        var reply = await actors.RequestToActor(request.ActorId, new ActorAsk(request.Scenario, request.ActorId, request.Value))
            .PacketName(nameof(ActorAsk))
            .Timeout(TimeSpan.FromSeconds(5))
            .Async<ActorReply>(ct);
        return Results.Ok(new ActorCallResponse(request.Scenario, request.ActorId, reply.Value));
    }
    catch (ZLinkFrameworkException error)
    {
        return Results.Ok(new ActorCallResponse(request.Scenario, request.ActorId, "failed", error.Kind.ToString()));
    }
});
app.MapPost("/shutdown", async (IHostApplicationLifetime lifetime) =>
{
    await Task.Yield();
    lifetime.StopApplication();
    return Results.Ok();
});
await app.RunAsync();

namespace ToActorMessaging.Caller
{
    internal sealed record ServerOptions(
        string Rid,
        string HttpUrl,
        string RedisEndpoint,
        string RedisKeyPrefix,
        string RouterEndpoint,
        string PubSubEndpoint,
        string LogDir)
    {
        public static ServerOptions Parse(string[] args, string role)
        {
            var values = ParseArgs(args);
            var logDir = Get(values, "log-dir", Path.Combine(AppContext.BaseDirectory, "logs"));
            return new ServerOptions(
                Get(values, "rid", role),
                Get(values, "http-url", "http://127.0.0.1:0"),
                Get(values, "redis-endpoint", "127.0.0.1:6379"),
                Get(values, "redis-key-prefix", "zlink:e2e:to-actor"),
                Get(values, "router-endpoint", "tcp://127.0.0.1:0"),
                Get(values, "pubsub-endpoint", "tcp://127.0.0.1:0"),
                logDir);
        }

        private static Dictionary<string, string> ParseArgs(string[] args)
        {
            var values = new Dictionary<string, string>(StringComparer.Ordinal);
            for (var i = 0; i < args.Length; i += 2)
            {
                var key = args[i].TrimStart('-');
                if (i + 1 >= args.Length) throw new ArgumentException($"Missing value for '{args[i]}'.");
                values[key] = args[i + 1];
            }

            return values;
        }

        private static string Get(Dictionary<string, string> values, string key, string fallback) =>
            values.TryGetValue(key, out var value) ? value : fallback;
    }
}
