namespace Zlink.Framework.Runtime.Core;

internal static class ZLinkPollingBackoff
{
    private static readonly TimeSpan NoDataDelay = TimeSpan.FromMilliseconds(1);

    public static Task NoDataAsync(CancellationToken cancellationToken)
    {
        return Task.Delay(NoDataDelay, cancellationToken);
    }
}
