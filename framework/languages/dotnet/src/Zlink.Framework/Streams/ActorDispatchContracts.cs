using Microsoft.Extensions.DependencyInjection;

namespace Zlink.Framework.Streams;

public interface IZLinkActorSendHandler<in TMessage>
{
    ValueTask HandleAsync(
        TMessage message,
        ZLinkActorSendContext context,
        CancellationToken cancellationToken);
}

public interface IZLinkActorRequestHandler<in TRequest, TReply>
{
    ValueTask<TReply> HandleAsync(
        TRequest request,
        ZLinkActorRequestContext context,
        CancellationToken cancellationToken);
}

public sealed class ZLinkActorSendContext : ZLinkHandlerContext
{
    internal ZLinkActorSendContext(
        string actorId,
        string routerChannelId,
        string? packetName,
        string? contentType,
        string? correlationId,
        IServiceProvider services,
        CancellationToken connectionAborted,
        ZLinkMessageMetadata? metadata = null)
        : base(routerChannelId, packetName, contentType, correlationId, null, services, connectionAborted)
    {
        ActorId = actorId;
        RouterChannelId = routerChannelId;
        Metadata = metadata ?? ZLinkMessageMetadata.Empty;
        SessionProxy = services.GetRequiredService<IZLinkSessionProxy>();
    }

    public string ActorId { get; }

    public string RouterChannelId { get; }

    public ZLinkMessageMetadata Metadata { get; }

    public IZLinkSessionProxy SessionProxy { get; }
}

public sealed class ZLinkActorRequestContext : ZLinkHandlerContext
{
    internal ZLinkActorRequestContext(
        string actorId,
        string routerChannelId,
        string? packetName,
        string? contentType,
        string? correlationId,
        DateTimeOffset? deadline,
        IServiceProvider services,
        CancellationToken connectionAborted,
        ZLinkMessageMetadata? metadata = null)
        : base(routerChannelId, packetName, contentType, correlationId, deadline, services, connectionAborted)
    {
        ActorId = actorId;
        RouterChannelId = routerChannelId;
        Metadata = metadata ?? ZLinkMessageMetadata.Empty;
        SessionProxy = services.GetRequiredService<IZLinkSessionProxy>();
        Deadline = deadline;
    }

    public string ActorId { get; }

    public string RouterChannelId { get; }

    public ZLinkMessageMetadata Metadata { get; }

    public IZLinkSessionProxy SessionProxy { get; }

    public new DateTimeOffset? Deadline { get; }
}
