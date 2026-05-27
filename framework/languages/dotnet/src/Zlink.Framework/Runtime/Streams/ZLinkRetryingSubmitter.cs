namespace Zlink.Framework.Runtime.Streams;

internal static class ZLinkRetryingSubmitter
{
    private static readonly TimeSpan RetryDelay = TimeSpan.FromMilliseconds(25);

    public static async ValueTask SubmitAsync(
        Func<bool> submit,
        TimeSpan timeout,
        string failureMessage,
        CancellationToken cancellationToken)
    {
        var elapsed = System.Diagnostics.Stopwatch.StartNew();
        Exception? lastError = null;
        while (true)
        {
            cancellationToken.ThrowIfCancellationRequested();
            try
            {
                if (submit())
                {
                    return;
                }
            }
            catch (ZlinkSubmitException error) when (IsRoutePending(error))
            {
                lastError = error;
            }

            if (elapsed.Elapsed >= timeout)
            {
                throw new InvalidOperationException(failureMessage, lastError);
            }

            var remaining = timeout - elapsed.Elapsed;
            await Task.Delay(remaining < RetryDelay ? remaining : RetryDelay, cancellationToken)
                .ConfigureAwait(false);
        }
    }

    private static bool IsRoutePending(ZlinkSubmitException error)
    {
        return error.Result == ZlinkSubmitException.ErrorCode.NotConnected;
    }
}
