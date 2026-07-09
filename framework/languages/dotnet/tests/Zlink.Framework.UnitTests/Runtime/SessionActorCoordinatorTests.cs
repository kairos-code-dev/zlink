using Microsoft.Extensions.DependencyInjection;
using Systems.Zlink.Stream.Connector.Contracts;

namespace Zlink.Framework.UnitTests;

public sealed class SessionActorCoordinatorTests
{
    [Fact]
    public async Task BindOrGetActorAsync_Rebinds_When_Generation_Changes_For_Same_ActorId()
    {
        var runtime = CreateRuntime();
        var context = new ZLinkSessionContext(
            runtime,
            new TestStream(RoutingId.From("session-node")),
            static () => ValueTask.CompletedTask,
            static _ => ValueTask.CompletedTask);

        var firstRef = new ActorRef(RoutingId.From("actor-node"), "actor-1", 1);
        var secondRef = new ActorRef(RoutingId.From("actor-node"), "actor-1", 2);

        var first = await context.ActorCoordinator.BindOrGetActorAsync(
            context,
            firstRef,
            CancellationToken.None);
        Assert.True(runtime.TryGetActorBoundSession("actor-1", out var firstSession));
        var firstToken = firstSession.BindingToken;

        var second = await context.ActorCoordinator.BindOrGetActorAsync(
            context,
            secondRef,
            CancellationToken.None);

        Assert.NotSame(first, second);
        Assert.Equal(secondRef, second.Ref);
        Assert.Single(context.Actors.Bound);
        Assert.DoesNotContain(first, context.Actors.Bound);
        Assert.False(runtime.TryGetSessionActorContext("actor-1", firstToken, out _));
        Assert.True(runtime.TryGetActorBoundSession("actor-1", out var secondSession));
        Assert.NotEqual(firstToken, secondSession.BindingToken);
        Assert.True(runtime.TryGetSessionActorContext("actor-1", secondSession.BindingToken, out var reboundContext));
        Assert.Same(context, reboundContext);
    }

    private static ZLinkFrameworkRuntime CreateRuntime()
    {
        var registration = new ZLinkFrameworkRegistration();
        var services = new ServiceCollection();
        services.AddSingleton(registration);
        var provider = services.BuildServiceProvider();

        return new ZLinkFrameworkRuntime(
            provider,
            null!,
            registration,
            new ZLinkHandlerRegistry([]),
            new ZLinkHandlerDispatcher(
                provider.GetRequiredService<IServiceScopeFactory>(),
                registration));
    }

    private sealed class TestStream(RoutingId routingId) : IZLinkStream
    {
        public string SessionId { get; } = routingId.ToHex();

        public RoutingId? RoutingId { get; } = routingId;

        public string? LocalAddr => null;

        public string? RemoteAddr => null;

        public bool Write(
            ZLinkMessage payload,
            SendFlags flags = SendFlags.None)
        {
            return true;
        }

        public ValueTask CloseAsync()
        {
            return ValueTask.CompletedTask;
        }
    }
}
