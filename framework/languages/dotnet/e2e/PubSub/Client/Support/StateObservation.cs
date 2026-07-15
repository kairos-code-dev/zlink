namespace PubSub.Client.Support;

internal static class StateObservation
{
    public static async Task WaitUntilAsync(
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

        throw new TimeoutException(failureMessage);
    }
}
