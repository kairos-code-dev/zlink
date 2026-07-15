using Microsoft.Extensions.Configuration;

using System.Collections.Concurrent;
using Systems.Zlink;
using ToActorMessaging.Shared;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Messaging;
using Zlink.Framework.Contracts.Streams;
using Zlink.Framework.Locations.Redis;
using Zlink.Framework.E2E.Configuration;

var options = SessionOptions.Parse(args);
Directory.CreateDirectory(options.LogDir);
var builder = WebApplication.CreateBuilder(args);
        builder.Configuration.Sources.Clear();
        builder.Configuration.AddInMemoryCollection();
builder.WebHost.UseUrls(options.HttpUrl);
builder.Services.AddSingleton(new SessionEvidence(options.EvidenceFile));
builder.Services.AddZLinkFramework(framework =>
{
    framework.AddLocationStore(new ZLinkRedisLocationStore(redis => redis
        .SetConnectionString(options.RedisEndpoint)
        .SetKeyPrefix(options.RedisKeyPrefix)));
    framework.AddHandlersFromAssemblyOf(typeof(Program));
    framework.AddSpotMesh("to-actor")
        .EnableRouter(options.RouterEndpoint)
        .SetRoutingId(RoutingId.From(options.Rid))
        .EnablePubSub(options.PubSubEndpoint);
    framework.AddStreamNode("to-actor-session")
        .Bind(options.StreamEndpoint)
        .RegisterSession<ToActorSession>();
});

var app = builder.Build();
app.MapGet("/health", () => Results.Ok(new { status = "ok", options.Rid }));
app.MapGet("/evidence", (SessionEvidence evidence) => Results.Ok(evidence.All()));
app.MapGet("/bindings/{actorId}", (
    string actorId,
    SessionEvidence evidence) => Results.Ok(evidence.Snapshot(actorId)));
await app.RunAsync();

internal sealed class ToActorSession(
    IZLinkSessionContext context,
    SessionEvidence evidence) : IZLinkSession
{
    public IZLinkSessionContext Context { get; } = context;

    public void Configure() => Context.Handlers.AddHandler<BindActorHandler>();

    public ValueTask OnConnectedAsync(CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        evidence.Add($"session-connected|session={Context.SessionId}");
        return ValueTask.CompletedTask;
    }

    public async ValueTask OnDisconnectedAsync(CancellationToken cancellationToken)
    {
        evidence.Add($"session-disconnected-start|session={Context.SessionId}");
        foreach (var actor in Context.Actors.Bound)
        {
            evidence.Unbind(actor.ActorId, Context.SessionId.ToString());
            await actor.NotifyDisconnectedAsync(CancellationToken.None);
        }
        evidence.Add($"session-disconnected|session={Context.SessionId}");
    }

    public ValueTask OnErrorAsync(ZLinkStreamError error, CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        evidence.Add($"session-error|error={error}");
        return ValueTask.CompletedTask;
    }

    public async ValueTask OnDispatchAsync(
        ZLinkSessionDispatchContext dispatch,
        ZLinkMessage payload,
        CancellationToken cancellationToken)
    {
        if (await Context.Handlers.TryHandleAsync(dispatch, payload, cancellationToken)) return;
        var actor = Context.Actors.Bound.Count == 1
            ? Context.Actors.Bound.Single()
            : throw new InvalidOperationException(
                $"Session packet '{dispatch.PacketName}' requires exactly one bound actor.");
        await actor.RelayAsync(payload, cancellationToken);
    }
}

internal sealed class BindActorHandler(
    IZLinkActorDirectory actorDirectory,
    SessionEvidence evidence)
    : IZLinkSessionPacketHandler<IZLinkSessionContext, BindActorRequest>
{
    public async ValueTask HandleAsync(
        IZLinkSessionContext context,
        ZLinkSessionDispatchContext dispatch,
        BindActorRequest request,
        CancellationToken cancellationToken)
    {
        _ = dispatch;
        var actor = await actorDirectory.FindAsync(request.ActorId, cancellationToken)
                    ?? throw new InvalidOperationException($"Actor '{request.ActorId}' was not found.");
        await context.Actors.BindOrGetAsync(actor, cancellationToken);
        evidence.Bind(actor.ActorId, context.SessionId.ToString());
        evidence.Add(
            $"actor-bound|session={context.SessionId}|actor={actor.ActorId}"
            + $"|node={actor.NodeRid}|generation={actor.Generation}");
        context.Client.Reply(new BindActorReply(actor.ActorId, actor.NodeRid.ToString(), actor.Generation))
            .Submit(cancellationToken);
    }
}

internal sealed class SessionEvidence(string? file)
{
    private readonly ConcurrentQueue<string> _entries = new();
    private readonly ConcurrentDictionary<string, BoundSessionSnapshot> _bindings = new(StringComparer.Ordinal);

    public void Add(string value)
    {
        _entries.Enqueue(value);
        if (!string.IsNullOrWhiteSpace(file)) File.AppendAllLines(file, [value]);
    }

    public string[] All() => _entries.ToArray();

    public void Bind(string actorId, string sessionRid)
    {
        _bindings.AddOrUpdate(
            actorId,
            _ => new BoundSessionSnapshot(actorId, sessionRid, 1),
            (_, current) => current with { SessionRid = sessionRid, Revision = current.Revision + 1 });
    }

    public void Unbind(string actorId, string sessionRid)
    {
        _bindings.AddOrUpdate(
            actorId,
            _ => new BoundSessionSnapshot(actorId, null, 1),
            (_, current) => string.Equals(current.SessionRid, sessionRid, StringComparison.Ordinal)
                ? current with { SessionRid = null, Revision = current.Revision + 1 }
                : current);
    }

    public BoundSessionSnapshot Snapshot(string actorId) =>
        _bindings.TryGetValue(actorId, out var snapshot)
            ? snapshot
            : new BoundSessionSnapshot(actorId, null, 0);
}

internal sealed record SessionOptions(
    string Rid,
    string HttpUrl,
    string RedisEndpoint,
    string RedisKeyPrefix,
    string RouterEndpoint,
    string PubSubEndpoint,
    string StreamEndpoint,
    string LogDir,
    string? EvidenceFile = null)
{
    public static SessionOptions Parse(string[] args)
        => E2eConfiguration.Load<SessionOptions>(args);
}
