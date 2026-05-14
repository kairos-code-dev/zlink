namespace Zlink.Framework.Contracts.Handlers;

public interface IZLinkRequestHandler<in TRequest, TResponse>
{
    ValueTask<TResponse> HandleAsync(
        TRequest request,
        ZLinkRequestContext context,
        CancellationToken cancellationToken);
}

public interface IZLinkSendHandler<in TMessage>
{
    ValueTask HandleAsync(
        TMessage message,
        ZLinkSendContext context,
        CancellationToken cancellationToken);
}

public interface IZLinkPublishHandler<in TMessage>
{
    ValueTask HandleAsync(
        TMessage message,
        ZLinkPublishContext context,
        CancellationToken cancellationToken);
}
