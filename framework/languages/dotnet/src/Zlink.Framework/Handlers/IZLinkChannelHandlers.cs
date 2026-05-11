namespace Zlink.Framework.Handlers;

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

public interface IZLinkEventHandler<in TEvent>
{
    ValueTask HandleAsync(
        TEvent message,
        ZLinkEventContext context,
        CancellationToken cancellationToken);
}
