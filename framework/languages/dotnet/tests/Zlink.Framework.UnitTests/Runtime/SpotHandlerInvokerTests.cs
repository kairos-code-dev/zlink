using Microsoft.Extensions.DependencyInjection;
using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Spots;
using Zlink.Framework.Runtime.Spots;

namespace Zlink.Framework.UnitTests.Runtime;

public sealed class SpotHandlerInvokerTests
{
    [Fact]
    public async Task InvokeActorLifecycleAsync_Uses_CurrentSpotInstance_WhenHandlerTypeIsSpotType()
    {
        var spot = new MemberLifecycleSpot();
        var actor = new MemberLifecycleActor("player-1");
        var descriptor = ZLinkSpotActorAttributedDescriptorFactory
            .CreateSpotLifecycleDescriptors(ZLinkSpotActorHandlerSurface.UserSpot, typeof(MemberLifecycleSpot))
            .Single(item => item.Joined is not null)
            .Joined!;

        var invoker = new ZLinkSpotHandlerInvoker(
            new ServiceCollection().BuildServiceProvider(),
            spot);

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
    public async Task InvokeActorJoinAsync_Uses_Default_Interface_Hook_WhenSpotDoesNotDeclareOne()
    {
        var spot = new DefaultHookSpot();
        var actor = new MemberLifecycleActor("player-1");
        var descriptor = ZLinkSpotDescriptorFactory
            .CreateSpotActorJoinDescriptors(typeof(DefaultHookSpot))
            .Single();
        using var request = Message.From("join");

        var invoker = new ZLinkSpotHandlerInvoker(
            new ServiceCollection().BuildServiceProvider(),
            spot);

        var reply = await invoker.InvokeActorJoinAsync(
            descriptor,
            actor,
            request,
            CancellationToken.None);

        Assert.False(reply.Accepted);
        Assert.Equal(typeof(DefaultHookSpot), descriptor.HandlerType);
        Assert.Equal(typeof(MemberLifecycleActor), descriptor.ActorType);
        Assert.False(descriptor.PassSpotArgument);
    }

    [Fact]
    public void CreateSpotLifecycleDescriptors_Uses_Default_Interface_Hooks_WhenSpotDoesNotDeclareThem()
    {
        var descriptors = ZLinkSpotActorAttributedDescriptorFactory
            .CreateSpotLifecycleDescriptors(ZLinkSpotActorHandlerSurface.UserSpot, typeof(DefaultHookSpot))
            .ToArray();

        var joined = descriptors.Single(item => item.Joined is not null).Joined!;
        var left = descriptors.Single(item => item.Left is not null).Left!;

        Assert.Equal(typeof(DefaultHookSpot), joined.HandlerType);
        Assert.Equal(typeof(MemberLifecycleActor), joined.ActorType);
        Assert.False(joined.PassSpotArgument);
        Assert.Equal(typeof(DefaultHookSpot), left.HandlerType);
        Assert.Equal(typeof(MemberLifecycleActor), left.ActorType);
        Assert.False(left.PassSpotArgument);
    }

    [Fact]
    public void CreateSpotLifecycleDescriptors_Uses_Default_Interface_Hook_ForOnlyMissingLifecycleMethod()
    {
        var descriptors = ZLinkSpotActorAttributedDescriptorFactory
            .CreateSpotLifecycleDescriptors(ZLinkSpotActorHandlerSurface.UserSpot, typeof(PartialLifecycleSpot))
            .ToArray();

        var joined = descriptors.Single(item => item.Joined is not null).Joined!;
        var left = descriptors.Single(item => item.Left is not null).Left!;

        Assert.Equal(typeof(PartialLifecycleSpot), joined.HandlerType);
        Assert.Equal(typeof(PartialLifecycleSpot), left.HandlerType);
        Assert.Equal(typeof(MemberLifecycleActor), left.ActorType);
        Assert.False(left.PassSpotArgument);
    }

    private sealed class MemberLifecycleSpot : IZLinkSpot<MemberLifecycleActor>
    {
        public IZLinkSpotContext Context => throw new NotSupportedException();

        public string? JoinedActorId { get; private set; }

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

    private sealed class DefaultHookSpot : IZLinkSpot<MemberLifecycleActor>
    {
        public IZLinkSpotContext Context => throw new NotSupportedException();
    }

    private sealed class PartialLifecycleSpot : IZLinkSpot<MemberLifecycleActor>
    {
        public IZLinkSpotContext Context => throw new NotSupportedException();

        public ValueTask OnJoinedActorAsync(
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

        public IZLinkActorContext Context => throw new NotSupportedException();
    }
}
