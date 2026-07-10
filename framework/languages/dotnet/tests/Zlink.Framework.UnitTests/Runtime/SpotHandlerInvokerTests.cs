using Microsoft.Extensions.DependencyInjection;
using Zlink.Framework.Contracts.Messaging;
using Zlink.Framework.Runtime.Codecs;

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

        public IZLinkActorContext Context => throw new NotSupportedException();
    }
}
