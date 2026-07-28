using Microsoft.Extensions.DependencyInjection;
using Zlink.Framework.Contracts.Messaging;
using Zlink.Framework.Runtime.Codecs;
using Zlink.Framework.Runtime.Handlers;

namespace Zlink.Framework.UnitTests.Runtime;

public sealed class SpotHandlerInvokerTests
{
    [Fact]
    public async Task InvokePacketAsync_Reuses_Unregistered_Handler_And_Disposes_It_Once()
    {
        var lifetime = new HandlerLifetime();
        using var provider = new ServiceCollection()
            .AddSingleton(lifetime)
            .BuildServiceProvider();
        var handlerInstances = new ZLinkScopedHandlerInstanceOwner(provider);
        var spot = new MemberLifecycleSpot();
        var descriptor = ZLinkSpotDescriptorFactory.CreatePacketDescriptor(
            typeof(DisposablePacketHandler),
            typeof(MemberLifecycleSpot));
        var invoker = new ZLinkSpotHandlerInvoker(
            handlerInstances,
            spot,
            new ZLinkCodecRegistryBuilder(),
            ZLinkStreamProtocolDefaults.CreateLz4CompressionCodec());

        await invoker.InvokePacketAsync(descriptor, new HandlerMessage(), CancellationToken.None);
        await invoker.InvokePacketAsync(descriptor, new HandlerMessage(), CancellationToken.None);

        Assert.Equal(2, lifetime.Invocations.Count);
        Assert.Same(lifetime.Invocations[0], lifetime.Invocations[1]);
        await handlerInstances.DisposeAsync();
        await handlerInstances.DisposeAsync();
        Assert.Equal(1, lifetime.DisposeCount);
    }

    [Fact]
    public async Task ExplicitlyRegistered_Handler_Remains_Owned_By_ServiceScope()
    {
        var lifetime = new HandlerLifetime();
        await using var provider = new ServiceCollection()
            .AddSingleton(lifetime)
            .AddScoped<DisposablePacketHandler>()
            .BuildServiceProvider();
        var scope = provider.CreateAsyncScope();
        var handlerInstances = new ZLinkScopedHandlerInstanceOwner(scope.ServiceProvider);

        Assert.Same(
            handlerInstances.Resolve<DisposablePacketHandler>(),
            handlerInstances.Resolve<DisposablePacketHandler>());

        await handlerInstances.DisposeAsync();
        Assert.Equal(0, lifetime.DisposeCount);
        await scope.DisposeAsync();
        Assert.Equal(1, lifetime.DisposeCount);
    }

    [Fact]
    public async Task InitializationFailure_Cleans_Up_AlreadyCreated_Fallback_Handler()
    {
        var lifetime = new HandlerLifetime();
        using var provider = new ServiceCollection()
            .AddSingleton(lifetime)
            .BuildServiceProvider();
        var handlerInstances = new ZLinkScopedHandlerInstanceOwner(provider);
        _ = handlerInstances.Resolve<DisposablePacketHandler>();

        Assert.Throws<InvalidOperationException>(() => handlerInstances.Resolve<FailingHandler>());
        await handlerInstances.DisposeAsync();

        Assert.Equal(1, lifetime.DisposeCount);
    }

    [Fact]
    public async Task InvokeActorLifecycleAsync_Uses_CurrentSpotInstance_WhenHandlerTypeIsSpotType()
    {
        var spot = new MemberLifecycleSpot();
        var actor = new MemberLifecycleActor("player-1");
        var descriptor = ZLinkSpotActorAttributedDescriptorFactory
            .CreateSpotLifecycleDescriptors(ZLinkSpotActorHandlerSurface.UserSpot, typeof(MemberLifecycleSpot))
            .Single(item => item.Joined is not null)
            .Joined!;

        using var provider = new ServiceCollection().BuildServiceProvider();
        await using var handlerInstances = new ZLinkScopedHandlerInstanceOwner(provider);
        var invoker = new ZLinkSpotHandlerInvoker(
            handlerInstances,
            spot,
            new ZLinkCodecRegistryBuilder(),
            ZLinkStreamProtocolDefaults.CreateLz4CompressionCodec());

        await invoker.InvokeActorLifecycleAsync(
            descriptor,
            actor,
            CancellationToken.None);

        Assert.Equal("player-1", spot.JoinedActorId);
    }

