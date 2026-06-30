namespace Zlink.Framework.Runtime.Diagnostics;

internal sealed class ZLinkRuntimeErrorSink : IZLinkRuntimeErrorSink
{
    public void ReportHandlerException(Exception exception)
    {
        ReportUnhandledCallbackException(exception);
    }

    public void ReportRuntimeTaskException(
        string taskName,
        Exception exception)
    {
        ZLinkFrameworkDebugLog.TaskFailure(taskName, exception);
        ReportUnhandledCallbackException(exception);
    }

    public static event Action<Exception>? UnhandledCallbackException;

    internal static void ReportUnhandledCallbackException(Exception exception)
    {
        try
        {
            UnhandledCallbackException?.Invoke(exception);
        }
        catch
        {
        }
    }
}