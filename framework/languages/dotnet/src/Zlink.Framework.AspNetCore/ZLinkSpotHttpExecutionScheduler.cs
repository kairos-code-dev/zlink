namespace Zlink.Framework.AspNetCore;

internal sealed class ZLinkSpotHttpExecutionScheduler : IZLinkHttpExecutionScheduler
{
    public ValueTask<TResult> YieldAsync<TResult>(
        Func<CancellationToken, ValueTask<TResult>> operation,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(operation);
        var turn = ZLinkSerialTurn.Current
                   ?? throw new ZLinkFrameworkException(
                       ZLinkFrameworkErrorKind.RequestProtocolError,
                       "HTTP Yield can only run inside a framework execution turn.");
        return turn.YieldFrameworkCallAsync(operation, cancellationToken);
    }
}
