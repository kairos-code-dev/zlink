using RuntimeMonitoring.Server.Trigger;
namespace RuntimeMonitoring.Server.Trigger.Support;

internal static class TriggerLogReader
{
    public static async Task<string[]> WaitForLinesAsync(
        string path,
        Func<string[], bool> predicate,
        TimeSpan timeout,
        CancellationToken cancellationToken)
    {
        var deadline = DateTimeOffset.UtcNow + timeout;
        while (DateTimeOffset.UtcNow < deadline)
        {
            var lines = File.Exists(path) ? await File.ReadAllLinesAsync(path, cancellationToken) : [];
            if (predicate(lines))
            {
                return lines;
            }

            await Task.Delay(100, cancellationToken);
        }

        throw new TimeoutException($"Timed out waiting for log file '{path}'.");
    }
}
