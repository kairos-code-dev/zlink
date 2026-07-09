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
builder.WebHost.UseUrls(options.HttpUrl);
builder.Services.AddSingleton(new EvidenceStore(options.EvidenceFile));
builder.Services.AddZLinkFramework(framework =>
{
    framework.AddLocationStore(new ZLinkRedisLocationStore(redis => redis
        .SetConnectionString(options.RedisEndpoint)
        .SetKeyPrefix(options.RedisKeyPrefix)));
    framework.AddHandlersFromAssemblyOf(typeof(Program));
    framework.AddSpotMesh("to-actor")
        .EnableRouter(options.RouterEndpoint)
        .SetRoutingId(RoutingId.From(options.Rid))
        .SetEntrySpotRoutingId(RoutingId.From(options.Rid))
        .EnablePubSub(options.PubSubEndpoint)
        .AddEntrySpot<TestEntrySpot>()
        .AddActorFactory<TestActorFactory>("test-actor")
        .AddStatelessActorTransfer<TestActor>("test-actor");
});

var app = builder.Build();
app.MapGet("/health", () => Results.Ok(new { status = "ok" }));
app.MapPost("/actors/{actorId}/ensure", async (
    string actorId,
    IZLinkActorManager actors,
    CancellationToken ct) =>
{
    await actors.GetOrCreateAsync(actorId, "test-actor", ct);
    return Results.Ok(new { actorId });
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
            string actorId,
            IZLinkActorContext context,
            CancellationToken cancellationToken = default)
        {
            return ValueTask.FromResult<IZLinkActor>(new TestActor(actorId, context));
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
        ZLinkActorJoinAdmission admission,
        ZLinkMessage request,
            CancellationToken cancellationToken)
        {
            evidence.Append(new ActorEvidence("join", actor.ActorId, "join", "joined"));
            return ValueTask.FromResult(ZLinkSpotActorJoinResult.Accept());
        }
    }

    internal sealed class NotifyHandler(EvidenceStore evidence)
        : IZLinkEntrySpotActorSendHandler<TestEntrySpot, TestActor, ActorNotify>
    {
        public ValueTask HandleAsync(
            TestEntrySpot spot,
            TestActor actor,
            ZLinkSpotActorSendContext context,
            ActorNotify message,
            CancellationToken cancellationToken)
        {
            evidence.Append(new ActorEvidence(message.Scenario, actor.ActorId, "send", message.Value));
            return ValueTask.CompletedTask;
        }
    }

    internal sealed class AskHandler(EvidenceStore evidence)
        : IZLinkEntrySpotActorRequestHandler<TestEntrySpot, TestActor, ActorAsk, ActorReply>
    {
        public ValueTask<ActorReply> HandleAsync(
            TestEntrySpot spot,
            TestActor actor,
            ZLinkSpotActorRequestContext context,
            ActorAsk request,
            CancellationToken cancellationToken)
        {
            evidence.Append(new ActorEvidence(request.Scenario, actor.ActorId, "request", request.Value));
            return ValueTask.FromResult(new ActorReply(request.Scenario, actor.ActorId, $"reply:{request.Value}"));
        }
    }
}
