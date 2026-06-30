namespace Zlink.Framework.UnitTests.Runtime;

public sealed class SpotActorHandlerRegistryTests
{
    [Fact]
    public void AddPacket_Rejects_Request_Handler_When_Send_Kind_Is_Required()
    {
        var registry = new ZLinkSpotActorHandlerRegistry(
            ZLinkSpotActorHandlerSurface.UserSpot,
            typeof(RegistrySpot));

        var ex = Assert.Throws<InvalidOperationException>(() =>
            registry.AddPacket(
                typeof(RequestHandler),
                typeof(RegistryActor),
                "actor.packet",
                ZLinkMessageKind.Command));

        Assert.Contains("expects 'Command'", ex.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void AddPacket_Rejects_Send_Handler_When_Request_Kind_Is_Required()
    {
        var registry = new ZLinkSpotActorHandlerRegistry(
            ZLinkSpotActorHandlerSurface.UserSpot,
            typeof(RegistrySpot));

        var ex = Assert.Throws<InvalidOperationException>(() =>
            registry.AddPacket(
                typeof(SendHandler),
                typeof(RegistryActor),
                "actor.packet",
                ZLinkMessageKind.Request));

        Assert.Contains("expects 'Request'", ex.Message, StringComparison.Ordinal);
    }

    private sealed class RegistrySpot : IZLinkSpot
    {
        public IZLinkSpotContext Context => throw new NotSupportedException();
    }

    private sealed class RegistryActor(string actorId) : IZLinkActor
    {
        public string ActorId { get; } = actorId;

        public IZLinkActorContext Context => throw new NotSupportedException();
    }

    private sealed class SendHandler :
        IZLinkSpotActorSendHandler<RegistrySpot, RegistryActor, RegistryMessage>
    {
        public ValueTask HandleAsync(
            RegistrySpot spot,
            RegistryActor actor,
            ZLinkSpotActorSendContext context,
            RegistryMessage message,
            CancellationToken cancellationToken)
        {
            return ValueTask.CompletedTask;
        }
    }

    private sealed class RequestHandler :
        IZLinkSpotActorRequestHandler<RegistrySpot, RegistryActor, RegistryMessage, RegistryReply>
    {
        public ValueTask<RegistryReply> HandleAsync(
            RegistrySpot spot,
            RegistryActor actor,
            ZLinkSpotActorRequestContext context,
            RegistryMessage request,
            CancellationToken cancellationToken)
        {
            return ValueTask.FromResult(new RegistryReply(request.Value));
        }
    }

    private sealed record RegistryMessage(string Value);

    private sealed record RegistryReply(string Value);
}