namespace ResilienceLifecycle.Client.Support;

internal static class ScenarioAssert
{
    public static void That(bool condition, string message)
    {
        if (!condition) throw new InvalidOperationException(message);
    }

    public static string[] AllProviderEvidence(LifecycleApiRes result)
    {
        return result.ProviderAEvidence.Concat(result.ProviderBEvidence).ToArray();
    }
}