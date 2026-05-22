using Zlink.Framework.Contracts.Handlers;
using Zlink.Framework.Contracts.Streams;

namespace Zlink.Framework.Contracts.Actors;

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

public interface IZLinkActorPacketHandler<in TActor, in TMessage>
    where TActor : IZLinkActor
{
    ValueTask HandleAsync(
        TActor actor,
        TMessage message,
        CancellationToken cancellationToken);
}

public interface IZLinkActorRequestHandler<in TActor, in TRequest, TReply>
    where TActor : IZLinkActor
{
    ValueTask<TReply> HandleAsync(
        TActor actor,
        TRequest request,
        CancellationToken cancellationToken);
}

public sealed class ZLinkActorSendContext : ZLinkHandlerContext
{
    internal ZLinkActorSendContext(
        string actorId,
        string? packetName,
        string? contentType,
        string? correlationId,
        IZLinkBoundSession boundSession,
        IServiceProvider services,
        CancellationToken connectionAborted,
        ZLinkMessageMetadata? metadata = null)
        : base(null, packetName, contentType, correlationId, null, services, connectionAborted)
    {
        ActorId = actorId;
        Metadata = metadata ?? ZLinkMessageMetadata.Empty;
        BoundSession = boundSession;
    }

    public string ActorId { get; }

    public ZLinkMessageMetadata Metadata { get; }

    public IZLinkBoundSession BoundSession { get; }
}

public sealed class ZLinkActorRequestContext : ZLinkHandlerContext
{
    internal ZLinkActorRequestContext(
        string actorId,
        string? packetName,
        string? contentType,
        string? correlationId,
        DateTimeOffset? deadline,
        IZLinkBoundSession boundSession,
        IServiceProvider services,
        CancellationToken connectionAborted,
        ZLinkMessageMetadata? metadata = null)
        : base(null, packetName, contentType, correlationId, deadline, services, connectionAborted)
    {
        ActorId = actorId;
        Metadata = metadata ?? ZLinkMessageMetadata.Empty;
        BoundSession = boundSession;
        Deadline = deadline;
    }

    public string ActorId { get; }

    public ZLinkMessageMetadata Metadata { get; }

    public IZLinkBoundSession BoundSession { get; }

    public new DateTimeOffset? Deadline { get; }
}
