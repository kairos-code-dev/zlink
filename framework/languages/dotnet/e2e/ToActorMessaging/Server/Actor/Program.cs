using Microsoft.Extensions.Configuration;

using Systems.Zlink;
using ToActorMessaging.Actor;
using ToActorMessaging.Shared;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Messaging;
using Zlink.Framework.Contracts.Spots;
using Zlink.Framework.Locations.Redis;

var options = ServerOptions.Parse(args, "actor");
Directory.CreateDirectory(options.LogDir);

var builder = WebApplication.CreateBuilder(args);
        builder.Configuration.Sources.Clear();
        builder.Configuration.AddInMemoryCollection();
builder.WebHost.UseUrls(options.HttpUrl);
builder.Services.AddSingleton(options);
builder.Services.AddSingleton(new EvidenceStore(options.EvidenceFile));
builder.Services.AddZLinkFramework(framework =>
{
    framework.AddLocationStore(new ZLinkRedisLocationStore(redis => redis
        .SetConnectionString(options.RedisEndpoint)
        .SetKeyPrefix(options.RedisKeyPrefix)));
    framework.AddHandlersFromAssemblyOf(typeof(Program));
    var mesh25 = framework.AddRouteMesh("to-actor")
        .Listen(options.RouterEndpoint)
        .SetRoutingId(RoutingId.From(options.Rid))
        .SetEntrySpotRoutingId(RoutingId.From(options.Rid))
        .AddEntrySpot<TestEntrySpot>()
        .AddActorFactory<TestActorFactory>("test-actor");
    mesh25.ChannelName("to-actor");
});

var app = builder.Build();
app.MapGet("/health", () => Results.Ok(new { status = "ok" }));
app.MapPost("/actors/{actorId}/ensure", async (
    string actorId,
    IZLinkActorManager actors,
    CancellationToken ct) =>
{
    _ = await actors.GetOrCreate(actorId, "test-actor").Async(ct);
    return Results.Ok(new { actorId });
});
app.MapPost("/actors/{actorId}/destroy", async (
    string actorId,
    string? scenario,
    IZLinkActorManager actorManager,
    IZLinkActorClient actorClient,
    CancellationToken ct) =>
{
    var actor = await actorManager.FindAsync(actorId, ct)
                ?? throw new InvalidOperationException($"Actor '{actorId}' was not found.");
    var reply = await actorClient.RequestToActor(
            "to-actor",
            actor,
            new DestroyActorRequest(actorId, scenario ?? "destroy"))
        .Timeout(TimeSpan.FromSeconds(5))
        .Async<DestroyActorReply>(ct);
    return Results.Ok(reply);
});
app.MapPost("/actors/{actorId}/push", async (
    string actorId,
    BoundPushRequest request,
    IZLinkActorManager actorManager,
    IZLinkActorClient actorClient,
    CancellationToken ct) =>
{
    if (!string.Equals(actorId, request.ActorId, StringComparison.Ordinal))
        throw new InvalidOperationException("Push request actor id mismatch.");
    var actor = await actorManager.FindAsync(actorId, ct)
                ?? throw new InvalidOperationException($"Actor '{actorId}' was not found.");
    try
    {
        var reply = await actorClient.RequestToActor("to-actor", actor, request)
            .Timeout(TimeSpan.FromSeconds(5))
            .Async<BoundPushReply>(ct);
        return Results.Ok(reply);
    }
    catch (Zlink.Framework.Contracts.Errors.ZLinkFrameworkException error)
    {
        return Results.Ok(new BoundPushReply(actorId, request.Value, false, error.Kind.ToString()));
    }
});
app.MapGet("/evidence", (EvidenceStore evidence) => Results.Ok(evidence.All()));
app.MapPost("/shutdown", async (IHostApplicationLifetime lifetime) =>
{
    await Task.Yield();
    lifetime.StopApplication();
    return Results.Ok();
});
await app.RunAsync();

namespace ToActorMessaging.Actor
{
    internal sealed class TestActor(string actorId, IZLinkActorContext context) : IZLinkActor
    {
        public string ActorId { get; } = actorId;

        public IZLinkActorContext Context { get; } = context;
    }

    internal sealed class TestActorFactory : IZLinkActorFactory
    {
        public ValueTask<IZLinkActor> CreateAsync(
            IZLinkActorContext context,
            CancellationToken cancellationToken = default)
        {
            return ValueTask.FromResult<IZLinkActor>(new TestActor(context.ActorId, context));
        }
    }