    [Fact]
    public void CreateSpotLifecycleDescriptors_Uses_onLeaveActor_Hook()
    {
        var descriptor = ZLinkSpotActorAttributedDescriptorFactory
            .CreateSpotLifecycleDescriptors(ZLinkSpotActorHandlerSurface.UserSpot, typeof(MemberLifecycleSpot))
            .Single(item => item.Left is not null)
            .Left!;

        Assert.Equal(typeof(MemberLifecycleSpot), descriptor.HandlerType);
        Assert.Equal(typeof(MemberLifecycleActor), descriptor.ActorType);
        Assert.False(descriptor.PassSpotArgument);
    }

    [Fact]
    public void CreateSpotLifecycleDescriptors_Accepts_Exact_EntryActorCreation_Hook()
    {
        var descriptor = ZLinkSpotActorAttributedDescriptorFactory
            .CreateSpotLifecycleDescriptors(
                ZLinkSpotActorHandlerSurface.EntrySpot,
                typeof(EntryLifecycleSpot))
            .Single(item => item.Created is not null)
            .Created!;

        Assert.Equal(typeof(EntryLifecycleSpot), descriptor.HandlerType);
        Assert.Equal(typeof(MemberLifecycleActor), descriptor.ActorType);
        Assert.True(descriptor.PassRequestArgument);
    }

    private sealed class MemberLifecycleSpot : IZLinkSpot<MemberLifecycleActor>
    {
        public string? JoinedActorId { get; private set; }
        public IZLinkSpotContext Context => throw new NotSupportedException();

        public ValueTask<ZLinkSpotActorJoinResult> OnActorJoinAsync(
            string actorId,
            ZLinkMessage request,
            CancellationToken cancellationToken)
        {
            return ValueTask.FromResult(ZLinkSpotActorJoinResult.Accept());
        }

        public ValueTask OnJoinedActorAsync(
            MemberLifecycleActor actor,
            CancellationToken cancellationToken)
        {
            _ = cancellationToken;
            JoinedActorId = actor.ActorId;
            return ValueTask.CompletedTask;
        }

        public ValueTask OnLeaveActorAsync(
            MemberLifecycleActor actor,
            CancellationToken cancellationToken)
        {
            _ = actor;
            _ = cancellationToken;
            return ValueTask.CompletedTask;
        }
    }

    private sealed class MemberLifecycleActor(string actorId) : IZLinkActor
    {
        public string ActorId { get; } = actorId;

        public IZLinkActorContext Context { get; } = new TestActorContext(actorId);
    }

    private sealed class EntryLifecycleSpot : IZLinkEntrySpot<MemberLifecycleActor>
    {
        public IZLinkEntrySpotContext Context => throw new NotSupportedException();

        public ValueTask<ZLinkActorCreateResponse> OnCreateActorAsync(
            MemberLifecycleActor actor,
            ZLinkMessage createRequest,
            CancellationToken cancellationToken)
        {
            _ = actor;
            _ = createRequest;
            cancellationToken.ThrowIfCancellationRequested();
            return ValueTask.FromResult(ZLinkActorCreateResponse.Accept());
        }

        public ValueTask OnJoinedActorAsync(
            MemberLifecycleActor actor,
            CancellationToken cancellationToken)
        {
            _ = actor;
            cancellationToken.ThrowIfCancellationRequested();
            return ValueTask.CompletedTask;
        }

        public ValueTask OnLeaveActorAsync(
            MemberLifecycleActor actor,
            CancellationToken cancellationToken)
        {
            _ = actor;
            cancellationToken.ThrowIfCancellationRequested();
            return ValueTask.CompletedTask;
        }
    }

    private sealed record HandlerMessage;

    private sealed class HandlerLifetime
    {
        public List<object> Invocations { get; } = [];

        public int DisposeCount { get; set; }
    }

    private sealed class DisposablePacketHandler(HandlerLifetime lifetime)
        : IZLinkSpotPacketHandler<MemberLifecycleSpot, HandlerMessage>, IDisposable
    {
        public ValueTask HandleAsync(
            MemberLifecycleSpot spot,
            HandlerMessage message,
            CancellationToken cancellationToken)
        {
            _ = spot;
            _ = message;
            cancellationToken.ThrowIfCancellationRequested();
            lifetime.Invocations.Add(this);
            return ValueTask.CompletedTask;
        }

        public void Dispose()
        {
            lifetime.DisposeCount++;
        }
    }

    private sealed class FailingHandler
    {
        public FailingHandler()
        {
            throw new InvalidOperationException("handler initialization failed");
        }
    }
}
