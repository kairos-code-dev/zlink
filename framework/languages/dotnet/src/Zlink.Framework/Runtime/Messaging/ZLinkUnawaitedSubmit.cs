namespace Zlink.Framework.Runtime.Messaging;

/// <summary>
/// Observes fire-and-forget submit tasks. A Submit() caller opted out of
/// awaiting the result, not out of the failure existing: a submit that dies
/// locally (unroutable target, stopped socket) must still reach monitoring,
/// or stale-address sends vanish without a trace (spot-address messaging
/// spec §4). Cancellation is not a failure.
/// </summary>
internal static class ZLinkUnawaitedSubmit
{
    private static readonly ZLinkRuntimeErrorSink Sink = new();

    public static void Observe(ValueTask task, string operationName)
    {
        if (task.IsCompletedSuccessfully) return;

        _ = ObserveAsync(task, operationName);
    }

    private static async Task ObserveAsync(ValueTask task, string operationName)
    {
        try
        {
            await task.ConfigureAwait(false);
        }
        catch (OperationCanceledException)
        {
        }
        catch (Exception exception)
        {
            Sink.ReportRuntimeTaskException(operationName, exception);
        }
    }
}
