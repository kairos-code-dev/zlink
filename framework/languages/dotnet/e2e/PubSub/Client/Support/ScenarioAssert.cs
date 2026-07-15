namespace PubSub.Client.Support;

internal static class ScenarioAssert
{
    public static async Task EventuallyAsync(
        Func<Task<bool>> condition,
        string failureMessage,
        TimeSpan? timeout = null)
    {
        using var timeoutSource = new CancellationTokenSource(timeout ?? TimeSpan.FromSeconds(20));
        while (!timeoutSource.IsCancellationRequested)
        {
            if (await condition()) return;

            await Task.Delay(100, timeoutSource.Token).ContinueWith(_ => { });
        }

        throw new InvalidOperationException(failureMessage);
    }

    public static bool IsConnectionFailure(Exception ex)
    {
        return ex is HttpRequestException
               || ex.InnerException is HttpRequestException;
    }
}
