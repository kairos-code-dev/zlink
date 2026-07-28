namespace Zlink.Framework.Contracts.Handlers;

public delegate ValueTask ZLinkHandlerFilterNext();

public interface IZLinkHandlerFilter
{
    ValueTask InvokeAsync(
        IZLinkMessageContext context,
        ZLinkHandlerFilterNext next,
        CancellationToken cancellationToken);
}
