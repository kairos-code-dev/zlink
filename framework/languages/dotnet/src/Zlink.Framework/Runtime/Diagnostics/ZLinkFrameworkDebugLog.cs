namespace Zlink.Framework.Runtime.Diagnostics;

internal static class ZLinkFrameworkDebugLog
{
    public static void Startup(Exception exception)
    {
        if (IsEnabled("ZLINK_DEBUG_FRAMEWORK_STARTUP")) Write(exception.ToString());
    }

    public static void SpotDiscovery(string message)
    {
        if (IsEnabled("ZLINK_DEBUG_FRAMEWORK_SPOT_DISCOVERY")) Write($"[zlink-framework-spot-discovery] {message}");
    }

    public static void TaskFailure(string taskName, Exception exception)
    {
        if (IsEnabled("ZLINK_DEBUG_FRAMEWORK_TASKS")) Write($"[zlink-framework] task '{taskName}' failed: {exception}");
    }

    private static bool IsEnabled(string variableName)
    {
        return Environment.GetEnvironmentVariable(variableName) == "1";
    }

    private static void Write(string message)
    {
        Console.Error.WriteLine(message);
    }
}