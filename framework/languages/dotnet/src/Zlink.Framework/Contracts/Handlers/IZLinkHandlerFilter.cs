namespace Zlink.Framework.Contracts.Handlers;

public delegate ValueTask ZLinkHandlerFilterNext();

public interface IZLinkHandlerFilter
{
    ValueTask InvokeAsync(
        ZLinkHandlerInvocation invocation,
        ZLinkHandlerFilterNext next,
        CancellationToken cancellationToken);
}