    internal sealed class TestEntrySpot(
        IZLinkEntrySpotContext context,
        EvidenceStore evidence) : IZLinkEntrySpot<TestActor>
    {
        public IZLinkEntrySpotContext Context { get; } = context;

        public void Configure()
        {
            Context.Handlers.AddHandler<NotifyHandler>();
            Context.Handlers.AddHandler<AskHandler>();
            Context.Handlers.AddHandler<DestroyHandler>();
            Context.Handlers.AddHandler<BoundPushHandler>();
        }

        public ValueTask OnCreateActorAsync(
            TestActor actor,
            ZLinkMessage createRequest,
            CancellationToken cancellationToken)
        {
            evidence.Append(new ActorEvidence("create", actor.ActorId, "create", "created"));
            return ValueTask.CompletedTask;
        }

        public ValueTask<ZLinkSpotActorJoinResult> OnActorJoinAsync(
            string actorId,
            ZLinkMessage request,
            CancellationToken cancellationToken)
        {
            evidence.Append(new ActorEvidence("join", actorId, "join", "joined"));
            return ValueTask.FromResult(ZLinkSpotActorJoinResult.Accept());
        }

        public ValueTask OnJoinedActorAsync(TestActor actor, CancellationToken cancellationToken) =>
            ValueTask.CompletedTask;

        public ValueTask OnLeaveActorAsync(TestActor actor, CancellationToken cancellationToken) =>
            ValueTask.CompletedTask;
    }

    internal sealed class NotifyHandler(
        EvidenceStore evidence,
        IZLinkActorManager actors,
        ServerOptions options)
        : IZLinkEntrySpotActorSendHandler<TestEntrySpot, TestActor, ActorNotify>
    {
        public async ValueTask HandleAsync(
            TestEntrySpot spot,
            TestActor actor,
            ZLinkSpotActorSendContext context,
            ActorNotify message,
            CancellationToken cancellationToken)
        {
            var actorRef = await actors.FindAsync(actor.ActorId, cancellationToken);
            evidence.Append(new ActorEvidence(
                message.Scenario,
                actor.ActorId,
                "send",
                message.Value,
                options.Rid,
                actorRef?.Generation,
                nameof(ActorNotify),
                message.Scenario));
        }
    }

    internal sealed class AskHandler(
        EvidenceStore evidence,
        IZLinkActorManager actors,
        ServerOptions options)
        : IZLinkEntrySpotActorRequestHandler<TestEntrySpot, TestActor, ActorAsk, ActorReply>
    {
        public async ValueTask<ActorReply> HandleAsync(
            TestEntrySpot spot,
            TestActor actor,
            ZLinkSpotActorRequestContext context,
            ActorAsk request,
            CancellationToken cancellationToken)
        {
            var actorRef = await actors.FindAsync(actor.ActorId, cancellationToken);
            evidence.Append(new ActorEvidence(
                request.Scenario,
                actor.ActorId,
                "request",
                request.Value,
                options.Rid,
                actorRef?.Generation,
                nameof(ActorAsk),
                request.Scenario));
            return new ActorReply(request.Scenario, actor.ActorId, $"reply:{request.Value}");
        }
    }

    internal sealed class DestroyHandler(EvidenceStore evidence)
        : IZLinkEntrySpotActorRequestHandler<TestEntrySpot, TestActor, DestroyActorRequest, DestroyActorReply>
    {
        public async ValueTask<DestroyActorReply> HandleAsync(
            TestEntrySpot spot,
            TestActor actor,
            ZLinkSpotActorRequestContext context,
            DestroyActorRequest request,
            CancellationToken cancellationToken)
        {
            _ = context;
            if (!string.Equals(request.ActorId, actor.ActorId, StringComparison.Ordinal))
                throw new InvalidOperationException("Destroy request actor id mismatch.");

            await spot.Context.DestroyActorAsync(actor, cancellationToken);
            evidence.Append(new ActorEvidence(request.Scenario, actor.ActorId, "destroy", "destroyed"));
            return new DestroyActorReply(actor.ActorId, true);
        }
    }

    internal sealed class BoundPushHandler(EvidenceStore evidence)
        : IZLinkEntrySpotActorRequestHandler<TestEntrySpot, TestActor, BoundPushRequest, BoundPushReply>
    {
        public async ValueTask<BoundPushReply> HandleAsync(
            TestEntrySpot spot,
            TestActor actor,
            ZLinkSpotActorRequestContext context,
            BoundPushRequest request,
            CancellationToken cancellationToken)
        {
            _ = spot;
            _ = context;
            await actor.Context.BoundSession.Send(
                    new BoundPushNotify(request.Scenario, actor.ActorId, request.Value))
                .SubmitAsync(cancellationToken);
            evidence.Append(new ActorEvidence(request.Scenario, actor.ActorId, "bound-push", request.Value));
            return new BoundPushReply(actor.ActorId, request.Value, true);
        }
    }
}
