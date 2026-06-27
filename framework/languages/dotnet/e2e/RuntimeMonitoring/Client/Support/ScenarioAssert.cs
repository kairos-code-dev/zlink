namespace RuntimeMonitoring.Client;

internal static class ScenarioAssert
{
    public static void That(bool condition, string message)
    {
        if (!condition)
        {
            throw new InvalidOperationException(message);
        }
    }
}
